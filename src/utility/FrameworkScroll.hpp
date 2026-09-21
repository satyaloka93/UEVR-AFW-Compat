#pragma once
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>

namespace uevr::ui {
// UEVR maps VR left stick to D-pad navigation, but this ImGui version's
// continuous gamepad scrolling consumes LStick, not RStick. Provide RStick
// scrolling directly in the framework's settings pane, without requiring a ray
// hover. Retain the existing wheel path when ray mouse emulation generated it.
inline void scroll_settings_with_right_stick() {
    auto& io = ImGui::GetIO();
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ||
        ImGui::IsAnyItemActive() || io.MouseDown[0] || io.MouseWheel != 0 ||
        !(io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) ||
        !(io.BackendFlags & ImGuiBackendFlags_HasGamepad)) return;

    const float axis = ImGui::GetKeyData(ImGuiKey_GamepadRStickDown)->AnalogValue -
                       ImGui::GetKeyData(ImGuiKey_GamepadRStickUp)->AnalogValue;
    if (axis > -0.1f && axis < 0.1f) return;
    const float delta = axis * ImGui::GetFontSize() * 60.0f * (std::min)(io.DeltaTime, 0.05f);
    ImGui::SetScrollY(std::clamp(ImGui::GetScrollY() + delta, 0.0f, ImGui::GetScrollMaxY()));
}
}
