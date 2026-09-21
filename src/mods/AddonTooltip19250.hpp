#pragma once
#include <imgui.h>
#include <algorithm>
#include <cstdarg>

namespace addon_ui {
inline thread_local int tooltip_depth = 0;
inline float tooltip_width() {
    return std::max(80.0f, std::min(35.0f * ImGui::GetFontSize(),
                                  ImGui::GetIO().DisplaySize.x - 32.0f));
}
inline bool begin_tooltip() {
    if (tooltip_depth != 0 || !ImGui::BeginTooltip()) return false;
    ImGui::PushTextWrapPos(tooltip_width());
    ++tooltip_depth;
    return true;
}
inline void end_tooltip() {
    if (tooltip_depth == 0) return; // never end a host-owned window
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    --tooltip_depth;
}
inline bool begin_item_tooltip() {
    return ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && begin_tooltip();
}
inline void set_tooltip(const char* format, va_list args) {
    if (begin_tooltip()) {
        ImGui::TextV(format, args);
        end_tooltip();
    }
}
inline void set_item_tooltip(const char* format, va_list args) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) set_tooltip(format, args);
}
}
