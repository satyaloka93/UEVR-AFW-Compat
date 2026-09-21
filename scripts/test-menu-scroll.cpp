#include "imgui.h"
#include "imgui_internal.h"
#include "../src/utility/FrameworkScroll.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

static void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static ImGuiWindow* draw(bool bounded, float height) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({700, height});
    ImGui::Begin("UEVR test");
    for (int i = 0; i < 7; ++i) ImGui::Text("Framework header %d", i);
    const float pane_height = bounded ? std::max(1.0f,
        ImGui::GetContentRegionAvail().y - ImGui::GetStyle().CellPadding.y * 2.0f) : 0;
    ImGuiWindow* pane{};
    if (ImGui::BeginTable("UEVRTable", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("UEVRLeftPane", {0, pane_height}, true);
        ImGui::Selectable("VR"); ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::BeginChild("UEVRRightPane", {0, pane_height}, true, ImGuiWindowFlags_AlwaysUseWindowPadding);
        pane = ImGui::GetCurrentWindow();
        if (bounded) uevr::ui::scroll_settings_with_right_stick();
        for (int i = 0; i < 90; ++i) ImGui::Text("Performance CVar %d", i);
        ImGui::EndChild(); ImGui::EndTable();
    }
    ImGui::End(); ImGui::Render();
    return pane;
}

static void run(bool bounded, float height) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1200, 1000}; io.DeltaTime = 1.0f / 60;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    unsigned char* pixels; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGuiWindow* pane{};
    for (int i = 0; i < 5; ++i) pane = draw(bounded, height);
    std::printf("bounded=%d height=%.0f pane_height=%.1f scroll_max=%.1f bottom=%.1f\n",
        bounded, height, pane->Size.y, pane->ScrollMax.y, pane->Pos.y + pane->Size.y);
    if (!bounded) {
        ImGui::FocusWindow(pane);
        for (int i = 0; i < 30; ++i) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown, true, 1);
            pane = draw(bounded, height);
        }
        check(pane->Scroll.y == 0, "legacy right-stick input reproduces stuck list");
    }
    if (bounded) {
        check(pane->Pos.y + pane->Size.y <= height, "pane fits visible framework");
        check(pane->ScrollMax.y > 500, "long CVar list has scroll range");
        ImGui::FocusWindow(pane);
        for (int i = 0; i < 30; ++i) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown, true, 1);
            pane = draw(bounded, height);
        }
        check(pane->Scroll.y > 0, "right thumbstick scrolls down");
        const float down = pane->Scroll.y;
        for (int i = 0; i < 15; ++i) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown, false, 0);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickUp, true, 1);
            pane = draw(bounded, height);
        }
        check(pane->Scroll.y < down, "right thumbstick scrolls up");
        io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickUp, false, 0);
        pane = draw(bounded, height);
        pane = draw(bounded, height); // settle deferred scroll target
        const float settled = pane->Scroll.y;
        for (int i = 0; i < 8; ++i) {
            io.AddMouseButtonEvent(0, true);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown, true, 1);
            pane = draw(bounded, height);
        }
        check(pane->Scroll.y == settled, "no right-stick scroll while dragging");
        io.AddMouseButtonEvent(0, false);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown, false, 0);
        io.AddMousePosEvent(pane->Pos.x + 60, pane->Pos.y + 60);
        for (int i = 0; i < 3; ++i) pane = draw(bounded, height);
        const float before_wheel = pane->Scroll.y;
        io.AddMouseWheelEvent(0, -2);
        for (int i = 0; i < 3; ++i) pane = draw(bounded, height);
        check(pane->Scroll.y > before_wheel, "mouse-wheel path still scrolls");
    }
    ImGui::DestroyContext();
}
int main() {
    run(false, 700);
    run(true, 700);
    run(true, 355);
    std::puts("PASS bounded framework panes and thumbstick scrolling");
}
