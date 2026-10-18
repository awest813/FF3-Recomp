#pragma once
#include "psprecomp/runtime.hpp"
namespace ff3 {
// Bounded PSP EABI formatter. Only 32-bit integer/string register arguments;
// unsupported formats or stack arguments throw before any output is emitted.
std::string format_guest_printf(const psprecomp::GuestMemory &, const psprecomp::AllegrexContext &);
}
