#pragma once
#include <memory>
#ifndef SHARED_MEMORY_ADAPTER_HPP
#define SHARED_MEMORY_ADAPTER_HPP

#include "oc/oc_adapter.hpp"

namespace ex = stdexec;

namespace oc::oc_adapters {

template <typename T> struct shared_memory_adapter {
public:
  using local_buf_t = std::span<T>;
  using remote_buf_t = std::span<T>;
  using transfer_type = decltype(ex::just());

  // shared memory, so source and destination are the same
  shared_memory_adapter(local_buf_t buffer)
      : src_buffer_(buffer), dst_buffer_(buffer) {}

  // no need to transfer
  transfer_type transfer(local_buf_t src, remote_buf_t dst) {
    return ex::just();
  }

private:
  local_buf_t src_buffer_;
  remote_buf_t dst_buffer_;
};

} // namespace oc::oc_adapters

#endif