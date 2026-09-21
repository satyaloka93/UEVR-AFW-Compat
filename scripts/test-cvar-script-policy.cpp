#include "../src/mods/vr/CVarScriptPolicy.hpp"
using cvar_script::action;
using cvar_script::Action;
static_assert(action(0, 0, false, 0) == Action::unchanged);
static_assert(action(0, 1, false, 0) == Action::apply);
static_assert(action(1, 0, true, 0) == Action::frozen_conflict);
static_assert(action(1, 1, true, 0) == Action::frozen_conflict);
static_assert(action(0, 1, true, 0) == Action::apply);
static_assert(action(0, 0, true, 0) == Action::unchanged);
static_assert(action(0.6f, 0.6f, false, 0) == Action::unchanged);
int main() { return 0; }
