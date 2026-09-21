#pragma once
#include <imgui.h>

namespace addon_ui {
inline thread_local unsigned disabled_depth = 0;
inline void begin_disabled(bool disabled) {
    ImGui::BeginDisabled(disabled);
    ++disabled_depth; // false scopes must also be paired
}
inline void end_disabled() {
    if (!disabled_depth) return; // do not consume a host-owned scope
    ImGui::EndDisabled();
    --disabled_depth;
}
inline void restore_disabled(unsigned depth) {
    while (disabled_depth > depth) end_disabled();
}
}
