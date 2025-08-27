#pragma once

#ifndef UTILS_ASSERTION_HPP
#define UTILS_ASSERTION_HPP

#include <cstdio>

inline void panic(const char *message) {
  printf("OC PAINT: %s\n", message);
  __builtin_trap();
}

#endif