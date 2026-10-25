// state_engine.hpp — manifest condition evaluator (SF-4).
//
// Evaluates expressions from {game}.sense.json states, e.g.
//   "is_menu_or_map_open == 1"
//   "is_menu_or_map_open == 0 && is_aiming_bow == 0"
// against the live binding values. Fail-closed: an identifier without a
// healthy live value makes the whole condition false (the game must never
// receive trigger feedback driven by a stale/missing read).

#pragma once

#include <map>
#include <string>

namespace senseforge {

using BindingValues = std::map<std::string, double>;

// Returns true when `condition` holds for `values`. Malformed expressions
// also evaluate to false (fail-closed).
bool EvaluateCondition(const std::string& condition,
                       const BindingValues& values);

}  // namespace senseforge
