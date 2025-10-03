#pragma once
#include <memory>
#ifndef RDMA_SEND_ADAPTER_HPP
#define RDMA_SEND_ADAPTER_HPP

#include "oc/oc_adapter.hpp"
#include <doca_stdexec/rdma.hpp>

namespace oc::oc_adapters {

template <typename T> struct rdma_send_adapter {
public:
  using transfer_type =
      doca_stdexec::rdma::task::rdma_sender<doca_stdexec::rdma::RdmaSendTask,
                                             doca_buf *>;

  using local_buf_t = doca_stdexec::Buf;
  using remote_buf_t = doca_stdexec::Buf;
  rdma_send_adapter(
      std::shared_ptr<doca_stdexec::rdma::RdmaConnection> connection) {
    connection_ = connection;
  }

  transfer_type transfer(local_buf_t src, remote_buf_t dst) {
    // For send operations, typically only src buffer is used
    // dst parameter kept for interface compatibility
    return connection_->send(src);
  }

private:
  std::shared_ptr<doca_stdexec::rdma::RdmaConnection> connection_;
};

} // namespace oc::oc_adapters

#endif
