#pragma once
#include <imgui.h>
#include <cstddef>

// ABI field order from https://github.com/ocornut/imgui/blob/v1.92.5/imgui.h
// Separate from the host's 1.89.9 ImGuiStyle. Snapshot for addon reads; direct
// writes to this object intentionally do not mutate the host's incompatible ABI.
namespace addon_ui {
struct Style19250 {
    float FontSizeBase{}, FontScaleMain{1}, FontScaleDpi{1};
    float Alpha{}, DisabledAlpha{};
    ImVec2 WindowPadding;
    float WindowRounding{}, WindowBorderSize{}, WindowBorderHoverPadding{4};
    ImVec2 WindowMinSize, WindowTitleAlign;
    int WindowMenuButtonPosition{};
    float ChildRounding{}, ChildBorderSize{}, PopupRounding{}, PopupBorderSize{};
    ImVec2 FramePadding;
    float FrameRounding{}, FrameBorderSize{};
    ImVec2 ItemSpacing, ItemInnerSpacing, CellPadding, TouchExtraPadding;
    float IndentSpacing{}, ColumnsMinSpacing{}, ScrollbarSize{}, ScrollbarRounding{};
    float ScrollbarPadding{2}, GrabMinSize{}, GrabRounding{}, LogSliderDeadzone{};
    float ImageBorderSize{}, TabRounding{}, TabBorderSize{}, TabMinWidthBase{};
    float TabMinWidthShrink{}, TabCloseButtonMinWidthSelected{-1};
    float TabCloseButtonMinWidthUnselected{}, TabBarBorderSize{1}, TabBarOverlineSize{2};
    float TableAngledHeadersAngle{0.610865238f};
    ImVec2 TableAngledHeadersTextAlign{0.5f, 0};
    int TreeLinesFlags{};
    float TreeLinesSize{1}, TreeLinesRounding{}, DragDropTargetRounding{};
    float DragDropTargetBorderSize{2}, DragDropTargetPadding{3.5f};
    int ColorButtonPosition{};
    ImVec2 ButtonTextAlign, SelectableTextAlign;
    float SeparatorTextBorderSize{};
    ImVec2 SeparatorTextAlign, SeparatorTextPadding, DisplayWindowPadding, DisplaySafeAreaPadding;
    float MouseCursorScale{};
    bool AntiAliasedLines{}, AntiAliasedLinesUseTex{}, AntiAliasedFill{};
    float CurveTessellationTol{}, CircleTessellationMaxError{};
    ImVec4 Colors[60];
    float HoverStationaryDelay{}, HoverDelayShort{}, HoverDelayNormal{};
    int HoverFlagsForTooltipMouse{}, HoverFlagsForTooltipNav{};
    float _MainScale{1}, _NextFrameFontSizeBase{};
};
static_assert(offsetof(Style19250, FramePadding) == 0x4c);
static_assert(offsetof(Style19250, FrameRounding) == 0x54);

// 1.92.5 indices 0..32 are unchanged. New colors use the closest host color.
inline int host_color(int index) {
    static constexpr int tail[] = {
        ImGuiCol_Text, ImGuiCol_TabHovered, ImGuiCol_Tab, ImGuiCol_TabActive,
        ImGuiCol_TabActive, ImGuiCol_TabUnfocused, ImGuiCol_TabUnfocusedActive,
        ImGuiCol_TabUnfocusedActive, ImGuiCol_PlotLines, ImGuiCol_PlotLinesHovered,
        ImGuiCol_PlotHistogram, ImGuiCol_PlotHistogramHovered, ImGuiCol_TableHeaderBg,
        ImGuiCol_TableBorderStrong, ImGuiCol_TableBorderLight, ImGuiCol_TableRowBg,
        ImGuiCol_TableRowBgAlt, ImGuiCol_CheckMark, ImGuiCol_TextSelectedBg,
        ImGuiCol_Border, ImGuiCol_DragDropTarget, ImGuiCol_DragDropTarget,
        ImGuiCol_Text, ImGuiCol_NavHighlight, ImGuiCol_NavWindowingHighlight,
        ImGuiCol_NavWindowingDimBg, ImGuiCol_ModalWindowDimBg
    };
    static_assert(sizeof(tail) / sizeof(tail[0]) == 27);
    if (index < 0 || index >= 60) return ImGuiCol_Text;
    return index < 33 ? index : tail[index - 33];
}

inline Style19250& style() {
    static thread_local Style19250 out;
    const auto& src = ImGui::GetStyle();
    out.FontSizeBase = ImGui::GetFontSize();
#define COPY_STYLE(name) out.name = src.name
    COPY_STYLE(Alpha); COPY_STYLE(DisabledAlpha); COPY_STYLE(WindowPadding);
    COPY_STYLE(WindowRounding); COPY_STYLE(WindowBorderSize); COPY_STYLE(WindowMinSize);
    COPY_STYLE(WindowTitleAlign); COPY_STYLE(WindowMenuButtonPosition);
    COPY_STYLE(ChildRounding); COPY_STYLE(ChildBorderSize); COPY_STYLE(PopupRounding);
    COPY_STYLE(PopupBorderSize); COPY_STYLE(FramePadding); COPY_STYLE(FrameRounding);
    COPY_STYLE(FrameBorderSize); COPY_STYLE(ItemSpacing); COPY_STYLE(ItemInnerSpacing);
    COPY_STYLE(CellPadding); COPY_STYLE(TouchExtraPadding); COPY_STYLE(IndentSpacing);
    COPY_STYLE(ColumnsMinSpacing); COPY_STYLE(ScrollbarSize); COPY_STYLE(ScrollbarRounding);
    COPY_STYLE(GrabMinSize); COPY_STYLE(GrabRounding); COPY_STYLE(LogSliderDeadzone);
    COPY_STYLE(TabRounding); COPY_STYLE(TabBorderSize); COPY_STYLE(ColorButtonPosition);
    COPY_STYLE(ButtonTextAlign); COPY_STYLE(SelectableTextAlign); COPY_STYLE(SeparatorTextBorderSize);
    COPY_STYLE(SeparatorTextAlign); COPY_STYLE(SeparatorTextPadding); COPY_STYLE(DisplayWindowPadding);
    COPY_STYLE(DisplaySafeAreaPadding); COPY_STYLE(MouseCursorScale); COPY_STYLE(AntiAliasedLines);
    COPY_STYLE(AntiAliasedLinesUseTex); COPY_STYLE(AntiAliasedFill); COPY_STYLE(CurveTessellationTol);
    COPY_STYLE(CircleTessellationMaxError); COPY_STYLE(HoverStationaryDelay);
    COPY_STYLE(HoverDelayShort); COPY_STYLE(HoverDelayNormal);
    COPY_STYLE(HoverFlagsForTooltipMouse); COPY_STYLE(HoverFlagsForTooltipNav);
#undef COPY_STYLE
    out.TabCloseButtonMinWidthUnselected = src.TabMinWidthForCloseButton;
    for (int i = 0; i < 60; ++i) out.Colors[i] = src.Colors[host_color(i)];
    return out;
}

inline thread_local int color_depth = 0;
inline const ImVec4& style_color(int index) {
    return ImGui::GetStyleColorVec4(host_color(index));
}
inline void push_color_u32(int index, ImU32 color) {
    ImGui::PushStyleColor(host_color(index), color);
    ++color_depth;
}
inline void push_color_vec4(int index, const ImVec4& color) {
    ImGui::PushStyleColor(host_color(index), color);
    ++color_depth;
}
inline void pop_colors(int count) {
    if (count < 0) return;
    if (count > color_depth) count = color_depth;
    if (count) ImGui::PopStyleColor(count);
    color_depth -= count;
}
inline void restore_colors(int depth) {
    if (color_depth > depth) pop_colors(color_depth - depth);
}
inline ImU32 color_vec4(const ImVec4& color) { return ImGui::GetColorU32(color); }
inline ImU32 color_u32(ImU32 color, float alpha) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(color);
    v.w *= alpha;
    return ImGui::GetColorU32(v);
}
} // namespace addon_ui
