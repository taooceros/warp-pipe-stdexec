#include <iostream>
#include <vector>
#include <span>
#include "oc/ring_buffer.hpp"

#include <stdexec/execution.hpp>
#include <doca_stdexec/buf.hpp>


int main(int argc, char** argv) {
    std::cout << "High-Performance SCSP Ring Buffer Demo\n";
    std::cout << "======================================\n\n";
    
    // Demonstrate basic ring buffer usage
    oc::RingBuffer<std::string> buffer(1024);
    
    std::cout << "Created ring buffer with capacity: " << buffer.capacity() << "\n";
    
    // Push some data
    buffer.try_push("Hello");
    buffer.try_push("World");
    buffer.try_emplace("from C++23!");
    
    std::cout << "Buffer size after pushes: " << buffer.size() << "\n";
    
    // Pop and display
    while (auto item = buffer.try_pop()) {
        std::cout << "Popped: " << *item << "\n";
    }
    
    // Demonstrate POD optimizations
    oc::FastPodRingBuffer<int> pod_buffer(512);
    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    
    size_t pushed = pod_buffer.try_push_bulk(std::span(data));
    std::cout << "\nBulk pushed " << pushed << " integers to POD buffer\n";
    
    std::vector<int> output(pushed);
    size_t popped = pod_buffer.try_pop_bulk(std::span(output));
    std::cout << "Bulk popped " << popped << " integers: ";
    for (int val : output) {
        std::cout << val << " ";
    }
    std::cout << "\n";
    
    // Demonstrate zero-copy operations
    std::cout << "\n--- Zero-Copy Operations ---\n";
    
    // Zero-copy write
    {
        auto write_view = pod_buffer.get_write_view(5);
        auto span = write_view.as_span();
        for (size_t i = 0; i < span.size(); ++i) {
            span[i] = static_cast<int>(100 + i);
        }
        write_view.commit(span.size());
        std::cout << "Zero-copy wrote " << span.size() << " elements\n";
    }
    
    // Zero-copy read
    {
        auto read_view = pod_buffer.get_contiguous_read_view();
        std::cout << "Zero-copy read " << read_view.size() << " elements: ";
        for (const auto& val : read_view) {
            std::cout << val << " ";
        }
        std::cout << "\n";
        
        pod_buffer.advance_read(read_view.size());
    }
    
    std::cout << "\n✅ Ring buffer demonstration completed successfully!\n";
    
    return 0;
}
