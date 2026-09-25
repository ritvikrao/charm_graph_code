// Just enough of Charm++ for weighted_node_struct.h in a standalone test.
#pragma once
#include <cstdio>
#include <cstdlib>
#define CkAbort(...) (std::fprintf(stderr, __VA_ARGS__), std::fputc('\n', stderr), std::abort())
