// minimal stand-in so the HLS source compiles with plain g++ (board-side functional check only)
#pragma once
#include <stdint.h>
struct shim_u64 { uint64_t v; shim_u64(uint64_t x = 0) : v(x) {} uint64_t to_uint64() const { return v; } };
template <int D, int U, int TI, int TD> struct ap_axiu { shim_u64 data; uint8_t keep, strb; bool last; };
