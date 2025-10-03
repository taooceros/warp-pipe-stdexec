#pragma once
#include "doca_stdexec/progress_engine.hpp"
#include "stdexec/__detail/__start_detached.hpp"
#include <memory>
#include <optional>
#include <queue>
#ifndef PIPE_HPP
#define PIPE_HPP

#include "oc/oc_adapter.hpp"
#include <doca_stdexec/buf.hpp>
#include <exec/task.hpp>
#include <stdexec/execution.hpp>

namespace ex = stdexec;

namespace oc {

class PipeBase;
class PipeLine;

struct PendingUpdate {
  uint32_t before_src_tail;
  uint32_t before_dst_tail;
  uint32_t after_src_tail;
  uint32_t after_dst_tail;

  std::strong_ordering operator<=>(const PendingUpdate &other) const {
    return before_src_tail <=> other.before_src_tail;
  }
};

// store tail to next pipe (so possibly remote)
// fetch head from previous pipe (assuming local)
class ForwardPipeMetadataBase {
public:
  virtual ~ForwardPipeMetadataBase() = default;
  virtual uint32_t fetch_head() = 0;
  virtual exec::task<void> store_tail(uint32_t tail) = 0;
};

// store head to previous pipe (so possibly remote)
// fetch tail from next pipe (assuming local)
class BackwardPipeMetadataBase {
public:
  virtual ~BackwardPipeMetadataBase() = default;
  virtual uint32_t fetch_tail() = 0;
  virtual exec::task<void> store_head(uint32_t head) = 0;
};

class PipeMetadataBase {
public:
  virtual ~PipeMetadataBase() = default;
  PipeMetadataBase(std::shared_ptr<ForwardPipeMetadataBase> forward_metadata,
                   std::shared_ptr<BackwardPipeMetadataBase> backward_metadata)
      : forward_metadata(forward_metadata),
        backward_metadata(backward_metadata) {}
  std::shared_ptr<ForwardPipeMetadataBase> forward_metadata;
  std::shared_ptr<BackwardPipeMetadataBase> backward_metadata;
};

template <oc_adapter MetadataAdapter>
class ForwardPipeMetadata : public ForwardPipeMetadataBase {
public:
  ForwardPipeMetadata(MetadataAdapter metadata_adapter,
                      MetadataAdapter::local_buf_t local_buf,
                      MetadataAdapter::local_buf_t head_buf,
                      MetadataAdapter::remote_buf_t remote_tail_buf)
      : metadata_adapter(metadata_adapter), local_buf(local_buf),
        head_buf(head_buf), remote_tail_buf(remote_tail_buf) {}

  MetadataAdapter metadata_adapter;

  // some buffer just for temporary transfer storage
  MetadataAdapter::local_buf_t local_buf;
  // metadata head and tail buffers (remote)
  MetadataAdapter::local_buf_t head_buf;
  MetadataAdapter::remote_buf_t remote_tail_buf;

  uint32_t fetch_head() override { return head_buf.get_data(); }
  exec::task<void> store_tail(uint32_t tail) override {
    return metadata_adapter.transfer(local_buf, remote_tail_buf);
  }
};

template <oc_adapter MetadataAdapter>
class BackwardPipeMetadata : public BackwardPipeMetadataBase {
public:
  BackwardPipeMetadata(MetadataAdapter metadata_adapter,
                       MetadataAdapter::local_buf_t local_buf,
                       MetadataAdapter::remote_buf_t remote_head_buf,
                       MetadataAdapter::local_buf_t tail_buf)
      : metadata_adapter(metadata_adapter), local_buf(local_buf),
        remote_head_buf(remote_head_buf), tail_buf(tail_buf) {}

  MetadataAdapter metadata_adapter;
  MetadataAdapter::local_buf_t local_buf;
  MetadataAdapter::remote_buf_t remote_head_buf;
  MetadataAdapter::local_buf_t tail_buf;

  uint32_t fetch_tail() override { return tail_buf.get_data(); }
  exec::task<void> store_head(uint32_t head) override {
    return metadata_adapter.transfer(local_buf, remote_head_buf);
  }
};

template <oc_adapter MetadataAdapter>
class PipeMetadata : public PipeMetadataBase {
public:
  PipeMetadata(MetadataAdapter forward_metadata_adapter,
               MetadataAdapter backward_metadata_adapter,
               MetadataAdapter::local_buf_t forward_buf,
               MetadataAdapter::local_buf_t backward_buf,
               MetadataAdapter::local_buf_t local_head_buf,
               MetadataAdapter::local_buf_t local_tail_buf,
               MetadataAdapter::remote_buf_t remote_head_buf,
               MetadataAdapter::remote_buf_t remote_tail_buf)
      : forward_metadata(forward_metadata_adapter, forward_buf, local_head_buf,
                         remote_tail_buf),
        backward_metadata(backward_metadata_adapter, backward_buf,
                          remote_head_buf, local_tail_buf) {}

  ForwardPipeMetadata<MetadataAdapter> forward_metadata;
  BackwardPipeMetadata<MetadataAdapter> backward_metadata;
};

class PipeBase {

public:
  PipeBase(uint32_t src_capacity, uint32_t dst_capacity)
      : src_capacity(src_capacity), dst_capacity(dst_capacity), src_tail(0),
        src_head(0), dst_tail(0), dst_head(0) {}
  virtual exec::task<void> transfer() = 0;
  virtual exec::task<void> fetch_tail() = 0;
  virtual exec::task<void> sync_tail() = 0;
  virtual exec::task<void> fetch_head() = 0;
  virtual exec::task<void> sync_head() = 0;

public:
  uint32_t src_capacity;
  uint32_t dst_capacity;

  // cached values
  // TODO: currently asssuming some magic people will update these values
  uint32_t src_tail;
  uint32_t src_head;
  uint32_t dst_tail;
  uint32_t dst_head;

  std::priority_queue<PendingUpdate>
      pending_completed_transfers; // in-ordered commit

  PipeLine *pipe_line;
  std::shared_ptr<PipeBase> prev;
  std::shared_ptr<PipeBase> next;
};

class PipeLine {
public:
  PipeLine() {}

  std::shared_ptr<PipeBase> head;

  exec::task<void> progress(ex::scheduler auto scheduler) {
    for (auto pipe = head; pipe; pipe = pipe->next) {
      co_await (scheduler.schedule() |
                ex::let_value([pipe](auto &&...) { return pipe->transfer(); }));
    }
  }

  void push_pipe(std::shared_ptr<PipeBase> pipe) {
    pipe->pipe_line = this;
    if (head) {
      head->prev = pipe;
      pipe->next = head;
    }
    head = pipe;
  }
};

// TODO: follow stdexec sender/receiver pattern
// Assumption: Symmetric Transfer (size invariant transfer)
// TODO: make pointers readonly/writeonly and have subtype to safeguard this
template <oc_adapter Adapter, oc_adapter PrevMetadataAdapter,
          oc_adapter NextMetadataAdapter>
class Pipe : public PipeBase {

public:
  Pipe(Adapter adapter, Adapter::local_buf_t src_buf,
       Adapter::remote_buf_t dst_buf)
      : PipeBase(src_buf.get_len(), dst_buf.get_len()), adapter(adapter),
        next_metadata(next_metadata) {}
  exec::task<void> transfer() override {
    co_await ex::when_all(forward(), backward());
  }

  exec::task<void> forward() {
    if (src_tail == src_head) {
      co_await fetch_tail();
      co_await fetch_head();
      if (src_tail == src_head) {
        co_return;
      }
    }

    int num_transfer_senders = 0;

    auto current_src_tail = src_tail;
    auto current_dst_tail = dst_tail;

    constexpr int max_transfer_senders = 16;

    typename Adapter::transfer_type transfer_senders[max_transfer_senders];

    for (num_transfer_senders = 0;
         src_tail > src_head && num_transfer_senders < max_transfer_senders;
         num_transfer_senders++) {

      // reset src_buf if it is at the start of a new segment
      if (src_head % src_capacity == 0) {
        src_buf.set_data(src_buf.get_data(), 0);
      }

      // reset dst_buf if it is at the start of a new segment
      if (dst_tail % dst_capacity == 0) {
        dst_buf.set_data(dst_buf.get_data(), 0);
      }

      auto max_to_transfer = std::min(
          src_tail - src_head,
          src_capacity -
              (src_head %
               src_capacity)); // max to transfer from src_buf (contiguous)
      auto max_remaining_capacity = std::min(
          dst_capacity - (dst_tail - dst_head),
          dst_capacity - (dst_tail % dst_capacity)); // max remaining capacity
                                                     // in dst_buf (contiguous)

      auto transfer_size = std::min(max_to_transfer, max_remaining_capacity);

      if (transfer_size == 0) {
        break; // no more data can be transferred
      }

      src_buf.set_data_len(transfer_size);

      auto target_src_tail = src_tail + transfer_size;
      auto target_dst_tail = dst_tail + transfer_size;

      // capture by value to record epoch
      // TODO: figure out better way without using callback
      transfer_senders[num_transfer_senders] =
          adapter.transfer(src_buf, dst_buf);

      current_src_tail = target_src_tail;
      current_dst_tail = target_dst_tail;
    }

    if (num_transfer_senders == 0) {
      co_return;
    }

    src_tail = src_head;
    dst_tail = dst_head;

    auto job = ex::just(std::span<typename Adapter::transfer_type>(
                   transfer_senders, num_transfer_senders)) |
               ex::bulk(num_transfer_senders,
                        [&](int i, auto &&...) { return transfer_senders[i]; });

    co_await job;

    if (next) {
      auto *next_pipe = next.get();

      next_pipe->src_tail = dst_tail;
    }
  }

  exec::task<void> backward() {
    if (dst_head == src_head) {
      co_await fetch_head();
      if (dst_head == src_head) {
        co_return;
      }
      co_return;
    }

    src_head = dst_head;

    if (prev) {
      prev->dst_head = dst_head;
      // TODO: sync dst_head => remote
    }
  }

  // one way sync to next pipe
  exec::task<void> sync_tail() override {
    if (next_metadata) {
      // assuming writing to buffer is synchronous
      auto ptr = next_metadata->local_buf.data();
      std::atomic_uint32_t *atomic_ptr =
          reinterpret_cast<std::atomic_uint32_t *>(ptr);
      atomic_ptr->store(dst_tail);
      co_await next_metadata->metadata_adapter.transfer(
          next_metadata->local_buf, next_metadata->remote_tail_buf);
    }
    co_return;
  }

  exec::task<void> fetch_tail() override {
    if (prev_metadata) {
      auto ptr = prev_metadata->local_buf.data();
      std::atomic_uint32_t *atomic_ptr =
          reinterpret_cast<std::atomic_uint32_t *>(ptr);
      src_tail = atomic_ptr->load();
    }
    co_return;
  }

  exec::task<void> fetch_head() override {
    if (next_metadata) {
      auto ptr = next_metadata->local_buf.data();
      std::atomic_uint32_t *atomic_ptr =
          reinterpret_cast<std::atomic_uint32_t *>(ptr);
      dst_head = atomic_ptr->load();
    }
    co_return;
  }

  // one way sync to prev pipe
  exec::task<void> sync_head() override {
    if (prev_metadata) {
      // assuming writing to buffer is synchronous
      auto ptr = prev_metadata->local_buf.data();
      std::atomic_uint32_t *atomic_ptr =
          reinterpret_cast<std::atomic_uint32_t *>(ptr);
      atomic_ptr->store(src_head);
      co_await prev_metadata->metadata_adapter.transfer(
          prev_metadata->local_buf, prev_metadata->remote_head_buf);
    }
    co_return;
  }

private:
  Adapter adapter;
  Adapter::local_buf_t src_buf;
  Adapter::remote_buf_t dst_buf;
  std::optional<BackwardPipeMetadata<PrevMetadataAdapter>> prev_metadata;
  std::optional<ForwardPipeMetadata<NextMetadataAdapter>> next_metadata;
};

} // namespace oc

#endif