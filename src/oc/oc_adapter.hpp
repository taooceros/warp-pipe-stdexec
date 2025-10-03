#pragma once
#ifndef OC_ADAPTER_HPP
#define OC_ADAPTER_HPP

#include "oc/rb/pod_rb.hpp"
#include <doca_stdexec/buf.hpp>

namespace oc {

template <typename T>
concept oc_adapter = requires(T t, typename T::local_buf_t src_buf,
                              typename T::remote_buf_t dst_buf) {
  typename T::transfer_type;
  typename T::remote_buf_t;
  { src_buf.size_bytes() } -> std::convertible_to<std::size_t>;
  { dst_buf.data() } -> std::convertible_to<void *>;
  { src_buf.size_bytes() } -> std::convertible_to<std::size_t>;
  { dst_buf.size_bytes() } -> std::convertible_to<std::size_t>;
  stdexec::sender<typename T::transfer_type>;
  { t.transfer(src_buf, dst_buf) } -> stdexec::sender;
};

} // namespace oc

#endif