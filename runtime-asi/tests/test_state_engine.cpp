// test_state_engine.cpp — manifest condition evaluation (SF-4).

#include "test_common.hpp"

#include "state_engine.hpp"

using senseforge::BindingValues;
using senseforge::EvaluateCondition;

int main() {
    const BindingValues values{
        {"is_menu_or_map_open", 1.0},
        {"is_aiming_bow", 0.0},
        {"bow_draw_tension", 0.42},
        {"player_health_ratio", 0.75},
        {"adrenaline_segments", 3.0},
    };

    // Simple equality.
    assert(EvaluateCondition("is_menu_or_map_open == 1", values));
    assert(!EvaluateCondition("is_menu_or_map_open == 0", values));

    // Conjunction — the melee state from spec §3.1.
    assert(EvaluateCondition("is_menu_or_map_open == 0 && is_aiming_bow == 0",
                             values) == false);
    assert(EvaluateCondition("is_menu_or_map_open == 1 && is_aiming_bow == 0",
                             values));

    // Comparisons and floats.
    assert(EvaluateCondition("bow_draw_tension >= 0.4", values));
    assert(EvaluateCondition("bow_draw_tension < 1", values));
    assert(EvaluateCondition("adrenaline_segments > 2", values));
    assert(!EvaluateCondition("player_health_ratio <= 0.5", values));
    assert(EvaluateCondition("player_health_ratio != 0", values));

    // Parentheses and negation.
    assert(EvaluateCondition(
        "(is_menu_or_map_open == 1) && !(is_aiming_bow == 1)", values));

    // Disjunction.
    assert(EvaluateCondition("is_aiming_bow == 1 || is_menu_or_map_open == 1",
                             values));

    // ---- Fail-closed contract --------------------------------------------
    // Unknown identifier: never true, even negated or OR-ed.
    assert(!EvaluateCondition("unknown_binding == 1", values));
    assert(!EvaluateCondition("unknown_binding != 1", values));
    assert(!EvaluateCondition("!unknown_binding", values));
    assert(!EvaluateCondition("unknown_binding == 0 || is_menu_or_map_open == 1",
                             values));
    // One unknown operand poisons the whole && (fail-closed).
    assert(!EvaluateCondition("is_menu_or_map_open == 1 && nope == 0",
                              values));

    // Malformed expressions evaluate false.
    assert(!EvaluateCondition("", values));
    assert(!EvaluateCondition("===", values));
    assert(!EvaluateCondition("is_menu_or_map_open ==", values));
    assert(!EvaluateCondition("(is_menu_or_map_open == 1", values));

    return 0;
}
