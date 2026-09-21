#pragma once

namespace cvar_script {
enum class Action { apply, unchanged, frozen_conflict };
// Values use the same float representation as IConsoleVariable::GetFloat.
// Callers must reject non-finite/out-of-range input before consulting policy.
constexpr Action action(float desired, float current, bool frozen, float frozen_value) {
    if (frozen && desired != frozen_value) return Action::frozen_conflict;
    return desired == current ? Action::unchanged : Action::apply;
}
}
