#pragma once
#include <memory>
#ifndef RDMA_READ_ADAPTER_HPP
#define RDMA_READ_ADAPTER_HPP

#include "oc/oc_adapter.hpp"
#include <doca_stdexec/rdma.hpp>

namespace oc::oc_adapters {

template <typename T> struct rdma_read_adapter {
public:
  using transfer_type =
      doca_stdexec::rdma::task::rdma_sender<doca_stdexec::rdma::RdmaReadTask,
                                            doca_buf *, doca_buf *>;
  using local_buf_t = doca_stdexec::Buf;
  using remote_buf_t = doca_stdexec::Buf;

  rdma_read_adapter(
      std::shared_ptr<doca_stdexec::rdma::RdmaConnection> connection) {
    connection_ = connection;
  }

  auto transfer(local_buf_t src, remote_buf_t dst) {
    return connection_->read(src, dst);
  }

private:
  std::shared_ptr<doca_stdexec::rdma::RdmaConnection> connection_;
};

} // namespace oc::oc_adapters

#endif
