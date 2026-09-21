// Standalone test for the UI translation, not a game/runtime integration test.
#include "../src/mods/AddonStyle19250.hpp"
#include "../src/mods/AddonFont19250.hpp"
#include "../src/mods/AddonText19250.hpp"
#include "../src/mods/AddonTooltip19250.hpp"
#include "../src/mods/AddonDisabled19250.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
static auto colored(const char* format, ...) {
    va_list args;
    va_start(args, format);
    auto result = addon_ui::text_colored(ImVec4(1, 0, 0, 1), format, args);
    va_end(args);
    return result;
}
int main() {
    ImGui::CreateContext();
    auto& native = ImGui::GetStyle();
    native.FrameRounding = 7.25f;
    native.FramePadding = ImVec2(11, 13);
    native.Colors[ImGuiCol_TabActive] = ImVec4(0.1f, 0.2f, 0.3f, 0.4f);
    auto& translated = addon_ui::style();
    assert(translated.FrameRounding == 7.25f);
    assert(translated.FramePadding.x == 11 && translated.FramePadding.y == 13);
    assert(translated.Colors[36].w == 0.4f);
    for (int i = 0; i < 60; ++i)
        assert(addon_ui::host_color(i) >= 0 && addon_ui::host_color(i) < ImGuiCol_COUNT);
    assert(addon_ui::host_color(-1) == ImGuiCol_Text);
    assert(addon_ui::host_color(60) == ImGuiCol_Text);
    translated.FrameRounding = 99;
    assert(native.FrameRounding == 7.25f);
    assert(addon_ui::style().FrameRounding == 7.25f);
    const auto old_color = addon_ui::style_color(36);
    addon_ui::push_color_vec4(36, ImVec4(1, 0, 0, 1));
    assert(addon_ui::style_color(36).x == 1 && addon_ui::style_color(36).y == 0);
    addon_ui::push_color_u32(36, 0xffffffff);
    addon_ui::pop_colors(1);
    assert(addon_ui::style_color(36).y == 0);
    addon_ui::restore_colors(0);
    assert(addon_ui::style_color(36).x == old_color.x);
    assert(addon_ui::style_color(36).w == old_color.w);
    addon_ui::pop_colors(1); // cannot consume a host-owned style push
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(640, 480);
    io.DeltaTime = 1.0f / 60;
    io.AddMousePosEvent(123, 145);
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(600, 440));
    ImGui::Begin("bridge-test");
    const float alpha_before = ImGui::GetStyle().Alpha;
    addon_ui::begin_disabled(true);
    assert(ImGui::GetStyle().Alpha < alpha_before);
    addon_ui::begin_disabled(false);
    addon_ui::end_disabled();
    assert(ImGui::GetStyle().Alpha < alpha_before);
    addon_ui::restore_disabled(0);
    assert(ImGui::GetStyle().Alpha == alpha_before);
    ImGui::BeginDisabled(); // host-owned scope must survive unmatched addon end
    const float host_alpha = ImGui::GetStyle().Alpha;
    addon_ui::end_disabled();
    assert(ImGui::GetStyle().Alpha == host_alpha);
    ImGui::EndDisabled();
    const auto text_color_before = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const float cursor_before = ImGui::GetCursorPosY();
    const auto status = colored("%s: Waiting (%d, %.1f)", "NR", 7, 2.5);
    assert(std::strcmp(status.data(), "NR: Waiting (7, 2.5)") == 0);
    assert(ImGui::GetCursorPosY() > cursor_before);
    assert(ImGui::GetStyleColorVec4(ImGuiCol_Text).x == text_color_before.x);
    const std::string long_text(800, 'x');
    const auto bounded = colored("%s", long_text.c_str());
    assert(std::strlen(bounded.data()) == 511);
    assert(ImGui::GetMousePos().x == 123 && ImGui::GetMousePos().y == 145);
    assert(ImGui::CalcTextSize("Choice").x > 0);
    addon_ui::begin_font_callback();
    const float before = ImGui::GetFont()->Scale;
    const float size_before = ImGui::GetFontSize();
    auto* handle = addon_ui::font();
    auto* draw_list = ImGui::GetWindowDrawList();
    const int vertices_before = draw_list->VtxBuffer.Size;
    addon_ui::draw_text(draw_list, handle, ImGui::GetFontSize(),
                        ImGui::GetCursorScreenPos(), 0xffffffff, "Choice", nullptr, 0, nullptr);
    assert(draw_list->VtxBuffer.Size > vertices_before);
    handle->Scale *= 2;
    addon_ui::push_font(handle, handle->LegacySize);
    assert(ImGui::GetFont()->Scale == before * 2);
    assert(ImGui::GetFontSize() == size_before * 2);
    addon_ui::push_font(handle, handle->LegacySize);
    addon_ui::pop_font();
    assert(ImGui::GetFont()->Scale == before * 2);
    addon_ui::restore_font_scopes(0); // interrupted overlay cleanup
    assert(ImGui::GetFont()->Scale == before);
    assert(ImGui::GetFontSize() == size_before);
    assert(addon_ui::font_scope_depth() == 0);
    addon_ui::pop_font(); // unmatched addon pop must not touch host stack
    const auto host_cursor = ImGui::GetCursorScreenPos();
    assert(addon_ui::begin_tooltip());
    assert(!addon_ui::begin_tooltip()); // reject nested addon tooltip
    const float tooltip_y = ImGui::GetCursorPosY();
    ImGui::TextUnformatted("A long tooltip sentence repeated to verify word wrapping. A long tooltip sentence repeated to verify word wrapping. A long tooltip sentence repeated to verify word wrapping.");
    assert(ImGui::GetCursorPosY() - tooltip_y > 2 * ImGui::GetTextLineHeight());
    addon_ui::end_tooltip();
    assert(addon_ui::tooltip_depth == 0);
    assert(ImGui::GetCursorScreenPos().x == host_cursor.x);
    assert(ImGui::GetCursorScreenPos().y == host_cursor.y);
    addon_ui::end_tooltip(); // cannot close host window
    ImGui::End();
    ImGui::EndFrame();
    ImGui::DestroyContext();
    std::puts("PASS: text status formatting/bounds/layout, style/color, font and scope cleanup");
}
