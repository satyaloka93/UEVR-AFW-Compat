// See DlssNeuralRendering.hpp for why UEVR hosts ReShade addons itself.
//
// Ported from the CyberpunkVR port's addon host. Everything here was established by measuring the
// closed binary rather than from documentation, because none exists: the addon's source is not in
// the public RenoDX repository, and the community distribution that packages it ships binaries and
// an installer profile table only.

#include <windows.h>
#include <d3d12.h>
#include <imgui.h>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <atomic>
#include <new>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <utility>

#include "Framework.hpp"
#include "DlssNeuralRendering.hpp"

std::shared_ptr<DlssNeuralRendering>& DlssNeuralRendering::get() {
    static auto inst = std::make_shared<DlssNeuralRendering>();
    return inst;
}

namespace {

// RECURSIVE, and it has to be. An addon binds from inside its own DllMain, on the thread that is
// still inside our LoadLibrary call, so ReShadeRegisterAddon re-enters this lock on a thread that
// already holds it. With a plain std::mutex MSVC throws system_error out of the re-lock, the
// exception unwinds through the addon's DllMain, and the loader reports ERROR_DLL_INIT_FAILED with
// no indication why.
std::recursive_mutex g_mtx;

// Set while WE are recording commands, so our own copies are never counted or logged as the
// addon's. Declared here because it is used well before the command probe that reads it.
thread_local bool t_our_own_commands = false;

struct Entry { std::string section, key, value; bool asked{false}, set{false}; };
std::vector<Entry> g_entries;
struct EventSub { uint32_t id{0}; std::vector<void*> fns; };
std::vector<EventSub> g_events;

void*    g_overlay_cb{nullptr};
bool     g_overlay_faulted{false};
int      g_addons_registered{0};
uint32_t g_api_version{0};
uint32_t g_imgui_version_asked{0};
char     g_addon_name[128]{};
char     g_addon_desc[256]{};
char     g_last_error[256]{};
uint64_t g_present_calls{0};
bool     g_present_faulted{false};

// ---- frame-time A/B ----------------------------------------------------------------------------
// Whether the box actually costs less has never been measured. Frame rate was compared by eye
// across runs with DIFFERENT render targets (3060x3120 vs 3060x2160) and different box sizes, which
// is not a measurement of anything. This accumulates present-to-present time into two buckets by
// the current state of the foveation toggle, so the same scene can be A/B'd inside one run.
//
// It is deliberately NOT a GPU timer. The tail-jump thunk gives up the return path, so there is no
// point after NR's dispatch at which to close a timestamp query on that command list. Present
// interval measures the thing that actually matters anyway.
LARGE_INTEGER g_qpc_freq{};
uint64_t g_last_present_qpc{0};
double   g_ms_sum[2]{};
uint64_t g_ms_n[2]{};
bool     g_last_fov_state{false};
int      g_ab_warmup{0};

// Buckets by the FOVEATE toggle, so frames where neural rendering itself was switched off would
// otherwise land in "box ON" and flatter it -- which is exactly what happened when NeuralUplift was
// toggled mid-run. Only count frames where an evaluation actually happened recently.
std::atomic<uint64_t> g_nr_eval_ticks{0};   // bumped by the NR prehook
uint64_t g_ab_last_evals{0};
int g_ab_idle_frames{0};

void accumulate_frame_time(bool foveating) {
    const uint64_t evals = g_nr_eval_ticks.load(std::memory_order_relaxed);
    if (evals != g_ab_last_evals) { g_ab_last_evals = evals; g_ab_idle_frames = 0; }
    else if (g_ab_idle_frames < 1000) ++g_ab_idle_frames;
    const bool nr_running = g_ab_idle_frames < 30;
    if (g_qpc_freq.QuadPart == 0) QueryPerformanceFrequency(&g_qpc_freq);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);

    if (foveating != g_last_fov_state) {      // let the pipeline settle after a toggle
        g_last_fov_state = foveating;
        g_ab_warmup = 30;
    }
    if (g_last_present_qpc != 0 && g_qpc_freq.QuadPart != 0) {
        const double ms = static_cast<double>(now.QuadPart - g_last_present_qpc) * 1000.0 /
                          static_cast<double>(g_qpc_freq.QuadPart);
        if (g_ab_warmup > 0) --g_ab_warmup;
        else if (ms > 0.5 && ms < 200.0 && nr_running) {   // ignore hitches, stalls, NR-off
            const int i = foveating ? 1 : 0;
            g_ms_sum[i] += ms;
            ++g_ms_n[i];
            if (((g_ms_n[0] + g_ms_n[1]) % 900) == 0 && g_ms_n[0] && g_ms_n[1]) {
                const double off = g_ms_sum[0] / (double)g_ms_n[0];
                const double on = g_ms_sum[1] / (double)g_ms_n[1];
                spdlog::info("[DLSSNR-FOV] frame time -- box OFF {:.2f} ms ({} frames), "
                             "box ON {:.2f} ms ({} frames), box saves {:.2f} ms ({:+.1f}%)",
                             off, g_ms_n[0], on, g_ms_n[1], off - on,
                             off > 0.0 ? (off - on) / off * 100.0 : 0.0);
            }
        }
    }
    g_last_present_qpc = static_cast<uint64_t>(now.QuadPart);
}

bool hosting() { return DlssNeuralRendering::get()->hosting_enabled(); }

// ---- forwarding --------------------------------------------------------------------------------
// Exporting these makes this DLL a candidate host for EVERY addon in the process. An addon takes
// the first module that exports ReShadeRegisterAddon and does NOT fall back if it refuses, so
// refusing is not a neutral act. With hosting off we forward to a real ReShade if one is present.
HMODULE g_forward{nullptr};
bool    g_forward_resolved{false};

HMODULE forward_target() {
    if (g_forward_resolved) return g_forward;
    g_forward_resolved = true;
    using PFN = BOOL(WINAPI*)(HANDLE, HMODULE*, DWORD, LPDWORD);
    auto k32 = GetModuleHandleW(L"kernel32.dll");
    auto enum_mods = k32 ? reinterpret_cast<PFN>(GetProcAddress(k32, "K32EnumProcessModules")) : nullptr;
    if (!enum_mods) return nullptr;
    HMODULE self{nullptr};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&forward_target), &self);
    HMODULE mods[1024]{}; DWORD needed{0};
    if (!enum_mods(GetCurrentProcess(), mods, sizeof(mods), &needed)) return nullptr;
    for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < 1024; ++i) {
        if (mods[i] == self) continue;
        if (GetProcAddress(mods[i], "ReShadeRegisterAddon")) { g_forward = mods[i]; break; }
    }
    return g_forward;
}
template <typename Fn> Fn forwarded(const char* name) {
    auto m = forward_target();
    return m ? reinterpret_cast<Fn>(GetProcAddress(m, name)) : nullptr;
}

// Printable ASCII only. A looser test let an uninitialised section pointer through and it was
// written into the store as a section header of raw bytes.
bool readable_string(const void* p, size_t max_len) {
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT) return false;
    const DWORD ok = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                     PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if ((mbi.Protect & ok) == 0 || (mbi.Protect & PAGE_GUARD)) return false;
    auto* s = static_cast<const char*>(p);
    auto* end = static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize;
    for (size_t i = 0; i < max_len && (s + i) < end; ++i) {
        if (s[i] == '\0') return i > 0;
        const auto c = static_cast<unsigned char>(s[i]);
        if (c != '\t' && (c < 0x20 || c > 0x7E)) return false;
    }
    return false;
}

bool contains_ci(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    const size_t n = strlen(needle);
    for (const char* p = haystack; *p; ++p)
        if (_strnicmp(p, needle, n) == 0) return true;
    return false;
}

Entry& touch(const char* sec, const char* key) {
    for (auto& e : g_entries)
        if (_stricmp(e.section.c_str(), sec) == 0 && _stricmp(e.key.c_str(), key) == 0) return e;
    g_entries.push_back(Entry{sec ? sec : "", key ? key : "", "", false, false});
    return g_entries.back();
}

// ---- persistence --------------------------------------------------------------------------------
// The addon reads all sixteen of its keys exactly ONCE, during registration, and never writes any
// of them back -- its settings page mutates live variables instead. So with an in-memory-only store
// every key came back "(its own default)" on every launch and neural rendering switched itself on
// again each time, no matter what had been chosen in the menu.
//
// Both halves therefore have to be built here: the values are captured from the addon's own ImGui
// widgets as the user changes them, and replayed through ReShadeGetConfigValue before the addon is
// loaded. Ordering is the whole trick -- load_addon_config() must run BEFORE LoadLibrary, because
// registration is where the addon reads.
constexpr const char* kAddonSection = "RenoDX.DLSS5";

// Widget label -> INI key. Established by reverse engineering; the addon documents nothing. A label
// that matches nothing here is logged once rather than silently dropped, because a build that
// renames a control would otherwise look exactly like working persistence.
struct KeyMap { const char* label; const char* key; };
constexpr KeyMap kAddonKeys[] = {
    {"Enable DLSS Neural Rendering", "NeuralUplift"},
    {"Enable Upscaling",             "NREnableUpscaling"},
    {"NR Preset",                    "NRPreset"},
    {"NR Style",                     "NRStyle"},
    {"NR Intensity",                 "NRIntensity"},
    {"Local Tone Strength",          "NRLocalTone"},
    {"Local Structure Strength",     "NRLocalStructure"},
    {"Skin Structure Strength",      "NRSkinStructure"},
    {"Automatic Mask",               "NRAutoMask"},
    {"NR UI Correction",             "NRUICorrection"},
    {"Depth Convention",             "NRDepthMode"},
    {"Motion Scale X Multiplier",    "NRMVecScaleX"},
    {"Motion Scale Y Multiplier",    "NRMVecScaleY"},
    {"Scene Paper-White Scale",      "NRPaperWhiteScale"},
    {"HDR Transfer Strength",        "NRTransferStrength"},
    {"Color Strength",               "NRColorStrength"},
};

bool     g_config_dirty{false};
bool     g_config_loaded{false};
int      g_config_restored{0};
uint64_t g_config_saved_at{0};
char     g_unmatched_label[96]{};

std::filesystem::path addon_config_path() {
    return Framework::get_persistent_dir("reshade-addon-config.ini");
}

// ImGui labels carry an optional "##id" suffix that is not part of the visible text.
std::string visible_label(const char* label) {
    std::string s = label ? label : "";
    const auto hash = s.find("##");
    if (hash != std::string::npos) s.erase(hash);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

const char* key_for_label(const char* label) {
    const std::string s = visible_label(label);
    if (s.empty()) return nullptr;
    for (const auto& m : kAddonKeys)
        if (_stricmp(m.label, s.c_str()) == 0) return m.key;
    // Tolerate small wording drift between addon builds ("NR Intensity" vs "NR Intensity:").
    for (const auto& m : kAddonKeys) {
        const std::string want = m.label;
        if (s.size() >= want.size() && _strnicmp(s.c_str(), want.c_str(), want.size()) == 0)
            return m.key;
    }
    return nullptr;
}

// Every distinct control the addon draws is announced once, with the key it resolved to. The
// previous version logged only the FIRST unmapped label and only on change, which is how a control
// that silently failed to persist looked identical to one that was never touched.
std::vector<std::string> g_seen_controls;

void note_control(const char* label) {
    const std::string vis = visible_label(label);
    if (vis.empty()) return;
    std::lock_guard lock(g_mtx);
    if (std::find(g_seen_controls.begin(), g_seen_controls.end(), vis) != g_seen_controls.end()) return;
    g_seen_controls.push_back(vis);
    if (const char* key = key_for_label(label)) {
        spdlog::info("[DLSSNR] control \"{}\" -> [{}] {}", vis, kAddonSection, key);
    } else {
        if (!g_unmatched_label[0]) strncpy_s(g_unmatched_label, vis.c_str(), _TRUNCATE);
        spdlog::warn("[DLSSNR] control \"{}\" maps to no known config key; it will NOT persist", vis);
    }
}

void remember(const char* label, const char* formatted) {
    const char* key = key_for_label(label);
    if (key == nullptr) return;
    std::lock_guard lock(g_mtx);
    Entry& e = touch(kAddonSection, key);
    if (e.value == formatted) return;
    e.value = formatted;
    e.set = true;
    g_config_dirty = true;
    spdlog::info("[DLSSNR] {} = {}", key, formatted);
}

void remember_bool(const char* label, bool v)  { remember(label, v ? "1" : "0"); }
void remember_int(const char* label, int v)    { char b[24]; _snprintf_s(b, sizeof(b), _TRUNCATE, "%d", v); remember(label, b); }
void remember_float(const char* label, float v){ char b[40]; _snprintf_s(b, sizeof(b), _TRUNCATE, "%.6f", v); remember(label, b); }

void save_addon_config() {
    std::lock_guard lock(g_mtx);
    if (!g_config_dirty) return;
    std::ofstream out{addon_config_path(), std::ios::trunc};
    if (!out) { spdlog::error("[DLSSNR] cannot write {}", addon_config_path().string()); return; }
    out << "; Settings for the hosted ReShade addon. Written by UEVR, read by the addon at\n"
           "; registration -- so edits here take effect on the NEXT launch.\n";
    std::string section;
    for (const auto& e : g_entries) {
        if (e.value.empty()) continue;
        if (e.section != section) { section = e.section; out << "\n[" << section << "]\n"; }
        out << e.key << '=' << e.value << '\n';
    }
    g_config_dirty = false;
    g_config_saved_at = GetTickCount64();
}

void load_addon_config() {
    std::lock_guard lock(g_mtx);
    if (g_config_loaded) return;
    g_config_loaded = true;

    std::ifstream in{addon_config_path()};
    std::string section, line;
    while (in && std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        line.erase(0, first);
        if (line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') { section = line.substr(1, line.size() - 2); continue; }
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        std::string key = line.substr(0, eq);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        touch(section.c_str(), key.c_str()).value = line.substr(eq + 1);
        ++g_config_restored;
    }
    spdlog::info("[DLSSNR] restored {} addon setting(s) from {}", g_config_restored,
                 addon_config_path().string());

    // Nothing stored for the master switch means a first run, and it starts OFF. The addon's own
    // built-in default is ON, which is how a fresh launch kept re-enabling neural rendering. Once
    // the user turns it on it is written here and stays on -- this seeds, it does not override.
    Entry& uplift = touch(kAddonSection, "NeuralUplift");
    if (uplift.value.empty()) {
        uplift.value = "0";
        g_config_dirty = true;
        spdlog::info("[DLSSNR] no stored NeuralUplift; seeding it off for this first run");
    }
}

// ---- the ImGui function table -------------------------------------------------------------------
// NOT optional, and null is NOT the safe answer: reshade.hpp's register_addon returns false when
// this comes back null, so the addon aborts before installing its NGX hooks. The exact slots below
// were identified by logging each stub's first argument -- an ImGui widget's label -- because
// counting members of ReShade's table produced a set no settings page would ever call.
constexpr int kSlots = 512;
void* g_imgui_table[8192];
bool  g_slot_seen[kSlots]{};
char  g_slot_list[192]{};

void note_slot(int slot) {
    if (slot < 0 || slot >= kSlots || g_slot_seen[slot]) return;
    g_slot_seen[slot] = true;
    const size_t used = strlen(g_slot_list);
    char tmp[16];
    _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s%d", used ? "," : "", slot);
    if (used + strlen(tmp) + 1 < sizeof(g_slot_list))
        strncat_s(g_slot_list, sizeof(g_slot_list), tmp, _TRUNCATE);
}

void imgui_noop() {}
template <int I> uint64_t imgui_stub(void* a1, void*, void*, void*) {
    if (!g_slot_seen[I < kSlots ? I : 0] && readable_string(a1, 96))
        spdlog::info("[DLSSNR] addon overlay slot {} label \"{}\"", I, static_cast<const char*>(a1));
    note_slot(I);
    return 0;                                  // "unchanged" -- a stub claiming a change makes the
}                                              // addon commit and rebuild its feature every frame

// Real forwarders for the measured controls. 115/129/144 had their argument types confirmed by
// dereferencing the second argument: a bool reading 0/1, an int index, a float in range.
// Each of these records the new value on change. This is the only place a setting can be captured:
// the addon never writes its config back, so the widget is where the user's choice exists.
bool impl_checkbox(const char* label, bool* v) {
    if (!readable_string(label, 128) || !v) return false;
    note_control(label);
    const bool changed = ImGui::Checkbox(label, v);
    if (changed) remember_bool(label, *v);
    return changed;
}
bool impl_slider(const char* label, float* v, float vmin, float vmax, const char* fmt, int flags) {
    if (!readable_string(label, 128) || !v) return false;
    note_control(label);
    if (!(vmin < vmax)) { vmin = 0.0f; vmax = 1.0f; }
    const bool changed = ImGui::SliderFloat(label, v, vmin, vmax,
                                            readable_string(fmt, 32) ? fmt : "%.3f", flags);
    if (changed) remember_float(label, *v);
    return changed;
}
bool impl_button(const char* label, const ImVec2& size) {
    return readable_string(label, 128) ? ImGui::Button(label, size) : false;
}
void impl_text_unformatted(const char* text, const char* end) {
    if (readable_string(text, 1024)) ImGui::TextUnformatted(text, end);
}
void impl_textv(const char* fmt, va_list args) {
    if (readable_string(fmt, 256)) ImGui::TextV(fmt, args);
}
void impl_separator() { ImGui::Separator(); }
// Combo has three table entries and which one slot 129 is cannot be told apart, so it is inferred
// from the argument: a char** whose first element reads as a string is the array form.
bool impl_combo(const char* label, int* current, const void* items, int count, int popup_max) {
    if (!readable_string(label, 128) || !current) return false;
    note_control(label);
    if (items) {
        auto* const* as_array = static_cast<const char* const*>(items);
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(items, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
            count > 0 && count < 256 && readable_string(as_array[0], 64)) {
            const bool changed = ImGui::Combo(label, current, as_array, count, popup_max);
            if (changed) remember_int(label, *current);
            return changed;
        }
        if (readable_string(items, 64)) {
            const bool changed = ImGui::Combo(label, current, static_cast<const char*>(items));
            if (changed) remember_int(label, *current);
            return changed;
        }
    }
    ImGui::BeginDisabled(); ImGui::Text("%s (choices unreadable)", label); ImGui::EndDisabled();
    return false;
}

template <int... I> void fill_stubs(std::integer_sequence<int, I...>) {
    ((g_imgui_table[I] = reinterpret_cast<void*>(&imgui_stub<I>)), ...);
}

void build_imgui_table() {
    for (auto& s : g_imgui_table) s = reinterpret_cast<void*>(&imgui_noop);
    fill_stubs(std::make_integer_sequence<int, kSlots>{});
    g_imgui_table[80]  = reinterpret_cast<void*>(&impl_separator);
    g_imgui_table[103] = reinterpret_cast<void*>(&impl_text_unformatted);
    g_imgui_table[104] = reinterpret_cast<void*>(&impl_textv);
    g_imgui_table[111] = reinterpret_cast<void*>(&impl_button);
    g_imgui_table[115] = reinterpret_cast<void*>(&impl_checkbox);
    g_imgui_table[129] = reinterpret_cast<void*>(&impl_combo);
    g_imgui_table[144] = reinterpret_cast<void*>(&impl_slider);
}

// ---- fake ReShade objects -----------------------------------------------------------------------
// The addon stores the device pointer at init_device and never calls a method on it, and it called
// nothing at all on the queue or swapchain either -- verified with 128-slot recording vtables. So
// these exist only to be identities; there is no ReShade object model to reproduce.
constexpr int kVt = 128;
void* g_dev_vt[kVt]; void* g_queue_vt[kVt]; void* g_swap_vt[kVt]; void* g_runtime_vt[kVt];
void* g_fake_dev[2]{g_dev_vt, nullptr};
void* g_fake_queue[2]{g_queue_vt, nullptr};
void* g_fake_swap[2]{g_swap_vt, nullptr};
void* g_fake_runtime[2]{g_runtime_vt, nullptr};
uint64_t g_native_dev{0}, g_native_queue{0}, g_native_swap{0};

template <int I> uint64_t __fastcall dev_slot(void*)   { return g_native_dev; }
template <int I> uint64_t __fastcall queue_slot(void*) { return g_native_queue; }
template <int I> uint64_t __fastcall swap_slot(void*)  { return g_native_swap; }
template <int I> uint64_t __fastcall rt_slot(void*)    { return 0; }
template <int... I> void fill_vts(std::integer_sequence<int, I...>) {
    ((g_dev_vt[I]     = reinterpret_cast<void*>(&dev_slot<I>)), ...);
    ((g_queue_vt[I]   = reinterpret_cast<void*>(&queue_slot<I>)), ...);
    ((g_swap_vt[I]    = reinterpret_cast<void*>(&swap_slot<I>)), ...);
    ((g_runtime_vt[I] = reinterpret_cast<void*>(&rt_slot<I>)), ...);
}

using PresentFn = void (*)(void*, void*, const int32_t*, const int32_t*, uint32_t, const void*);

// SEH cannot share a frame with C++ unwinding, hence the separate functions. The guard is the
// point: this runs on the present thread every frame, calling a third-party binary through an
// interface we are approximating. A fault without it is a hard crash on every subsequent frame.
bool invoke_present_guarded(PresentFn fn) {
    __try { fn(g_fake_queue, g_fake_swap, nullptr, nullptr, 0, nullptr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool invoke_overlay_guarded(void (*fn)(void*)) {
    __try { fn(g_fake_runtime); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool invoke_init_device_guarded(void (*fn)(void*)) {
    __try { fn(g_fake_dev); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool invoke_destroy_device_guarded(void (*fn)(void*)) {
    __try { fn(g_fake_dev); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ================================================================================================
// Foveal Neural Rendering -- step 1: a static centre box.
// ================================================================================================
//
// WHAT THIS IS FOR. NR here evaluates at 3060x3120 per eye, roughly three times the pixel cost that
// already halved Cyberpunk's frame rate, which is why it is correct but unplayable. Restricting the
// model to a box costs quadratically less: at fraction 0.5 the box is a quarter of the pixels.
//
// WHY A *STATIC* BOX FIRST, WITH NO EYE TRACKING. Foveated shading works because peripheral acuity
// is poor. Foveated *denoising* is a different bet, because the periphery is highly sensitive to
// temporal flicker -- exactly what a denoiser suppresses. A clean centre ringed by un-denoised
// shimmer, with a hard rectangular seam between them, may simply be worse than uniform NR. If that
// is objectionable while stationary, gaze tracking makes it worse rather than better, so the seam
// is worth judging before any of the PSVR2Toolkit plumbing is written.
//
// WHY THE CACHE PERMITS THIS AT ALL. The addon keys its single-slot resource cache on
// {output resource pointer + five scalars}; subrect BASE is not part of the key. That is why both
// eyes share one entry instead of thrashing. So a fixed-size box whose origin moves is free, while
// changing the box SIZE per frame would rebuild. The design constrains itself: constant size.
//
// THE OPEN QUESTION IS THE HOOK, NOT THE BOX. In the CyberpunkVR port, detouring these same
// exports was abandoned permanently (g_nrDiagState = -4): the signed 310.8 runtime validates its
// caller from the return address, and even a read-only trampoline made feature 18 return
// 0xBAD00002. A MinHook/safetyhook detour calls the original from OUR module, so the snippet sees
// UEVRBackend.dll as its caller instead of the addon.
//
// That result was measured in a different process against a different caller, so it is a warning
// rather than a verdict -- hence ProbeNGX, which installs the detour and forwards arguments
// untouched. If evaluations keep succeeding, the check does not bite here and the box is a few
// lines. If they turn 0xBAD00002, the fix is a tail-JMP thunk that leaves the addon's own return
// address on the stack, and that is worth writing only once we know it is needed.

// Official NVSDK_NGX_Parameter ABI (nvsdk_ngx_params.h): eight Set overloads -- ULL, float, double,
// unsigned, int, ID3D11Resource*, ID3D12Resource*, void* -- then the same eight as Get. So SetUI is
// slot 3 and returns void; GetUI is slot 11 and returns an NVSDK_NGX_Result.
bool nr_get_uint(const void* params, const char* name, uint32_t* out) {
    if (!params || !name || !out) return false;
    uint32_t result = 0xFFFFFFFFu;
    __try {
        void** vt = *reinterpret_cast<void***>(const_cast<void*>(params));
        using Fn = uint32_t(__fastcall*)(const void*, const char*, uint32_t*);
        result = reinterpret_cast<Fn>(vt[11])(params, name, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return (result & 0x80000000u) == 0;
}

// Slot 14 is GetD3D12Resource and 15 is GetVoidPointer in the official ABI. Both are tried because
// NGX stores parameters BY TYPE: a resource the addon set as a raw pointer does not come back from
// the typed getter, and the failure is indistinguishable from an absent parameter.
uint32_t nr_get_ptr_slot(const void* params, const char* name, void** out, int slot) {
    if (!params || !name || !out) return 0xFFFFFFFFu;
    uint32_t result = 0xFFFFFFFFu;
    __try {
        void** vt = *reinterpret_cast<void***>(const_cast<void*>(params));
        using Fn = uint32_t(__fastcall*)(const void*, const char*, void**);
        result = reinterpret_cast<Fn>(vt[slot])(params, name, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFFFFFFu; }
    return result;
}

ID3D12Resource* nr_get_resource(const void* params, const char* name, const char** how,
                                uint32_t* last_result) {
    void* p = nullptr;
    uint32_t r = nr_get_ptr_slot(params, name, &p, 14);
    if ((r & 0x80000000u) == 0 && p != nullptr) { *how = "GetD3D12Resource"; *last_result = r;
                                                  return static_cast<ID3D12Resource*>(p); }
    const uint32_t typed = r;
    p = nullptr;
    r = nr_get_ptr_slot(params, name, &p, 15);
    if ((r & 0x80000000u) == 0 && p != nullptr) { *how = "GetVoidPointer"; *last_result = r;
                                                  return static_cast<ID3D12Resource*>(p); }
    *how = "neither";
    *last_result = typed;
    return nullptr;
}

bool nr_set_uint(void* params, const char* name, uint32_t value) {
    if (!params || !name) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(params);
        using Fn = void(__fastcall*)(void*, const char*, uint32_t);
        reinterpret_cast<Fn>(vt[3])(params, name, value);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// Every plane nvngx_dlssnr.dll defines a subrect for. The first four are the ones the addon is
// known to write and the only ones we rewrite; the rest are dumped because the snippet cross-checks
// Backbuffer against Output ("Invalid Backbuffer/active Output rect configuration") and we need to
// know whether they carry values here before shrinking anything.
constexpr const char* kRewritePlanes[] = { "Color", "Depth", "MVec", "Output" };
constexpr const char* kProbePlanes[]   = { "Color", "Depth", "MVec", "Output",
                                           "Backbuffer", "UI", "UIAlpha", "ControlMask" };

struct Rect { uint32_t x{0}, y{0}, w{0}, h{0}; bool valid{false}; };

void subrect_name(char (&buf)[64], const char* plane, const char* field) {
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "DLSSNR.%sSubrect%s", plane, field);
}

Rect read_subrect(const void* params, const char* plane) {
    Rect r{};
    char n[64];
    subrect_name(n, plane, "BaseX");  const bool a = nr_get_uint(params, n, &r.x);
    subrect_name(n, plane, "BaseY");  const bool b = nr_get_uint(params, n, &r.y);
    subrect_name(n, plane, "Width");  const bool c = nr_get_uint(params, n, &r.w);
    subrect_name(n, plane, "Height"); const bool d = nr_get_uint(params, n, &r.h);
    r.valid = a && b && c && d && r.w > 0 && r.h > 0 && r.w <= 16384 && r.h <= 16384;
    return r;
}

std::atomic<uint64_t> g_nr_evals{0};
std::atomic<uint64_t> g_nr_ok{0};
std::atomic<uint64_t> g_nr_bad{0};
std::atomic<uint32_t> g_nr_box_w{0}, g_nr_box_h{0};
std::atomic<uint32_t> g_nr_box_x{0}, g_nr_box_y{0};
std::atomic<uint64_t> g_box_applies{0};
std::atomic<uint32_t> g_last_plane_w{0};
std::atomic<bool>     g_nr_hooked{false};
std::atomic<bool>     g_nr_tamper_suspected{false};

safetyhook::InlineHook g_nr_eval_hook{};
HMODULE g_snippet_base{nullptr};   // to tell a reload from a survivor across a reset

// Rounded DOWN to a multiple of eight on both axes. NGX is content with unaligned subrects in
// principle, but the guide planes here run at a third of colour resolution, and letting an odd
// width land on a plane that gets divided by three is how a rect ends up one pixel outside the
// resource it was validated against.
uint32_t align8_down(uint32_t v) { return v & ~7u; }

// The SAME fractional region on every plane, computed from that plane's own dimensions, so colour
// and output stay mutually consistent without needing to know their ratio -- which matters because
// the snippet rejects the evaluation outright if Color and Output disagree.
// WHY THE TWO BOXES MUST NOT SIT AT THE SAME IMAGE COORDINATES.
//
// A box centred in each eye's own image covers a DIFFERENT piece of the world in each eye. The eyes
// are horizontally displaced, so a point at any finite distance projects to different x in the two
// images. Converge on something and it can fall inside the box in one eye and outside it in the
// other: neural rendering on one retina and not the other, which the visual system reports as one
// eye looking wrong rather than as a misplaced rectangle.
//
// Shifting the boxes toward each other by the disparity at the distance being looked at makes both
// cover the same world region. With parallel eye projections a point appears further right in the
// left eye, so the left box moves right (+) and the right box left (-). The correct magnitude is
// f * IPD / (2 * distance); rather than derive it from projection matrices that UEVR may have
// already modified, it is a slider -- dial it until the boxes fuse into one.
//
// Which evaluation is which eye is not labelled anywhere, so it is taken from the order within the
// frame, with a toggle to swap if the guess is backwards.
std::atomic<int> g_eval_in_frame{0};
std::atomic<uint64_t> g_eval_parity{0};
std::atomic<bool> g_last_first_eye{true};


void apply_foveal_box(void* params, float fraction, float convergence_pct, bool swap_eyes) {
    if (fraction >= 0.999f) return;
    const uint64_t parity = g_eval_parity.fetch_add(1, std::memory_order_relaxed);
    const bool even = (parity & 1ull) == 0ull;
    const bool first_eye = swap_eyes ? !even : even;
    g_last_first_eye.store(first_eye, std::memory_order_relaxed);
    g_eval_in_frame.fetch_add(1, std::memory_order_relaxed);   // kept for the diagnostic only
    const float signed_pct = first_eye ? convergence_pct : -convergence_pct;


    for (const char* plane : kRewritePlanes) {
        const Rect r = read_subrect(params, plane);
        if (!r.valid) continue;

        uint32_t w = align8_down(static_cast<uint32_t>(r.w * fraction));
        uint32_t h = align8_down(static_cast<uint32_t>(r.h * fraction));
        if (w < 64 || h < 64) continue;                    // too small to be a sane model input
        if (w > r.w || h > r.h) continue;

        // A fraction of THIS plane's width, so colour and the third-resolution guides converge by
        // the same visual amount without needing to know their ratio.
        const int scaled = static_cast<int>(signed_pct * static_cast<float>(r.w));
        int cx = static_cast<int>(r.x + align8_down((r.w - w) / 2)) + scaled;
        cx = std::max<int>(static_cast<int>(r.x), std::min<int>(cx, static_cast<int>(r.x + r.w - w)));

        const uint32_t x = align8_down(static_cast<uint32_t>(cx));
        const uint32_t y = r.y + align8_down((r.h - h) / 2);
        if (x + w > r.x + r.w || y + h > r.y + r.h) continue;

        char n[64];
        subrect_name(n, plane, "BaseX");  nr_set_uint(params, n, x);
        subrect_name(n, plane, "BaseY");  nr_set_uint(params, n, y);
        subrect_name(n, plane, "Width");  nr_set_uint(params, n, w);
        subrect_name(n, plane, "Height"); nr_set_uint(params, n, h);

        if (strcmp(plane, "Output") == 0) {
            g_nr_box_w.store(w, std::memory_order_relaxed);
            g_nr_box_h.store(h, std::memory_order_relaxed);
            g_nr_box_x.store(x, std::memory_order_relaxed);
            g_nr_box_y.store(y, std::memory_order_relaxed);
            g_last_plane_w.store(r.w, std::memory_order_relaxed);

            // Logging the box only on evaluations 1 and 2 meant a run where the writes later
            // stopped landing looked identical to one where they never stopped. Read it back and
            // say so periodically, so "the box vanished" is answerable from the log.
            const uint64_t k = g_box_applies.fetch_add(1, std::memory_order_relaxed) + 1;
            if (k == 1 || (k % 600) == 0) {
                const Rect back = read_subrect(params, "Output");
                const bool stuck = back.valid && back.x == x && back.y == y &&
                                   back.w == w && back.h == h;
                spdlog::info("[DLSSNR-FOV] apply {}: wrote ({},{}) {}x{} into plane {}x{} -- "
                             "read back ({},{}) {}x{} {}", k, x, y, w, h, r.w, r.h,
                             back.x, back.y, back.w, back.h, stuck ? "OK" : "MISMATCH");
            }
        }
    }
}


// WHY THIS EXISTS. DLSSNR.Output is a persistent resource the addon owns and blits back over the
// frame in full. At full size NR writes every pixel of it, so nothing stale can ever show. Once the
// model is confined to a box, everything outside keeps whatever NR left there on an earlier frame
// and the blit puts that back on screen -- a frozen border, which is exactly what a foveal box
// looked like in the headset.
//
// So the whole output is seeded with the current frame first. NR then overwrites the box with
// denoised pixels and the rest is live, un-denoised image. Colour is the codec's proxy rather than
// the display image, but the addon decodes the full output afterwards, so the untouched region
// round-trips through encode/decode unchanged.
//
// STATES ARE AN ASSUMPTION, and the one thing here that can bite: D3D12 has no way to query a
// resource's current state, so the transitions below assume UNORDERED_ACCESS, which is what NGX
// asks for and what the addon's own inline resources are created as. Both are returned to that
// state, so nothing recorded afterwards sees a difference.
std::atomic<bool> g_copy_faulted{false};
std::atomic<uint64_t> g_copies{0};

// Defined further down with the Set recorder, which has to sit after this because it forwards
// through helpers declared here.
ID3D12Resource* recorded_resource(const char* needle);
int recorded_name_count();

// 0 = copied, 1 = descriptions differ, 2 = faulted. POD only: SEH cannot share a frame with C++
// unwinding, and every D3D12 type used here is a plain struct.
int record_periphery_copy(ID3D12GraphicsCommandList* list, ID3D12Resource* color,
                          ID3D12Resource* output, D3D12_RESOURCE_DESC* cd_out,
                          D3D12_RESOURCE_DESC* od_out,
                          uint32_t box_x, uint32_t box_y, uint32_t box_w, uint32_t box_h) {
    __try {
        const D3D12_RESOURCE_DESC cd = color->GetDesc();
        const D3D12_RESOURCE_DESC od = output->GetDesc();
        *cd_out = cd;
        *od_out = od;
        if (cd.Width != od.Width || cd.Height != od.Height || cd.Format != od.Format ||
            cd.Dimension != od.Dimension || cd.MipLevels != od.MipLevels)
            return 1;

        D3D12_RESOURCE_BARRIER to_copy[2]{};
        to_copy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_copy[0].Transition.pResource = color;
        to_copy[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        to_copy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        to_copy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        to_copy[1] = to_copy[0];
        to_copy[1].Transition.pResource = output;
        to_copy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

        D3D12_RESOURCE_BARRIER back[2]{};
        back[0] = to_copy[0];
        back[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        back[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        back[1] = to_copy[1];
        back[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        back[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        list->ResourceBarrier(2, to_copy);

        // BANDS ONLY, never the box. Copying the whole resource meant that if the addon evaluates
        // both eyes and composites afterwards, the second eye's copy wiped the first eye's neural
        // rendering -- one eye keeps its lighting and the other loses it. Restricting the copy to
        // the region outside the box makes it impossible to overwrite NR's own output, and moves a
        // good deal less memory besides.
        const uint32_t W = static_cast<uint32_t>(cd.Width);
        const uint32_t H = cd.Height;
        const uint32_t bx = box_x, by = box_y, bw = box_w, bh = box_h;

        D3D12_TEXTURE_COPY_LOCATION d{}, sl{};
        d.pResource = output;  d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;  d.SubresourceIndex = 0;
        sl.pResource = color;  sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; sl.SubresourceIndex = 0;

        struct Band { uint32_t l, t, r, b; };
        const Band bands[4] = {
            { 0,        0,        W,        by },                 // above
            { 0,        by + bh,  W,        H  },                 // below
            { 0,        by,       bx,       by + bh },            // left
            { bx + bw,  by,       W,        by + bh },            // right
        };
        for (const Band& n : bands) {
            if (n.r <= n.l || n.b <= n.t) continue;               // empty when the box touches an edge
            D3D12_BOX src{ n.l, n.t, 0, n.r, n.b, 1 };
            list->CopyTextureRegion(&d, n.l, n.t, 0, &sl, &src);
        }

        list->ResourceBarrier(2, back);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 2; }
}

// EVERY exit announces itself. The previous version had four silent returns, so a run that never
// copied anything produced no line at all and looked identical to one where the copy simply did not
// help -- which cost a test.
void refresh_periphery(ID3D12GraphicsCommandList* list, const void* params) {
    struct Own { Own(){t_our_own_commands=true;} ~Own(){t_our_own_commands=false;} } own;
    if (g_copy_faulted.load(std::memory_order_relaxed)) return;
    // Not a one-shot: the recorder only populates from the frame AFTER the vtable swap, so the first
    // attempt legitimately finds nothing. Reporting once would freeze that transient as the verdict.
    static std::atomic<uint64_t> s_attempts{0};
    const uint64_t attempt = s_attempts.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool first = (attempt == 1 || attempt == 3 || attempt == 60 || attempt == 300);

    if (list == nullptr) {
        if (first) spdlog::error("[DLSSNR-FOV] no command list; cannot refresh the periphery");
        return;
    }

    // From the recorder, not from Get: the names the getters need are not the ones in the binaries,
    // so the only reliable source is what the addon itself passed to Set.
    ID3D12Resource* color  = recorded_resource("color");
    ID3D12Resource* output = recorded_resource("out");
    if (first)
        spdlog::info("[DLSSNR-FOV] periphery from recorded sets: colour={} output={} ({} names seen)",
                     (void*)color, (void*)output, recorded_name_count());

    if (color == nullptr || output == nullptr) {
        if (first) spdlog::error("[DLSSNR-FOV] no recorded colour/output resource yet; periphery "
                                 "stays frozen this frame ({} names recorded so far)",
                                 recorded_name_count());
        return;
    }
    if (color == output) {
        if (first) spdlog::warn("[DLSSNR-FOV] colour and output are the same resource, so the "
                                "periphery should already be live -- something else is writing it");
        return;
    }

    D3D12_RESOURCE_DESC cd{}, od{};
    const uint32_t bx = g_nr_box_x.load(std::memory_order_relaxed);
    const uint32_t by = g_nr_box_y.load(std::memory_order_relaxed);
    const uint32_t bw = g_nr_box_w.load(std::memory_order_relaxed);
    const uint32_t bh = g_nr_box_h.load(std::memory_order_relaxed);
    if (bw == 0 || bh == 0) return;                     // no box yet; nothing to work around
    const int r = record_periphery_copy(list, color, output, &cd, &od, bx, by, bw, bh);
    if (r == 1) {
        g_copy_faulted.store(true, std::memory_order_relaxed);
        spdlog::error("[DLSSNR-FOV] colour {}x{} fmt {} and output {}x{} fmt {} differ; periphery "
                      "refresh disabled", cd.Width, cd.Height, (int)cd.Format,
                      od.Width, od.Height, (int)od.Format);
        return;
    }
    if (r == 2) {
        g_copy_faulted.store(true, std::memory_order_relaxed);
        spdlog::error("[DLSSNR-FOV] faulted while recording the periphery copy; disabled");
        return;
    }

    const auto n = g_copies.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1)
        spdlog::info("[DLSSNR-FOV] seeding the full output from colour before each evaluation "
                     "({}x{} fmt {})", cd.Width, cd.Height, (int)cd.Format);
}

// ---- recording what the addon actually puts in the parameter block ----------------------------
//
// Guessing names failed outright: 17 candidates across all three pointer getters returned
// FAIL_UnsupportedParameter while a control read on the same object, in the same call, succeeded.
// So "DLSSNR.Color" and "DLSSNR.Output" are log FORMAT strings in those binaries, not parameter
// keys, and no amount of further guessing is going to land on the real ones.
//
// This stops guessing. NVSDK_NGX_Parameter is a pure virtual class: slots 0-7 are the eight Set
// overloads, 8-15 the eight Gets. The addon calls Set on this object every frame immediately before
// it evaluates. Replacing ONLY the Set slots and forwarding each to the original records every name
// and value it passes -- no guessing, and no risk to the snippet, whose own reads all go through
// the untouched Get slots.
//
// Float and double take their value in XMM2 rather than R8, so each overload needs its own properly
// typed forwarder; one generic forwarder would corrupt exactly those two.
constexpr int kParamVtSlots = 24;
void* g_orig_param_vt[kParamVtSlots]{};
void* g_our_param_vt[kParamVtSlots]{};
void* g_hooked_params{nullptr};

struct ParamSet { char name[80]; void* ptr; int slot; };
ParamSet g_param_sets[64];
int      g_param_set_count{0};

void record_named(const char* name, void* ptr, int slot) {
    if (!readable_string(name, 79)) return;
    for (int i = 0; i < g_param_set_count; ++i)
        if (strcmp(g_param_sets[i].name, name) == 0) { g_param_sets[i].ptr = ptr; return; }
    if (g_param_set_count >= 64) return;
    ParamSet& e = g_param_sets[g_param_set_count++];
    strncpy_s(e.name, name, _TRUNCATE);
    e.ptr = ptr;
    e.slot = slot;
    spdlog::info("[DLSSNR-FOV] addon set resource \"{}\" = {}", name, ptr);
}

// ONLY SLOT 1 IS REPLACED NOW, and that is a correction, not caution for its own sake.
//
// The first version wrapped all eight Set slots using the DOCUMENTED NVSDK_NGX_Parameter order, in
// which slot 1 is Set(name, float). This implementation does not follow it: the RESOURCE setter is
// at slot 1 and the float setter at slot 6 -- the reverse -- even though unsigned int sits at Set 3
// and Get 11 exactly as documented, which is what made the documented order look confirmed.
//
// The consequence was not a wrong log line. Declaring slot 1 as taking a float made the forwarder
// read XMM2 and then call the original WITHOUT R8 set, destroying every resource pointer the addon
// passed. The game hung at the title screen with NR chewing on garbage. The tell is slot 6, where
// every recorded "pointer" was the identical 0x7ff8368d5b50 -- R8 garbage read where the real float
// sat in XMM2.
//
// Slots 0, 2 and 4-7 are left untouched. Their types are still unproven, the full name list is
// already captured from that run, and the resource pointers are the only thing actually needed.
using SetPFn = void(__fastcall*)(void*, const char*, void*);

void __fastcall rec_set_resource(void* s, const char* n, void* v) {
    record_named(n, v, 1);
    reinterpret_cast<SetPFn>(g_orig_param_vt[1])(s, n, v);
}

int copy_vtable(void* params) {                     // POD only; SEH cannot share a C++ frame
    __try {
        void** vt = *reinterpret_cast<void***>(params);
        if (vt == nullptr) return 1;
        if (vt == g_our_param_vt) return 2;          // already ours
        memcpy(g_orig_param_vt, vt, sizeof(g_orig_param_vt));
        memcpy(g_our_param_vt, vt, sizeof(g_our_param_vt));
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
}

void install_param_set_recorder(void* params) {
    if (params == nullptr || params == g_hooked_params) return;
    const int r = copy_vtable(params);
    if (r == 2) { g_hooked_params = params; return; }
    if (r != 0) {
        spdlog::error("[DLSSNR-FOV] could not read the parameter vtable (code {})", r);
        g_hooked_params = params;                    // do not retry every frame
        return;
    }
    g_our_param_vt[1] = reinterpret_cast<void*>(&rec_set_resource);   // and nothing else
    *reinterpret_cast<void***>(params) = g_our_param_vt;
    g_hooked_params = params;
    spdlog::info("[DLSSNR-FOV] recording resource Set calls (slot 1) on parameter object {}; "
                 "pointers appear from the next frame onwards", params);
}

// Whatever the addon calls them, the output is the one whose name mentions "out". Substring
// matching rather than an exact key means this works without another round trip to find the name.
int recorded_name_count() { return g_param_set_count; }

ID3D12Resource* recorded_resource(const char* needle) {
    for (int i = 0; i < g_param_set_count; ++i) {
        const ParamSet& e = g_param_sets[i];
        if (e.slot != 1) continue;                   // the resource setter
        if (e.ptr == nullptr) continue;
        if (contains_ci(e.name, needle)) return static_cast<ID3D12Resource*>(e.ptr);
    }
    return nullptr;
}

void probe_resource_names(const void* params) {
    static const char* const kNames[] = {
        "DLSSNR.Color", "DLSSNR.Output", "DLSSNR.MVec", "DLSSNR.Depth",
        "DLSSNR.Backbuffer", "DLSSNR.UI", "DLSSNR.UIAlpha", "DLSSNR.ControlMask",
        "DLSSNR.BidirectionalDistortionField",
        "DLSSNR.Input.Color", "DLSSNR.Output.Color", "DLSSNR.InputColor", "DLSSNR.OutputColor",
        "Color", "Output", "MVec", "Depth",
    };
    int found = 0;
    for (const char* n : kNames) {
        for (int slot : {13, 14, 15}) {
            void* p = nullptr;
            const uint32_t r = nr_get_ptr_slot(params, n, &p, slot);
            if ((r & 0x80000000u) == 0 && p != nullptr) {
                spdlog::info("[DLSSNR-FOV] probe: \"{}\" slot {} -> {}", n, slot, p);
                ++found;
            }
        }
    }
    uint32_t v = 0;
    const bool control = nr_get_uint(params, "DLSSNR.ColorSubrectWidth", &v);
    spdlog::info("[DLSSNR-FOV] probe finished: {} resource name(s) found; control "
                 "ColorSubrectWidth ok={} value={}", found, control, v);
}

// ---- is Close() late enough to blend at? -------------------------------------------------------
//
// A proper feather needs to run AFTER neural rendering has written the box and BEFORE the addon
// consumes Output. The only point guaranteed to be after all NR work on a command list is Close().
// Whether it is also before the addon's own decode is unknown, and if it is not, every line of
// shader, PSO, root-signature and descriptor code written for the blend would be wasted.
//
// So this settles it with a copy instead of a shader: at Close, copy Colour back over the BOX,
// undoing neural rendering exactly where it was applied. If NR visibly vanishes, Close is early
// enough and the blend is worth building. If NR still shows, the addon has already consumed Output
// by then and the blend needs a different insertion point.
//
// The vtable is copied and swapped PER INSTANCE, the same technique already proven on the NGX
// parameter object, so no other command list in the engine is affected.
constexpr int kListVtSlots = 64;
constexpr int kCloseSlot = 9;          // IUnknown 0-2, ID3D12Object 3-6, DeviceChild 7, CommandList 8
void* g_list_orig_vt[kListVtSlots]{};
void* g_list_our_vt[kListVtSlots]{};
std::atomic<uint64_t> g_close_overwrites{0};

struct PendingBox { ID3D12Resource* color; ID3D12Resource* output; uint32_t x, y, w, h; };
PendingBox g_pending{};
bool g_pending_valid{false};

using CloseFn = HRESULT(__stdcall*)(ID3D12GraphicsCommandList*);

int record_box_overwrite(ID3D12GraphicsCommandList* list, const PendingBox& b) {
    t_our_own_commands = true;
    __try {
        D3D12_RESOURCE_BARRIER to[2]{};
        to[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to[0].Transition.pResource = b.color;
        to[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        to[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        to[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        to[1] = to[0];
        to[1].Transition.pResource = b.output;
        to[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        D3D12_RESOURCE_BARRIER back[2]{};
        back[0] = to[0];
        back[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        back[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        back[1] = to[1];
        back[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        back[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        D3D12_TEXTURE_COPY_LOCATION d{}, sl{};
        d.pResource = b.output;  d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;  d.SubresourceIndex = 0;
        sl.pResource = b.color;  sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; sl.SubresourceIndex = 0;
        D3D12_BOX src{ b.x, b.y, 0, b.x + b.w, b.y + b.h, 1 };

        list->ResourceBarrier(2, to);
        list->CopyTextureRegion(&d, b.x, b.y, 0, &sl, &src);
        list->ResourceBarrier(2, back);
        t_our_own_commands = false;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { t_our_own_commands = false; return 1; }
}

std::atomic<uint64_t> g_close_calls{0};

// ---- what consumes Output, and when ------------------------------------------------------------
//
// Close is definitively too late: 7,800 overwrites of the box at Close, zero faults, and neural
// rendering survived every one. So the addon reads Output somewhere between EvaluateFeature
// returning and the list closing, and a blend has to go immediately before that read.
//
// This logs the first handful of command-list operations recorded after each NR evaluation, with
// resource pointers where the call carries them. A copy whose SOURCE is our known Output names the
// consumption point outright. If instead only dispatches appear, the decode is a shader and the
// insertion point has to be chosen differently -- which is still an answer.
//
// Slot numbers follow ID3D12GraphicsCommandList after IUnknown(0-2), ID3D12Object(3-6),
// ID3D12DeviceChild(7) and ID3D12CommandList(8).
constexpr int kDrawSlot        = 12;
constexpr int kDrawIndexedSlot = 13;
constexpr int kDispatchSlot    = 14;
constexpr int kCopyTexSlot     = 16;
constexpr int kCopyResSlot     = 17;

std::atomic<int> g_ops_after_eval{999};      // 999 = not armed
std::atomic<uint64_t> g_op_bursts{0};

bool op_should_log() {
    if (t_our_own_commands) return false;
    if (!DlssNeuralRendering::get()->cmd_probe_enabled()) return false;
    const int n = g_ops_after_eval.fetch_add(1, std::memory_order_relaxed);
    return n < 14;
}

void log_op(const char* op, ID3D12Resource* a, ID3D12Resource* b) {
    ID3D12Resource* out = recorded_resource("out");
    ID3D12Resource* col = recorded_resource("color");
    const char* tag = "";
    if (a == out || b == out) tag = "  <-- TOUCHES OUTPUT";
    else if (a == col || b == col) tag = "  <-- touches colour";
    spdlog::info("[DLSSNR-FOV][op] {} dst={} src={}{}", op, (void*)a, (void*)b, tag);
}

using DrawFn        = void(__stdcall*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
using DrawIdxFn     = void(__stdcall*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
using DispatchFn    = void(__stdcall*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
using CopyTexFn     = void(__stdcall*)(ID3D12GraphicsCommandList*, const D3D12_TEXTURE_COPY_LOCATION*,
                                       UINT, UINT, UINT, const D3D12_TEXTURE_COPY_LOCATION*,
                                       const D3D12_BOX*);
using CopyResFn     = void(__stdcall*)(ID3D12GraphicsCommandList*, ID3D12Resource*, ID3D12Resource*);

void __stdcall hooked_draw(ID3D12GraphicsCommandList* l, UINT a, UINT b, UINT c, UINT d) {
    if (op_should_log()) spdlog::info("[DLSSNR-FOV][op] DrawInstanced");
    reinterpret_cast<DrawFn>(g_list_orig_vt[kDrawSlot])(l, a, b, c, d);
}
void __stdcall hooked_draw_indexed(ID3D12GraphicsCommandList* l, UINT a, UINT b, UINT c, INT d, UINT e) {
    if (op_should_log()) spdlog::info("[DLSSNR-FOV][op] DrawIndexedInstanced");
    reinterpret_cast<DrawIdxFn>(g_list_orig_vt[kDrawIndexedSlot])(l, a, b, c, d, e);
}
void __stdcall hooked_dispatch(ID3D12GraphicsCommandList* l, UINT x, UINT y, UINT z) {
    if (op_should_log()) spdlog::info("[DLSSNR-FOV][op] Dispatch {}x{}x{}", x, y, z);
    reinterpret_cast<DispatchFn>(g_list_orig_vt[kDispatchSlot])(l, x, y, z);
}
void __stdcall hooked_copy_tex(ID3D12GraphicsCommandList* l, const D3D12_TEXTURE_COPY_LOCATION* d,
                               UINT x, UINT y, UINT z, const D3D12_TEXTURE_COPY_LOCATION* s,
                               const D3D12_BOX* box) {
    if (op_should_log())
        log_op("CopyTextureRegion", d ? d->pResource : nullptr, s ? s->pResource : nullptr);
    reinterpret_cast<CopyTexFn>(g_list_orig_vt[kCopyTexSlot])(l, d, x, y, z, s, box);
}
void __stdcall hooked_copy_res(ID3D12GraphicsCommandList* l, ID3D12Resource* d, ID3D12Resource* s) {
    if (op_should_log()) log_op("CopyResource", d, s);
    reinterpret_cast<CopyResFn>(g_list_orig_vt[kCopyResSlot])(l, d, s);
}

// EVERY path reports. The first version logged only on success, so a probe that never ran looked
// exactly like a probe that ran and proved Close is too late -- and that ambiguity cost a run.
HRESULT __stdcall hooked_close(ID3D12GraphicsCommandList* list) {
    const uint64_t c = g_close_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool enabled = DlssNeuralRendering::get()->close_probe_enabled();
    if (c == 1 || (c % 600) == 0)
        spdlog::info("[DLSSNR-FOV] Close #{} on {} (probe {}, pending {})", c, (void*)list,
                     enabled ? "on" : "off", g_pending_valid ? "yes" : "no");
    if (enabled && g_pending_valid) {
        const int r = record_box_overwrite(list, g_pending);
        if (r == 0) {
            const auto n = g_close_overwrites.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n == 1 || (n % 600) == 0)
                spdlog::info("[DLSSNR-FOV] Close probe #{}: overwrote box ({},{}) {}x{} from colour "
                             "{} into output {}", n, g_pending.x, g_pending.y, g_pending.w,
                             g_pending.h, (void*)g_pending.color, (void*)g_pending.output);
        } else {
            static std::atomic<uint64_t> s_fail{0};
            if (s_fail.fetch_add(1, std::memory_order_relaxed) == 0)
                spdlog::error("[DLSSNR-FOV] Close probe FAULTED recording the overwrite; the probe "
                              "result is meaningless until this is fixed");
        }
        g_pending_valid = false;
    }
    return reinterpret_cast<CloseFn>(g_list_orig_vt[kCloseSlot])(list);
}

int copy_list_vtable(void* list) {
    __try {
        void** vt = *reinterpret_cast<void***>(list);
        if (vt == nullptr) return 1;
        if (vt == g_list_our_vt) return 2;
        memcpy(g_list_orig_vt, vt, sizeof(g_list_orig_vt));
        memcpy(g_list_our_vt, vt, sizeof(g_list_our_vt));
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
}

bool g_list_vt_built{false};
void* g_list_source_vt{nullptr};
std::atomic<uint64_t> g_lists_hooked{0};

void hook_list_close(ID3D12GraphicsCommandList* list) {
    if (list == nullptr) return;
    void** vt = nullptr;
    __try { vt = *reinterpret_cast<void***>(list); } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (vt == nullptr || vt == g_list_our_vt) return;          // already ours

    if (!g_list_vt_built) {
        if (copy_list_vtable(list) != 0) {
            spdlog::error("[DLSSNR-FOV] cannot read the command list vtable");
            return;
        }
        g_list_our_vt[kCloseSlot]        = reinterpret_cast<void*>(&hooked_close);
        g_list_our_vt[kDrawSlot]         = reinterpret_cast<void*>(&hooked_draw);
        g_list_our_vt[kDrawIndexedSlot]  = reinterpret_cast<void*>(&hooked_draw_indexed);
        g_list_our_vt[kDispatchSlot]     = reinterpret_cast<void*>(&hooked_dispatch);
        g_list_our_vt[kCopyTexSlot]      = reinterpret_cast<void*>(&hooked_copy_tex);
        g_list_our_vt[kCopyResSlot]      = reinterpret_cast<void*>(&hooked_copy_res);
        g_list_source_vt = vt;
        g_list_vt_built = true;
        spdlog::info("[DLSSNR-FOV] built the Close-hook vtable from list {}", (void*)list);
    } else if (vt != g_list_source_vt) {
        static std::atomic<bool> s_said{false};
        bool e = false;
        if (s_said.compare_exchange_strong(e, true))
            spdlog::warn("[DLSSNR-FOV] a command list has a different vtable; not hooking it");
        return;
    }
    *reinterpret_cast<void***>(list) = g_list_our_vt;
    const auto n = g_lists_hooked.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 3 || (n % 600) == 0)
        spdlog::info("[DLSSNR-FOV] pointed list {} at the hooked vtable ({} total)", (void*)list, n);
}

void dump_subrects(const void* params, uint64_t n) {
    for (const char* plane : kProbePlanes) {
        const Rect r = read_subrect(params, plane);
        if (!r.valid) continue;                            // absent planes are the common case
        spdlog::info("[DLSSNR-FOV] eval {} {:<11} base=({},{}) size={}x{}",
                     n, plane, r.x, r.y, r.w, r.h);
    }
    uint32_t v = 0;
    if (nr_get_uint(params, "DLSSNR.Width", &v))  spdlog::info("[DLSSNR-FOV] eval {} DLSSNR.Width  {}", n, v);
    if (nr_get_uint(params, "DLSSNR.Height", &v)) spdlog::info("[DLSSNR-FOV] eval {} DLSSNR.Height {}", n, v);
}

// MEASURED, not assumed: a plain detour here makes the runtime refuse. With safetyhook's
// destination reached by CALL from the trampoline, the snippet's entry sees a return address inside
// UEVRBackend.dll instead of the addon, and every evaluation came back 0xBAD00002 -- while the one
// evaluation five milliseconds BEFORE the detour succeeded. Same run, same feature, one variable.
//
// So we never call the original. The stub below modifies the parameters and then JMPs to the
// trampoline, leaving the addon's own return address exactly where it was: the snippet returns
// straight to the addon, and its caller check sees what it saw unhooked.
//
// The cost is that we no longer observe the return value. It is recovered for free from the addon's
// own log lines, which come through ReShadeLogMessage -- see the sniffing in that function.
void foveal_prehook(ID3D12GraphicsCommandList* list, void* params) {
    // No unwind information exists for hand-assembled code, so an exception escaping into the stub
    // would be undefined. Nothing may propagate out of here.
    try {
        const uint64_t n = g_nr_evals.fetch_add(1, std::memory_order_relaxed) + 1;
        g_nr_eval_ticks.fetch_add(1, std::memory_order_relaxed);
        if ((n % 601) == 0 || (n % 601) == 1) {
            ID3D12Resource* o = recorded_resource("out");
            ID3D12Resource* c = recorded_resource("color");
            spdlog::info("[DLSSNR-FOV] eval {} present {} in-frame {} colour={} output={}",
                         n, g_present_calls, g_eval_in_frame.load(std::memory_order_relaxed),
                         (void*)c, (void*)o);
        }
        const bool verbose = (n == 1 || n == 2);
        if (verbose) dump_subrects(params, n);
        if (n == 1) probe_resource_names(params);
        // Must happen regardless of the foveation toggle: the names are what we are after, and the
        // recorder needs to be in place a frame before anything can use what it captures.
        install_param_set_recorder(params);

        auto& mod = DlssNeuralRendering::get();
        if (mod->foveation_enabled()) {
            // BEFORE the box is applied and before NR runs, so the model overwrites the centre of
            // an output that already holds this frame everywhere else.
            // Box first: the periphery copy now needs to know where the box is so it can avoid it.
            apply_foveal_box(params, mod->foveal_fraction(), mod->convergence_pct(), mod->swap_eyes());
            if (mod->refresh_periphery_enabled()) refresh_periphery(list, params);

            if (mod->close_probe_enabled()) {
                ID3D12Resource* c = recorded_resource("color");
                ID3D12Resource* o = recorded_resource("out");
                if (c != nullptr && o != nullptr) {
                    g_pending = PendingBox{ c, o,
                                            g_nr_box_x.load(std::memory_order_relaxed),
                                            g_nr_box_y.load(std::memory_order_relaxed),
                                            g_nr_box_w.load(std::memory_order_relaxed),
                                            g_nr_box_h.load(std::memory_order_relaxed) };
                    g_pending_valid = g_pending.w != 0 && g_pending.h != 0;
                    hook_list_close(list);
                }
            }
            if (mod->cmd_probe_enabled()) {
                hook_list_close(list);
                const auto b = g_op_bursts.fetch_add(1, std::memory_order_relaxed);
                if ((b % 600) == 0) {
                    spdlog::info("[DLSSNR-FOV][op] --- commands recorded after NR evaluation {} ---", b);
                    g_ops_after_eval.store(0, std::memory_order_relaxed);
                } else {
                    g_ops_after_eval.store(999, std::memory_order_relaxed);
                }
            }
            // Read back rather than trusting the writes. The snippet rejects an inconsistent
            // Color/Output pair silently, so proving our values landed is a separate question from
            // whether they were accepted.
            if (verbose) {
                const Rect out = read_subrect(params, "Output");
                if (out.valid)
                    spdlog::info("[DLSSNR-FOV] eval {} Output now base=({},{}) size={}x{}",
                                 n, out.x, out.y, out.w, out.h);
            }
        }
    } catch (...) {
    }
}

// The thunk, entered by JMP from the patched export, so [rsp] is still the addon's return address:
//
//   sub  rsp, 0x48                 0x48 keeps the ABI's 16-byte alignment across the call below
//   mov  [rsp+0x20..0x38], rcx/rdx/r8/r9      save the four register arguments
//   mov  rdx, r8                   rcx is already the command list; r8 is the parameter block
//   mov  rax, foveal_prehook ; call rax
//   mov  rcx/rdx/r8/r9, [rsp+0x20..0x38]      restore them untouched
//   add  rsp, 0x48
//   mov  rax, trampoline ; jmp rax            JMP, not CALL -- this is the entire point
//
constexpr size_t kStubSize = 0x4B;
constexpr size_t kStubPrehookImm = 0x1D;       // imm64 operand of the first mov rax
constexpr size_t kStubTargetImm  = 0x41;       // imm64 operand of the second
constexpr uint8_t kStubCode[kStubSize] = {
    0x48,0x83,0xEC,0x48,
    0x48,0x89,0x4C,0x24,0x20,
    0x48,0x89,0x54,0x24,0x28,
    0x4C,0x89,0x44,0x24,0x30,
    0x4C,0x89,0x4C,0x24,0x38,
    0x4C,0x89,0xC2,                            // mov rdx, r8   (rcx stays: the command list)
    0x48,0xB8,0,0,0,0,0,0,0,0,
    0xFF,0xD0,
    0x48,0x8B,0x4C,0x24,0x20,
    0x48,0x8B,0x54,0x24,0x28,
    0x4C,0x8B,0x44,0x24,0x30,
    0x4C,0x8B,0x4C,0x24,0x38,
    0x48,0x83,0xC4,0x48,
    0x48,0xB8,0,0,0,0,0,0,0,0,
    0xFF,0xE0,
};

uint8_t* build_tail_jmp_stub() {
    auto* stub = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, kStubSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (stub == nullptr) return nullptr;
    memcpy(stub, kStubCode, kStubSize);
    const auto prehook = reinterpret_cast<uint64_t>(&foveal_prehook);
    memcpy(stub + kStubPrehookImm, &prehook, sizeof(prehook));
    return stub;                                // target imm and RX protection are set once the
}                                               // trampoline address is known

} // namespace

// ================================================================================================
// The ReShade addon entry points. Undecorated names, which is what GetProcAddress looks for.
// ================================================================================================

extern "C" __declspec(dllexport)
bool ReShadeRegisterAddon(HMODULE addon, uint32_t api_version) {
    spdlog::info("[DLSSNR] ReShadeRegisterAddon(module={}, api={}) entered", (void*)addon, api_version);
    if (!hosting()) {
        if (auto fn = forwarded<bool(*)(HMODULE, uint32_t)>("ReShadeRegisterAddon"))
            return fn(addon, api_version);
        return false;
    }
    std::lock_guard lock(g_mtx);
    g_api_version = api_version;
    // NAME and DESCRIPTION are the addon's only exports and are const char* GLOBALS, so
    // GetProcAddress returns the address of the pointer variable.
    if (auto** n = reinterpret_cast<const char**>(GetProcAddress(addon, "NAME")))
        if (readable_string(*n, 127)) strncpy_s(g_addon_name, *n, _TRUNCATE);
    if (auto** d = reinterpret_cast<const char**>(GetProcAddress(addon, "DESCRIPTION")))
        if (readable_string(*d, 255)) strncpy_s(g_addon_desc, *d, _TRUNCATE);
    ++g_addons_registered;
    spdlog::info("[DLSSNR] registered \"{}\" (API {}) -- {}", g_addon_name, api_version, g_addon_desc);
    return true;
}

extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(HMODULE addon) {
    if (!hosting()) { if (auto fn = forwarded<void(*)(HMODULE)>("ReShadeUnregisterAddon")) fn(addon); return; }
    std::lock_guard lock(g_mtx);
    if (g_addons_registered > 0) --g_addons_registered;
}

extern "C" __declspec(dllexport) void ReShadeRegisterEvent(uint32_t ev, void* cb) {
    if (!hosting()) { if (auto fn = forwarded<void(*)(uint32_t, void*)>("ReShadeRegisterEvent")) fn(ev, cb); return; }
    std::lock_guard lock(g_mtx);
    for (auto& e : g_events) if (e.id == ev) { e.fns.push_back(cb); return; }
    g_events.push_back(EventSub{ev, {cb}});
    spdlog::info("[DLSSNR] addon subscribed to event {} ({})", ev,
                 ev == 0 ? "init_device" : ev == 1 ? "destroy_device" : ev == 74 ? "present" : "?");
}

extern "C" __declspec(dllexport) void ReShadeUnregisterEvent(uint32_t ev, void* cb) {
    if (!hosting()) { if (auto fn = forwarded<void(*)(uint32_t, void*)>("ReShadeUnregisterEvent")) fn(ev, cb); return; }
    std::lock_guard lock(g_mtx);
    for (auto& e : g_events) if (e.id == ev) std::erase(e.fns, cb);
}

extern "C" __declspec(dllexport) void ReShadeRegisterOverlay(const char* title, void* cb) {
    if (!hosting()) { if (auto fn = forwarded<void(*)(const char*, void*)>("ReShadeRegisterOverlay")) fn(title, cb); return; }
    std::lock_guard lock(g_mtx);
    g_overlay_cb = cb;
    spdlog::info("[DLSSNR] addon registered its settings page ({})", cb);
}

extern "C" __declspec(dllexport) void ReShadeUnregisterOverlay(const char* title, void* cb) {
    if (!hosting()) { if (auto fn = forwarded<void(*)(const char*, void*)>("ReShadeUnregisterOverlay")) fn(title, cb); return; }
    std::lock_guard lock(g_mtx);
    if (g_overlay_cb == cb) g_overlay_cb = nullptr;
}

extern "C" __declspec(dllexport)
void ReShadeLogMessage(HMODULE module, uint32_t level, const char* message) {
    if (!hosting()) {
        if (auto fn = forwarded<void(*)(HMODULE, uint32_t, const char*)>("ReShadeLogMessage"))
            fn(module, level, message);
        return;
    }
    // Arity moved between ReShade generations, so take whichever argument is actually a string.
    const char* text = readable_string(message, 1024) ? message
                     : readable_string(reinterpret_cast<const char*>(module), 1024)
                       ? reinterpret_cast<const char*>(module) : nullptr;
    if (!text) return;
    spdlog::info("[addon] {}", text);

    // The thunk gives up the return value to keep the addon's return address on the stack, so the
    // addon's own reporting is where evaluation results come from now. It says either
    // "inline feature 18 evaluation succeeded" or "feature 18 evaluate failed with 0xbad00002".
    if (contains_ci(text, "0xbad00002")) {
        if (g_nr_bad.fetch_add(1, std::memory_order_relaxed) == 0) {
            g_nr_tamper_suspected.store(true, std::memory_order_relaxed);
            spdlog::error("[DLSSNR-FOV] the runtime still refuses us as the caller even through the "
                          "tail-jump thunk. Neural rendering is being skipped; turn ProbeNGX off.");
        }
    } else if (contains_ci(text, "evaluation succeeded")) {
        g_nr_ok.fetch_add(1, std::memory_order_relaxed);
    }
}

// The API-18 config signatures include the addon module argument. Omitting it shifts every
// parameter and produces nonsense such as a key of "RenoDX.DLSS5" with a setting name as its value.
extern "C" __declspec(dllexport)
bool ReShadeGetConfigValue(void* module, void* runtime, const char* section, const char* key,
                           char* value, size_t* value_size) {
    if (!hosting()) {
        if (auto fn = forwarded<bool(*)(void*, void*, const char*, const char*, char*, size_t*)>("ReShadeGetConfigValue"))
            return fn(module, runtime, section, key, value, value_size);
        return false;
    }
    if (!readable_string(key, 95)) return false;
    const char* sec = readable_string(section, 63) ? section : "";
    std::lock_guard lock(g_mtx);
    Entry& e = touch(sec, key);
    if (!e.asked) { e.asked = true; spdlog::info("[DLSSNR] addon read config [{}] {} -> {}", sec, key,
                                                 e.value.empty() ? "(its own default)" : e.value); }
    if (e.value.empty()) return false;          // false = "keep your built-in default"
    if (value == nullptr) { if (value_size) *value_size = e.value.size() + 1; return true; }
    const size_t cap = value_size ? *value_size : 0;
    if (cap == 0) return false;
    const size_t n = std::min(e.value.size(), cap - 1);
    memcpy(value, e.value.c_str(), n);
    value[n] = '\0';
    if (value_size) *value_size = n;
    return true;
}

extern "C" __declspec(dllexport)
void ReShadeSetConfigValue(void* module, void* runtime, const char* section, const char* key,
                           const char* value) {
    if (!hosting()) {
        if (auto fn = forwarded<void(*)(void*, void*, const char*, const char*, const char*)>("ReShadeSetConfigValue"))
            fn(module, runtime, section, key, value);
        return;
    }
    if (!readable_string(key, 95)) return;
    std::lock_guard lock(g_mtx);
    Entry& e = touch(readable_string(section, 63) ? section : "", key);
    e.set = true;
    const std::string next = readable_string(value, 191) ? value : "";
    if (e.value != next) { e.value = next; g_config_dirty = true; }
}

extern "C" __declspec(dllexport) const void* ReShadeGetImGuiFunctionTable(uint32_t version) {
    if (!hosting()) {
        if (auto fn = forwarded<const void*(*)(uint32_t)>("ReShadeGetImGuiFunctionTable"))
            return fn(version);
        return nullptr;
    }
    g_imgui_version_asked = version;
    return g_imgui_table;                       // never null while hosting; see build_imgui_table
}

// ================================================================================================

std::optional<std::string> DlssNeuralRendering::on_initialize() {
    build_imgui_table();
    fill_vts(std::make_integer_sequence<int, kVt>{});
    return Mod::on_initialize();
}

void DlssNeuralRendering::on_config_load(const utility::Config& cfg, bool set_defaults) {
    for (auto& o : m_options) o.get().config_load(cfg, set_defaults);
}
void DlssNeuralRendering::on_config_save(utility::Config& cfg) {
    for (auto& o : m_options) o.get().config_save(cfg);
}

// The addon's detours target NGX, so it must not be loaded before NGX is in the process; and
// init_device cannot be delivered before the device exists. Both arrive later than mod init.
void DlssNeuralRendering::load_addons_once() {
    if (!m_enabled->value() || m_device_delivered) return;
    if (!GetModuleHandleW(L"_nvngx.dll") && !GetModuleHandleW(L"nvngx_dlssnr.dll")) return;

    auto& hook = g_framework->get_d3d12_hook();
    auto* dev = hook->get_device();
    if (dev == nullptr) return;

    if (forward_target() != nullptr) {
        strncpy_s(g_last_error, "a real ReShade is loaded; standing down so addons are not hosted twice", _TRUNCATE);
        spdlog::warn("[DLSSNR] {}", g_last_error);
        return;
    }

    g_native_dev   = reinterpret_cast<uint64_t>(dev);
    g_native_queue = reinterpret_cast<uint64_t>(hook->get_command_queue());
    g_native_swap  = reinterpret_cast<uint64_t>(hook->get_swap_chain());

    if (m_load_attempted) {          // library already in the process; only the device is new
        std::lock_guard lock(g_mtx);
        for (const auto& e : g_events) {
            if (e.id != 0) continue;
            for (void* fn : e.fns) {
                spdlog::info("[DLSSNR] re-delivering init_device(device={}) after reset", (void*)dev);
                invoke_init_device_guarded(reinterpret_cast<void(*)(void*)>(fn));
            }
        }
        m_device_delivered = true;
        return;
    }
    m_load_attempted = true;

    // BEFORE the library goes in. Registration is where the addon reads every one of its keys, and
    // it never reads them again, so anything restored after this point would be ignored until the
    // launch after next.
    load_addon_config();

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto dir = std::filesystem::path{exe}.parent_path();
    int found = 0;
    std::error_code ec;
    for (const auto& f : std::filesystem::directory_iterator{dir, ec}) {
        if (f.path().extension() != ".addon64") continue;
        ++found;
        const int before = g_addons_registered;
        if (LoadLibraryW(f.path().c_str()) == nullptr) {
            spdlog::error("[DLSSNR] LoadLibrary({}) failed, error {}",
                          f.path().filename().string(), GetLastError());
            continue;
        }
        spdlog::info("[DLSSNR] loaded {} -- {}", f.path().filename().string(),
                     g_addons_registered > before ? "registered" : "did NOT register");
    }
    if (found == 0) {
        strncpy_s(g_last_error, "no *.addon64 beside the game executable", _TRUNCATE);
        spdlog::warn("[DLSSNR] {}", g_last_error);
        return;
    }

    std::lock_guard lock(g_mtx);
    for (const auto& e : g_events) {
        if (e.id != 0) continue;                // init_device
        for (void* fn : e.fns) {
            spdlog::info("[DLSSNR] delivering init_device(device={})", (void*)dev);
            invoke_init_device_guarded(reinterpret_cast<void(*)(void*)>(fn));
        }
    }
    m_device_delivered = true;
}

// Installed only after the addon is up, because the addon is what brings nvngx_dlssnr.dll into the
// process in the first place -- it LoadLibrarys the snippet from beside itself and resolves
// NVSDK_NGX_D3D12_CreateFeature / EvaluateFeature by name. There is no _nvngx.dll dispatcher in
// this path to sit upstream of, which is precisely why the detour has to go on the signed module.
void DlssNeuralRendering::install_ngx_probe() {
    if (m_probe_attempted || !m_enabled->value() || !m_probe_ngx->value()) return;
    if (!m_device_delivered) return;                       // addon not up yet; snippet not loaded

    HMODULE snippet = GetModuleHandleW(L"nvngx_dlssnr.dll");
    if (snippet == nullptr) return;                        // keep trying; it loads on first NR use
    m_probe_attempted = true;
    g_snippet_base = snippet;

    auto* target = reinterpret_cast<void*>(GetProcAddress(snippet, "NVSDK_NGX_D3D12_EvaluateFeature"));
    if (target == nullptr) {
        spdlog::error("[DLSSNR-FOV] nvngx_dlssnr.dll exports no NVSDK_NGX_D3D12_EvaluateFeature");
        return;
    }

    // If something already detoured this, our trampoline would chain onto theirs and the eval times
    // below would be measuring both. Worth one line to know which situation we are in.
    const auto* b = static_cast<const uint8_t*>(target);
    spdlog::info("[DLSSNR-FOV] EvaluateFeature at {} prologue {:02X} {:02X} {:02X} {:02X} {:02X}",
                 target, b[0], b[1], b[2], b[3], b[4]);
    if (b[0] == 0xE9 || (b[0] == 0xFF && b[1] == 0x25))
        spdlog::warn("[DLSSNR-FOV] prologue is already a jump -- another detour is present");

    uint8_t* stub = build_tail_jmp_stub();
    if (stub == nullptr) {
        spdlog::error("[DLSSNR-FOV] could not allocate the tail-jump thunk");
        return;
    }

    // Created DISABLED: the stub cannot run until its jump target is filled in, and NR evaluations
    // happen on the render thread while this runs on the present thread.
    auto hook = safetyhook::InlineHook::create(target, stub, safetyhook::InlineHook::StartDisabled);
    if (!hook) {
        spdlog::error("[DLSSNR-FOV] failed to detour EvaluateFeature");
        VirtualFree(stub, 0, MEM_RELEASE);
        return;
    }
    g_nr_eval_hook = std::move(*hook);

    const auto trampoline = reinterpret_cast<uint64_t>(g_nr_eval_hook.original<void*>());
    memcpy(stub + kStubTargetImm, &trampoline, sizeof(trampoline));

    DWORD old = 0;
    if (!VirtualProtect(stub, kStubSize, PAGE_EXECUTE_READ, &old)) {
        spdlog::error("[DLSSNR-FOV] could not make the thunk executable");
        return;                                 // hook is still disabled, so nothing is patched
    }
    FlushInstructionCache(GetCurrentProcess(), stub, kStubSize);

    if (auto en = g_nr_eval_hook.enable(); !en) {
        spdlog::error("[DLSSNR-FOV] failed to enable the detour");
        return;
    }
    g_nr_hooked.store(true, std::memory_order_relaxed);
    spdlog::info("[DLSSNR-FOV] EvaluateFeature detoured via tail-jump thunk at {} -> trampoline {:#x}. "
                 "The addon's return address is preserved, so the caller check should not fire.",
                 (void*)stub, trampoline);
}

// Delivered on device reset and at teardown. Guarded twice over: once so it cannot fire without a
// matching init_device, and once by SEH, because by teardown the addon may already be partway
// through its own unload.
void DlssNeuralRendering::deliver_destroy_device() {
    std::lock_guard lock(g_mtx);
    if (!m_device_delivered) return;
    m_device_delivered = false;
    for (const auto& e : g_events) {
        if (e.id != 1) continue;                // destroy_device
        for (void* fn : e.fns) {
            spdlog::info("[DLSSNR] delivering destroy_device");
            if (!invoke_destroy_device_guarded(reinterpret_cast<void(*)(void*)>(fn)))
                spdlog::error("[DLSSNR] destroy_device callback {} faulted", fn);
        }
    }
    g_present_calls = 0;
}

DlssNeuralRendering::~DlssNeuralRendering() {
    save_addon_config();          // a change made in the last two seconds is still unwritten
    deliver_destroy_device();
}

// A reset destroys the device the addon was given, so its feature registry and NGX hooks are stale
// from here. Tell it, then let load_addons_once() hand it the replacement -- without reloading the
// library, which is already in the process and must not be loaded twice.
// The detour does not survive a device reset: the addon detaches and re-attaches its NGX hooks,
// and the snippet can be reloaded, so our patch ends up in code that is no longer being called.
// Evidence: the last box application is one second BEFORE destroy_device, while NR keeps creating
// features for minutes afterwards. Re-arm so the next evaluation re-installs it.
void DlssNeuralRendering::reset_ngx_probe() {
    const HMODULE now = GetModuleHandleW(L"nvngx_dlssnr.dll");
    if (g_nr_hooked.load(std::memory_order_relaxed)) {
        // Only unhook when the module is still the one we patched. If it reloaded or unloaded,
        // let the old hook object leak rather than write the original bytes into whatever now
        // occupies that address.
        if (now != nullptr && now == g_snippet_base) {
            g_nr_eval_hook = {};
        } else {
            new (&g_nr_eval_hook) safetyhook::InlineHook{};   // abandon without restoring
        }
    }
    g_nr_hooked.store(false, std::memory_order_relaxed);
    m_probe_attempted = false;
    g_snippet_base = nullptr;

    // Recorded resources belonged to the device that is going away, so every pointer is stale and
    // the periphery copy would be reading freed textures.
    g_param_set_count = 0;
    g_hooked_params = nullptr;
    g_copy_faulted.store(false, std::memory_order_relaxed);
    spdlog::info("[DLSSNR-FOV] detour re-armed after device reset");
}

void DlssNeuralRendering::on_device_reset() {
    reset_ngx_probe();
    deliver_destroy_device();
    g_native_dev = g_native_queue = g_native_swap = 0;
    g_present_faulted = false;
}

void DlssNeuralRendering::on_present() {
    load_addons_once();
    install_ngx_probe();
    accumulate_frame_time(m_foveate->value() && g_nr_hooked.load(std::memory_order_relaxed));
    g_eval_in_frame.store(0, std::memory_order_relaxed);   // eye index is per frame

    // Throttled, because dragging one of the addon's sliders marks this dirty on every frame of the
    // drag. Ahead of the dispatch gate below so settings are still written when present delivery is
    // switched off.
    if (g_config_dirty && GetTickCount64() - g_config_saved_at > 2000) save_addon_config();

    if (!m_enabled->value() || !m_dispatch_present->value() || g_present_faulted) return;

    for (const auto& e : g_events) {
        if (e.id != 74) continue;               // present
        for (void* fn : e.fns) {
            if (!invoke_present_guarded(reinterpret_cast<PresentFn>(fn))) {
                g_present_faulted = true;
                spdlog::error("[DLSSNR] present callback {} FAULTED -- dispatch disabled this session", fn);
                return;
            }
        }
        const auto n = ++g_present_calls;
        if (n == 1 || n == 60 || n == 600)
            spdlog::info("[DLSSNR] present delivered {} time(s)", n);
    }
}

void DlssNeuralRendering::on_draw_sidebar_entry(std::string_view entry) {
    const bool addon_page = (entry == kPageNeural);
    const bool hooked = g_nr_hooked.load(std::memory_order_relaxed);
    const bool refused = g_nr_tamper_suspected.load(std::memory_order_relaxed);

    // ---- status -----------------------------------------------------------------------------
    if (g_addons_registered > 0) {
        ImGui::Text("%s  (addon API %u)", g_addon_name, g_api_version);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "No addon loaded.");
        ImGui::TextDisabled("Needs renodx-dlss5.addon64 and nvngx_dlssnr.dll beside the game exe.");
    }
    if (hooked) {
        const auto bw = g_nr_box_w.load(std::memory_order_relaxed);
        ImGui::Text("Evaluations %llu   Box %ux%u",
                    (unsigned long long)g_nr_evals.load(std::memory_order_relaxed),
                    bw, g_nr_box_h.load(std::memory_order_relaxed));
    }
    if (refused) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                           "The runtime refused our detour (0xBAD00002): neural rendering is being "
                           "SKIPPED, so a high frame rate here means it is not running.");
    }
    if (g_last_error[0])
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "%s", g_last_error);
    if (g_present_faulted)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Present callback faulted; dispatch off.");

    if (addon_page) {
        // ---- the addon's own settings, on their own page ---------------------------------------
        ImGui::Separator();
        if (m_draw_addon_ui->value() && g_overlay_cb != nullptr && !g_overlay_faulted) {
            if (!invoke_overlay_guarded(reinterpret_cast<void(*)(void*)>(g_overlay_cb))) {
                g_overlay_faulted = true;
                spdlog::error("[DLSSNR] the addon's settings page faulted; not called again");
            }
        } else if (g_overlay_faulted) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "The addon's settings page faulted.");
        } else {
            ImGui::TextDisabled("No addon page registered.");
        }

        if (ImGui::CollapsingHeader("Saved settings")) {
            ImGui::TextWrapped(
                "The addon reads its settings once at load and never saves them, so UEVR stores "
                "them and replays them at the next launch.");
            int stored = 0;
            for (const auto& e : g_entries) if (!e.value.empty()) ++stored;
            ImGui::Text("%d stored, %d restored this launch.", stored, g_config_restored);
            for (const auto& e : g_entries) {
                if (e.value.empty()) continue;
                ImGui::TextDisabled("  %s = %s", e.key.c_str(), e.value.c_str());
            }
            if (ImGui::Button("Forget stored addon settings")) {
                std::lock_guard lock(g_mtx);
                for (auto& e : g_entries) e.value.clear();
                g_config_dirty = true;
                save_addon_config();
                g_config_restored = 0;
            }
            if (g_unmatched_label[0])
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                                   "\"%s\" has no known key and will not persist.", g_unmatched_label);
        }
        return;
    }

    // ---- the foveal box ---------------------------------------------------------------------
    ImGui::Separator();
    if (!hooked) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Foveal controls hidden: no NGX detour.");
        ImGui::TextWrapped(
            "Neural rendering is off, so nvngx_dlssnr.dll is not loaded and there is nothing to "
            "detour. Turn on \"Enable DLSS Neural Rendering\" in Addon settings below; the detour "
            "installs a second later and these controls appear. No restart needed.");
    } else {
        m_foveate->draw("Restrict NR to a centre box");

        // Fixed steps: a thumbstick on an ImGui slider skips straight past values.
        const float kSteps[] = { 0.35f, 0.40f, 0.50f, 0.60f, 0.70f, 1.00f };
        for (int i = 0; i < IM_ARRAYSIZE(kSteps); ++i) {
            if (i != 0) ImGui::SameLine();
            char label[16];
            _snprintf_s(label, sizeof(label), _TRUNCATE, "%.2f", kSteps[i]);
            const bool active = std::fabs(m_foveal_fraction->value() - kSteps[i]) < 0.005f;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.5f, 0.3f, 1.0f));
            if (ImGui::Button(label)) m_foveal_fraction->value() = kSteps[i];
            if (active) ImGui::PopStyleColor();
        }
        const float f = m_foveal_fraction->value();
        ImGui::TextDisabled("Keeps %.0f%% of the pixels.", f * f * 100.0f);

        m_cmd_probe->draw("Probe: log commands after NR");
        ImGui::TextDisabled("Finds what reads Output, so a blend can go just before it.");
        m_close_probe->draw("Probe: overwrite the box at Close");
        ImGui::TextDisabled("Diagnostic. If neural rendering VANISHES, Close runs before the addon "
                            "consumes its output and a proper feather can be built there. %llu done.",
                            (unsigned long long)g_close_overwrites.load(std::memory_order_relaxed));

        m_convergence_pct->draw("Stereo convergence (fraction of eye width)");
        ImGui::TextDisabled("Mirrored per eye. OpenXR-Toolkit's fixed-foveated path uses 0.04; "
                            "at %u px wide that is %d px per eye.",
                            g_last_plane_w.load(std::memory_order_relaxed),
                            (int)(m_convergence_pct->value() * g_last_plane_w.load(std::memory_order_relaxed)));
        m_swap_eyes->draw("Swap which eye shifts");




        // ---- is the box actually cheaper? ----
        ImGui::Separator();
        ImGui::TextDisabled("Stand still. Box off ~10 s, then on ~10 s.");
        for (int i = 0; i < 2; ++i) {
            const double avg = g_ms_n[i] ? g_ms_sum[i] / (double)g_ms_n[i] : 0.0;
            if (g_ms_n[i] == 0) ImGui::TextDisabled("Box %s: no samples", i ? "ON " : "OFF");
            else ImGui::Text("Box %s: %.2f ms (%.1f fps) over %llu frames", i ? "ON " : "OFF",
                             avg, avg > 0.0 ? 1000.0 / avg : 0.0, (unsigned long long)g_ms_n[i]);
        }
        if (g_ms_n[0] && g_ms_n[1]) {
            const double off = g_ms_sum[0] / (double)g_ms_n[0];
            const double on = g_ms_sum[1] / (double)g_ms_n[1];
            ImGui::TextColored(on < off ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.6f, 0.5f, 1.0f),
                               "Box saves %.2f ms/frame (%+.1f%%)", off - on,
                               off > 0.0 ? (off - on) / off * 100.0 : 0.0);
        }
        if (ImGui::Button("Reset measurement")) {
            g_ms_sum[0] = g_ms_sum[1] = 0.0;
            g_ms_n[0] = g_ms_n[1] = 0;
            g_ab_warmup = 30;
        }
    }

    // ---- setup: changed once, then never ------------------------------------------------------
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Setup")) {
        ImGui::TextWrapped(
            "UEVR exports the ReShade addon API itself, because ReShade cannot coexist with UEVR "
            "here: a dxgi proxy stops UEVR injecting, and ReShade's OpenXR layer declines any "
            "session created without a device it wrapped.");
        m_enabled->draw("Host addons in-process");
        m_probe_ngx->draw("Detour NGX EvaluateFeature");
        ImGui::TextDisabled("Both take effect on the next launch. The detour is what the box needs.");
        m_refresh_periphery->draw("Keep the area outside the box live");
        ImGui::TextDisabled("Without it the periphery shows a frozen frame.");
        m_dispatch_present->draw("Deliver the per-frame present event");
        m_draw_addon_ui->draw("Show the addon's own settings below");
    }

    // ---- diagnostics --------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Diagnostics")) {
        ImGui::Text("Frames delivered: %llu", (unsigned long long)g_present_calls);
        ImGui::Text("Periphery copies: %llu", (unsigned long long)g_copies.load(std::memory_order_relaxed));
        ImGui::Text("Addon reported succeeded %llu, refused %llu",
                    (unsigned long long)g_nr_ok.load(std::memory_order_relaxed),
                    (unsigned long long)g_nr_bad.load(std::memory_order_relaxed));
        if (g_copy_faulted.load(std::memory_order_relaxed))
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Periphery refresh disabled itself.");
        if (g_slot_list[0]) ImGui::TextDisabled("ImGui slots used: %s", g_slot_list);
        if (g_addon_desc[0]) ImGui::TextWrapped("%s", g_addon_desc);
    }

}
