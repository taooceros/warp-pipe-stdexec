#pragma once
#include <memory>
#ifndef RDMA_ATOMIC_ADAPTER_HPP
#define RDMA_ATOMIC_ADAPTER_HPP

#include "oc/oc_adapter.hpp"
#include <doca_stdexec/rdma.hpp>

namespace oc::oc_adapters {

template <typename T> struct rdma_atomic_adapter {
public:
  using local_buf_t = doca_stdexec::Buf;
  using remote_buf_t = doca_stdexec::Buf;

  rdma_atomic_adapter(
      std::shared_ptr<doca_stdexec::rdma::RdmaConnection> connection) {
    connection_ = connection;
  }

  auto transfer(local_buf_t src, remote_buf_t dst) {
    // Default atomic operation - could be compare-and-swap
    // This is a generic interface; specific atomic operations might need
    // additional parameters in practice
    panic("Not implemented");
  }

  // Additional atomic operations that might be available
  auto fetch_and_add(local_buf_t target, remote_buf_t addend) {
    panic("Not implemented");
  }

  auto compare_and_swap(local_buf_t target, remote_buf_t compare,
                        remote_buf_t swap) {
    panic("Not implemented");
  }

private:
  std::shared_ptr<doca_stdexec::rdma::RdmaConnection> connection_;
};

} // namespace oc::oc_adapters

#endif
