#include "doca_stdexec/common/tcp.hpp"
#include "oc/oc_adapters/rdma_write_adapter.hpp"
#include "oc/oc_adapters/shared_memory_adapter.hpp"
#include "oc/pipe.hpp"
#include "rdma_sample_common.hpp"

using namespace doca_stdexec;

constexpr size_t buf_size = 4096 * 4096;

std::vector<std::byte> buf(buf_size);

uint32_t tail = 0;
uint32_t head = 0;

exec::task<void> read_task() {
  while (true) {
    if (buf_size - (tail - head) > buf_size) {
      auto ptr = buf.data() + tail * buf_size;
      std::memcpy(ptr, buf.data() + head * buf_size, buf_size);
      tail += buf_size;
    }

    co_await std::suspend_always{};
  }
}

exec::task<void> server_task() {
  tcp::tcp_server server;
  auto doca_runtime = doca_stdexec::doca_pe_context();

  server.listen(8080);
  auto socket = server.accept();

  auto metadata = co_await setup_rdma(socket, doca_runtime);

  auto adapter = oc::oc_adapters::shared_memory_adapter<std::byte>(
      std::span<std::byte>(buf));

  auto src_head_buf =
      std::span<std::byte>{(std::byte *)metadata.src_head_buf.data(), buf_size};
  auto dst_tail_buf =
      std::span<std::byte>{(std::byte *)metadata.dst_tail_buf.data(), buf_size};

  auto middle_buf = std::vector<std::byte>(8);
  auto middle_buf_span = std::span<std::byte>(middle_buf);

  auto pipe_metadata = oc::ForwardPipeMetadata<
      oc::oc_adapters::shared_memory_adapter<std::byte>>(
      adapter, middle_buf_span, src_head_buf, dst_tail_buf);

  auto src_buf_span = std::span<std::byte>{
      (std::byte *)metadata.src_buffer_buf.data(), buf_size};
  auto dst_buf_span = std::span<std::byte>{
      (std::byte *)metadata.dst_buffer_buf.data(), buf_size};

  auto rdma_pipe = std::make_shared<
      oc::Pipe<oc::oc_adapters::shared_memory_adapter<std::byte>,
               oc::oc_adapters::shared_memory_adapter<std::byte>,
               oc::oc_adapters::shared_memory_adapter<std::byte>>>(
      adapter, src_buf_span, dst_buf_span);

  oc::PipeLine pipe_line;

  pipe_line.push_pipe(rdma_pipe);

  while (true) {
    co_await pipe_line.progress(doca_runtime.get_scheduler());
  }

  co_return;
}

int main(int argc, char **argv) {
  stdexec::sync_wait(server_task());
  return 0;
}