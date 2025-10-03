#pragma once
#include <memory>
#ifndef RDMA_RECEIVE_ADAPTER_HPP
#define RDMA_RECEIVE_ADAPTER_HPP

#include "oc/oc_adapter.hpp"
#include <doca_stdexec/rdma.hpp>

namespace oc::oc_adapters {

template <typename T> struct rdma_receive_adapter {
public:
  using transfer_type =
      doca_stdexec::rdma::task::rdma_sender<doca_stdexec::rdma::RdmaRecvTask,
                                            doca_buf *>;

  using local_buf_t = doca_stdexec::Buf;
  using remote_buf_t = doca_stdexec::Buf;

  rdma_receive_adapter(std::shared_ptr<doca_stdexec::rdma::Rdma> rdma) {
    rdma_ = rdma;
  }

  transfer_type transfer(local_buf_t src, remote_buf_t dst) {
    // For receive operations, typically only dst buffer is used
    // src parameter kept for interface compatibility
    // return rdma_->recv(dst);
    panic("Not implemented");
  }

private:
  std::shared_ptr<doca_stdexec::rdma::Rdma> rdma_;
};

} // namespace oc::oc_adapters

#endif
