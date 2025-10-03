#include "tap.hpp"
#include "stdexec/__detail/__then.hpp"
#include <stdexec/execution.hpp>

int test() {
    auto s2 = stdexec::just(1) | tap([](auto const&...) { /*observe*/ });


    auto s3 = stdexec::then(stdexec::just(1), [](auto const&...) { /*observe*/ });

    stdexec::just(1) | stdexec::then([](auto const&...) { /*observe*/ });
    return 0;
}