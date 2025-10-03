#pragma once
#include "doca_stdexec/common/tcp.hpp"
#include "doca_stdexec/rdma.hpp"
#include "doca_types.h"
#include <cstddef>
#ifndef RDMA_SAMPLE_COMMON_HPP
#define RDMA_SAMPLE_COMMON_HPP
#include "doca_stdexec/buf_inventory.hpp"
#include "doca_stdexec/device.hpp"
#include "doca_stdexec/mmap.hpp"
#include "doca_stdexec/progress_engine.hpp"
#include "exec/task.hpp"

#include <memory>

struct RDMASetupResult {
  std::shared_ptr<doca_stdexec::Device> device;
  std::shared_ptr<doca_stdexec::rdma::Rdma> rdma;
  std::shared_ptr<doca_stdexec::rdma::RdmaConnection> rdma_connection;
  doca_stdexec::MMap<std::byte> src_buffer_mmap;
  doca_stdexec::MMap<std::byte> dst_buffer_mmap;
  doca_stdexec::MMap<std::byte> src_tail_mmap;
  doca_stdexec::MMap<std::byte> dst_tail_mmap;
  doca_stdexec::MMap<std::byte> src_head_mmap;
  doca_stdexec::MMap<std::byte> dst_head_mmap;

  doca_stdexec::BufInventory buf_inventory;
  doca_stdexec::Buf src_buffer_buf;
  doca_stdexec::Buf dst_buffer_buf;
  doca_stdexec::Buf src_tail_buf;
  doca_stdexec::Buf dst_tail_buf;
  doca_stdexec::Buf src_head_buf;
  doca_stdexec::Buf dst_head_buf;

  doca_stdexec::Buf forward_metadata_buf;
  doca_stdexec::Buf backward_metadata_buf;
};

struct SymmetricMMapPair {
  doca_stdexec::MMap<std::byte> src_mmap;
  doca_stdexec::MMap<std::byte> dst_mmap;
};

inline SymmetricMMapPair
create_symmetric_mmap(size_t size, std::shared_ptr<doca_stdexec::Device> device,
                      doca_stdexec::tcp::tcp_socket &comm) {

  auto buffer = aligned_alloc(4096, size);
  auto mmap = doca_stdexec::MMap<std::byte>(
      std::span<std::byte>(static_cast<std::byte *>(buffer), size));
  mmap.add_device(device);
  mmap.set_permissions(DOCA_ACCESS_FLAG_RDMA_WRITE |
                       DOCA_ACCESS_FLAG_RDMA_READ |
                       DOCA_ACCESS_FLAG_LOCAL_READ_WRITE);
  mmap.set_max_devices(8);
  mmap.set_free_callback([](std::span<std::byte> data) { free(data.data()); });
  mmap.start();

  auto export_desc = mmap.export_rdma(*device);
  comm.send_dynamic(export_desc);
  auto received_desc = comm.receive_dynamic();
  auto dst_mmap = doca_stdexec::MMap<std::byte>::create_from_export(
      nullptr, received_desc.data(), received_desc.size(), device);

  return SymmetricMMapPair{std::move(mmap), std::move(dst_mmap)};
}

inline exec::task<RDMASetupResult>
setup_rdma(doca_stdexec::tcp::tcp_socket &comm,
           doca_stdexec::doca_pe_context &doca_runtime) {

  using namespace doca_stdexec;

  auto device = doca_stdexec::Device::open_from_ib_name("mlx5_1");

  auto rdma = rdma::Rdma::open_from_dev(device);
  rdma->set_gid_index(3);

  doca_runtime.connect_ctx(rdma);

  co_await doca_runtime.get_scheduler()
      .schedule(); // not sure whether this works

  rdma->start();

  auto connection = std::make_shared<doca_stdexec::rdma::RdmaConnection>(
      co_await rdma->connect(comm));

  auto [src_buffer_mmap, dst_buffer_mmap] =
      create_symmetric_mmap(1024 * 1024, device, comm);

  auto [src_tail_mmap, dst_tail_mmap] = create_symmetric_mmap(8, device, comm);
  auto [src_head_mmap, dst_head_mmap] = create_symmetric_mmap(8, device, comm);

  auto buf_inventory = BufInventory(16);
  buf_inventory.start();
  auto src_buf = buf_inventory.get_buffer_for_mmap(src_buffer_mmap);
  auto dst_buf = buf_inventory.get_buffer_for_mmap(dst_buffer_mmap);
  auto src_tail_buf = buf_inventory.get_buffer_for_mmap(src_tail_mmap);
  auto dst_tail_buf = buf_inventory.get_buffer_for_mmap(dst_tail_mmap);
  auto src_head_buf = buf_inventory.get_buffer_for_mmap(src_head_mmap);
  auto dst_head_buf = buf_inventory.get_buffer_for_mmap(dst_head_mmap);

  auto [src_metadata_mmap, dst_metadata_mmap] =
      create_symmetric_mmap(8, device, comm);
  auto src_metadata_buf = buf_inventory.get_buffer_for_mmap(src_metadata_mmap);
  auto dst_metadata_buf = buf_inventory.get_buffer_for_mmap(dst_metadata_mmap);

  co_return RDMASetupResult{device,
                            std::move(rdma),
                            std::move(connection),
                            std::move(src_buffer_mmap),
                            std::move(dst_buffer_mmap),
                            std::move(src_tail_mmap),
                            std::move(dst_tail_mmap),
                            std::move(src_head_mmap),
                            std::move(dst_head_mmap),
                            std::move(buf_inventory),
                            std::move(src_buf),
                            std::move(dst_buf),
                            std::move(src_metadata_buf),
                            std::move(dst_metadata_buf)};
}

#endif