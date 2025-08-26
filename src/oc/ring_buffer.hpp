#pragma once

/**
 * @file ring_buffer.hpp
 * @brief High-performance Single Consumer Single Producer (SCSP) ring buffer implementation
 * 
 * This header provides a comprehensive ring buffer implementation with modern C++ features:
 * 
 * Key Features:
 * - Lock-free design using atomic operations with optimized memory ordering
 * - Cache-line aligned structures to minimize false sharing
 * - Move semantics and perfect forwarding for efficient data transfer
 * - C++20/23 concepts for type safety and clear interface requirements
 * - Template specializations for POD types with memcpy optimizations
 * - Multiple overflow handling policies (Block, Drop, Overwrite)
 * - Bulk operations for improved throughput
 * - Comprehensive test suite and benchmarks
 * 
 * Usage Examples:
 * 
 * @code
 * // Basic usage with blocking policy
 * oc::rb::BlockingRingBuffer<std::string> buffer(1024);
 * buffer.try_push("Hello");
 * auto result = buffer.try_pop(); // Returns std::optional<std::string>
 * 
 * // POD types with optimized bulk operations
 * oc::rb::PodBlockingRingBuffer<int> pod_buffer(1024);
 * std::vector<int> data = {1, 2, 3, 4, 5};
 * size_t pushed = pod_buffer.try_push_bulk(std::span(data));
 * 
 * // Different overflow policies
 * oc::rb::DroppingRingBuffer<int> dropping(512);    // Drops when full
 * oc::rb::OverwritingRingBuffer<int> overwriting(512); // Overwrites oldest
 * 
 * // Producer-consumer pattern
 * std::thread producer([&buffer]() {
 *     for (int i = 0; i < 1000; ++i) {
 *         buffer.try_emplace(i, "data"); // Perfect forwarding
 *     }
 * });
 * 
 * std::thread consumer([&buffer]() {
 *     while (auto item = buffer.try_pop()) {
 *         process(*item);
 *     }
 * });
 * @endcode
 * 
 * Performance Characteristics:
 * - Sub-microsecond latency for individual operations
 * - High throughput with bulk operations (>1M ops/sec)
 * - Minimal memory overhead with power-of-2 sizing
 * - Excellent cache locality and memory access patterns
 * 
 * Thread Safety:
 * - Designed for single producer, single consumer scenarios
 * - Uses atomic operations with acquire-release semantics
 * - No locks or blocking synchronization primitives
 * 
 * @author Generated for warp-pipe-stdexec project
 * @version 1.0
 */

#include "rb/basic_rb.hpp"
#include "rb/pod_rb.hpp"

namespace oc {

// Re-export the main ring buffer types for convenience
using rb::OverflowPolicy;
using rb::BasicRingBuffer;
using rb::PodRingBuffer;

// Convenience aliases
template<rb::RingBufferStorable T>
using RingBuffer = rb::BlockingRingBuffer<T>;

template<rb::RingBufferStorable T>
using DroppingRingBuffer = rb::DroppingRingBuffer<T>;

template<rb::RingBufferStorable T>
using OverwritingRingBuffer = rb::OverwritingRingBuffer<T>;

template<rb::PodType T>
using FastPodRingBuffer = rb::PodBlockingRingBuffer<T>;

template<rb::PodType T>
using PodDroppingRingBuffer = rb::PodDroppingRingBuffer<T>;

template<rb::PodType T>
using PodOverwritingRingBuffer = rb::PodOverwritingRingBuffer<T>;

} // namespace oc
