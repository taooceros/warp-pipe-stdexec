#include <stdexec/execution.hpp>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ex = stdexec;

// Receiver adaptor: call f(as...) and forward original values.
template <class R, class F> class tap_receiver {
  R r_;
  F f_;

public:
  // P2300-style member customization of receiver
  using receiver_concept = ex::receiver_t;

  tap_receiver(R r, F f) : r_(std::move(r)), f_(std::move(f)) {}

  template <class... As> void set_value(As &&...as) && noexcept {
    try {
      // Call side-effect; do not consume the values
      std::invoke(f_, as...);
      ex::set_value(std::move(r_), std::forward<As>(as)...);
    } catch (...) {
      ex::set_error(std::move(r_), std::current_exception());
    }
  }

  template <class E> void set_error(E &&e) && noexcept {
    ex::set_error(std::move(r_), std::forward<E>(e));
  }

  void set_stopped() && noexcept { ex::set_stopped(std::move(r_)); }

  // Forward queries
  decltype(auto) get_env() const noexcept { return ex::get_env(r_); }
};

// Sender wrapper
template <class S, class F> struct tap_sender {
  using sender_concept = ex::sender_t;

  S s_;
  F f_;

  template <class Env>
  auto get_completion_signatures(Env &&) const noexcept
      -> stdexec::completion_signatures_of_t<S, Env> {
    return {};
  }

  // Member connect: wrap receiver and connect to child
  template <class R> auto connect(R r) && {
    return ex::connect(std::move(s_),
                       tap_receiver<R, F>(std::move(r), std::move(f_)));
  }

  // Forward environment by default
  decltype(auto) get_env() const noexcept { return ex::get_env(s_); }
};

template <class _Fun, class... _As>
struct __binder_back
    : stdexec::__tuple_for<_As...>,
      stdexec::sender_adaptor_closure<__binder_back<_Fun, _As...>> {
  STDEXEC_ATTRIBUTE(no_unique_address) _Fun __fun_ {};

  template <stdexec::sender _Sender>
    requires stdexec::__callable<_Fun, _Sender, _As...>
  STDEXEC_ATTRIBUTE(host, device, always_inline)
  auto operator()(_Sender &&__sndr) && noexcept(
      stdexec::__nothrow_callable<_Fun, _Sender, _As...>)
      -> stdexec::__call_result_t<_Fun, _Sender, _As...> {
    return this->apply(
        [&__sndr, this](_As &...__as) noexcept(
            stdexec::__nothrow_callable<_Fun, _Sender, _As...>)
            -> stdexec::__call_result_t<_Fun, _Sender, _As...> {
          return static_cast<_Fun &&>(__fun_)(static_cast<_Sender &&>(__sndr),
                                              static_cast<_As &&>(__as)...);
        },
        *this);
  }

  template <stdexec::sender _Sender>
    requires stdexec::__callable<const _Fun &, _Sender, const _As &...>
  STDEXEC_ATTRIBUTE(host, device, always_inline)
  auto operator()(_Sender &&__sndr) const & noexcept(
      stdexec::__nothrow_callable<const _Fun &, _Sender, const _As &...>)
      -> stdexec::__call_result_t<const _Fun &, _Sender, const _As &...> {
    return this->apply(
        [&__sndr, this](const _As &...__as) noexcept(
            stdexec::__nothrow_callable<const _Fun &, _Sender, const _As &...>)
            -> stdexec::__call_result_t<const _Fun &, _Sender, const _As &...> {
          return __fun_(static_cast<_Sender &&>(__sndr), __as...);
        },
        *this);
  }
};

// Pipeable adaptor
struct tap_t {
  template <class S, class F> auto operator()(S &&s, F &&f) const {
    return tap_sender<std::decay_t<S>, std::decay_t<F>>{std::forward<S>(s),
                                                        std::forward<F>(f)};
  }

  template <class F> auto operator()(F &&f) const {
    return __binder_back<tap_t, F>{std::forward<F>(f)};
  }
};

// Usage: s | tap([](auto const&...) { /*observe*/ });
inline constexpr tap_t tap{};
