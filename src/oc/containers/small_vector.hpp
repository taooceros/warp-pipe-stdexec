#pragma once

#include "../../utils/assertions.hpp"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace oc::containers {

/**
 * @brief Small vector with fixed stack storage only
 *
 * A vector-like container that stores elements only on the stack with a
 * fixed maximum capacity. This is ideal for ring buffer views which
 * typically have 1-2 segments and never need more than N elements.
 *
 * Features:
 * - Stack storage only - no heap allocation ever
 * - Fixed maximum capacity of N elements
 * - STL-compatible interface
 * - Move semantics and exception safety
 * - Zero allocation overhead
 * - Panics if capacity is exceeded (fail-fast behavior)
 *
 * @tparam T Element type
 * @tparam N Maximum number of elements to store (default: 2)
 */
template <typename T, std::size_t N = 2> class small_vector {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T &;
  using const_reference = const T &;
  using pointer = T *;
  using const_pointer = const T *;
  using iterator = pointer;
  using const_iterator = const_pointer;

private:
  static constexpr size_type stack_capacity = N;

  // Stack storage for small sizes
  alignas(T) std::byte stack_storage_[stack_capacity * sizeof(T)];

  size_type size_;
  size_type capacity_;

  // Helper to get stack storage as T*
  pointer stack_data() noexcept {
    return reinterpret_cast<pointer>(stack_storage_);
  }

  const_pointer stack_data() const noexcept {
    return reinterpret_cast<const_pointer>(stack_storage_);
  }

  // Get current data pointer (stack or heap)
  pointer data_ptr() noexcept { return stack_data(); }

  const_pointer data_ptr() const noexcept { return stack_data(); }

     // Always uses stack storage in this implementation
   bool is_on_stack() const noexcept { return true; }

     // Check capacity when needed (panics if exceeded)
   void check_capacity() {
     if (size_ >= capacity_) {
       panic("Small vector capacity exceeded: cannot grow beyond stack capacity");
     }
   }

  // Destroy all elements
  void destroy_all() noexcept {
    auto *ptr = data_ptr();
    for (size_type i = 0; i < size_; ++i) {
      std::destroy_at(&ptr[i]);
    }
    size_ = 0;
  }

public:
  // Default constructor
  small_vector() noexcept : size_(0), capacity_(stack_capacity) {}

  // Constructor with initial size
  explicit small_vector(size_type count)
      : size_(0), capacity_(count <= stack_capacity ? stack_capacity : count) {

    if (!is_on_stack()) {
      panic("Small vector growth is not implemented");
    }

    // Default construct elements
    auto *ptr = data_ptr();
    for (size_type i = 0; i < count; ++i) {
      std::construct_at(&ptr[i]);
      ++size_;
    }
  }

  // Constructor with initial size and value
  small_vector(size_type count, const T &value)
      : size_(0), capacity_(stack_capacity) {

    if (count > stack_capacity) {
      panic("Small vector constructor: size exceeds stack capacity");
    }

    // Copy construct elements
    auto *ptr = data_ptr();
    for (size_type i = 0; i < count; ++i) {
      std::construct_at(&ptr[i], value);
      ++size_;
    }
  }

  // Constructor from initializer list
  small_vector(std::initializer_list<T> init)
      : size_(0), capacity_(stack_capacity) {

    if (init.size() > stack_capacity) {
      panic("Small vector initializer list: size exceeds stack capacity");
    }

    auto *ptr = data_ptr();
    for (const auto &item : init) {
      std::construct_at(&ptr[size_], item);
      ++size_;
    }
  }

  // Copy constructor
  small_vector(const small_vector &other)
      : size_(0), capacity_(stack_capacity) {

    if (other.size_ > stack_capacity) {
      panic(
          "Small vector copy constructor: source size exceeds stack capacity");
    }

    auto *ptr = data_ptr();
    auto *other_ptr = other.data_ptr();
    for (size_type i = 0; i < other.size_; ++i) {
      std::construct_at(&ptr[i], other_ptr[i]);
      ++size_;
    }
  }

     // Move constructor
   small_vector(small_vector &&other) noexcept
       : size_(other.size_), capacity_(stack_capacity) {

     // Move elements from other's stack to our stack
     auto *ptr = stack_data();
     auto *other_ptr = other.stack_data();

     if constexpr (std::is_nothrow_move_constructible_v<T>) {
       for (size_type i = 0; i < size_; ++i) {
         std::construct_at(&ptr[i], std::move(other_ptr[i]));
         std::destroy_at(&other_ptr[i]);
       }
     } else {
       for (size_type i = 0; i < size_; ++i) {
         std::construct_at(&ptr[i], other_ptr[i]);
         std::destroy_at(&other_ptr[i]);
       }
     }

     other.size_ = 0;
     other.capacity_ = stack_capacity;
   }

     // Copy assignment
   small_vector &operator=(const small_vector &other) {
     if (this != &other) {
       destroy_all();

       if (other.size_ > stack_capacity) {
         panic("Small vector copy assignment: source size exceeds stack capacity");
       }

       // Copy elements
       auto *ptr = data_ptr();
       auto *other_ptr = other.data_ptr();
       for (size_type i = 0; i < other.size_; ++i) {
         std::construct_at(&ptr[i], other_ptr[i]);
         ++size_;
       }
     }
     return *this;
   }

     // Move assignment
   small_vector &operator=(small_vector &&other) noexcept {
     if (this != &other) {
       destroy_all();

       size_ = other.size_;
       capacity_ = stack_capacity;

       // Move elements from other's stack to our stack
       auto *ptr = stack_data();
       auto *other_ptr = other.stack_data();

       if constexpr (std::is_nothrow_move_constructible_v<T>) {
         for (size_type i = 0; i < size_; ++i) {
           std::construct_at(&ptr[i], std::move(other_ptr[i]));
           std::destroy_at(&other_ptr[i]);
         }
       } else {
         for (size_type i = 0; i < size_; ++i) {
           std::construct_at(&ptr[i], other_ptr[i]);
           std::destroy_at(&other_ptr[i]);
         }
       }

       other.size_ = 0;
       other.capacity_ = stack_capacity;
     }
     return *this;
   }

  // Destructor
  ~small_vector() { destroy_all(); }

  // Element access
  reference operator[](size_type index) noexcept { return data_ptr()[index]; }

  const_reference operator[](size_type index) const noexcept {
    return data_ptr()[index];
  }

  reference at(size_type index) {
    if (index >= size_) {
      throw std::out_of_range("small_vector index out of range");
    }
    return data_ptr()[index];
  }

  const_reference at(size_type index) const {
    if (index >= size_) {
      throw std::out_of_range("small_vector index out of range");
    }
    return data_ptr()[index];
  }

  reference front() noexcept { return data_ptr()[0]; }

  const_reference front() const noexcept { return data_ptr()[0]; }

  reference back() noexcept { return data_ptr()[size_ - 1]; }

  const_reference back() const noexcept { return data_ptr()[size_ - 1]; }

  pointer data() noexcept { return data_ptr(); }

  const_pointer data() const noexcept { return data_ptr(); }

  // Iterators
  iterator begin() noexcept { return data_ptr(); }

  const_iterator begin() const noexcept { return data_ptr(); }

  const_iterator cbegin() const noexcept { return data_ptr(); }

  iterator end() noexcept { return data_ptr() + size_; }

  const_iterator end() const noexcept { return data_ptr() + size_; }

  const_iterator cend() const noexcept { return data_ptr() + size_; }

  // Capacity
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  [[nodiscard]] size_type size() const noexcept { return size_; }

  [[nodiscard]] size_type capacity() const noexcept { return capacity_; }

  [[nodiscard]] constexpr size_type max_size() const noexcept {
    return std::numeric_limits<size_type>::max() / sizeof(T);
  }

     // Check if currently using stack storage (always true)
   [[nodiscard]] bool is_using_stack_storage() const noexcept {
     return true;
   }

     // Reserve capacity (no-op since capacity is fixed)
   void reserve(size_type new_capacity) {
     if (new_capacity > stack_capacity) {
       panic("Small vector reserve: requested capacity exceeds fixed stack capacity");
     }
   }

  // Modifiers
  void clear() noexcept { destroy_all(); }

     void push_back(const T &value) {
     check_capacity();
     std::construct_at(&data_ptr()[size_], value);
     ++size_;
   }

   void push_back(T &&value) {
     check_capacity();
     std::construct_at(&data_ptr()[size_], std::move(value));
     ++size_;
   }

   template <typename... Args> reference emplace_back(Args &&...args) {
     check_capacity();
     auto *ptr = &data_ptr()[size_];
     std::construct_at(ptr, std::forward<Args>(args)...);
     ++size_;
     return *ptr;
   }

  void pop_back() noexcept {
    if (size_ > 0) {
      --size_;
      std::destroy_at(&data_ptr()[size_]);
    }
  }

     void resize(size_type new_size) {
     if (new_size > size_) {
       if (new_size > capacity_) {
         panic("Small vector resize: new size exceeds fixed stack capacity");
       }

       // Default construct new elements
       auto *ptr = data_ptr();
       for (size_type i = size_; i < new_size; ++i) {
         std::construct_at(&ptr[i]);
       }
     } else if (new_size < size_) {
       // Destroy excess elements
       auto *ptr = data_ptr();
       for (size_type i = new_size; i < size_; ++i) {
         std::destroy_at(&ptr[i]);
       }
     }
     size_ = new_size;
   }

   void resize(size_type new_size, const T &value) {
     if (new_size > size_) {
       if (new_size > capacity_) {
         panic("Small vector resize: new size exceeds fixed stack capacity");
       }

       // Copy construct new elements
       auto *ptr = data_ptr();
       for (size_type i = size_; i < new_size; ++i) {
         std::construct_at(&ptr[i], value);
       }
     } else if (new_size < size_) {
       // Destroy excess elements
       auto *ptr = data_ptr();
       for (size_type i = new_size; i < size_; ++i) {
         std::destroy_at(&ptr[i]);
       }
     }
     size_ = new_size;
   }
};

// Comparison operators
template <typename T, std::size_t N>
bool operator==(const small_vector<T, N> &lhs, const small_vector<T, N> &rhs) {
  return lhs.size() == rhs.size() &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

template <typename T, std::size_t N>
bool operator!=(const small_vector<T, N> &lhs, const small_vector<T, N> &rhs) {
  return !(lhs == rhs);
}

template <typename T, std::size_t N>
bool operator<(const small_vector<T, N> &lhs, const small_vector<T, N> &rhs) {
  return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(),
                                      rhs.end());
}

// Convenience aliases for common use cases
template <typename T>
using small_vector2 = small_vector<T, 2>; // Perfect for ring buffer views

template <typename T> using small_vector4 = small_vector<T, 4>;

template <typename T> using small_vector8 = small_vector<T, 8>;

} // namespace oc::containers
