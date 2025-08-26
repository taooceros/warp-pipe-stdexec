#include "../oc/rb/basic_rb.hpp"
#include "../oc/rb/pod_rb.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace oc::rb;

// Test helper macros
#define ASSERT(condition, message)                                             \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "ASSERTION FAILED: " << message << " at " << __FILE__       \
                << ":" << __LINE__ << "\n";                                    \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

#define TEST_CASE(name)                                                        \
  std::cout << "Running test: " << #name << "... ";                            \
  test_##name();                                                               \
  std::cout << "PASSED\n"

// Test structures
struct TestMessage {
  int id;
  std::string data;

  TestMessage() = default;
  TestMessage(int i, std::string d) : id(i), data(std::move(d)) {}

  bool operator==(const TestMessage &other) const {
    return id == other.id && data == other.data;
  }
};

struct TestPod {
  int a;
  float b;
  char c[16];

  bool operator==(const TestPod &other) const {
    return a == other.a && std::abs(b - other.b) < 1e-6f &&
           std::strncmp(c, other.c, sizeof(c)) == 0;
  }
};

void test_basic_construction() {
  BlockingRingBuffer<int> buffer(1024);
  ASSERT(buffer.capacity() >= 1024,
         "Capacity should be at least requested size");
  ASSERT(buffer.empty(), "New buffer should be empty");
  ASSERT(!buffer.full(), "New buffer should not be full");
  ASSERT(buffer.size() == 0, "New buffer size should be 0");
  ASSERT(buffer.available() == buffer.capacity(),
         "Available should equal capacity");
}

void test_single_element_operations() {
  DroppingRingBuffer<int> buffer(4);

  // Test push and pop
  ASSERT(buffer.try_push(42), "Should be able to push to empty buffer");
  ASSERT(buffer.size() == 1, "Size should be 1 after push");
  ASSERT(!buffer.empty(), "Buffer should not be empty");

  auto value = buffer.try_pop();
  ASSERT(value.has_value(), "Should be able to pop from non-empty buffer");
  ASSERT(*value == 42, "Popped value should match pushed value");
  ASSERT(buffer.empty(), "Buffer should be empty after pop");
}

void test_emplace_operations() {
  BlockingRingBuffer<TestMessage> buffer(8);

  // Test emplace
  ASSERT(buffer.try_emplace(1, "Hello"), "Should be able to emplace");
  ASSERT(buffer.size() == 1, "Size should be 1 after emplace");

  auto msg = buffer.try_pop();
  ASSERT(msg.has_value(), "Should be able to pop emplaced item");
  ASSERT(msg->id == 1 && msg->data == "Hello",
         "Emplaced values should be correct");
}

void test_peek_operations() {
  BlockingRingBuffer<int> buffer(4);

  // Test peek on empty buffer
  auto peek_empty = buffer.try_peek();
  ASSERT(!peek_empty.has_value(), "Peek on empty buffer should return nullopt");

  // Add element and peek
  buffer.try_push(123);
  auto peek_result = buffer.try_peek();
  ASSERT(peek_result.has_value(), "Peek should return value");
  ASSERT(peek_result->get() == 123, "Peeked value should be correct");
  ASSERT(buffer.size() == 1, "Peek should not modify size");

  // Pop and verify peek is empty again
  (void)buffer.try_pop();
  auto peek_after_pop = buffer.try_peek();
  ASSERT(!peek_after_pop.has_value(), "Peek should be empty after pop");
}

void test_capacity_and_wraparound() {
  DroppingRingBuffer<int> buffer(4); // Capacity will be rounded to power of 2

  // Fill buffer to capacity
  for (int i = 0; i < 4; ++i) {
    ASSERT(buffer.try_push(i), "Should be able to push to non-full buffer");
  }
  ASSERT(buffer.full(), "Buffer should be full");

  // Try to push when full (should fail with dropping policy)
  ASSERT(!buffer.try_push(999), "Push should fail when buffer is full");

  // Pop one and verify we can push again
  auto popped = buffer.try_pop();
  ASSERT(popped.has_value() && *popped == 0, "First popped value should be 0");
  ASSERT(buffer.try_push(4), "Should be able to push after pop");

  // Verify FIFO order
  for (int i = 1; i < 5; ++i) {
    auto value = buffer.try_pop();
    ASSERT(value.has_value() && *value == i,
           "Values should come out in FIFO order");
  }
}

void test_overflow_policies() {
  // Test dropping policy
  {
    DroppingRingBuffer<int> buffer(2);
    buffer.try_push(1);
    buffer.try_push(2);
    ASSERT(!buffer.try_push(3), "Dropping buffer should reject when full");

    auto v1 = buffer.try_pop();
    auto v2 = buffer.try_pop();
    ASSERT(v1.has_value() && *v1 == 1, "First value should be 1");
    ASSERT(v2.has_value() && *v2 == 2, "Second value should be 2");
  }

  // Test overwriting policy
  {
    OverwritingRingBuffer<int> buffer(2);
    buffer.try_push(1);
    buffer.try_push(2);
    buffer.try_push(3); // Should overwrite 1

    auto v1 = buffer.try_pop();
    auto v2 = buffer.try_pop();
    ASSERT(v1.has_value() && *v1 == 2,
           "First value should be 2 (1 was overwritten)");
    ASSERT(v2.has_value() && *v2 == 3, "Second value should be 3");
  }
}

void test_bulk_operations() {
  BlockingRingBuffer<int> buffer(16);

  // Test bulk push
  std::vector<int> input_data = {1, 2, 3, 4, 5};
  size_t pushed = buffer.try_push_bulk(std::span(input_data));
  ASSERT(pushed == 5, "Should push all 5 elements");
  ASSERT(buffer.size() == 5, "Buffer size should be 5");

  // Test bulk pop
  std::vector<int> output_data(3);
  size_t popped = buffer.try_pop_bulk(std::span(output_data));
  ASSERT(popped == 3, "Should pop 3 elements");
  ASSERT(buffer.size() == 2, "Buffer size should be 2 after bulk pop");

  // Verify data
  for (size_t i = 0; i < 3; ++i) {
    ASSERT(output_data[i] == static_cast<int>(i + 1),
           "Bulk popped data should be correct");
  }
}

void test_pod_specialization() {
  PodBlockingRingBuffer<TestPod> buffer(8);

  TestPod pod1{42, 3.14f, "test1"};
  TestPod pod2{84, 2.71f, "test2"};

  ASSERT(buffer.try_push(pod1), "Should be able to push POD");
  ASSERT(buffer.try_push(pod2), "Should be able to push second POD");

  auto result1 = buffer.try_pop();
  auto result2 = buffer.try_pop();

  ASSERT(result1.has_value() && *result1 == pod1, "First POD should match");
  ASSERT(result2.has_value() && *result2 == pod2, "Second POD should match");
}

void test_pod_bulk_operations() {
  PodBlockingRingBuffer<int> buffer(100);

  // Create test data
  std::vector<int> input(50);
  std::iota(input.begin(), input.end(), 1);

  // Bulk push
  size_t pushed = buffer.try_push_bulk(std::span(input));
  ASSERT(pushed == 50, "Should push all 50 elements");

  // Bulk pop
  std::vector<int> output(50);
  size_t popped = buffer.try_pop_bulk(std::span(output));
  ASSERT(popped == 50, "Should pop all 50 elements");

  // Verify data integrity
  for (size_t i = 0; i < 50; ++i) {
    ASSERT(output[i] == input[i], "Bulk data should match");
  }
}

void test_move_semantics() {
  BlockingRingBuffer<std::string> buffer(8);

  std::string original = "Hello, World!";
  std::string to_move = original;

  buffer.try_push(std::move(to_move));
  ASSERT(to_move.empty(), "Moved string should be empty");

  auto result = buffer.try_pop();
  ASSERT(result.has_value() && *result == original,
         "Moved string should preserve content");
}

void test_thread_safety() {
  constexpr size_t buffer_size = 1024;
  constexpr size_t num_items = 10000;

  BlockingRingBuffer<size_t> buffer(buffer_size);
  std::atomic<bool> producer_done{false};
  std::atomic<size_t> items_produced{0};
  std::atomic<size_t> items_consumed{0};

  // Producer thread
  std::thread producer([&]() {
    for (size_t i = 0; i < num_items; ++i) {
      while (!buffer.try_push(i)) {
        std::this_thread::yield();
      }
      items_produced.fetch_add(1, std::memory_order_relaxed);
    }
    producer_done.store(true, std::memory_order_release);
  });

  // Consumer thread
  std::thread consumer([&]() {
    while (!producer_done.load(std::memory_order_acquire) || !buffer.empty()) {
      if (auto item = buffer.try_pop()) {
        items_consumed.fetch_add(1, std::memory_order_relaxed);
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  ASSERT(items_produced.load() == num_items, "All items should be produced");
  ASSERT(items_consumed.load() == num_items, "All items should be consumed");
  ASSERT(buffer.empty(), "Buffer should be empty at end");
}

void test_clear_operation() {
  BlockingRingBuffer<TestMessage> buffer(8);

  // Add some items
  for (int i = 0; i < 5; ++i) {
    buffer.try_emplace(i, "message " + std::to_string(i));
  }

  ASSERT(buffer.size() == 5, "Buffer should have 5 items");

  buffer.clear();

  ASSERT(buffer.empty(), "Buffer should be empty after clear");
  ASSERT(buffer.size() == 0, "Buffer size should be 0 after clear");
}

void test_move_constructor_and_assignment() {
  BlockingRingBuffer<int> buffer1(8);
  buffer1.try_push(1);
  buffer1.try_push(2);

  // Move constructor
  auto buffer2 = std::move(buffer1);
  ASSERT(buffer2.size() == 2, "Moved buffer should have 2 items");

  auto val1 = buffer2.try_pop();
  auto val2 = buffer2.try_pop();
  ASSERT(val1.has_value() && *val1 == 1, "First value should be 1");
  ASSERT(val2.has_value() && *val2 == 2, "Second value should be 2");

  // Move assignment
  BlockingRingBuffer<int> buffer3(4);
  buffer3.try_push(99);

  buffer3 = std::move(buffer2);
  ASSERT(buffer3.empty(), "Move-assigned buffer should be empty");
}

void test_zero_copy_read_operations() {
  PodBlockingRingBuffer<int> buffer(16);

  // Add test data
  for (int i = 0; i < 10; ++i) {
    buffer.try_push(i * 10);
  }

  // Test single contiguous read view
  auto contiguous_view = buffer.get_contiguous_read_view(5);
  ASSERT(contiguous_view.size() == 5, "Contiguous view should have 5 elements");
  ASSERT(contiguous_view[0] == 0, "First element should be 0");
  ASSERT(contiguous_view[4] == 40, "Fifth element should be 40");

  // Test multiple read views
  auto read_views = buffer.get_read_views(8);
  size_t total_elements = read_views[0].size() + read_views[1].size();
  ASSERT(total_elements == 8, "Total elements in views should be 8");

  // Verify data integrity
  std::vector<int> extracted_data;
  for (const auto &view : read_views) {
    for (const auto &value : view) {
      extracted_data.push_back(value);
    }
  }

  for (size_t i = 0; i < 8; ++i) {
    ASSERT(extracted_data[i] == static_cast<int>(i * 10),
           "Data should match original");
  }

  // Advance read position
  buffer.advance_read(8);
  ASSERT(buffer.size() == 2, "Buffer should have 2 elements remaining");
}

void test_zero_copy_write_operations() {
  PodBlockingRingBuffer<int> buffer(16);

  // Test single write view
  {
    auto write_view = buffer.get_write_view(5);
    ASSERT(write_view.capacity() == 5, "Write view capacity should be 5");

    auto span = write_view.as_span();
    for (size_t i = 0; i < span.size(); ++i) {
      span[i] = static_cast<int>(i * 100);
    }

    write_view.commit(span.size());
    ASSERT(write_view.is_committed(), "Write view should be committed");
  }

  ASSERT(buffer.size() == 5, "Buffer should have 5 elements after commit");

  // Test write view with bulk operation
  {
    auto write_view = buffer.get_write_view(3);
    std::vector<int> data = {999, 888, 777};
    size_t written = write_view.write(std::span(data));

    ASSERT(written == 3, "Should write all 3 elements");
    write_view.commit(written);
  }

  ASSERT(buffer.size() == 8, "Buffer should have 8 elements total");

  // Verify written data
  auto values = {0, 100, 200, 300, 400, 999, 888, 777};
  auto it = values.begin();
  while (auto value = buffer.try_pop()) {
    ASSERT(*value == *it, "Popped value should match expected");
    ++it;
  }
}

void test_zero_copy_wraparound_handling() {
  PodBlockingRingBuffer<int> buffer(8);

  // Fill buffer partially to position head near end
  for (int i = 0; i < 6; ++i) {
    buffer.try_push(i);
  }

  // Pop some elements to advance tail
  buffer.try_pop();
  buffer.try_pop();
  buffer.try_pop();

  // Now head is at position 6, tail at position 3
  // Buffer has capacity for 5 more elements (8 - 3 = 5)
  {
    // Test non-contiguous write view with wraparound
    auto write_view = buffer.get_non_contiguous_write_view(5);

    // Fill using sequential write
    std::vector<int> test_data = {100, 101, 102, 103, 104};
    size_t written = write_view.write(std::span(test_data));
    
    ASSERT(written == 5, "Should write all 5 elements");
    write_view.commit(written);
  }

  // Views auto-commit on destruction

  printf("buffer.size() == %zu\n", buffer.size());
  ASSERT(buffer.size() == 8, "Buffer should be full after wraparound write");
  // Test read views with wraparound
  auto read_views = buffer.get_read_views();
  size_t total_read_elements = read_views[0].size() + read_views[1].size();
  ASSERT(total_read_elements == 8, "Should be able to read all 8 elements");
}

void test_zero_copy_memory_safety() {
  PodBlockingRingBuffer<int> buffer(8);

  // Test empty buffer scenarios
  auto empty_read_view = buffer.get_contiguous_read_view();
  ASSERT(empty_read_view.empty(), "Read view of empty buffer should be empty");

  auto empty_write_view = buffer.get_write_view(0);
  ASSERT(empty_write_view.capacity() == 0,
         "Zero-capacity write view should be empty");

  // Test bounds checking
  buffer.try_push(42);

  try {
    buffer.advance_read(2); // Try to advance more than available
    ASSERT(false, "Should throw exception for over-advance");
  } catch (const std::out_of_range &) {
    // Expected
  }

  // Test write view bounds
  auto write_view = buffer.get_write_view(2);
  auto span = write_view.as_span();
  span[0] = 1;
  span[1] = 2;

  try {
    write_view.commit(3); // Should throw for count > capacity
    ASSERT(false, "Should throw exception for write count overflow");
  } catch (const std::out_of_range &) {
    // Expected
  }
}

void test_zero_copy_performance_characteristics() {
  constexpr size_t large_buffer_size = 1024;
  PodBlockingRingBuffer<int> buffer(large_buffer_size);

  // Test that zero-copy views don't actually copy data
  std::vector<int> test_data(100);
  std::iota(test_data.begin(), test_data.end(), 0);

  // Write data using traditional method
  buffer.try_push_bulk(std::span(test_data));

  // Get zero-copy read view
  auto read_view = buffer.get_contiguous_read_view(100);

  // Verify the view points to the actual buffer memory
  // (this is implementation-specific but important for zero-copy)
  ASSERT(read_view.size() == 100, "Read view should contain all elements");

  // Verify we can process data without additional copying
  int sum1 = 0;
  for (const auto &value : read_view) {
    sum1 += value;
  }

  int expected_sum = (99 * 100) / 2; // Sum of 0 to 99
  ASSERT(sum1 == expected_sum, "Sum should match expected value");

  buffer.advance_read(100);
  ASSERT(buffer.empty(), "Buffer should be empty after advancing read");
}

void test_non_contiguous_write_view_iterator() {
  PodBlockingRingBuffer<int> buffer(8);
  
  // Fill buffer to create wrap-around scenario
  for (int i = 0; i < 6; ++i) {
    buffer.try_push(i);
  }
  buffer.try_pop();
  buffer.try_pop();
  buffer.try_pop();
  
  // Get non-contiguous write view
  auto write_view = buffer.get_non_contiguous_write_view(5);
  
  ASSERT(write_view.total_capacity() == 5, "Total capacity should be 5");
  
  // Test span extraction
  auto max_span = write_view.max_contiguous_span();
  auto first_span = write_view.first_span();
  
  ASSERT(!max_span.empty(), "Max contiguous span should not be empty");
  ASSERT(!first_span.empty(), "First span should not be empty");
  
  // Test iterator functionality
  size_t element_count = 0;
  for (auto it = write_view.begin(); it != write_view.end(); ++it) {
    *it = static_cast<int>(element_count + 1000);
    ++element_count;
  }
  
  ASSERT(element_count == 5, "Iterator should visit all 5 elements");
  
  write_view.commit(5);
  ASSERT(buffer.size() == 8, "Buffer should be full after commit");
  
  // Verify the data was written correctly
  std::vector<int> expected = {3, 4, 5, 1000, 1001, 1002, 1003, 1004};
  for (size_t i = 0; i < expected.size(); ++i) {
    auto value = buffer.try_pop();
    ASSERT(value.has_value() && *value == expected[i], 
           "Iterator-written data should be correct");
  }
}

int main() {
  std::cout << "Running SCSP Ring Buffer Tests\n";
  std::cout << "==============================\n\n";

  try {
    TEST_CASE(basic_construction);
    TEST_CASE(single_element_operations);
    TEST_CASE(emplace_operations);
    TEST_CASE(peek_operations);
    TEST_CASE(capacity_and_wraparound);
    TEST_CASE(overflow_policies);
    TEST_CASE(bulk_operations);
    TEST_CASE(pod_specialization);
    TEST_CASE(pod_bulk_operations);
    TEST_CASE(move_semantics);
    TEST_CASE(thread_safety);
    TEST_CASE(clear_operation);
    TEST_CASE(move_constructor_and_assignment);
    TEST_CASE(zero_copy_read_operations);
    TEST_CASE(zero_copy_write_operations);
    TEST_CASE(zero_copy_wraparound_handling);
    TEST_CASE(zero_copy_memory_safety);
    TEST_CASE(zero_copy_performance_characteristics);
    TEST_CASE(non_contiguous_write_view_iterator);

    std::cout << "\n🎉 All tests passed successfully!\n";
  } catch (const std::exception &e) {
    std::cerr << "\n❌ Test failed with exception: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
