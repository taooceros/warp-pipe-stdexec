#include "oc/containers/small_vector.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace oc::containers;

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

// Test structure for move semantics
struct TestItem {
    int value;
    bool* destroyed_flag;
    
    TestItem(int v = 0, bool* flag = nullptr) 
        : value(v), destroyed_flag(flag) {}
    
    TestItem(const TestItem& other) 
        : value(other.value), destroyed_flag(other.destroyed_flag) {}
    
    TestItem(TestItem&& other) noexcept 
        : value(other.value), destroyed_flag(other.destroyed_flag) {
        other.value = -1;
        other.destroyed_flag = nullptr;
    }
    
    TestItem& operator=(const TestItem& other) {
        value = other.value;
        destroyed_flag = other.destroyed_flag;
        return *this;
    }
    
    TestItem& operator=(TestItem&& other) noexcept {
        value = other.value;
        destroyed_flag = other.destroyed_flag;
        other.value = -1;
        other.destroyed_flag = nullptr;
        return *this;
    }
    
    ~TestItem() {
        if (destroyed_flag) {
            *destroyed_flag = true;
        }
    }
    
    bool operator==(const TestItem& other) const {
        return value == other.value;
    }
};

void test_basic_construction() {
    // Default constructor
    small_vector<int, 4> vec1;
    ASSERT(vec1.empty(), "Default constructed vector should be empty");
    ASSERT(vec1.size() == 0, "Default constructed vector size should be 0");
    ASSERT(vec1.capacity() >= 4, "Capacity should be at least stack size");
    ASSERT(vec1.is_using_stack_storage(), "Should use stack storage initially");
    
    // Constructor with size
    small_vector<int, 4> vec2(3);
    ASSERT(vec2.size() == 3, "Size constructor should set correct size");
    ASSERT(vec2.is_using_stack_storage(), "Should use stack storage for small size");
    
    // Constructor with size and value
    small_vector<int, 4> vec3(2, 42);
    ASSERT(vec3.size() == 2, "Size should be 2");
    ASSERT(vec3[0] == 42 && vec3[1] == 42, "All elements should have specified value");
    
    // Constructor from initializer list
    small_vector<int, 4> vec4{1, 2, 3};
    ASSERT(vec4.size() == 3, "Initializer list size should be 3");
    ASSERT(vec4[0] == 1 && vec4[1] == 2 && vec4[2] == 3, "Elements should match initializer list");
}

void test_stack_only_storage() {
    // All sizes should use stack storage
    small_vector<int, 4> small_vec;
    for (int i = 0; i < 4; ++i) {
        small_vec.push_back(i);
    }
    ASSERT(small_vec.is_using_stack_storage(), "Should always use stack storage");
    ASSERT(small_vec.size() == 4, "Size should be 4");
    
    // Trying to grow beyond capacity should panic (we can't test this directly)
    // but we can verify the behavior up to capacity
    for (int i = 0; i < 4; ++i) {
        ASSERT(small_vec[i] == i, "Data should be correct");
    }
}

void test_element_access() {
    small_vector<int, 4> vec{10, 20, 30, 40};
    
    // Operator[]
    ASSERT(vec[0] == 10, "First element should be 10");
    ASSERT(vec[3] == 40, "Last element should be 40");
    
    // at()
    ASSERT(vec.at(1) == 20, "Second element should be 20");
    
    try {
        vec.at(10); // Should throw
        ASSERT(false, "at() should throw for out of bounds access");
    } catch (const std::out_of_range&) {
        // Expected
    }
    
    // front() and back()
    ASSERT(vec.front() == 10, "front() should return first element");
    ASSERT(vec.back() == 40, "back() should return last element");
    
    // data()
    ASSERT(vec.data()[2] == 30, "data() should provide direct access");
}

void test_iterators() {
    small_vector<int, 4> vec{1, 2, 3, 4}; // Fits in stack storage
    
    // Basic iteration
    int expected = 1;
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        ASSERT(*it == expected, "Iterator should visit elements in order");
        ++expected;
    }
    
    // Range-based for loop
    expected = 1;
    for (const auto& item : vec) {
        ASSERT(item == expected, "Range-based for should work correctly");
        ++expected;
    }
    
    // Const iterators
    const auto& const_vec = vec;
    expected = 1;
    for (auto it = const_vec.cbegin(); it != const_vec.cend(); ++it) {
        ASSERT(*it == expected, "Const iterator should work correctly");
        ++expected;
    }
}

void test_modifiers() {
    small_vector<int, 4> vec;
    
    // push_back
    vec.push_back(10);
    vec.push_back(20);
    ASSERT(vec.size() == 2, "Size should be 2 after two pushes");
    ASSERT(vec[0] == 10 && vec[1] == 20, "Elements should be in correct order");
    
    // emplace_back
    vec.emplace_back(30);
    ASSERT(vec.size() == 3, "Size should be 3 after emplace");
    ASSERT(vec.back() == 30, "Emplaced element should be correct");
    
    // pop_back
    vec.pop_back();
    ASSERT(vec.size() == 2, "Size should be 2 after pop");
    ASSERT(vec.back() == 20, "Last element should be 20 after pop");
    
    // clear
    vec.clear();
    ASSERT(vec.empty(), "Vector should be empty after clear");
}

void test_resize_operations() {
    small_vector<int, 8> vec{1, 2, 3}; // Use larger capacity for resize tests
    
    // Resize larger
    vec.resize(5);
    ASSERT(vec.size() == 5, "Size should be 5 after resize");
    ASSERT(vec[0] == 1 && vec[1] == 2 && vec[2] == 3, "Original elements should be preserved");
    ASSERT(vec[3] == 0 && vec[4] == 0, "New elements should be default constructed");
    
    // Resize with value
    vec.resize(7, 99);
    ASSERT(vec.size() == 7, "Size should be 7 after resize with value");
    ASSERT(vec[5] == 99 && vec[6] == 99, "New elements should have specified value");
    
    // Resize smaller
    vec.resize(3);
    ASSERT(vec.size() == 3, "Size should be 3 after shrinking");
    ASSERT(vec[0] == 1 && vec[1] == 2 && vec[2] == 3, "Remaining elements should be preserved");
}

void test_copy_semantics() {
    small_vector<int, 4> vec1{1, 2, 3, 4}; // Fits in stack storage
    
    // Copy constructor
    small_vector<int, 4> vec2(vec1);
    ASSERT(vec2.size() == vec1.size(), "Copy should have same size");
    ASSERT(vec2 == vec1, "Copy should be equal to original");
    
    // Copy assignment
    small_vector<int, 4> vec3;
    vec3 = vec1;
    ASSERT(vec3.size() == vec1.size(), "Assignment should have same size");
    ASSERT(vec3 == vec1, "Assignment should be equal to original");
    
    // Modify copy to ensure independence
    vec2[0] = 999;
    ASSERT(vec1[0] != vec2[0], "Copies should be independent");
}

void test_move_semantics() {
    bool destroyed1 = false, destroyed2 = false, destroyed3 = false;
    
    {
        small_vector<TestItem, 4> vec1;
        vec1.emplace_back(1, &destroyed1);
        vec1.emplace_back(2, &destroyed2);
        vec1.emplace_back(3, &destroyed3);
        
        // Move constructor
        small_vector<TestItem, 4> vec2(std::move(vec1));
        ASSERT(vec2.size() == 3, "Move target should have correct size");
        ASSERT(vec2[0].value == 1, "Moved elements should be correct");
        ASSERT(vec1.empty(), "Move source should be empty");
        
        // Move assignment
        small_vector<TestItem, 4> vec3;
        vec3 = std::move(vec2);
        ASSERT(vec3.size() == 3, "Move assignment target should have correct size");
        ASSERT(vec2.empty(), "Move assignment source should be empty");
    }
    
    // Elements should be destroyed only once
    ASSERT(destroyed1 && destroyed2 && destroyed3, "All elements should be destroyed");
}

void test_stack_only_move() {
    // Test moving with stack storage only
    small_vector<int, 4> vec1;
    for (int i = 0; i < 4; ++i) {
        vec1.push_back(i);
    }
    ASSERT(vec1.is_using_stack_storage(), "Should be using stack storage");
    
    // Move to another vector
    small_vector<int, 4> vec2(std::move(vec1));
    ASSERT(vec2.is_using_stack_storage(), "Target should use stack storage");
    ASSERT(vec2.size() == 4, "All elements should be moved");
    ASSERT(vec1.empty(), "Source should be empty");
    
    // Verify data integrity
    for (int i = 0; i < 4; ++i) {
        ASSERT(vec2[i] == i, "Data should be preserved during stack move");
    }
}

void test_reserve_functionality() {
    small_vector<int, 4> vec;
    
    // Reserve within stack capacity
    vec.reserve(3);
    ASSERT(vec.is_using_stack_storage(), "Should use stack storage");
    ASSERT(vec.capacity() == 4, "Capacity should be fixed stack size");
    
    // Reserve equal to stack capacity
    vec.reserve(4);
    ASSERT(vec.is_using_stack_storage(), "Should still use stack storage");
    ASSERT(vec.capacity() == 4, "Capacity should remain fixed");
    
    // Note: Reserving beyond stack capacity would panic, so we don't test it
}

void test_comparison_operators() {
    small_vector<int, 4> vec1{1, 2, 3};
    small_vector<int, 4> vec2{1, 2, 3};
    small_vector<int, 4> vec3{1, 2, 4};
    small_vector<int, 4> vec4{1, 2};
    
    // Equality
    ASSERT(vec1 == vec2, "Equal vectors should compare equal");
    ASSERT(!(vec1 == vec3), "Different vectors should not compare equal");
    ASSERT(!(vec1 == vec4), "Different sized vectors should not compare equal");
    
    // Inequality
    ASSERT(!(vec1 != vec2), "Equal vectors should not compare not equal");
    ASSERT(vec1 != vec3, "Different vectors should compare not equal");
    
    // Less than
    ASSERT(vec1 < vec3, "Lexicographically smaller vector should compare less");
    ASSERT(vec4 < vec1, "Shorter vector should compare less when prefix matches");
}

void test_performance_characteristics() {
    // Test that stack storage is always used
    small_vector<int, 8> small_vec;
    for (size_t i = 0; i < 8; ++i) {
        small_vec.push_back(static_cast<int>(i));
    }
    ASSERT(small_vec.is_using_stack_storage(), "Should always use stack storage");
    ASSERT(small_vec.size() == 8, "Should fill up to capacity");
    
    // Verify all data is intact
    for (size_t i = 0; i < 8; ++i) {
        ASSERT(small_vec[i] == static_cast<int>(i), "Data should be correct");
    }
    
    // Test that we don't have heap overhead
    constexpr size_t expected_size = sizeof(small_vector<int, 8>);
    constexpr size_t stack_storage_size = 8 * sizeof(int);
    constexpr size_t metadata_size = 2 * sizeof(size_t); // size_ and capacity_
    ASSERT(expected_size >= stack_storage_size + metadata_size, "Size should be reasonable for stack-only storage");
}

void test_ring_buffer_use_case() {
    // Simulate typical ring buffer view usage (1-2 segments)
    struct Segment {
        int* data;
        size_t size;
        
        Segment(int* d, size_t s) : data(d), size(s) {}
        
        bool operator==(const Segment& other) const {
            return data == other.data && size == other.size;
        }
    };
    
    small_vector2<Segment> segments; // This is the typical use case
    
    int buffer1[10];
    int buffer2[5];
    
    // Single segment case
    segments.emplace_back(buffer1, 10);
    ASSERT(segments.size() == 1, "Should have one segment");
    ASSERT(segments.is_using_stack_storage(), "Should use stack storage for typical case");
    
    // Two segment case (wrap-around)
    segments.emplace_back(buffer2, 5);
    ASSERT(segments.size() == 2, "Should have two segments");
    ASSERT(segments.is_using_stack_storage(), "Should still use stack storage for two segments");
    
    // Verify data
    ASSERT(segments[0].data == buffer1 && segments[0].size == 10, "First segment should be correct");
    ASSERT(segments[1].data == buffer2 && segments[1].size == 5, "Second segment should be correct");
    
    // This is the optimal case - no heap allocation for typical ring buffer views
}

int main() {
    std::cout << "Running Small Vector Tests\n";
    std::cout << "==========================\n\n";
    
    try {
        TEST_CASE(basic_construction);
        TEST_CASE(stack_only_storage);
        TEST_CASE(element_access);
        TEST_CASE(iterators);
        TEST_CASE(modifiers);
        TEST_CASE(resize_operations);
        TEST_CASE(copy_semantics);
        TEST_CASE(move_semantics);
        TEST_CASE(stack_only_move);
        TEST_CASE(reserve_functionality);
        TEST_CASE(comparison_operators);
        TEST_CASE(performance_characteristics);
        TEST_CASE(ring_buffer_use_case);
        
        std::cout << "\n🎉 All small vector tests passed successfully!\n";
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed with exception: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
