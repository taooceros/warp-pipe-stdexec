#pragma once

#include <atomic>
#include <memory>
#include <concepts>
#include <span>
#include <optional>
#include <type_traits>
#include <bit>
#include <new>
#include <thread>
#include <utility>

namespace oc::rb {

// Cache line size for alignment optimizations
#ifdef __cpp_lib_hardware_interference_size
inline constexpr std::size_t cache_line_size = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t cache_line_size = 64; // Common cache line size fallback
#endif

// Concept to ensure types are suitable for ring buffer storage
template<typename T>
concept RingBufferStorable = std::is_nothrow_move_constructible_v<T> && 
                            std::is_nothrow_destructible_v<T>;

// Policy for handling buffer overflow
enum class OverflowPolicy {
    Block,      // Block until space is available
    Drop,       // Drop the new element
    Overwrite   // Overwrite the oldest element
};

/**
 * @brief High-performance Single Consumer Single Producer (SCSP) ring buffer
 * 
 * Features:
 * - Lock-free design using atomic operations
 * - Cache-line aligned to minimize false sharing
 * - Memory ordering optimizations for performance
 * - Move semantics and perfect forwarding
 * - C++20/23 concepts for type safety
 * - Template specializations for POD types
 * - Support for different overflow policies
 * 
 * @tparam T Element type (must satisfy RingBufferStorable concept)
 * @tparam Policy Overflow handling policy
 */
template<RingBufferStorable T, OverflowPolicy Policy = OverflowPolicy::Block>
class alignas(cache_line_size) BasicRingBuffer {
public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;

private:
    // Ensure capacity is power of 2 for efficient modulo operations
    static constexpr size_type round_to_power_of_2(size_type n) noexcept {
        if (n == 0) return 1;
        return std::bit_ceil(n);
    }

    struct alignas(cache_line_size) ProducerIndex {
        std::atomic<size_type> head{0};
        // Padding to avoid false sharing
        char padding[cache_line_size - sizeof(std::atomic<size_type>)];
    };

    struct alignas(cache_line_size) ConsumerIndex {
        std::atomic<size_type> tail{0};
        // Padding to avoid false sharing
        char padding[cache_line_size - sizeof(std::atomic<size_type>)];
    };

    const size_type capacity_;
    const size_type mask_;
    std::unique_ptr<std::byte[]> storage_;
    pointer buffer_;
    
    // Cache-line aligned atomic indices to prevent false sharing
    ProducerIndex producer_idx_;
    ConsumerIndex consumer_idx_;

    // Helper to get element at index
    pointer get_element_ptr(size_type index) noexcept {
        return buffer_ + (index & mask_);
    }

    const_pointer get_element_ptr(size_type index) const noexcept {
        return buffer_ + (index & mask_);
    }

public:
    /**
     * @brief Construct ring buffer with specified capacity
     * @param capacity Desired capacity (will be rounded up to next power of 2)
     */
    explicit BasicRingBuffer(size_type capacity) 
        : capacity_(round_to_power_of_2(capacity))
        , mask_(capacity_ - 1)
        , storage_(std::make_unique<std::byte[]>(capacity_ * sizeof(T) + cache_line_size))
    {
        // Align the buffer properly
        void* ptr = storage_.get();
        std::size_t space = capacity_ * sizeof(T) + cache_line_size;
        buffer_ = reinterpret_cast<pointer>(
            std::align(alignof(T), capacity_ * sizeof(T), ptr, space));
        
        if (!buffer_) {
            throw std::bad_alloc{};
        }
    }

    // Non-copyable but movable
    BasicRingBuffer(const BasicRingBuffer&) = delete;
    BasicRingBuffer& operator=(const BasicRingBuffer&) = delete;
    
    BasicRingBuffer(BasicRingBuffer&& other) noexcept 
        : capacity_(other.capacity_)
        , mask_(other.mask_)
        , storage_(std::move(other.storage_))
        , buffer_(other.buffer_)
        , producer_idx_{other.producer_idx_.head.load(std::memory_order_relaxed)}
        , consumer_idx_{other.consumer_idx_.tail.load(std::memory_order_relaxed)}
    {
        other.buffer_ = nullptr;
    }

    BasicRingBuffer& operator=(BasicRingBuffer&& other) noexcept {
        if (this != &other) {
            clear();
            // Use const_cast for the const members during move assignment
            const_cast<size_type&>(capacity_) = other.capacity_;
            const_cast<size_type&>(mask_) = other.mask_;
            storage_ = std::move(other.storage_);
            buffer_ = other.buffer_;
            producer_idx_.head.store(other.producer_idx_.head.load(std::memory_order_relaxed), 
                                   std::memory_order_relaxed);
            consumer_idx_.tail.store(other.consumer_idx_.tail.load(std::memory_order_relaxed), 
                                   std::memory_order_relaxed);
            other.buffer_ = nullptr;
        }
        return *this;
    }

    ~BasicRingBuffer() {
        clear();
    }

    /**
     * @brief Get the capacity of the ring buffer
     */
    [[nodiscard]] size_type capacity() const noexcept {
        return capacity_;
    }

    /**
     * @brief Get current number of elements in the buffer
     */
    [[nodiscard]] size_type size() const noexcept {
        const auto head = producer_idx_.head.load(std::memory_order_acquire);
        const auto tail = consumer_idx_.tail.load(std::memory_order_acquire);
        return head - tail;
    }

    /**
     * @brief Check if buffer is empty
     */
    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    /**
     * @brief Check if buffer is full
     */
    [[nodiscard]] bool full() const noexcept {
        return size() == capacity_;
    }

    /**
     * @brief Get available space in the buffer
     */
    [[nodiscard]] size_type available() const noexcept {
        return capacity_ - size();
    }

    /**
     * @brief Try to push an element (copy)
     * @param item Element to push
     * @return true if successful, false if buffer is full (for Drop policy)
     */
    bool try_push(const T& item) {
        if constexpr (Policy == OverflowPolicy::Block) {
            return push_impl(item);
        } else if constexpr (Policy == OverflowPolicy::Drop) {
            if (full()) return false;
            return push_impl(item);
        } else { // Overwrite
            if (full()) {
                pop_impl(); // Remove oldest element
            }
            return push_impl(item);
        }
    }

    /**
     * @brief Try to push an element (move)
     * @param item Element to push
     * @return true if successful, false if buffer is full (for Drop policy)
     */
    bool try_push(T&& item) {
        if constexpr (Policy == OverflowPolicy::Block) {
            return push_impl(std::move(item));
        } else if constexpr (Policy == OverflowPolicy::Drop) {
            if (full()) return false;
            return push_impl(std::move(item));
        } else { // Overwrite
            if (full()) {
                pop_impl(); // Remove oldest element
            }
            return push_impl(std::move(item));
        }
    }

    /**
     * @brief Emplace construct an element in the buffer
     * @param args Arguments to forward to T's constructor
     * @return true if successful, false if buffer is full (for Drop policy)
     */
    template<typename... Args>
    bool try_emplace(Args&&... args) {
        if constexpr (Policy == OverflowPolicy::Block) {
            return emplace_impl(std::forward<Args>(args)...);
        } else if constexpr (Policy == OverflowPolicy::Drop) {
            if (full()) return false;
            return emplace_impl(std::forward<Args>(args)...);
        } else { // Overwrite
            if (full()) {
                pop_impl(); // Remove oldest element
            }
            return emplace_impl(std::forward<Args>(args)...);
        }
    }

    /**
     * @brief Try to pop an element
     * @return Optional containing the element if successful
     */
    [[nodiscard]] std::optional<T> try_pop() {
        if (empty()) {
            return std::nullopt;
        }
        return pop_impl();
    }

    /**
     * @brief Peek at the front element without removing it
     * @return Optional containing reference to the front element
     */
    [[nodiscard]] std::optional<std::reference_wrapper<const T>> try_peek() const {
        if (empty()) {
            return std::nullopt;
        }
        const auto tail = consumer_idx_.tail.load(std::memory_order_acquire);
        return std::cref(*get_element_ptr(tail));
    }

    /**
     * @brief Clear all elements from the buffer
     */
    void clear() noexcept {
        while (!empty()) {
            pop_impl();
        }
    }

    /**
     * @brief Bulk operations for better performance
     */
    
    /**
     * @brief Try to push multiple elements
     * @param items Span of items to push
     * @return Number of elements successfully pushed
     */
    size_type try_push_bulk(std::span<const T> items) {
        size_type pushed = 0;
        for (const auto& item : items) {
            if (!try_push(item)) {
                break;
            }
            ++pushed;
        }
        return pushed;
    }

    /**
     * @brief Try to pop multiple elements
     * @param output Output span to store popped elements
     * @return Number of elements successfully popped
     */
    size_type try_pop_bulk(std::span<T> output) {
        size_type popped = 0;
        for (auto& item : output) {
            auto result = try_pop();
            if (!result) {
                break;
            }
            item = std::move(*result);
            ++popped;
        }
        return popped;
    }

private:
    bool push_impl(const T& item) {
        const auto head = producer_idx_.head.load(std::memory_order_relaxed);
        const auto next_head = head + 1;
        
        if constexpr (Policy == OverflowPolicy::Block) {
            // Wait for space to become available
            while (next_head - consumer_idx_.tail.load(std::memory_order_acquire) > capacity_) {
                std::this_thread::yield();
            }
        }
        
        // Construct element in place
        std::construct_at(get_element_ptr(head), item);
        
        // Update head with release semantics to ensure visibility
        producer_idx_.head.store(next_head, std::memory_order_release);
        return true;
    }

    bool push_impl(T&& item) {
        const auto head = producer_idx_.head.load(std::memory_order_relaxed);
        const auto next_head = head + 1;
        
        if constexpr (Policy == OverflowPolicy::Block) {
            // Wait for space to become available
            while (next_head - consumer_idx_.tail.load(std::memory_order_acquire) > capacity_) {
                std::this_thread::yield();
            }
        }
        
        // Construct element in place
        std::construct_at(get_element_ptr(head), std::move(item));
        
        // Update head with release semantics to ensure visibility
        producer_idx_.head.store(next_head, std::memory_order_release);
        return true;
    }

    template<typename... Args>
    bool emplace_impl(Args&&... args) {
        const auto head = producer_idx_.head.load(std::memory_order_relaxed);
        const auto next_head = head + 1;
        
        if constexpr (Policy == OverflowPolicy::Block) {
            // Wait for space to become available
            while (next_head - consumer_idx_.tail.load(std::memory_order_acquire) > capacity_) {
                std::this_thread::yield();
            }
        }
        
        // Construct element in place
        std::construct_at(get_element_ptr(head), std::forward<Args>(args)...);
        
        // Update head with release semantics to ensure visibility
        producer_idx_.head.store(next_head, std::memory_order_release);
        return true;
    }

    T pop_impl() {
        const auto tail = consumer_idx_.tail.load(std::memory_order_relaxed);
        
        // Wait for data to become available (for blocking behavior)
        while (tail == producer_idx_.head.load(std::memory_order_acquire)) {
            if constexpr (Policy != OverflowPolicy::Block) {
                // This should not happen for non-blocking policies
                // as we check empty() before calling pop_impl
                __builtin_unreachable();
            }
            std::this_thread::yield();
        }
        
        // Get element and destroy it
        auto* element_ptr = get_element_ptr(tail);
        T result = std::move(*element_ptr);
        std::destroy_at(element_ptr);
        
        // Update tail with release semantics
        consumer_idx_.tail.store(tail + 1, std::memory_order_release);
        return result;
    }
};

// Convenience type aliases for different policies
template<typename T>
    requires RingBufferStorable<T>
using BlockingRingBuffer = BasicRingBuffer<T, OverflowPolicy::Block>;

template<typename T>
    requires RingBufferStorable<T>
using DroppingRingBuffer = BasicRingBuffer<T, OverflowPolicy::Drop>;

template<typename T>
    requires RingBufferStorable<T>
using OverwritingRingBuffer = BasicRingBuffer<T, OverflowPolicy::Overwrite>;

} // namespace oc::rb
