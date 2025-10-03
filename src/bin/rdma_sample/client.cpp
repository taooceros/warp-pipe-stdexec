#include "doca_stdexec/common/tcp.hpp"
#include "oc/oc_adapters/rdma_write_adapter.hpp"
#include "oc/pipe.hpp"
#include "rdma_sample_common.hpp"
#include <coroutine>
#include <doca_stdexec/buf.hpp>
#include <stdexec/execution.hpp>

using namespace doca_stdexec;

uint32_t tail = 0;
uint32_t head = 0;

constexpr size_t buf_size = 4096 * 4096;

std::vector<std::byte> buf(buf_size);

exec::task<void> write_task() {
  while (true) {

    if (buf_size - (tail - head) > buf_size) {
      auto ptr = buf.data() + tail * buf_size;
      std::memcpy(ptr, buf.data() + head * buf_size, buf_size);
      tail += buf_size;
    }

    co_await std::suspend_always{};
  }
}

exec::task<void> client_task() {
  tcp::tcp_socket client;
  client.connect("127.0.0.1", 8080);

  auto doca_runtime = doca_stdexec::doca_pe_context();

  auto metadata = co_await setup_rdma(client, doca_runtime);

  auto adapter =
      oc::oc_adapters::rdma_write_adapter<std::byte>(metadata.rdma_connection);

  auto forward_meta =
      oc::ForwardPipeMetadata<oc::oc_adapters::rdma_write_adapter<std::byte>>(
          adapter, metadata.forward_metadata_buf, metadata.src_head_buf,
          metadata.dst_tail_buf);

  auto rdma_pipe = std::make_shared<
      oc::Pipe<oc::oc_adapters::rdma_write_adapter<std::byte>,
               oc::oc_adapters::rdma_write_adapter<std::byte>,
               oc::oc_adapters::rdma_write_adapter<std::byte>>>(
      adapter, metadata.src_buffer_buf, metadata.dst_buffer_buf);

  oc::PipeLine pipe_line;

  pipe_line.push_pipe(rdma_pipe);

  while (true) {
    co_await pipe_line.progress(doca_runtime.get_scheduler());
  }

  co_return;
}

int main(int argc, char **argv) {
  stdexec::sync_wait(client_task());
  return 0;
}