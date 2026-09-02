/*
 * This file must NOT compile.
 *
 * {fmt}'s consteval checking is the whole reason the C++ header asks for
 * C++20: a format string that does not match its arguments should be rejected
 * where it is written, not at run time and not at all. A test that asserts
 * that has to be a compilation that fails, so ctest builds this target and
 * expects it to fail - see the cpp_format_rejected test.
 *
 * If this ever starts compiling, the compile-time checking has quietly
 * stopped working and every bad format string in a caller's code went with
 * it.
 */
#include <nack/nack.hpp>

void two_placeholders_one_argument(nack::console_view console)
{
    console.print(0, 0, nack::white, nack::black, "{} and {}", 1);
}
