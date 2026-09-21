#pragma once
#include <imgui.h>
#include <array>
#include <cstdarg>
#include <cstdio>

namespace addon_ui {
// ImVec4 and these printf-style signatures match both ImGui versions.
// Copy the argument list: diagnostics must not consume the renderer's arguments.
inline std::array<char, 512> text_colored(const ImVec4& color, const char* format, va_list args) {
    std::array<char, 512> message{};
    va_list copy;
    va_copy(copy, args);
    std::vsnprintf(message.data(), message.size(), format, copy);
    va_end(copy);
    message.back() = '\0';
    ImGui::TextColoredV(color, format, args);
    return message;
}
}
