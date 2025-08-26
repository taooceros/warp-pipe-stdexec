#pragma once

#include "basic_rb.hpp"
#include "../containers/small_vector.hpp"
#include <cstring>
#include <array>
#include <functional>
#include <stdexcept>
#include <limits>

namespace oc::rb {

// Concept for POD types that can use memcpy optimizations
template<typename T>
concept PodType = std::is_trivially_copyable_v<T> && 
                  std::is_trivially_destructible_v<T> &&
                  std::is_standard_layout_v<T>;

/**
 * @brief Zero-copy memory view for contiguous buffer regions
 * 
 * Provides safe access to buffer memory without copying data.
 * The view becomes invalid when the underlying buffer is modified.
 */
template<PodType T>
class ZeroCopyView {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = std::size_t;
    
private:
    const_pointer data_;
    size_type size_;
    
public:
    ZeroCopyView() noexcept : data_(nullptr), size_(0) {}
    
    ZeroCopyView(const_pointer data, size_type size) noexcept 
        : data_(data), size_(size) {}
    
    [[nodiscard]] const_pointer data() const noexcept { return data_; }
    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    
    [[nodiscard]] const T& operator[](size_type index) const noexcept {
        return data_[index];
    }
    
    [[nodiscard]] const T& at(size_type index) const {
        if (index >= size_) {
            throw std::out_of_range("ZeroCopyView index out of range");
        }
        return data_[index];
    }
    
    // Iterator support
    [[nodiscard]] const_pointer begin() const noexcept { return data_; }
    [[nodiscard]] const_pointer end() const noexcept { return data_ + size_; }
    [[nodiscard]] const_pointer cbegin() const noexcept { return data_; }
    [[nodiscard]] const_pointer cend() const noexcept { return data_ + size_; }
    
    // Conversion to span
    [[nodiscard]] std::span<const T> to_span() const noexcept {
        return std::span<const T>(data_, size_);
    }
};

/**
 * @brief Zero-copy writable memory view for single contiguous buffer region
 * 
 * Provides direct write access to a single contiguous buffer memory region.
 * Does not support individual element insertion - use span-based operations.
 */
template<PodType T>
class ZeroCopyWriteView {
public:
    using value_type = T;
    using pointer = T*;
    using size_type = std::size_t;
    
private:
    pointer data_;
    size_type capacity_;
    std::function<void(size_type)> commit_fn_;
    bool committed_;
    
public:
    ZeroCopyWriteView() noexcept 
        : data_(nullptr), capacity_(0), committed_(true) {}
    
    ZeroCopyWriteView(pointer data, size_type capacity, std::function<void(size_type)> commit_fn) noexcept
        : data_(data), capacity_(capacity), commit_fn_(std::move(commit_fn)), committed_(false) {}
    
    // Move-only type
    ZeroCopyWriteView(const ZeroCopyWriteView&) = delete;
    ZeroCopyWriteView& operator=(const ZeroCopyWriteView&) = delete;
    
    ZeroCopyWriteView(ZeroCopyWriteView&& other) noexcept
        : data_(other.data_), capacity_(other.capacity_)
        , commit_fn_(std::move(other.commit_fn_)), committed_(other.committed_) {
        other.committed_ = true; // Prevent double commit
    }
    
    ZeroCopyWriteView& operator=(ZeroCopyWriteView&& other) noexcept {
        if (this != &other) {
            if (!committed_) {
                commit(0); // Auto-commit with 0 size on reassignment
            }
            data_ = other.data_;
            capacity_ = other.capacity_;
            commit_fn_ = std::move(other.commit_fn_);
            committed_ = other.committed_;
            other.committed_ = true;
        }
        return *this;
    }
    
    ~ZeroCopyWriteView() {
        if (!committed_) {
            commit(0); // Auto-commit with 0 size on destruction if not explicitly committed
        }
    }
    
    [[nodiscard]] pointer data() noexcept { return data_; }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept { return capacity_ == 0; }
    
    // Direct element access (user must manage bounds)
    [[nodiscard]] T& operator[](size_type index) noexcept {
        return data_[index];
    }
    
    // Get as mutable span for external manipulation
    [[nodiscard]] std::span<T> as_span() noexcept {
        return std::span<T>(data_, capacity_);
    }
    
    // Bulk write from span - returns number of elements written
    size_type write(std::span<const T> source) {
        const auto to_write = std::min(source.size(), capacity_);
        std::memcpy(data_, source.data(), to_write * sizeof(T));
        return to_write;
    }
    
    // Commit the written data to the buffer
    void commit(size_type written_count) {
        if (!committed_ && commit_fn_) {
            if (written_count > capacity_) {
                throw std::out_of_range("Written count exceeds view capacity");
            }
            commit_fn_(written_count);
            committed_ = true;
        }
    }
    
    // Check if view has been committed
    [[nodiscard]] bool is_committed() const noexcept {
        return committed_;
    }
};

/**
 * @brief Non-contiguous write view for handling multiple buffer segments
 * 
 * Manages writing to potentially non-contiguous buffer regions while
 * maintaining logical ordering. Provides iterator support and span extraction.
 */
template<PodType T>
class NonContiguousWriteView {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = std::size_t;
    
    struct Segment {
        pointer data;
        size_type capacity;
        
        Segment(pointer d, size_type c) : data(d), capacity(c) {}
        
        [[nodiscard]] std::span<T> as_span() noexcept {
            return std::span<T>(data, capacity);
        }
    };
    
    // Iterator for non-contiguous view
    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;
        
    private:
        const NonContiguousWriteView* view_;
        size_type segment_idx_;
        size_type element_idx_;
        
    public:
        iterator(const NonContiguousWriteView* view, size_type segment_idx, size_type element_idx)
            : view_(view), segment_idx_(segment_idx), element_idx_(element_idx) {}
        
        reference operator*() const {
            return view_->segments_[segment_idx_].data[element_idx_];
        }
        
        pointer operator->() const {
            return &view_->segments_[segment_idx_].data[element_idx_];
        }
        
        iterator& operator++() {
            ++element_idx_;
            if (segment_idx_ < view_->segments_.size() && 
                element_idx_ >= view_->segments_[segment_idx_].capacity) {
                ++segment_idx_;
                element_idx_ = 0;
            }
            return *this;
        }
        
        iterator operator++(int) {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }
        
        bool operator==(const iterator& other) const {
            return view_ == other.view_ && 
                   segment_idx_ == other.segment_idx_ && 
                   element_idx_ == other.element_idx_;
        }
        
        bool operator!=(const iterator& other) const {
            return !(*this == other);
        }
    };
    
private:
    containers::small_vector2<Segment> segments_;
    std::function<void(size_type)> commit_fn_;
    bool committed_;
    
public:
    NonContiguousWriteView() : committed_(true) {}
    
    NonContiguousWriteView(containers::small_vector2<Segment> segments, std::function<void(size_type)> commit_fn)
        : segments_(std::move(segments)), commit_fn_(std::move(commit_fn)), committed_(false) {}
    
    // Move-only type
    NonContiguousWriteView(const NonContiguousWriteView&) = delete;
    NonContiguousWriteView& operator=(const NonContiguousWriteView&) = delete;
    
    NonContiguousWriteView(NonContiguousWriteView&& other) noexcept
        : segments_(std::move(other.segments_))
        , commit_fn_(std::move(other.commit_fn_))
        , committed_(other.committed_) {
        other.committed_ = true;
    }
    
    NonContiguousWriteView& operator=(NonContiguousWriteView&& other) noexcept {
        if (this != &other) {
            if (!committed_) {
                commit(0); // Auto-commit with 0 size
            }
            segments_ = std::move(other.segments_);
            commit_fn_ = std::move(other.commit_fn_);
            committed_ = other.committed_;
            other.committed_ = true;
        }
        return *this;
    }
    
    ~NonContiguousWriteView() {
        if (!committed_) {
            commit(0); // Auto-commit with 0 size if not explicitly committed
        }
    }
    
    // Basic properties
    [[nodiscard]] size_type total_capacity() const noexcept {
        size_type total = 0;
        for (const auto& segment : segments_) {
            total += segment.capacity;
        }
        return total;
    }
    
    [[nodiscard]] size_type segment_count() const noexcept {
        return segments_.size();
    }
    
    [[nodiscard]] bool empty() const noexcept {
        return segments_.empty() || total_capacity() == 0;
    }
    
    // Segment access
    [[nodiscard]] const Segment& segment(size_type index) const {
        if (index >= segments_.size()) {
            throw std::out_of_range("Segment index out of range");
        }
        return segments_[index];
    }
    
    [[nodiscard]] Segment& segment(size_type index) {
        if (index >= segments_.size()) {
            throw std::out_of_range("Segment index out of range");
        }
        return segments_[index];
    }
    
    // Get the largest contiguous span available
    [[nodiscard]] std::span<T> max_contiguous_span() noexcept {
        if (segments_.empty()) {
            return std::span<T>{};
        }
        
        size_t max_size = 0;
        size_t max_idx = 0;
        
        for (size_t i = 0; i < segments_.size(); ++i) {
            if (segments_[i].capacity > max_size) {
                max_size = segments_[i].capacity;
                max_idx = i;
            }
        }
        
        return segments_[max_idx].as_span();
    }
    
    // Get first segment as span (most common case)
    [[nodiscard]] std::span<T> first_span() noexcept {
        return segments_.empty() ? std::span<T>{} : segments_[0].as_span();
    }
    
    // Write data sequentially across segments
    size_type write(std::span<const T> source) {
        size_type total_written = 0;
        size_type source_offset = 0;
        
        for (auto& segment : segments_) {
            if (source_offset >= source.size()) break;
            
            const auto remaining_source = source.size() - source_offset;
            const auto to_write = std::min(remaining_source, segment.capacity);
            
            std::memcpy(segment.data, source.data() + source_offset, to_write * sizeof(T));
            
            total_written += to_write;
            source_offset += to_write;
        }
        
        return total_written;
    }
    
    // Iterator support
    [[nodiscard]] iterator begin() {
        return iterator(this, 0, 0);
    }
    
    [[nodiscard]] iterator end() {
        return iterator(this, segments_.size(), 0);
    }
    
    // Commit written data
    void commit(size_type written_count) {
        if (!committed_ && commit_fn_) {
            if (written_count > total_capacity()) {
                throw std::out_of_range("Written count exceeds total capacity");
            }
            commit_fn_(written_count);
            committed_ = true;
        }
    }
    
    [[nodiscard]] bool is_committed() const noexcept {
        return committed_;
    }
};

/**
 * @brief Specialized high-performance ring buffer for POD types
 * 
 * This specialization provides additional optimizations for POD types:
 * - Uses memcpy for bulk operations instead of individual constructors
 * - Simplified memory management without explicit constructor/destructor calls
 * - Vectorized operations where possible
 * - Better cache locality with contiguous memory layout
 * 
 * @tparam T POD element type
 * @tparam Policy Overflow handling policy
 */
template<PodType T, OverflowPolicy Policy = OverflowPolicy::Block>
class alignas(cache_line_size) PodRingBuffer {
public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;

private:
    static constexpr size_type round_to_power_of_2(size_type n) noexcept {
        if (n == 0) return 1;
        return std::bit_ceil(n);
    }

    struct alignas(cache_line_size) ProducerIndex {
        std::atomic<size_type> head{0};
        char padding[cache_line_size - sizeof(std::atomic<size_type>)];
    };

    struct alignas(cache_line_size) ConsumerIndex {
        std::atomic<size_type> tail{0};
        char padding[cache_line_size - sizeof(std::atomic<size_type>)];
    };

    const size_type capacity_;
    const size_type mask_;
    std::unique_ptr<T[]> buffer_;
    
    ProducerIndex producer_idx_;
    ConsumerIndex consumer_idx_;

public:
    explicit PodRingBuffer(size_type capacity) 
        : capacity_(round_to_power_of_2(capacity))
        , mask_(capacity_ - 1)
        , buffer_(std::make_unique<T[]>(capacity_))
    {}

    // Non-copyable but movable
    PodRingBuffer(const PodRingBuffer&) = delete;
    PodRingBuffer& operator=(const PodRingBuffer&) = delete;
    
    PodRingBuffer(PodRingBuffer&& other) noexcept 
        : capacity_(other.capacity_)
        , mask_(other.mask_)
        , buffer_(std::move(other.buffer_))
        , producer_idx_{other.producer_idx_.head.load(std::memory_order_relaxed)}
        , consumer_idx_{other.consumer_idx_.tail.load(std::memory_order_relaxed)}
    {}

    PodRingBuffer& operator=(PodRingBuffer&& other) noexcept {
        if (this != &other) {
            const_cast<size_type&>(capacity_) = other.capacity_;
            const_cast<size_type&>(mask_) = other.mask_;
            buffer_ = std::move(other.buffer_);
            producer_idx_.head.store(other.producer_idx_.head.load(std::memory_order_relaxed), 
                                   std::memory_order_relaxed);
            consumer_idx_.tail.store(other.consumer_idx_.tail.load(std::memory_order_relaxed), 
                                   std::memory_order_relaxed);
        }
        return *this;
    }

    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    
    [[nodiscard]] size_type size() const noexcept {
        const auto head = producer_idx_.head.load(std::memory_order_acquire);
        const auto tail = consumer_idx_.tail.load(std::memory_order_acquire);
        return head - tail;
    }
    
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] bool full() const noexcept { return size() == capacity_; }
    [[nodiscard]] size_type available() const noexcept { return capacity_ - size(); }

    // Optimized push for POD types
    bool try_push(const T& item) {
        if constexpr (Policy == OverflowPolicy::Drop) {
            if (full()) return false;
        } else if constexpr (Policy == OverflowPolicy::Overwrite) {
            if (full()) {
                try_pop(); // Remove oldest element
            }
        }

        const auto head = producer_idx_.head.load(std::memory_order_relaxed);
        const auto next_head = head + 1;
        
        if constexpr (Policy == OverflowPolicy::Block) {
            while (next_head - consumer_idx_.tail.load(std::memory_order_acquire) > capacity_) {
                std::this_thread::yield();
            }
        }
        
        // Simple assignment for POD types
        buffer_[head & mask_] = item;
        
        producer_idx_.head.store(next_head, std::memory_order_release);
        return true;
    }

    std::optional<T> try_pop() {
        if (empty()) return std::nullopt;
        
        const auto tail = consumer_idx_.tail.load(std::memory_order_relaxed);
        const auto next_tail = tail + 1;
        
        // For blocking policy, wait for data
        if constexpr (Policy == OverflowPolicy::Block) {
            while (tail == producer_idx_.head.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        }
        
        T result = buffer_[tail & mask_];
        consumer_idx_.tail.store(next_tail, std::memory_order_release);
        return result;
    }

    std::optional<std::reference_wrapper<const T>> try_peek() const {
        if (empty()) return std::nullopt;
        const auto tail = consumer_idx_.tail.load(std::memory_order_acquire);
        return std::cref(buffer_[tail & mask_]);
    }

    void clear() noexcept {
        consumer_idx_.tail.store(producer_idx_.head.load(std::memory_order_relaxed), 
                               std::memory_order_relaxed);
    }

    // Highly optimized bulk operations using memcpy
    size_type try_push_bulk(std::span<const T> items) {
        const auto available_space = available();
        const auto to_copy = std::min(items.size(), available_space);
        
        if (to_copy == 0) return 0;
        
        const auto head = producer_idx_.head.load(std::memory_order_relaxed);
        const auto head_idx = head & mask_;
        
        // Check if we can do a single contiguous copy
        if (head_idx + to_copy <= capacity_) {
            // Single contiguous copy
            std::memcpy(&buffer_[head_idx], items.data(), to_copy * sizeof(T));
        } else {
            // Wrap-around copy
            const auto first_chunk = capacity_ - head_idx;
            const auto second_chunk = to_copy - first_chunk;
            
            std::memcpy(&buffer_[head_idx], items.data(), first_chunk * sizeof(T));
            std::memcpy(&buffer_[0], items.data() + first_chunk, second_chunk * sizeof(T));
        }
        
        producer_idx_.head.store(head + to_copy, std::memory_order_release);
        return to_copy;
    }

    size_type try_pop_bulk(std::span<T> output) {
        const auto current_size = size();
        const auto to_copy = std::min(output.size(), current_size);
        
        if (to_copy == 0) return 0;
        
        const auto tail = consumer_idx_.tail.load(std::memory_order_relaxed);
        const auto tail_idx = tail & mask_;
        
        // Check if we can do a single contiguous copy
        if (tail_idx + to_copy <= capacity_) {
            // Single contiguous copy
            std::memcpy(output.data(), &buffer_[tail_idx], to_copy * sizeof(T));
        } else {
            // Wrap-around copy
            const auto first_chunk = capacity_ - tail_idx;
            const auto second_chunk = to_copy - first_chunk;
            
            std::memcpy(output.data(), &buffer_[tail_idx], first_chunk * sizeof(T));
            std::memcpy(output.data() + first_chunk, &buffer_[0], second_chunk * sizeof(T));
        }
        
        consumer_idx_.tail.store(tail + to_copy, std::memory_order_release);
        return to_copy;
    }

    // ========== Zero-Copy Operations ==========

    /**
     * @brief Get zero-copy read view for available data
     * 
     * Returns up to two contiguous memory regions that can be read without copying.
     * The first region is always valid, the second may be empty if no wrap-around.
     * 
     * @param max_elements Maximum number of elements to include in view
     * @return Array of up to 2 zero-copy views
     */
    [[nodiscard]] std::array<ZeroCopyView<T>, 2> get_read_views(size_type max_elements = SIZE_MAX) const {
        const auto current_size = size();
        const auto to_read = std::min({max_elements, current_size});
        
        if (to_read == 0) {
            return {ZeroCopyView<T>{}, ZeroCopyView<T>{}};
        }
        
        const auto tail = consumer_idx_.tail.load(std::memory_order_acquire);
        const auto tail_idx = tail & mask_;
        
        // Check if we need wrap-around
        if (tail_idx + to_read <= capacity_) {
            // Single contiguous region
            return {
                ZeroCopyView<T>{&buffer_[tail_idx], to_read},
                ZeroCopyView<T>{}
            };
        } else {
            // Two regions due to wrap-around
            const auto first_chunk = capacity_ - tail_idx;
            const auto second_chunk = to_read - first_chunk;
            
            return {
                ZeroCopyView<T>{&buffer_[tail_idx], first_chunk},
                ZeroCopyView<T>{&buffer_[0], second_chunk}
            };
        }
    }

    /**
     * @brief Get zero-copy read view for a single contiguous region
     * 
     * Returns the largest contiguous region available for reading.
     * This may be smaller than the total available data if wrap-around is needed.
     * 
     * @param max_elements Maximum number of elements to include in view
     * @return Zero-copy view of contiguous region
     */
    [[nodiscard]] ZeroCopyView<T> get_contiguous_read_view(size_type max_elements = SIZE_MAX) const {
        const auto current_size = size();
        if (current_size == 0) {
            return ZeroCopyView<T>{};
        }
        
        const auto tail = consumer_idx_.tail.load(std::memory_order_acquire);
        const auto tail_idx = tail & mask_;
        
        // Calculate largest contiguous chunk
        const auto contiguous_size = std::min({
            max_elements,
            current_size,
            capacity_ - tail_idx
        });
        
        return ZeroCopyView<T>{&buffer_[tail_idx], contiguous_size};
    }

    /**
     * @brief Advance the read position after consuming data via zero-copy views
     * 
     * @param count Number of elements that were consumed
     */
    void advance_read(size_type count) {
        if (count > size()) {
            throw std::out_of_range("Cannot advance read beyond available data");
        }
        
        const auto tail = consumer_idx_.tail.load(std::memory_order_relaxed);
        consumer_idx_.tail.store(tail + count, std::memory_order_release);
    }

    /**
     * @brief Get zero-copy write view for largest contiguous available space
     * 
     * Returns a writable view for the largest contiguous region available.
     * This may be smaller than total available space if wrap-around is needed.
     * 
     * @param max_elements Maximum number of elements to reserve for writing
     * @return Zero-copy write view for contiguous region (move-only)
     */
    [[nodiscard]] ZeroCopyWriteView<T> get_write_view(size_type max_elements = SIZE_MAX) {
        const auto available_space = available();
        if (available_space == 0) {
            return ZeroCopyWriteView<T>{}; // Empty view
        }
        
        const auto head = producer_idx_.head.load(std::memory_order_relaxed);
        const auto head_idx = head & mask_;
        
        // Calculate largest contiguous chunk for writing
        const auto contiguous_space = std::min({
            max_elements,
            available_space,
            capacity_ - head_idx
        });
        
        // Create commit function that updates producer head
        auto commit_fn = [this, head](size_type written_count) {
            producer_idx_.head.store(head + written_count, std::memory_order_release);
        };
        
        return ZeroCopyWriteView<T>{&buffer_[head_idx], contiguous_space, std::move(commit_fn)};
    }

    /**
     * @brief Get non-contiguous write view for maximum space utilization
     * 
     * Returns a write view that can handle wrap-around scenarios by managing
     * multiple segments. Provides sequential write operations and iterator support.
     * 
     * @param max_elements Maximum number of elements to reserve for writing
     * @return Non-contiguous write view (move-only)
     */
    [[nodiscard]] NonContiguousWriteView<T> get_non_contiguous_write_view(size_type max_elements = SIZE_MAX) {
        const auto available_space = available();
        const auto to_reserve = std::min(max_elements, available_space);
        
        if (to_reserve == 0) {
            return NonContiguousWriteView<T>{}; // Empty view
        }
        
        const auto head = producer_idx_.head.load(std::memory_order_relaxed);
        const auto head_idx = head & mask_;
        
        containers::small_vector2<typename NonContiguousWriteView<T>::Segment> segments;
        
        // Check if we need wrap-around
        if (head_idx + to_reserve <= capacity_) {
            // Single contiguous region
            segments.emplace_back(&buffer_[head_idx], to_reserve);
        } else {
            // Two regions due to wrap-around
            const auto first_chunk = capacity_ - head_idx;
            const auto second_chunk = to_reserve - first_chunk;
            
            segments.emplace_back(&buffer_[head_idx], first_chunk);
            segments.emplace_back(&buffer_[0], second_chunk);
        }
        
        // Create commit function that updates producer head
        auto commit_fn = [this, head](size_type written_count) {
            producer_idx_.head.store(head + written_count, std::memory_order_release);
        };
        
        return NonContiguousWriteView<T>{std::move(segments), std::move(commit_fn)};
    }

    /**
     * @brief Reserve space for writing and advance write position
     * 
     * This is useful when you know how much data you'll write in advance.
     * 
     * @param count Number of elements to reserve
     * @return Pointer to the reserved memory region (may wrap around)
     */
    [[nodiscard]] pointer reserve_write_space(size_type count) {
        if (count > available()) {
            throw std::out_of_range("Cannot reserve more space than available");
        }
        
        const auto head = producer_idx_.head.load(std::memory_order_relaxed);
        const auto head_idx = head & mask_;
        
        // Advance the head pointer
        producer_idx_.head.store(head + count, std::memory_order_release);
        
        return &buffer_[head_idx];
    }
};

// Convenience aliases for POD ring buffers
template<PodType T>
using PodBlockingRingBuffer = PodRingBuffer<T, OverflowPolicy::Block>;

template<PodType T>
using PodDroppingRingBuffer = PodRingBuffer<T, OverflowPolicy::Drop>;

template<PodType T>
using PodOverwritingRingBuffer = PodRingBuffer<T, OverflowPolicy::Overwrite>;

} // namespace oc::rb
