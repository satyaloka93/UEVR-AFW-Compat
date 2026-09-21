#pragma once
#include <array>
#include <string_view>

namespace performance_cvars {
enum class Kind { toggle, integer, floating };
struct Entry {
    const wchar_t* name;
    const char* group;
    const char* label;
    Kind kind;
    float minimum, maximum;
    const char* help;
};
inline constexpr Entry entries[]{
    {L"r.Lumen.Reflections.Allow", "Lumen / reflections", "Lumen reflections", Kind::toggle, 0, 1, "Off removes Lumen reflections; the game's fallback may differ."},
    {L"r.Lumen.DiffuseIndirect.Allow", "Lumen / reflections", "Lumen global illumination", Kind::toggle, 0, 1, "Off can substantially change scene lighting. No universal performance gain is guaranteed."},
    {L"r.Lumen.ScreenProbeGather.DownsampleFactor", "Lumen / reflections", "GI probe spacing", Kind::integer, 4, 64, "Higher values reduce probe density and lighting detail."},
    {L"r.Lumen.ScreenProbeGather.TracingOctahedronResolution", "Lumen / reflections", "GI probe ray resolution", Kind::integer, 4, 16, "Lower values reduce tracing quality. Change one setting at a time."},
    {L"r.Lumen.Reflections.DownsampleFactor", "Lumen / reflections", "Reflection downsample factor", Kind::integer, 1, 4, "Higher values trade reflection detail for reduced work."},
    {L"r.SSR.Quality", "Lumen / reflections", "Screen-space reflection quality", Kind::integer, 0, 4, "0 disables SSR. Has an effect only when the game uses SSR."},
    {L"r.VolumetricFog", "Fog", "Volumetric fog", Kind::toggle, 0, 1, "Off changes fog and lighting appearance; try lowering grid quality first."},
    {L"r.VolumetricFog.GridPixelSize", "Fog", "Fog grid pixel size", Kind::integer, 4, 32, "Higher values use a coarser, cheaper grid; may reveal artifacts."},
    {L"r.VolumetricFog.GridSizeZ", "Fog", "Fog depth slices", Kind::integer, 16, 256, "Fewer slices reduce cost and depth precision."},
    {L"r.ShadowQuality", "Shadows", "Shadow quality", Kind::integer, 0, 5, "0 disables shadows. Leave the separate working VSM enable setting unchanged."},
    {L"r.Shadow.MaxResolution", "Shadows", "Maximum shadow resolution", Kind::integer, 256, 4096, "Lower values reduce conventional shadow-map detail and cost. Prefer powers of two."},
    {L"r.Shadow.MaxCSMResolution", "Shadows", "Maximum cascaded shadow resolution", Kind::integer, 256, 4096, "Conventional directional shadows; prefer powers of two."},
    {L"r.Shadow.CSM.MaxCascades", "Shadows", "Maximum shadow cascades", Kind::integer, 1, 4, "Fewer cascades reduce distant shadow quality and may reduce cost."},
    {L"r.Shadow.DistanceScale", "Shadows", "Shadow distance scale", Kind::floating, 0.2f, 2, "Lower values reduce conventional shadow coverage."},
    {L"r.ContactShadows", "Shadows", "Contact shadows", Kind::toggle, 0, 1, "Small-scale contact shadows. May affect stereo appearance."},
    {L"r.DistanceFieldShadowing", "Shadows", "Distance-field shadows", Kind::toggle, 0, 1, "Only affects scenes using distance-field shadows."},
    {L"r.ViewDistanceScale", "Geometry / detail", "View distance scale", Kind::floating, 0.2f, 2, "Scales configured object culling distances; not all objects have distance limits."},
    {L"r.DetailMode", "Geometry / detail", "Detail mode", Kind::integer, 0, 2, "Lower settings hide detail actors where the game supports it."},
    {L"foliage.DensityScale", "Geometry / detail", "Foliage density", Kind::floating, 0, 1, "Only affects foliage configured to support density scaling."},
    {L"grass.DensityScale", "Geometry / detail", "Grass density", Kind::floating, 0, 1, "Reduces landscape grass density where supported."},
};
constexpr bool valid_catalog() {
    for (size_t i = 0; i < std::size(entries); ++i) {
        if (entries[i].minimum >= entries[i].maximum || !*entries[i].label || !*entries[i].help) return false;
        for (size_t j = 0; j < i; ++j)
            if (std::wstring_view(entries[i].name) == entries[j].name) return false;
    }
    return true;
}
static_assert(valid_catalog());
}
