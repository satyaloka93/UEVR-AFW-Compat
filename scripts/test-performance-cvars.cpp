#include "../src/mods/vr/PerformanceCVars.hpp"
using namespace performance_cvars;
constexpr bool test_catalog() {
    if (std::size(entries) != 20 || !valid_catalog()) return false;
    bool reflections = false, fog = false, gi = false;
    for (const auto& entry : entries) {
        if (!*entry.group || !*entry.name) return false;
        if (entry.kind == Kind::toggle && (entry.minimum != 0 || entry.maximum != 1)) return false;
        if (entry.kind != Kind::floating &&
            (entry.minimum != (int)entry.minimum || entry.maximum != (int)entry.maximum)) return false;
        reflections |= std::wstring_view(entry.name) == L"r.Lumen.Reflections.Allow";
        gi |= std::wstring_view(entry.name) == L"r.Lumen.DiffuseIndirect.Allow";
        fog |= std::wstring_view(entry.name) == L"r.VolumetricFog.GridPixelSize";
        // Do not duplicate controls or silently change the working stereo fix.
        if (std::wstring_view(entry.name) == L"r.Shadow.Virtual.Enable") return false;
    }
    return reflections && fog && gi;
}
static_assert(test_catalog());
int main() { return test_catalog() ? 0 : 1; }
