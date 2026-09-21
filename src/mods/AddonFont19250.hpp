#pragma once
#include <imgui.h>
#include <cstddef>
#include <cmath>
#include <map>
#include <vector>

namespace addon_ui {
// 1.92.5 ImFont ABI, 16-bit ImWchar (confirmed by the binary's Scale +0x48).
// Only LegacySize/Scale are supported data accesses. Atlas, glyph and baked
// data are deliberately NOT aliases to the incompatible native structures.
struct Font19250 {
    void* LastBaked{};
    void* OwnerAtlas{};
    int Flags{};
    float CurrentRasterizerDensity{1};
    unsigned FontId{};
    float LegacySize{};
    int SourcesSize{}, SourcesCapacity{};
    void* SourcesData{};
    unsigned short EllipsisChar{}, FallbackChar{};
    unsigned char Used8kPagesMap{}, EllipsisAutoBake{};
    int RemapSize{}, RemapCapacity{};
    void* RemapData{};
    float Scale{1};
};
static_assert(offsetof(Font19250, LegacySize) == 0x1c);
static_assert(offsetof(Font19250, Scale) == 0x48);
static_assert(sizeof(Font19250) == 0x50);

struct FontScope { ImFont* font; float saved_scale; };
inline thread_local std::map<ImFont*, Font19250> font_handles;
inline thread_local std::vector<FontScope> font_scopes;

inline Font19250* font() {
    auto* native = ImGui::GetFont();
    auto [it, inserted] = font_handles.try_emplace(native);
    if (inserted) {
        it->second.LegacySize = native->FontSize;
        it->second.Scale = native->Scale;
    }
    return &it->second;
}

inline ImFont* native_font(Font19250* handle) {
    if (handle == nullptr) return ImGui::GetFont();
    for (auto& pair : font_handles)
        if (&pair.second == handle) return pair.first;
    return nullptr;
}
inline void draw_text(ImDrawList* list, Font19250* handle, float size,
                      const ImVec2& pos, ImU32 color, const char* begin,
                      const char* end, float wrap, const ImVec4* clip) {
    auto* native = native_font(handle);
    if (!list || !native || !begin || !std::isfinite(size) || size <= 0) return;
    list->AddText(native, size, pos, color, begin, end, wrap, clip);
}

inline void push_font(Font19250* requested, float base_size) {
    ImFont* native = ImGui::GetFont();
    Font19250* handle = requested ? requested : font();
    bool known = false;
    for (auto& pair : font_handles) {
        if (&pair.second == handle) { native = pair.first; known = true; break; }
    }
    // Unknown foreign pointers are never dereferenced or passed to native ImGui.
    float scale = known ? handle->Scale : native->Scale;
    if (!std::isfinite(scale) || scale <= 0) scale = 1;
    if (known && (!std::isfinite(base_size) || base_size <= 0)) base_size = handle->LegacySize;
    if (known && native->FontSize > 0 && std::isfinite(base_size) && base_size > 0)
        scale *= base_size / native->FontSize;
    if (!std::isfinite(scale) || scale <= 0 || scale > 16) scale = native->Scale;
    font_scopes.push_back({native, native->Scale});
    native->Scale = scale;
    ImGui::PushFont(native);
}
inline void pop_font() {
    if (font_scopes.empty()) return; // do not pop a font owned by UEVR
    const auto scope = font_scopes.back();
    font_scopes.pop_back();
    scope.font->Scale = scope.saved_scale;
    ImGui::PopFont();
}
inline size_t font_scope_depth() { return font_scopes.size(); }
inline void begin_font_callback() {
    // Handles are callback-local. Do not retain raw native fonts across atlas
    // or context rebuilds, or dereference stale entries during cleanup.
    if (font_scopes.empty()) font_handles.clear();
}
inline void restore_font_scopes(size_t depth) {
    while (font_scopes.size() > depth) pop_font();
    // Start the next callback from native values even after an interrupted
    // addon restore sequence. Handles stay at stable addresses in the map.
    for (auto& pair : font_handles) {
        pair.second.Scale = pair.first->Scale;
        pair.second.LegacySize = pair.first->FontSize;
    }
}
} // namespace addon_ui
