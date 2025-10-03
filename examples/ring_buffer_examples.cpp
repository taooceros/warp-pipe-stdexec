#include "oc/rb/basic_rb.hpp"
#include "oc/rb/pod_rb.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <random>
#include <numeric>
#include <cstring>

using namespace oc::rb;

// Example data structures for testing
struct Message {
    uint64_t id;
    std::string content;
    std::chrono::steady_clock::time_point timestamp;
    
    Message() = default;
    Message(uint64_t i, std::string c) 
        : id(i), content(std::move(c)), timestamp(std::chrono::steady_clock::now()) {}
};

struct PodData {
    int64_t id;
    double value;
    char name[32];
};

void example_basic_usage() {
    std::cout << "\n=== Basic Ring Buffer Usage ===\n";
    
    // Create a blocking ring buffer for messages
    BlockingRingBuffer<Message> msg_buffer(1024);
    
    std::cout << "Buffer capacity: " << msg_buffer.capacity() << "\n";
    std::cout << "Initial size: " << msg_buffer.size() << "\n";
    std::cout << "Is empty: " << msg_buffer.empty() << "\n";
    
    // Push some messages
    for (int i = 0; i < 5; ++i) {
        msg_buffer.try_push(Message{static_cast<uint64_t>(i), "Message " + std::to_string(i)});
    }
    
    std::cout << "After pushing 5 messages - size: " << msg_buffer.size() << "\n";
    
    // Pop and display messages
    while (auto msg = msg_buffer.try_pop()) {
        std::cout << "Popped: ID=" << msg->id << ", Content=" << msg->content << "\n";
    }
    
    std::cout << "After popping all - size: " << msg_buffer.size() << "\n";
}

void example_producer_consumer() {
    std::cout << "\n=== Producer-Consumer Example ===\n";
    
    constexpr size_t buffer_size = 1000;
    constexpr size_t num_messages = 10000;
    
    DroppingRingBuffer<int> buffer(buffer_size);
    std::atomic<bool> finished{false};
    std::atomic<size_t> produced{0};
    std::atomic<size_t> consumed{0};
    
    // Producer thread
    std::thread producer([&buffer, &finished, &produced]() {
        for (size_t i = 0; i < num_messages; ++i) {
            while (!buffer.try_push(static_cast<int>(i))) {
                std::this_thread::yield(); // Wait for space
            }
            produced.fetch_add(1, std::memory_order_relaxed);
        }
        finished.store(true, std::memory_order_release);
    });
    
    // Consumer thread
    std::thread consumer([&buffer, &finished, &consumed]() {
        while (!finished.load(std::memory_order_acquire) || !buffer.empty()) {
            if (auto value = buffer.try_pop()) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    std::cout << "Produced: " << produced.load() << " messages\n";
    std::cout << "Consumed: " << consumed.load() << " messages\n";
    std::cout << "Final buffer size: " << buffer.size() << "\n";
}

void example_pod_optimizations() {
    std::cout << "\n=== POD Ring Buffer Optimizations ===\n";
    
    constexpr size_t buffer_size = 1024;
    PodBlockingRingBuffer<PodData> pod_buffer(buffer_size);
    
    // Generate test data
    std::vector<PodData> test_data;
    test_data.reserve(500);
    
    for (int i = 0; i < 500; ++i) {
        PodData data{};
        data.id = i;
        data.value = i * 3.14159;
        std::snprintf(data.name, sizeof(data.name), "Item_%d", i);
        test_data.push_back(data);
    }
    
    // Bulk push operation
    auto start = std::chrono::high_resolution_clock::now();
    size_t pushed = pod_buffer.try_push_bulk(std::span(test_data));
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Bulk pushed " << pushed << " POD items in " << duration.count() << " μs\n";
    std::cout << "Buffer size after bulk push: " << pod_buffer.size() << "\n";
    
    // Bulk pop operation
    std::vector<PodData> output(pushed);
    start = std::chrono::high_resolution_clock::now();
    size_t popped = pod_buffer.try_pop_bulk(std::span(output));
    end = std::chrono::high_resolution_clock::now();
    
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Bulk popped " << popped << " POD items in " << duration.count() << " μs\n";
    std::cout << "Buffer size after bulk pop: " << pod_buffer.size() << "\n";
    
    // Verify data integrity
    bool integrity_ok = true;
    for (size_t i = 0; i < popped; ++i) {
        if (output[i].id != static_cast<int64_t>(i) || 
            std::abs(output[i].value - i * 3.14159) > 1e-10) {
            integrity_ok = false;
            break;
        }
    }
    std::cout << "Data integrity check: " << (integrity_ok ? "PASSED" : "FAILED") << "\n";
}

void example_zero_copy_operations() {
    std::cout << "\n=== Zero-Copy Operations Example ===\n";
    
    constexpr size_t buffer_size = 1024;
    PodBlockingRingBuffer<int> buffer(buffer_size);
    
    // Zero-copy write example - contiguous
    std::cout << "\n-- Zero-Copy Writing (Contiguous) --\n";
    {
        auto write_view = buffer.get_write_view(100);
        std::cout << "Got contiguous write view with capacity: " << write_view.capacity() << "\n";
        
        // Get direct span access for manual writing
        auto span = write_view.as_span();
        for (size_t i = 0; i < std::min(span.size(), size_t(50)); ++i) {
            span[i] = static_cast<int>(i * 2);
        }
        
        // Commit the written data
        write_view.commit(std::min(span.size(), size_t(50)));
        std::cout << "Committed " << std::min(span.size(), size_t(50)) << " elements via direct span access\n";
    }
    
    // Zero-copy write example - non-contiguous (handles wrap-around)
    std::cout << "\n-- Zero-Copy Writing (Non-Contiguous) --\n";
    {
        auto write_view = buffer.get_non_contiguous_write_view(50);
        std::cout << "Got non-contiguous write view with " << write_view.segment_count() 
                  << " segment(s), total capacity: " << write_view.total_capacity() << "\n";
        
        // Use the largest contiguous span for bulk writing
        auto max_span = write_view.max_contiguous_span();
        std::vector<int> source_data = {1000, 1001, 1002, 1003, 1004};
        
        const auto to_write = std::min(source_data.size(), max_span.size());
        std::memcpy(max_span.data(), source_data.data(), to_write * sizeof(int));
        
        write_view.commit(to_write);
        std::cout << "Wrote " << to_write << " elements using max contiguous span\n";
    }
    
    std::cout << "Buffer size after zero-copy write: " << buffer.size() << "\n";
    
    // Zero-copy read example
    std::cout << "\n-- Zero-Copy Reading --\n";
    {
        auto read_views = buffer.get_read_views(30);
        
        std::cout << "Got " << (read_views[1].empty() ? 1 : 2) << " read view(s)\n";
        
        size_t total_processed = 0;
        for (const auto& view : read_views) {
            if (!view.empty()) {
                std::cout << "Processing view with " << view.size() << " elements: ";
                
                // Process first few elements without copying
                for (size_t i = 0; i < std::min(view.size(), size_t(5)); ++i) {
                    std::cout << view[i] << " ";
                }
                if (view.size() > 5) std::cout << "...";
                std::cout << "\n";
                
                total_processed += view.size();
            }
        }
        
        // Advance read position to consume the data
        buffer.advance_read(total_processed);
        std::cout << "Consumed " << total_processed << " elements via zero-copy\n";
    }
    
    std::cout << "Buffer size after zero-copy read: " << buffer.size() << "\n";
    
    // Demonstrate contiguous vs wrapped access
    std::cout << "\n-- Contiguous Access Pattern --\n";
    
    // Fill buffer close to capacity to demonstrate wrap-around
    for (int i = 0; i < 900; ++i) {
        buffer.try_push(i + 2000);
    }
    
    // Get contiguous read view (may be smaller than total available)
    auto contiguous_view = buffer.get_contiguous_read_view();
    std::cout << "Contiguous view size: " << contiguous_view.size() 
              << " (total available: " << buffer.size() << ")\n";
    
    // Process without copying
    int sum = 0;
    for (const auto& value : contiguous_view) {
        sum += value;
    }
    std::cout << "Sum of contiguous values: " << sum << "\n";
    
    buffer.advance_read(contiguous_view.size());
}

void benchmark_zero_copy_performance() {
    std::cout << "\n=== Zero-Copy Performance Benchmark ===\n";
    
    constexpr size_t buffer_size = 8192;
    constexpr size_t iterations = 100000;
    constexpr size_t batch_size = 100;
    
    PodBlockingRingBuffer<int> buffer(buffer_size);
    
    // Benchmark traditional copy operations
    {
        std::vector<int> data(batch_size);
        std::iota(data.begin(), data.end(), 0);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < iterations; ++i) {
            // Traditional copy approach
            buffer.try_push_bulk(std::span(data));
            
            std::vector<int> output(batch_size);
            buffer.try_pop_bulk(std::span(output));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        std::cout << "Traditional copy operations: " << iterations * batch_size * 2 
                  << " ops in " << duration.count() << " μs\n";
        std::cout << "Average: " << static_cast<double>(duration.count() * 1000) / (iterations * batch_size * 2) 
                  << " ns per operation\n";
    }
    
    // Benchmark zero-copy operations
    {
        std::vector<int> source_data(batch_size);
        std::iota(source_data.begin(), source_data.end(), 0);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < iterations; ++i) {
            // Zero-copy write
            {
                auto write_view = buffer.get_write_view(batch_size);
                auto written = write_view.write(std::span(source_data));
                write_view.commit(written);
            }
            
            // Zero-copy read
            {
                auto read_view = buffer.get_contiguous_read_view(batch_size);
                // Process data directly from buffer (simulate work)
                volatile int sum = 0;
                for (const auto& value : read_view) {
                    sum += value;
                }
                buffer.advance_read(read_view.size());
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        std::cout << "Zero-copy operations: " << iterations * batch_size * 2 
                  << " ops in " << duration.count() << " μs\n";
        std::cout << "Average: " << static_cast<double>(duration.count() * 1000) / (iterations * batch_size * 2) 
                  << " ns per operation\n";
    }
    
    // Memory bandwidth test
    {
        constexpr size_t large_batch = 1000;
        std::vector<int> large_data(large_batch);
        std::iota(large_data.begin(), large_data.end(), 0);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < 1000; ++i) {
            auto write_view = buffer.get_write_view(large_batch);
            auto written = write_view.write(std::span(large_data));
            write_view.commit(written);
            
            auto read_view = buffer.get_contiguous_read_view(large_batch);
            buffer.advance_read(read_view.size());
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        const size_t bytes_processed = 1000 * large_batch * sizeof(int) * 2; // read + write
        const double mb_per_sec = (bytes_processed / 1024.0 / 1024.0) / (duration.count() / 1000000.0);
        
        std::cout << "Memory bandwidth test: " << mb_per_sec << " MB/s\n";
    }
}

void example_overflow_policies() {
    std::cout << "\n=== Overflow Policy Examples ===\n";
    
    constexpr size_t small_buffer_size = 4;
    
    // Dropping policy
    std::cout << "\n-- Dropping Policy --\n";
    DroppingRingBuffer<int> dropping_buffer(small_buffer_size);
    
    for (int i = 0; i < 8; ++i) {
        bool success = dropping_buffer.try_push(i);
        std::cout << "Push " << i << ": " << (success ? "SUCCESS" : "DROPPED") 
                  << " (size: " << dropping_buffer.size() << ")\n";
    }
    
    // Overwriting policy
    std::cout << "\n-- Overwriting Policy --\n";
    OverwritingRingBuffer<int> overwriting_buffer(small_buffer_size);
    
    for (int i = 0; i < 8; ++i) {
        overwriting_buffer.try_push(i);
        std::cout << "Push " << i << " (size: " << overwriting_buffer.size() << ")\n";
    }
    
    std::cout << "Remaining values in overwriting buffer:\n";
    while (auto value = overwriting_buffer.try_pop()) {
        std::cout << "  " << *value << "\n";
    }
}

void benchmark_performance() {
    std::cout << "\n=== Performance Benchmark ===\n";
    
    constexpr size_t buffer_size = 8192;
    constexpr size_t iterations = 1000000;
    
    // Benchmark basic ring buffer
    {
        BlockingRingBuffer<int> buffer(buffer_size);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < iterations; ++i) {
            buffer.try_push(static_cast<int>(i));
            if (i % 2 == 1) { // Pop every other iteration to prevent overflow
                (void)buffer.try_pop();
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        
        std::cout << "Basic ring buffer: " << iterations << " operations in " 
                  << duration.count() / 1000 << " μs\n";
        std::cout << "Average: " << static_cast<double>(duration.count()) / iterations 
                  << " ns per operation\n";
    }
    
    // Benchmark POD ring buffer
    {
        PodBlockingRingBuffer<int> buffer(buffer_size);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < iterations; ++i) {
            buffer.try_push(static_cast<int>(i));
            if (i % 2 == 1) { // Pop every other iteration to prevent overflow
                (void)buffer.try_pop();
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        
        std::cout << "POD ring buffer: " << iterations << " operations in " 
                  << duration.count() / 1000 << " μs\n";
        std::cout << "Average: " << static_cast<double>(duration.count()) / iterations 
                  << " ns per operation\n";
    }
    
    // Benchmark bulk operations
    {
        PodBlockingRingBuffer<int> buffer(buffer_size);
        constexpr size_t bulk_size = 1000;
        constexpr size_t bulk_iterations = iterations / bulk_size;
        
        std::vector<int> data(bulk_size);
        std::iota(data.begin(), data.end(), 0);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < bulk_iterations; ++i) {
            buffer.try_push_bulk(std::span(data));
            std::vector<int> output(bulk_size);
            buffer.try_pop_bulk(std::span(output));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        
        std::cout << "Bulk operations: " << bulk_iterations * bulk_size * 2 
                  << " operations in " << duration.count() / 1000 << " μs\n";
        std::cout << "Average: " << static_cast<double>(duration.count()) / (bulk_iterations * bulk_size * 2) 
                  << " ns per operation\n";
    }
}

int main() {
    std::cout << "SCSP Ring Buffer Examples and Benchmarks\n";
    std::cout << "=========================================\n";
    
    try {
        example_basic_usage();
        example_producer_consumer();
        example_pod_optimizations();
        example_zero_copy_operations();
        example_overflow_policies();
        benchmark_performance();
        benchmark_zero_copy_performance();
        
        std::cout << "\nAll examples completed successfully!\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
