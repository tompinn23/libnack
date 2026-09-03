/*
 * The one piece of <nack/nack.hpp>'s own internals a test needs to reach.
 *
 * Real backends cannot synthesise input, so cpp_smoke.cpp builds a raw
 * engine event by hand and needs the same translation nack::app::poll() uses
 * to turn it into the public variant. That translation has no business being
 * public - no caller ever constructs a struct nack_event themselves - so it
 * is declared here, for the test, rather than in <nack/nack.hpp> for
 * everyone.
 */
#ifndef NACK_HPP_TEST_HOOKS_H_INCLUDED
#define NACK_HPP_TEST_HOOKS_H_INCLUDED

#include <nack/nack.hpp>

#include "nack_core.h"

#include <optional>

namespace nack {
namespace detail {

std::optional<event> to_event(const struct nack_event &ev);

}  // namespace detail
}  // namespace nack

#endif /* NACK_HPP_TEST_HOOKS_H_INCLUDED */
