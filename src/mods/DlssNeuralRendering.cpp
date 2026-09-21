// See DlssNeuralRendering.hpp for why UEVR hosts ReShade addons itself.
//
// Ported from the CyberpunkVR port's addon host. Everything here was established by measuring the
// closed binary rather than from documentation, because none exists: the addon's source is not in
// the public RenoDX repository, and the community distribution that packages it ships binaries and
// an installer profile table only.

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#include <imgui.h>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <atomic>
#include <new>
#include <cmath>
#include <iterator>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <utility>

#include "Framework.hpp"
#include "mods/VR.hpp"
#include "DlssNeuralRendering.hpp"
#include "AddonStyle19250.hpp"
#include "AddonFont19250.hpp"
#include "AddonText19250.hpp"
#include "AddonTooltip19250.hpp"
#include "AddonDisabled19250.hpp"
#include "AddonSwapchain.hpp"

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
std::vector<HMODULE> g_addon_uninit_targets;
uint32_t g_api_version{0};
uint32_t g_imgui_version_asked{0};
char     g_addon_name[128]{};
char     g_addon_desc[256]{};
char     g_last_error[256]{};
uint64_t g_present_calls{0};
bool     g_present_faulted{false};
std::atomic<bool> g_sr_evaluation_observed{false};
std::atomic<uint64_t> g_nr_last_eval_ms{0};

// Observe the exact API18 addon's NGX post-evaluation callback, not the
// separate fe120 processing path. These hooks never change inputs or decisions.
safetyhook::MidHook g_nr_ngx_probes[4];
std::atomic<uint64_t> g_nr_ngx_counts[4]{};
std::atomic<uint64_t> g_nr_ngx_tag_rejected{};
bool g_nr_ngx_attempted{};
bool nr_ngx_stack(uintptr_t frame, bool tag, uint64_t* out) {
    __try {
        const auto p = reinterpret_cast<const uint8_t*>(frame);
        if (tag) {
            out[0] = *reinterpret_cast<const uint32_t*>(p + 0x1e4);
            out[1] = *reinterpret_cast<const uintptr_t*>(p + 0x1f0) != 0;
        } else {
            out[0] = *reinterpret_cast<const uintptr_t*>(p + 0x1e8) != 0;
            out[1] = *reinterpret_cast<const uintptr_t*>(p + 0x1a8) != 0;
            out[2] = *reinterpret_cast<const uintptr_t*>(p + 0x1b0) != 0;
            out[3] = *reinterpret_cast<const uint32_t*>(p + 0x1fc);
            out[4] = *reinterpret_cast<const uint32_t*>(p + 0x200);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void observe_nr_ngx_entry(safetyhook::Context& ctx) {
    const auto n = ++g_nr_ngx_counts[0];
    if (n <= 4 || n % 600 == 0)
        spdlog::info("[DLSSNR-NGX-GATE] entry={} result=0x{:08x} commandlist={} parameters={}",
            n, uint32_t(ctx.r9), ctx.rcx != 0, ctx.r8 != 0);
}
void observe_nr_ngx_mode(safetyhook::Context& ctx) {
    const auto n = ++g_nr_ngx_counts[1];
    if (n <= 4 || n % 600 == 0)
        spdlog::info("[DLSSNR-NGX-GATE] mode-check={} mode_raw={}", n, ctx.r13 & 255);
}
void observe_nr_ngx_inputs(safetyhook::Context& ctx) {
    const auto n = ++g_nr_ngx_counts[2];
    if (n > 4 && n % 600 != 0) return;
    uint64_t s[5]{};
    const bool read = nr_ngx_stack(ctx.rbp, false, s);
    spdlog::info("[DLSSNR-NGX-GATE] inputs={} readable={} output={} motion={} depth={} render={}x{}",
        n, read, s[0], s[1], s[2], s[3], s[4]);
}
void observe_nr_ngx_tag(safetyhook::Context& ctx) {
    const auto n = ++g_nr_ngx_counts[3];
    uint64_t s[5]{};
    const bool read = nr_ngx_stack(ctx.rbp, true, s);
    const auto hr = static_cast<int32_t>(ctx.rax);
    const bool accepted = read && hr >= 0 && s[0] == 8 && s[1] != 0;
    if (!accepted) ++g_nr_ngx_tag_rejected;
    if (n <= 4 || n % 600 == 0)
        spdlog::info("[DLSSNR-NGX-GATE] native-wrapper={} hr=0x{:08x} readable={} size={} pointer_present={} accepted={}",
            n, uint32_t(hr), read, s[0], s[1], accepted);
}
void install_nr_ngx_gate_probes() {
    if (g_nr_ngx_attempted || g_api_version != 18 ||
        std::string_view(g_addon_name) != "RenoDX DLSS") return;
    auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(L"renodx-dlss.addon64"));
    if (!base) return;
    g_nr_ngx_attempted = true;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (pe->FileHeader.TimeDateStamp != 0xb6f86a12 ||
        pe->OptionalHeader.SizeOfImage != 0x279000) {
        spdlog::warn("[DLSSNR-NGX-GATE] unsupported addon fingerprint; no probes installed");
        return;
    }
    struct Site { size_t rva; uint8_t bytes[9]; size_t length; void (*callback)(safetyhook::Context&); };
    const Site sites[]{
        {0x86a16, {0x48,0x89,0xd3,0x8b,0x05,0xd9,0xb5,0x1c,0x00}, 9, observe_nr_ngx_entry},
        {0x86a83, {0x45,0x85,0xed,0x0f,0x84,0xc8,0x08,0x00,0x00}, 9, observe_nr_ngx_mode},
        {0x86d50, {0x48,0x83,0xbd,0xe8,0x01,0x00,0x00,0x00}, 8, observe_nr_ngx_inputs},
        {0x86dc7, {0x85,0xc0,0x0f,0x88,0x85,0x05,0x00,0x00}, 8, observe_nr_ngx_tag}
    };
    for (const auto& site : sites) {
        if (memcmp(base + site.rva, site.bytes, site.length) != 0) {
            spdlog::warn("[DLSSNR-NGX-GATE] fingerprint mismatch at {:x}; no probes installed", site.rva);
            return;
        }
    }
    for (size_t i = 0; i < std::size(sites); ++i) {
        g_nr_ngx_probes[i] = safetyhook::create_mid(const_cast<uint8_t*>(base + sites[i].rva), sites[i].callback);
        if (!g_nr_ngx_probes[i]) {
            for (auto& probe : g_nr_ngx_probes) probe = {};
            spdlog::warn("[DLSSNR-NGX-GATE] installation failed; probes removed");
            return;
        }
    }
    spdlog::info("[DLSSNR-NGX-GATE] installed entry/mode/inputs/native-wrapper observations");
}

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
// ROLLING, not cumulative. Cumulative averages let the two buckets be measured minutes apart, so a
// scene that quietly got cheaper looked like the box improving: box ON fell 19.45 -> 17.08 ms while
// box OFF sat unchanged at 21.00 from early sampling. A fixed window keeps both buckets recent and
// comparable, at the cost of noisier numbers -- which is the honest trade.
constexpr int kAbWindow = 600;
double   g_ms_ring[2][kAbWindow]{};
int      g_ms_head[2]{};
uint64_t g_ms_n[2]{};          // total ever seen, for the sample count shown
double   g_ms_sum[2]{};        // sum of what is currently IN the window
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
            g_ms_sum[i] += ms - g_ms_ring[i][g_ms_head[i]];   // evict the oldest
            g_ms_ring[i][g_ms_head[i]] = ms;
            g_ms_head[i] = (g_ms_head[i] + 1) % kAbWindow;
            ++g_ms_n[i];
            if (((g_ms_n[0] + g_ms_n[1]) % 900) == 0 && g_ms_n[0] && g_ms_n[1]) {
                const double off = g_ms_sum[0] / (double)std::min<uint64_t>(g_ms_n[0], kAbWindow);
                const double on = g_ms_sum[1] / (double)std::min<uint64_t>(g_ms_n[1], kAbWindow);
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
    {"Overall Intensity",            "NRIntensity"},   // renamed in the 4.7 build
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
    {"Diffuse White (nits)",         "NRDiffuseWhiteNits"},
    {"Global Tone Intensity",        "NRGlobalTone"},
    {"NR Toggle Key",                "NRToggleKey"},
    {"NR Screenshot Key",            "NRScreenshotKey"},
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
    const bool modern = std::any_of(g_entries.begin(), g_entries.end(), [](const Entry& e) {
        return e.asked && e.section == "RENODX-DLSS";
    });
    if (modern) {
        static constexpr KeyMap keys[] = {
            {"Diffuse White (nits)", "DirectNeuralRenderingDiffuseWhiteNits"},
            {"Overall Intensity", "DirectNeuralRenderingIntensity"},
            {"Structure Intensity", "DirectNeuralRenderingLocalStructureStrength"},
            {"Global Tone Intensity", "DirectNeuralRenderingGlobalToneStrength"},
            {"Local Tone Intensity", "DirectNeuralRenderingLocalToneStrength"},
            {"Skin Structure Strength", "DirectNeuralRenderingSkinStructureStrength"}
        };
        for (const auto& m : keys)
            if (_stricmp(m.label, s.c_str()) == 0) return m.key;
        return nullptr; // never write a modern widget into the old schema
    }
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

const char* section_for_key(const char* key) {
    if (strncmp(key, "DirectNeuralRendering", 21) != 0) return kAddonSection;
    const char* section = nullptr;
    for (const auto& e : g_entries) {
        if (!e.asked || e.key != key || e.section.rfind("RENODX-DLSS", 0) != 0) continue;
        if (section != nullptr) return nullptr; // cannot infer active preset
        section = e.section.c_str();
    }
    return section;
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
        const char* section = section_for_key(key);
        spdlog::info("[DLSSNR] control \"{}\" -> [{}] {}", vis,
                     section ? section : "ambiguous; not saved", key);
    } else {
        if (!g_unmatched_label[0]) strncpy_s(g_unmatched_label, vis.c_str(), _TRUNCATE);
        spdlog::warn("[DLSSNR] control \"{}\" maps to no known config key; it will NOT persist", vis);
    }
}

void remember(const char* label, const char* formatted) {
    const char* key = key_for_label(label);
    if (key == nullptr) return;
    std::lock_guard lock(g_mtx);
    const char* section = section_for_key(key);
    if (section == nullptr) {
        spdlog::warn("[DLSSNR] not saving {}: no unique requested section", key);
        return;
    }
    Entry& e = touch(section, key);
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
    // EVERY unimplemented slot announces itself, labelled or not. Logging only the ones whose first
    // argument happened to be a readable string meant a stub that faulted the overlay left no trace
    // at all -- the last line before the fault is the one that names the culprit.
    if (!g_slot_seen[I < kSlots ? I : 0]) {
        if (readable_string(a1, 96))
            spdlog::info("[DLSSNR] addon overlay slot {} label \"{}\"", I, static_cast<const char*>(a1));
        else
            spdlog::info("[DLSSNR] addon overlay slot {} (no readable label)", I);
    }
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
void impl_text_colored(const ImVec4& color, const char* fmt, va_list args) {
    if (!readable_string(fmt, 256)) return;
    const auto message = addon_ui::text_colored(color, fmt, args);
    // Bounded, deduplicated UI evidence only; never treat arbitrary addon text as
    // an independently measured successful NR evaluation.
    static std::vector<std::string> observed;
    if (g_api_version == 18 && std::string_view(g_addon_name) == "RenoDX DLSS" &&
        observed.size() < 32 &&
        std::find(observed.begin(), observed.end(), message.data()) == observed.end()) {
        observed.emplace_back(message.data());
        spdlog::info("[DLSSNR-ADDON-STATUS] {}", message.data());
    }
}
void impl_text_disabled(const char* fmt, va_list args) {
    if (readable_string(fmt, 256)) ImGui::TextDisabledV(fmt, args);
}
void impl_text_wrapped(const char* fmt, va_list args) {
    if (readable_string(fmt, 256)) ImGui::TextWrappedV(fmt, args);
}
void impl_label_text(const char* label, const char* fmt, va_list args) {
    if (readable_string(label, 256) && readable_string(fmt, 256)) ImGui::LabelTextV(label, fmt, args);
}
void impl_bullet_text(const char* fmt, va_list args) {
    if (readable_string(fmt, 256)) ImGui::BulletTextV(fmt, args);
}
void impl_set_tooltip(const char* fmt, va_list args) {
    if (readable_string(fmt, 256)) addon_ui::set_tooltip(fmt, args);
}
void impl_set_item_tooltip(const char* fmt, va_list args) {
    if (readable_string(fmt, 256)) addon_ui::set_item_tooltip(fmt, args);
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

// A C++ stub returning uint64_t sets RAX and leaves XMM0 as whatever the last call left there.
// ImGui's layout queries -- GetContentRegionAvail, GetCursorPos, CalcTextSize -- return ImVec2 in
// XMM0, so those slots received garbage dimensions, and the 4.7 addon faulted on slot 404 the first
// time it asked one. Six bytes of machine code zero BOTH return registers, which no C++ signature
// can do:
//
//     xor eax, eax        31 C0        integers and bools -> 0 / false ("unchanged")
//     xorps xmm0, xmm0    0F 57 C0     floats and ImVec2  -> 0 / (0,0)
//     ret                 C3
//
// Structs larger than eight bytes still come back through a hidden pointer we do not write, but
// nothing observed uses one.
volatile uint32_t g_last_imgui_slot = 0xFFFFFFFFu;

// One thunk PER SLOT, so a fault names the function that caused it -- the shared stub was safe but
// anonymous, and losing that identity is why the last two runs could not be diagnosed. Each records
// its own index and then zeroes BOTH return registers, which no C++ signature can do:
//
//   mov  rax, &g_last_imgui_slot     48 B8 <imm64>
//   mov  dword ptr [rax], slot       C7 00 <imm32>
//   xor  eax, eax                    31 C0      integers/bools -> 0
//   xorps xmm0, xmm0                 0F 57 C0   floats/ImVec2  -> 0
//   ret                              C3
//
// No call, no stack use, and RAX is volatile, so nothing needs preserving.
constexpr size_t kSlotThunkSize = 22;
void* make_slot_thunks(size_t count) {
    auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, kSlotThunkSize * count,
                                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (base == nullptr) return nullptr;
    const auto sink = reinterpret_cast<uint64_t>(&g_last_imgui_slot);
    for (size_t i = 0; i < count; ++i) {
        uint8_t* t = base + i * kSlotThunkSize;
        t[0] = 0x48; t[1] = 0xB8; memcpy(t + 2, &sink, 8);
        t[10] = 0xC7; t[11] = 0x00; const uint32_t idx = static_cast<uint32_t>(i);
        memcpy(t + 12, &idx, 4);
        t[16] = 0x31; t[17] = 0xC0;
        t[18] = 0x0F; t[19] = 0x57; t[20] = 0xC0;
        t[21] = 0xC3;
    }
    DWORD old = 0;
    if (!VirtualProtect(base, kSlotThunkSize * count, PAGE_EXECUTE_READ, &old)) return nullptr;
    FlushInstructionCache(GetCurrentProcess(), base, kSlotThunkSize * count);
    return base;
}

void* make_safe_stub() {
    static uint8_t* stub = nullptr;
    if (stub != nullptr) return stub;
    static const uint8_t code[] = { 0x31, 0xC0, 0x0F, 0x57, 0xC0, 0xC3 };
    stub = static_cast<uint8_t*>(VirtualAlloc(nullptr, sizeof(code), MEM_COMMIT | MEM_RESERVE,
                                              PAGE_READWRITE));
    if (stub == nullptr) return nullptr;
    memcpy(stub, code, sizeof(code));
    DWORD old = 0;
    if (!VirtualProtect(stub, sizeof(code), PAGE_EXECUTE_READ, &old)) return nullptr;
    FlushInstructionCache(GetCurrentProcess(), stub, sizeof(code));
    return stub;
}

// The 4.7 addon draws a CUSTOM TOGGLE WIDGET, which is what the faults were about: it asks for the
// window draw list (12), gets our zero, and then draws into that null pointer with
// ImDrawList_AddCircleFilled (389) and ImDrawList_PathArcTo (404). Everything below either returns
// a real object where zero is not a valid answer, or forwards a draw call to the list ImGui owns.
//
// Indices were PARSED from ReShade's imgui_function_table_19250.hpp, not read off a summary of it.
// Two earlier attempts used summarised indices that were wrong -- Separator placed at 96, which is
// really PushID3, and GetKeyName at 405, which is really ImDrawList_PathArcToFast -- and broke a
// table that had been correct. The measured original layout IS 19250; nothing needed remapping.
ImDrawList* impl_get_window_draw_list()      { return ImGui::GetWindowDrawList(); }
ImVec2 impl_get_cursor_screen_pos()          { return ImGui::GetCursorScreenPos(); }
float  impl_get_frame_height()               { return ImGui::GetFrameHeight(); }
ImU32  impl_get_color_u32(ImGuiCol i, float a){ return ImGui::GetColorU32(addon_ui::host_color(i), a); }
void   impl_same_line(float off, float sp)   { ImGui::SameLine(off, sp); }
bool   impl_invisible_button(const char* id, const ImVec2& sz, ImGuiButtonFlags f) {
    return readable_string(id, 128) ? ImGui::InvisibleButton(id, sz, f) : false;
}
bool   impl_is_item_hovered(ImGuiHoveredFlags f) { return ImGui::IsItemHovered(f); }
bool   impl_is_item_active()                     { return ImGui::IsItemActive(); }

void dl_add_line(ImDrawList* d, const ImVec2& a, const ImVec2& b, ImU32 c, float t) { if (d) d->AddLine(a, b, c, t); }
void dl_add_rect(ImDrawList* d, const ImVec2& a, const ImVec2& b, ImU32 c, float r, ImDrawFlags f, float t) { if (d) d->AddRect(a, b, c, r, f, t); }
void dl_add_rect_filled(ImDrawList* d, const ImVec2& a, const ImVec2& b, ImU32 c, float r, ImDrawFlags f) { if (d) d->AddRectFilled(a, b, c, r, f); }
void dl_add_tri_filled(ImDrawList* d, const ImVec2& a, const ImVec2& b, const ImVec2& c, ImU32 col) { if (d) d->AddTriangleFilled(a, b, c, col); }
void dl_add_circle(ImDrawList* d, const ImVec2& c, float r, ImU32 col, int seg, float t) { if (d) d->AddCircle(c, r, col, seg, t); }
void dl_add_circle_filled(ImDrawList* d, const ImVec2& c, float r, ImU32 col, int seg) { if (d) d->AddCircleFilled(c, r, col, seg); }
void dl_add_text(ImDrawList* d, const ImVec2& p, ImU32 c, const char* b, const char* e) { if (d && readable_string(b, 512)) d->AddText(p, c, b, e); }
void dl_path_arc_to(ImDrawList* d, const ImVec2& c, float r, float a0, float a1, int seg) { if (d) d->PathArcTo(c, r, a0, a1, seg); }
void dl_path_arc_to_fast(ImDrawList* d, const ImVec2& c, float r, int a0, int a1) { if (d) d->PathArcToFast(c, r, a0, a1); }

// Real signatures for the members 19250 needs that a zeroed stub cannot fake.
ImVec2 impl_content_region_avail() { return ImGui::GetContentRegionAvail(); }
void impl_push_id_str(const char* id)                 { ImGui::PushID(readable_string(id, 128) ? id : "?"); }
void impl_push_id_range(const char* b, const char* e) { if (readable_string(b, 128)) ImGui::PushID(b, e); else ImGui::PushID("?"); }
void impl_push_id_ptr(const void* p)                  { ImGui::PushID(p); }
void impl_push_id_int(int i)                          { ImGui::PushID(i); }
void impl_pop_id()                                    { ImGui::PopID(); }

// 4.7 added key-binding widgets for NRToggleKey and NRScreenshotKey, which put the addon in the
// keyboard block of the table (400-409). Most of those return bool or int, where a zeroed thunk is
// a correct answer -- but GetKeyName returns a POINTER, and answering a pointer request with zero
// hands the addon a null string to print. Forwarding to the real one gives it something valid.
const char* impl_get_key_name(int key) { return ImGui::GetKeyName((ImGuiKey)key); }

// ReShade publishes one table layout PER IMGUI VERSION, and the addon names the one it wants. We
// recorded that argument from the beginning and never used it, handing back a single measured
// layout regardless -- so a 19250 addon called GetContentRegionAvail (slot 80) and got Separator,
// which returns nothing in XMM0 and faulted the page on garbage dimensions.
//
// The legacy column is the layout measured from the older addon; the 19250 column is read off
// ReShade's own imgui_function_table_19250.hpp, so it is correct by construction rather than by
// observation.
// All parsed from the header. 421 members total.
struct Slot { int i; void* fn; };

void build_imgui_table(uint32_t version) {
    const size_t n = std::size(g_imgui_table);
    if (auto* thunks = static_cast<uint8_t*>(make_slot_thunks(n))) {
        for (size_t i = 0; i < n; ++i) g_imgui_table[i] = thunks + i * kSlotThunkSize;
    } else if (void* safe = make_safe_stub()) {
        for (auto& s : g_imgui_table) s = safe;
    } else {
        spdlog::error("[DLSSNR] could not allocate the safe ImGui stub; falling back");
        for (auto& s : g_imgui_table) s = reinterpret_cast<void*>(&imgui_noop);
        fill_stubs(std::make_integer_sequence<int, kSlots>{});
    }
    const Slot kMap[] = {
        {   1, reinterpret_cast<void*>(&addon_ui::style) },
        {  12, reinterpret_cast<void*>(&impl_get_window_draw_list) },
        {  44, reinterpret_cast<void*>(&addon_ui::push_font) },
        {  45, reinterpret_cast<void*>(&addon_ui::pop_font) },
        {  46, reinterpret_cast<void*>(&addon_ui::font) },
        {  47, reinterpret_cast<void*>(&ImGui::GetFontSize) },
        {  49, reinterpret_cast<void*>(&addon_ui::push_color_u32) },
        {  50, reinterpret_cast<void*>(&addon_ui::push_color_vec4) },
        {  51, reinterpret_cast<void*>(&addon_ui::pop_colors) },
        {  59, reinterpret_cast<void*>(&ImGui::PushItemWidth) },
        {  60, reinterpret_cast<void*>(&ImGui::PopItemWidth) },
        {  61, reinterpret_cast<void*>(&ImGui::SetNextItemWidth) },
        {  62, reinterpret_cast<void*>(&ImGui::CalcItemWidth) },
        {  63, reinterpret_cast<void*>(&ImGui::PushTextWrapPos) },
        {  64, reinterpret_cast<void*>(&ImGui::PopTextWrapPos) },
        {  66, reinterpret_cast<void*>(&impl_get_color_u32) },
        {  67, reinterpret_cast<void*>(&addon_ui::color_vec4) },
        {  68, reinterpret_cast<void*>(&addon_ui::color_u32) },
        {  69, reinterpret_cast<void*>(&addon_ui::style_color) },
        {  70, reinterpret_cast<void*>(&impl_get_cursor_screen_pos) },
        {  71, reinterpret_cast<void*>(&ImGui::SetCursorScreenPos) },
        {  72, reinterpret_cast<void*>(&impl_content_region_avail) },
        {  73, reinterpret_cast<void*>(&ImGui::GetCursorPos) },
        {  74, reinterpret_cast<void*>(&ImGui::GetCursorPosX) },
        {  75, reinterpret_cast<void*>(&ImGui::GetCursorPosY) },
        {  76, reinterpret_cast<void*>(&ImGui::SetCursorPos) },
        {  77, reinterpret_cast<void*>(&ImGui::SetCursorPosX) },
        {  78, reinterpret_cast<void*>(&ImGui::SetCursorPosY) },
        {  79, reinterpret_cast<void*>(&ImGui::GetCursorStartPos) },
        {  80, reinterpret_cast<void*>(&impl_separator) },
        {  81, reinterpret_cast<void*>(&impl_same_line) },
        {  82, reinterpret_cast<void*>(&ImGui::NewLine) },
        {  83, reinterpret_cast<void*>(&ImGui::Spacing) },
        {  84, reinterpret_cast<void*>(&ImGui::Dummy) },
        {  85, reinterpret_cast<void*>(&ImGui::Indent) },
        {  86, reinterpret_cast<void*>(&ImGui::Unindent) },
        {  87, reinterpret_cast<void*>(&ImGui::BeginGroup) },
        {  88, reinterpret_cast<void*>(&ImGui::EndGroup) },
        {  89, reinterpret_cast<void*>(&ImGui::AlignTextToFramePadding) },
        {  90, reinterpret_cast<void*>(&ImGui::GetTextLineHeight) },
        {  91, reinterpret_cast<void*>(&ImGui::GetTextLineHeightWithSpacing) },
        {  92, reinterpret_cast<void*>(&impl_get_frame_height) },
        {  93, reinterpret_cast<void*>(&ImGui::GetFrameHeightWithSpacing) },
        {  94, reinterpret_cast<void*>(&impl_push_id_str) },
        {  95, reinterpret_cast<void*>(&impl_push_id_range) },
        {  96, reinterpret_cast<void*>(&impl_push_id_ptr) },
        {  97, reinterpret_cast<void*>(&impl_push_id_int) },
        {  98, reinterpret_cast<void*>(&impl_pop_id) },
        { 103, reinterpret_cast<void*>(&impl_text_unformatted) },
        { 104, reinterpret_cast<void*>(&impl_textv) },
        { 105, reinterpret_cast<void*>(&impl_text_colored) },
        { 106, reinterpret_cast<void*>(&impl_text_disabled) },
        { 107, reinterpret_cast<void*>(&impl_text_wrapped) },
        { 108, reinterpret_cast<void*>(&impl_label_text) },
        { 109, reinterpret_cast<void*>(&impl_bullet_text) },
        { 111, reinterpret_cast<void*>(&impl_button) },
        { 113, reinterpret_cast<void*>(&impl_invisible_button) },
        { 115, reinterpret_cast<void*>(&impl_checkbox) },
        { 129, reinterpret_cast<void*>(&impl_combo) },
        { 130, reinterpret_cast<void*>(&impl_combo) },
        { 144, reinterpret_cast<void*>(&impl_slider) },
        { 218, reinterpret_cast<void*>(&addon_ui::begin_tooltip) },
        { 219, reinterpret_cast<void*>(&addon_ui::end_tooltip) },
        { 220, reinterpret_cast<void*>(&impl_set_tooltip) },
        { 221, reinterpret_cast<void*>(&addon_ui::begin_item_tooltip) },
        { 222, reinterpret_cast<void*>(&impl_set_item_tooltip) },
        { 279, reinterpret_cast<void*>(&addon_ui::begin_disabled) },
        { 280, reinterpret_cast<void*>(&addon_ui::end_disabled) },
        { 287, reinterpret_cast<void*>(&impl_is_item_hovered) },
        { 288, reinterpret_cast<void*>(&impl_is_item_active) },
        { 289, reinterpret_cast<void*>(&ImGui::IsItemFocused) },
        { 301, reinterpret_cast<void*>(&ImGui::GetItemRectMin) },
        { 302, reinterpret_cast<void*>(&ImGui::GetItemRectMax) },
        { 314, reinterpret_cast<void*>(&ImGui::CalcTextSize) },
        { 315, reinterpret_cast<void*>(&ImGui::ColorConvertU32ToFloat4) },
        { 316, reinterpret_cast<void*>(&ImGui::ColorConvertFloat4ToU32) },
        { 317, reinterpret_cast<void*>(&ImGui::ColorConvertRGBtoHSV) },
        { 318, reinterpret_cast<void*>(&ImGui::ColorConvertHSVtoRGB) },
        { 324, reinterpret_cast<void*>(&impl_get_key_name) },
        { 338, reinterpret_cast<void*>(&ImGui::GetMousePos) },
        { 380, reinterpret_cast<void*>(&dl_add_line) },
        { 381, reinterpret_cast<void*>(&dl_add_rect) },
        { 382, reinterpret_cast<void*>(&dl_add_rect_filled) },
        { 387, reinterpret_cast<void*>(&dl_add_tri_filled) },
        { 388, reinterpret_cast<void*>(&dl_add_circle) },
        { 389, reinterpret_cast<void*>(&dl_add_circle_filled) },
        { 394, reinterpret_cast<void*>(&dl_add_text) },
        { 395, reinterpret_cast<void*>(&addon_ui::draw_text) },
        { 404, reinterpret_cast<void*>(&dl_path_arc_to) },
        { 405, reinterpret_cast<void*>(&dl_path_arc_to_fast) },
    };
    for (const auto& e : kMap)
        if (e.i >= 0 && e.i < (int)std::size(g_imgui_table)) g_imgui_table[e.i] = e.fn;
    spdlog::info("[DLSSNR] ImGui table built for version {} ({} members mapped)", version,
                 (int)std::size(kMap));
}

// ---- ReShade object model ----------------------------------------------------------------------
//
// The first version presented objects whose every vtable slot returned the same native pointer.
// That was enough for addon 4.1.5, which the disassembly showed never called a method on them. 4.7
// does: it installs a D3D12 queue submission tracker so its workset pool can recycle, and to do
// that it needs a queue that answers correctly. With the uniform fakes, get_device() on the queue
// returned the QUEUE, the tracker never installed, all four workset generations went busy and the
// addon logged "NR workset pool exhausted; preserving game output" -- neural rendering silently
// stopped contributing while everything else looked healthy.
//
// The layouts below are PARSED from ReShade's own public headers, not guessed:
//
//   api_object    0 get_native, 1 get_private_data, 2 set_private_data
//   device_object : api_object, then 3 get_device
//   device        : api_object, then 3 get_api, 4 check_capability, ...
//   command_queue : device_object, then 4 get_type, 5 wait_idle, ...
//   swapchain     : device_object, then 4 get_back_buffer, ...
//   effect_runtime: device_object, then 4 get_back_buffer, ...
constexpr int kVt = 128;
void* g_dev_vt[kVt]; void* g_queue_vt[kVt]; void* g_swap_vt[kVt]; void* g_runtime_vt[kVt];
void* g_cmdlist_vt[kVt];
void* g_fake_dev[2]{g_dev_vt, nullptr};
void* g_fake_queue[2]{g_queue_vt, nullptr};
void* g_fake_swap[2]{g_swap_vt, nullptr};
void* g_fake_runtime[2]{g_runtime_vt, nullptr};
void* g_fake_cmdlist[2]{g_cmdlist_vt, nullptr};
uint64_t g_native_dev{0}, g_native_queue{0}, g_native_swap{0};
// Rebound per dispatch: one command_list object standing in for whichever list is being executed.
// THREAD-LOCAL, not global. With the lock gone from dispatch_execute_command_list, the game thread
// and the present thread can be inside a dispatch at the same time; a single global here would let
// one stomp the other's list pointer and hand the addon a command list belonging to another thread.
// Write (dispatch) and read (get_native, from inside the addon's callback) are on the same thread.
thread_local uint64_t g_native_cmdlist{0};

// Addons attach per-object state through private data, and answering with garbage is worse than
// answering with nothing. A small table is enough: the addon keeps a handful of entries.
struct PrivateSlot { void* obj; uint8_t guid[16]; uint64_t value; bool used; };
PrivateSlot g_private[32];

uint64_t __fastcall obj_get_native_dev(void*)   { return g_native_dev; }
uint64_t __fastcall obj_get_native_queue(void*) { return g_native_queue; }
uint64_t __fastcall obj_get_native_swap(void*)  { return g_native_swap; }
uint64_t __fastcall obj_get_native_cmdlist(void*) { return g_native_cmdlist; }

void __fastcall obj_get_private(void* self, const uint8_t* guid, uint64_t* out) {
    if (out == nullptr) return;
    *out = 0;
    if (guid == nullptr) return;
    for (const auto& e : g_private)
        if (e.used && e.obj == self && memcmp(e.guid, guid, 16) == 0) { *out = e.value; return; }
}
void __fastcall obj_set_private(void* self, const uint8_t* guid, uint64_t value) {
    if (guid == nullptr) return;
    for (auto& e : g_private)
        if (e.used && e.obj == self && memcmp(e.guid, guid, 16) == 0) { e.value = value; return; }
    for (auto& e : g_private)
        if (!e.used) { e.used = true; e.obj = self; memcpy(e.guid, guid, 16); e.value = value; return; }
}

// The one that mattered: a device_object must hand back a DEVICE, not itself.
void* __fastcall obj_get_device(void*) { return g_fake_dev; }

uint32_t __fastcall dev_get_api(void*)        { return 0xc000; }   // device_api::d3d12
uint32_t __fastcall queue_get_type(void*)     { return 0x1; }      // command_queue_type::graphics
void     __fastcall queue_wait_idle(void*)    {}
uint64_t __fastcall queue_timestamp_freq(void*) { return 0; }
addon_host::Swapchain g_swapchain;
// MSVC x64 virtual member returning a large struct: this=RCX, result=RDX,
// resource=R8, return the result pointer in RAX. A free struct-returning
// function would put the hidden result argument BEFORE this instead.
addon_host::ResourceDesc* __fastcall dev_resource_desc(void*, addon_host::ResourceDesc* out,
                                                       uint64_t resource) {
    std::lock_guard lock(g_mtx);
    if (out) *out = g_swapchain.describe(resource);
    return out;
}
void* __fastcall swap_hwnd(void*) { std::lock_guard lock(g_mtx); return g_swapchain.hwnd(); }
uint64_t __fastcall swap_backbuffer(void*, uint32_t index) {
    std::lock_guard lock(g_mtx); return g_swapchain.buffer(index);
}
uint32_t __fastcall swap_backbuffer_count(void*) {
    std::lock_guard lock(g_mtx); return g_swapchain.count();
}
uint32_t __fastcall swap_current_index(void*) {
    std::lock_guard lock(g_mtx); return g_swapchain.index();
}
bool __fastcall swap_supports_color(void*, uint32_t color) {
    std::lock_guard lock(g_mtx); return g_swapchain.supports_color(color);
}
// DXGI has no current-color-space getter. Until SetColorSpace1 is tracked,
// report unknown, not an HDR/SDR guess based on the backbuffer format.
uint32_t __fastcall swap_color_space(void*) { return 0; }

// Anything not modelled returns zero in both RAX and XMM0, so a member returning a struct or a
// float cannot corrupt the caller's frame -- the same reason the ImGui stubs are hand-assembled.
void* make_safe_stub();

template <int... I> void fill_vt(void** vt, void* safe, std::integer_sequence<int, I...>) {
    ((vt[I] = safe), ...);
}

void fill_vts(...) {
    void* safe = make_safe_stub();
    for (int i = 0; i < kVt; ++i) {
        g_dev_vt[i] = g_queue_vt[i] = g_swap_vt[i] = g_runtime_vt[i] = g_cmdlist_vt[i] = safe;
    }
    g_cmdlist_vt[0] = reinterpret_cast<void*>(&obj_get_native_cmdlist);
    // api_object, shared by all four
    g_dev_vt[0]     = reinterpret_cast<void*>(&obj_get_native_dev);
    g_queue_vt[0]   = reinterpret_cast<void*>(&obj_get_native_queue);
    g_swap_vt[0]    = reinterpret_cast<void*>(&obj_get_native_swap);
    g_runtime_vt[0] = reinterpret_cast<void*>(&obj_get_native_swap);
    for (void** vt : { g_dev_vt, g_queue_vt, g_swap_vt, g_runtime_vt, g_cmdlist_vt }) {
        vt[1] = reinterpret_cast<void*>(&obj_get_private);
        vt[2] = reinterpret_cast<void*>(&obj_set_private);
    }
    // device_object::get_device on everything except the device itself
    for (void** vt : { g_queue_vt, g_swap_vt, g_runtime_vt, g_cmdlist_vt })
        vt[3] = reinterpret_cast<void*>(&obj_get_device);

    g_dev_vt[3]   = reinterpret_cast<void*>(&dev_get_api);
    g_dev_vt[10]  = reinterpret_cast<void*>(&dev_resource_desc);
    g_queue_vt[4] = reinterpret_cast<void*>(&queue_get_type);
    g_queue_vt[5] = reinterpret_cast<void*>(&queue_wait_idle);
    g_queue_vt[12] = reinterpret_cast<void*>(&queue_timestamp_freq);
    g_swap_vt[4]  = reinterpret_cast<void*>(&swap_hwnd);
    g_swap_vt[5]  = reinterpret_cast<void*>(&swap_backbuffer);
    g_swap_vt[6]  = reinterpret_cast<void*>(&swap_backbuffer_count);
    g_swap_vt[7]  = reinterpret_cast<void*>(&swap_current_index);
    g_swap_vt[8]  = reinterpret_cast<void*>(&swap_supports_color);
    g_swap_vt[9]  = reinterpret_cast<void*>(&swap_color_space);
    spdlog::info("[DLSSNR] ReShade object model built from the published vtable layouts");
}

using PresentFn = void (*)(void*, void*, const int32_t*, const int32_t*, uint32_t, const void*);
using ExecuteListFn = void (*)(void*, void*);          // (command_queue*, command_list*)

bool g_lifecycle_ready{}, g_lifecycle_failed{}, g_queue_started{}, g_swap_started{};
uint32_t g_resources_started{};
bool g_lifecycle_resize{};

std::vector<void*> lifecycle_callbacks(uint32_t id) {
    std::lock_guard lock(g_mtx);
    for (const auto& event : g_events) if (event.id == id) return event.fns;
    return {};
}
bool invoke_lifecycle(void* fn, uint32_t id, const addon_host::ResourceDesc* desc,
                      uint64_t resource, bool resize) {
    __try {
        switch (id) {
        case 4: case 5:
            reinterpret_cast<void(*)(void*)>(fn)(g_fake_queue); break;
        case 6: case 8:
            reinterpret_cast<void(*)(void*, bool)>(fn)(g_fake_swap, resize); break;
        case 14:
            reinterpret_cast<void(*)(void*, const addon_host::ResourceDesc&, const void*, uint32_t, uint64_t)>(fn)
                (g_fake_dev, *desc, nullptr, 0x80000804u, resource); break;
        case 16:
            reinterpret_cast<void(*)(void*, uint64_t)>(fn)(g_fake_dev, resource); break;
        default: return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool dispatch_lifecycle(uint32_t id, const addon_host::ResourceDesc* desc = nullptr,
                        uint64_t resource = 0, bool resize = false) {
    bool ok = true;
    for (void* fn : lifecycle_callbacks(id)) {
        if (!invoke_lifecycle(fn, id, desc, resource, resize)) {
            spdlog::error("[DLSSNR-LIFECYCLE] event {} callback {} faulted; NR dispatch disabled", id, fn);
            ok = false;
        }
    }
    return ok;
}
bool initialize_lifecycle() {
    // The earlier DLSS5 addon did not request this lifecycle. Scope the new
    // dispatch to the API18 controller whose interface was audited.
    if (g_api_version != 18 || strcmp(g_addon_name, "RenoDX DLSS") != 0) return true;
    if (g_lifecycle_failed) return false;
    if (g_lifecycle_ready) return true;
    if (!g_native_queue || g_swapchain.count() == 0) return false;
    g_queue_started = true;
    bool ok = dispatch_lifecycle(4);
    for (uint32_t i = 0; ok && i < g_swapchain.count(); ++i) {
        const auto handle = g_swapchain.buffer(i);
        const auto desc = g_swapchain.describe(handle);
        g_resources_started = i + 1;
        ok = desc.type != 0 && dispatch_lifecycle(14, &desc, handle);
    }
    if (ok) {
        g_swap_started = true;
        ok = dispatch_lifecycle(6, nullptr, 0, g_lifecycle_resize);
    }
    g_lifecycle_ready = ok;
    g_lifecycle_failed = !ok;
    spdlog::info("[DLSSNR-LIFECYCLE] initialized={} buffers={} resize={}",
                 ok, g_resources_started, g_lifecycle_resize);
    return ok;
}
void destroy_lifecycle(bool resize) {
    if (g_swap_started) dispatch_lifecycle(8, nullptr, 0, resize);
    for (uint32_t i = g_resources_started; i > 0; --i)
        dispatch_lifecycle(16, nullptr, g_swapchain.buffer(i - 1));
    if (g_queue_started) dispatch_lifecycle(5);
    g_swap_started = g_queue_started = g_lifecycle_ready = false;
    g_resources_started = 0;
    // A callback fault stays disabled for the session, including across reset.
    g_lifecycle_resize = resize;
}

bool invoke_execute_guarded(ExecuteListFn fn) {
    __try { fn(g_fake_queue, g_fake_cmdlist); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- addon_event::execute_command_list (72) ----------------------------------------------------
//
// Needed by addons that put their own work on the game's command lists rather than only reading the
// finished frame. CheekyFoveatedDLSS subscribes to exactly two events -- present and this one -- so
// without it such an addon registers, draws its page, and never does anything.
//
// The only way to know a list is being submitted is to watch the queue, so ExecuteCommandLists
// (ID3D12CommandQueue vtable slot 10: IUnknown 0-2, ID3D12Object 3-6, DeviceChild 7, then
// UpdateTileMappings, CopyTileMappings, ExecuteCommandLists) is intercepted per instance -- the same
// copy-the-vtable-and-point-this-object-at-it technique already used for the NGX parameter block.
constexpr int kExecuteListsSlot = 10;
void* g_queue_orig_vt[32]{};
void* g_queue_our_vt[32]{};
void* g_hooked_queue{nullptr};
bool  g_exec_event_faulted{false};
std::atomic<uint64_t> g_exec_dispatches{0};

using ExecuteCommandListsFn = void(__stdcall*)(void*, UINT, void* const*);

// NO LOCK ON THIS PATH. ExecuteCommandLists is the hottest call in the renderer, and taking g_mtx
// here put every submission behind the same mutex the config store and the addon's own
// get_config_value use -- which stalled a save load into unusable churn. The subscriber list is
// written once at registration and read thousands of times a second, so it is snapshotted into a
// fixed array and guarded by an atomic count: zero subscribers costs one relaxed load.
constexpr int kMaxExecSubs = 8;
void* g_exec_subs[kMaxExecSubs]{};
std::atomic<int> g_exec_sub_count{0};

void rebuild_exec_subscribers() {           // called under g_mtx, off the hot path
    int n = 0;
    for (const auto& e : g_events) {
        if (e.id != 72) continue;
        for (void* fn : e.fns) {
            if (n < kMaxExecSubs) g_exec_subs[n++] = fn;
        }
    }
    g_exec_sub_count.store(n, std::memory_order_release);
}

void dispatch_execute_command_list(UINT count, void* const* lists) {
    const int subs = g_exec_sub_count.load(std::memory_order_acquire);
    if (subs == 0 || g_exec_event_faulted || lists == nullptr) return;
    if (!DlssNeuralRendering::get()->exec_event_enabled()) return;

    for (UINT i = 0; i < count; ++i) {
        if (lists[i] == nullptr) continue;
        g_native_cmdlist = reinterpret_cast<uint64_t>(lists[i]);
        for (int k = 0; k < subs; ++k) {
            if (!invoke_execute_guarded(reinterpret_cast<ExecuteListFn>(g_exec_subs[k]))) {
                g_exec_event_faulted = true;
                spdlog::error("[DLSSNR] execute_command_list callback faulted; disabled");
                return;
            }
        }
    }
    g_exec_dispatches.fetch_add(1, std::memory_order_relaxed);
}

void __stdcall hooked_execute_command_lists(void* queue, UINT count, void* const* lists) {
    dispatch_execute_command_list(count, lists);
    reinterpret_cast<ExecuteCommandListsFn>(g_queue_orig_vt[kExecuteListsSlot])(queue, count, lists);
}

int copy_queue_vtable(void* q) {                       // POD only; SEH cannot share a C++ frame
    __try {
        void** vt = *reinterpret_cast<void***>(q);
        if (vt == nullptr) return 1;
        if (vt == g_queue_our_vt) return 2;
        memcpy(g_queue_orig_vt, vt, sizeof(g_queue_orig_vt));
        memcpy(g_queue_our_vt, vt, sizeof(g_queue_our_vt));
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
}

void hook_queue_execute(void* q) {
    if (q == nullptr || q == g_hooked_queue) return;
    const int r = copy_queue_vtable(q);
    if (r == 2) { g_hooked_queue = q; return; }
    if (r != 0) {
        spdlog::error("[DLSSNR] cannot read the command queue vtable ({})", r);
        g_hooked_queue = q;
        return;
    }
    g_queue_our_vt[kExecuteListsSlot] = reinterpret_cast<void*>(&hooked_execute_command_lists);
    *reinterpret_cast<void***>(q) = g_queue_our_vt;
    g_hooked_queue = q;
    spdlog::info("[DLSSNR] watching ExecuteCommandLists on queue {} for addon_event 72", q);
}

// SEH cannot share a frame with C++ unwinding, hence the separate functions. The guard is the
// point: this runs on the present thread every frame, calling a third-party binary through an
// interface we are approximating. A fault without it is a hard crash on every subsequent frame.
bool invoke_present_guarded(PresentFn fn) {
    __try { fn(g_fake_queue, g_fake_swap, nullptr, nullptr, 0, nullptr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Preserve the actual fault site. The last stub is only a breadcrumb: an addon
// may call supported functions between that stub and the instruction that faults.
int report_overlay_exception(EXCEPTION_POINTERS* ep) {
    if (ep == nullptr || ep->ExceptionRecord == nullptr) return EXCEPTION_EXECUTE_HANDLER;
    const auto* record = ep->ExceptionRecord;
    HMODULE module = nullptr;
    char path[MAX_PATH]{};
    const auto address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCSTR>(record->ExceptionAddress), &module);
    if (module != nullptr) GetModuleFileNameA(module, path, MAX_PATH);
    const auto offset = module ? address - reinterpret_cast<uintptr_t>(module) : 0;
    const bool memory_fault = (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
                               record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
                              record->NumberParameters >= 2;
    spdlog::error("[DLSSNR-UI-FAULT] code={:#x} address={:#x} module={} offset={:#x} "
                  "memory_fault={} access={} target={:#x} last_stub={}",
                  record->ExceptionCode, address, module ? path : "unknown", offset,
                  memory_fault, memory_fault ? record->ExceptionInformation[0] : 0,
                  memory_fault ? record->ExceptionInformation[1] : 0,
                  static_cast<uint32_t>(g_last_imgui_slot));
    return EXCEPTION_EXECUTE_HANDLER;
}

bool invoke_overlay_guarded(void (*fn)(void*)) {
    g_last_imgui_slot = 0xFFFFFFFFu;
    addon_ui::begin_font_callback();
    const auto depth = addon_ui::font_scope_depth();
    const auto colors = addon_ui::color_depth;
    const auto disabled = addon_ui::disabled_depth;
    bool ok = false;
    __try { fn(g_fake_runtime); ok = true; }
    __except (report_overlay_exception(GetExceptionInformation())) { ok = false; }
    addon_ui::end_tooltip(); // close an addon-owned tooltip left open by a fault
    addon_ui::restore_disabled(disabled);
    addon_ui::restore_font_scopes(depth);
    addon_ui::restore_colors(colors);
    return ok;
}
bool invoke_init_device_guarded(void (*fn)(void*)) {
    __try { fn(g_fake_dev); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// ReShade calls an addon's exported AddonInit(addon, reshade) after loading it, and an addon may do
// ALL of its real setup there. RenoDX's does everything in DllMain, so this was never needed and
// never noticed -- CheekyFoveatedDLSS registered, then subscribed to nothing and logged nothing,
// because its overlay and both event registrations live behind this call.
using AddonInitFn = bool (*)(HMODULE, HMODULE);
using AddonUninitFn = void (*)(HMODULE, HMODULE);

bool invoke_addon_init_guarded(AddonInitFn fn, HMODULE addon, HMODULE self, bool* ok) {
    __try { *ok = fn(addon, self); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool invoke_addon_uninit_guarded(AddonUninitFn fn, HMODULE addon, HMODULE self) {
    __try { fn(addon, self); return true; }
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
std::atomic<uint32_t> g_built_in_w{0}, g_built_in_h{0}, g_built_out_w{0}, g_built_out_h{0};
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


// NASAL-OPEN SLAB, ported from the CyberpunkVR port's solution to the same seam.
//
// A centred box has four visible edges. A full-height slab anchored to the NASAL side has one: top
// and bottom reach the image border, and the nasal edge reaches the binocular centre where the other
// eye is treated anyway. The single remaining edge lies in the temporal periphery -- the monocular
// crescent only one eye sees -- so there is no binocular rivalry and no seam in the region actually
// being looked at.
//
// Left eye keeps the RIGHT portion, right eye the LEFT, because the nose is on the inner side of
// each eye's field. Convergence is meaningless here and is ignored: the slab already spans the whole
// overlap, which is what the convergence offset was trying to achieve for a box.
//
// It buys less than a box of the same fraction -- 65% of the width against 12% of the area at 0.35
// -- and that is the price of it being seamless where it counts.
// Nasal anchoring needs to know which eye it is, and in UEVR nothing does. Both eyes share one
// Colour/Output pair with identical subrects, so parity is the only available signal -- and parity
// has already drifted once in this file. The CyberpunkVR port can mirror safely because it
// attributes MAIN vs VRCAM from the outer scope; we cannot.
//
// A CENTRED full-height slab needs no attribution: both eyes get the identical region, so the two
// edges land in the same place in both and there is nothing for the eyes to disagree about. Two
// peripheral edges instead of one, but neither can produce binocular rivalry, which is what makes
// a boundary conspicuous.
// One region routine for both presets.
//
// THE OUTPUT PLANE IS THE REFERENCE and every other plane is derived from it as a FRACTION, never
// aligned independently. Aligning each plane to 8 on its own broke upscaling mode: with colour at
// 848x864 against a 2544x2592 output the 3x relationship stopped holding (302 against 904/3), the
// snippet rejected the evaluation as an "Invalid Color/Output rect configuration" -- silently, as
// it always does -- and neural rendering disappeared along with the visible region.
//
// CONVERGENCE IS MIRRORED PER EYE and applies to both presets. A region at identical coordinates in
// both eyes covers a DIFFERENT piece of the world in each, so it reads as one box per eye rather
// than a single fused one. That is not a stereo-consistency win; it is the thing convergence exists
// to correct, and dropping it from the centre box was a regression.
void apply_region(void* params, float coverage, float height_frac,
                  bool nasal, bool swap_eyes) {
    const Rect out = read_subrect(params, "Output");
    if (!out.valid) return;

    // THE INPUT PLANE IS THE REFERENCE, NOT THE OUTPUT. Previously the region was chosen in Output
    // space and every other plane derived from it by rounded fractions. At scale 1 that is fine, but
    // under upscaling (colour 848x864 into output 2544x2592) rounding each plane independently
    // yields a Color/Output pair whose ratio is not exactly the scale DLSS was created with, and the
    // snippet rejects the evaluation SILENTLY -- which is why the region had to be abandoned in
    // upscaling mode and why "neural upscaling is broke".
    //
    // CheekyFoveatedDLSS derives it the other way and it is the correct way: choose the rect in
    // input space, then map it to the output with FLOOR on the start and CEIL on the end, so the
    // output rect always fully covers the input region instead of landing a fraction of a pixel
    // inside it. At scale 1 this reduces to the exact integers used before, so the working
    // full-resolution path is unchanged.
    const Rect col = read_subrect(params, "Color");
    const Rect ref = col.valid ? col : out;
    if (col.valid && (col.w != out.w || col.h != out.h)) {
        static std::atomic<bool> s_said{false};
        bool e = false;
        if (s_said.compare_exchange_strong(e, true))
            spdlog::info("[DLSSNR-FOV] upscaling active (colour {}x{} -> output {}x{}); deriving the "
                         "output rect by floor/ceil from the input", col.w, col.h, out.w, out.h);
    }

    // ASK UEVR WHICH EYE THIS IS. Nasal anchoring is mirrored, so getting the eye backwards puts
    // the untreated strip over the middle of the fused image instead of the temples -- which is what
    // "everything OUTSIDE the box was rendered" was. Deriving it from evaluation parity meant the
    // phase could start on either eye, so a user had to flip a Swap toggle by trial per game. That
    // is not something anyone should have to discover.
    //
    // In alternate-frame rendering UEVR already knows: m_frame_count % 2 against the interval it
    // established. This title evaluates NR once per frame on a single-eye 2544x2592 target, which
    // is exactly that mode. Parity remains only as a fallback if AFR is not in use.
    const uint64_t parity = g_eval_parity.fetch_add(1, std::memory_order_relaxed);
    bool left_eye;
    {
        auto& vr = VR::get();
        if (vr != nullptr && vr->is_using_afr()) {
            left_eye = vr->is_left_eye();
            static std::atomic<bool> s_said{false};
            bool e = false;
            if (s_said.compare_exchange_strong(e, true))
                spdlog::info("[DLSSNR-FOV] eye identity from UEVR (alternate-frame rendering)");
        } else {
            // TOW2 is NOT alternate-frame (RenderingMethod 0), so is_left_eye() means nothing here
            // and skipping outright removed the region entirely -- a worse outcome than the thing it
            // was guarding against. Both eyes are evaluated within one frame, so their order alone
            // identifies them; only the starting phase is unknown, which is what Swap eyes settles
            // once per game.
            left_eye = (parity & 1ull) == 0ull;
            static std::atomic<bool> s_said{false};
            bool e = false;
            if (s_said.compare_exchange_strong(e, true))
                spdlog::info("[DLSSNR-FOV] not alternate-frame rendering; eye identity from "
                             "evaluation order -- use Swap eyes if the untreated area is central");
        }
    }
    if (swap_eyes) left_eye = !left_eye;
    g_last_first_eye.store(left_eye, std::memory_order_relaxed);

    uint32_t w = align8_down(static_cast<uint32_t>(ref.w * coverage));
    uint32_t h = align8_down(static_cast<uint32_t>(ref.h * height_frac));
    if (w < 64 || w > ref.w || h < 64 || h > ref.h) return;

    // Horizontal placement: nasal-anchored, or centred and converged toward the nose.
    int x;
    if (nasal) {
        x = left_eye ? static_cast<int>(ref.w - w) : 0;
    } else {
        // NO PER-EYE SHIFT. The CyberpunkVR port offsets centre boxes by 4% of eye width, mirrored,
        // to align them on the same WORLD region. That is right when the region's boundary is
        // invisible; ours is a hard edge, and 4% puts the two edges 202 px apart on a 2544 px eye --
        // roughly 8% of the FOV, far outside fusion range -- so the border is perceived twice and
        // the user sees two boxes with a seam. Identical image coordinates in both eyes fuse as a
        // single window, which is the whole point of a centre box.
        //
        // This is deliberately NOT a setting. It was one, defaulted to 0.04, and because config.txt
        // already carried that key a changed default was inert -- three runs were spent testing a
        // value that never moved. Re-introduce a shift only together with a feathered edge, which is
        // what stops the boundary being a fusable feature at all.
        x = static_cast<int>(align8_down((ref.w - w) / 2));
    }
    const uint32_t y = align8_down((ref.h - h) / 2);

    for (const char* plane : kRewritePlanes) {
        const Rect r = read_subrect(params, plane);
        if (!r.valid) continue;

        uint32_t px, py, pw, ph;
        if (r.w == ref.w && r.h == ref.h) {
            px = r.x + static_cast<uint32_t>(x); pw = w;      // same scale: exact, no rounding
            py = r.y + y;                        ph = h;
        } else {
            const double sx = static_cast<double>(r.w) / ref.w;
            const double sy = static_cast<double>(r.h) / ref.h;
            const uint32_t bx = static_cast<uint32_t>(std::floor(x * sx));
            const uint32_t ex = std::min<uint32_t>(r.w, static_cast<uint32_t>(std::ceil((x + w) * sx)));
            const uint32_t by = static_cast<uint32_t>(std::floor(y * sy));
            const uint32_t ey = std::min<uint32_t>(r.h, static_cast<uint32_t>(std::ceil((y + h) * sy)));
            px = r.x + bx; pw = ex > bx ? ex - bx : 0u;
            py = r.y + by; ph = ey > by ? ey - by : 0u;
        }
        if (pw < 8 || ph < 8) continue;
        if (px + pw > r.x + r.w) pw = r.x + r.w - px;
        if (py + ph > r.y + r.h) ph = r.y + r.h - py;

        // Under upscaling the evaluation was rejected with 0xbad00005 and the reason is not yet
        // known. Log the exact quadruple written to every plane for the first few applications so
        // the next run answers it instead of another hypothesis.
        static std::atomic<uint64_t> s_upscale_dumps{0};
        if (ref.w != out.w && s_upscale_dumps.fetch_add(1, std::memory_order_relaxed) < 16)
            spdlog::info("[DLSSNR-FOV] upscale plane {}: wrote ({},{}) {}x{} into {}x{} (was ({},{}) {}x{})",
                         plane, px, py, pw, ph, r.w, r.h, r.x, r.y, r.w, r.h);

        char n[64];
        subrect_name(n, plane, "BaseX");  nr_set_uint(params, n, px);
        subrect_name(n, plane, "Width");  nr_set_uint(params, n, pw);
        subrect_name(n, plane, "BaseY");  nr_set_uint(params, n, py);
        subrect_name(n, plane, "Height"); nr_set_uint(params, n, ph);

        if (strcmp(plane, "Output") == 0) {
            g_nr_box_x.store(px, std::memory_order_relaxed);
            g_nr_box_y.store(py, std::memory_order_relaxed);
            g_nr_box_w.store(pw, std::memory_order_relaxed);
            g_nr_box_h.store(ph, std::memory_order_relaxed);
            g_last_plane_w.store(r.w, std::memory_order_relaxed);
            const uint64_t k = g_box_applies.fetch_add(1, std::memory_order_relaxed) + 1;
            if ((k % 601) == 0 || (k % 601) == 1)
                spdlog::info("[DLSSNR-FOV] region {}: {} {} eye ({},{}) {}x{} of {}x{} = {}%",
                             k, nasal ? "nasal" : "centred", left_eye ? "left" : "right",
                             px, py, pw, ph, r.w, r.h,
                             (100ull * pw * ph) / (1ull * r.w * r.h));
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

// ---- the ring blend ----------------------------------------------------------------------------
//
// The seam is where NR'd pixels meet untreated ones. This fades between them across a ring just
// inside the box: at the boundary the pixel is pure Colour, and it reaches pure NR `feather` pixels
// further in. The centre is not touched at all, so NR's own result is preserved where it matters.
//
// It has to run after NR writes Output and before the addon reads it. Close is too late -- proven
// with 7,800 box overwrites that neural rendering survived -- so it goes immediately before the
// first Dispatch recorded after an evaluation, which the command probe showed is always the addon's
// full-plane decode (159x162 groups of 16x16 = 2544x2592, first op, every time).
//
// BOTH resources are bound as UAVs rather than SRV+UAV. That avoids transitioning Colour to a
// shader-read state and back, which would be two more barriers against a state we can only assume.
const char* kBlendHLSL = R"(
RWTexture2D<float4> src : register(u0);   // Colour, pre-NR
RWTexture2D<float4> dst : register(u1);   // Output, post-NR
cbuffer C : register(b0) { uint4 box; uint feather; uint debugMark; uint2 pad; };
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= box.z || id.y >= box.w) return;
    uint dx = min(id.x, box.z - 1 - id.x);
    uint dy = min(id.y, box.w - 1 - id.y);
    float d = (float)min(dx, dy);
    float f = max((float)feather, 1.0);
    if (d >= f) return;                             // untouched centre: NR as written
    float t = saturate(d / f);
    t = t * t * (3.0 - 2.0 * t);
    uint2 p = uint2(box.x + id.x, box.y + id.y);
    if (debugMark != 0) { dst[p] = float4(1,0,0,1); return; }   // unmistakable: did our write survive?
    dst[p] = lerp(src[p], dst[p], t);               // edge -> Colour, inward -> NR
}
)";

ID3D12RootSignature*  g_blend_rs{nullptr};
ID3D12PipelineState*  g_blend_pso{nullptr};
ID3D12DescriptorHeap* g_blend_heap{nullptr};
UINT g_blend_inc{0};
ID3D12Resource* g_blend_bound[2][2]{};        // [eye][0]=colour [1]=output currently described
bool g_blend_failed{false};
bool g_blend_ready{false};
std::atomic<uint64_t> g_blends{0};

void release_blend() {
    if (g_blend_pso)  { g_blend_pso->Release();  g_blend_pso = nullptr; }
    if (g_blend_rs)   { g_blend_rs->Release();   g_blend_rs = nullptr; }
    if (g_blend_heap) { g_blend_heap->Release(); g_blend_heap = nullptr; }
    memset(g_blend_bound, 0, sizeof(g_blend_bound));
    g_blend_ready = false;
}

bool init_blend(ID3D12Device* dev) {
    if (g_blend_ready) return true;
    if (g_blend_failed || dev == nullptr) return false;

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 2;
    range.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &range;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.Num32BitValues = 8;
    params[1].Constants.ShaderRegister = 0;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 2;
    rsd.pParameters = params;

    ID3DBlob* sig = nullptr; ID3DBlob* err = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
        spdlog::error("[DLSSNR-FOV] blend root signature failed: {}",
                      err ? (const char*)err->GetBufferPointer() : "?");
        if (err) err->Release();
        g_blend_failed = true;
        return false;
    }
    if (err) { err->Release(); err = nullptr; }
    HRESULT hr = dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                          IID_PPV_ARGS(&g_blend_rs));
    sig->Release();
    if (FAILED(hr)) { spdlog::error("[DLSSNR-FOV] CreateRootSignature failed"); g_blend_failed = true; return false; }

    ID3DBlob* cs = nullptr;
    hr = D3DCompile(kBlendHLSL, strlen(kBlendHLSL), nullptr, nullptr, nullptr, "main", "cs_5_0",
                    0, 0, &cs, &err);
    if (FAILED(hr)) {
        spdlog::error("[DLSSNR-FOV] blend shader failed: {}",
                      err ? (const char*)err->GetBufferPointer() : "?");
        if (err) err->Release();
        g_blend_failed = true;
        return false;
    }
    if (err) { err->Release(); err = nullptr; }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = g_blend_rs;
    pd.CS.pShaderBytecode = cs->GetBufferPointer();
    pd.CS.BytecodeLength = cs->GetBufferSize();
    hr = dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&g_blend_pso));
    cs->Release();
    if (FAILED(hr)) { spdlog::error("[DLSSNR-FOV] blend PSO failed"); g_blend_failed = true; return false; }

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 4;                     // two per eye
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_blend_heap)))) {
        spdlog::error("[DLSSNR-FOV] blend descriptor heap failed");
        g_blend_failed = true;
        return false;
    }
    g_blend_inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_blend_ready = true;
    spdlog::info("[DLSSNR-FOV] ring blend ready");
    return true;
}

bool describe_pair(ID3D12Device* dev, int eye, ID3D12Resource* colour, ID3D12Resource* output) {
    if (g_blend_bound[eye][0] == colour && g_blend_bound[eye][1] == output) return true;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    const D3D12_RESOURCE_DESC cd = colour->GetDesc();
    uav.Format = cd.Format;
    auto h = g_blend_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(eye) * 2 * g_blend_inc;
    dev->CreateUnorderedAccessView(colour, nullptr, &uav, h);
    h.ptr += g_blend_inc;
    dev->CreateUnorderedAccessView(output, nullptr, &uav, h);
    g_blend_bound[eye][0] = colour;
    g_blend_bound[eye][1] = output;
    spdlog::info("[DLSSNR-FOV] blend descriptors for eye {} -> colour {} output {} fmt {}",
                 eye, (void*)colour, (void*)output, (int)cd.Format);
    return true;
}

// POD-only so it can carry the SEH guard.
int record_blend(ID3D12GraphicsCommandList* list, int eye,
                 uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh, uint32_t feather,
                 ID3D12Resource* output, uint32_t dbg) {
    __try {
        D3D12_RESOURCE_BARRIER uavb{};
        uavb.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavb.UAV.pResource = output;                  // NR's writes must land before we read them
        list->ResourceBarrier(1, &uavb);

        ID3D12DescriptorHeap* heaps[1] = { g_blend_heap };
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(g_blend_rs);
        list->SetPipelineState(g_blend_pso);
        auto g = g_blend_heap->GetGPUDescriptorHandleForHeapStart();
        g.ptr += static_cast<UINT64>(eye) * 2 * g_blend_inc;
        list->SetComputeRootDescriptorTable(0, g);
        const UINT c[8] = { bx, by, bw, bh, feather, dbg, 0, 0 };
        list->SetComputeRoot32BitConstants(1, 8, c, 0);
        list->Dispatch((bw + 7) / 8, (bh + 7) / 8, 1);

        list->ResourceBarrier(1, &uavb);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}

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
constexpr int kSetPSOSlot      = 25;
constexpr int kSetHeapsSlot    = 28;
constexpr int kSetComputeRSSlot= 29;

std::atomic<int> g_ops_after_eval{999};      // 999 = not armed
bool g_blend_pending{false};
int  g_blend_seen{0};
int  g_blend_eye{0};
uint32_t g_blend_box[4]{};
ID3D12Resource* g_blend_output{nullptr};
ID3D12GraphicsCommandList* g_blend_list{nullptr};
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

void blend_here_if_pending(ID3D12GraphicsCommandList* l, const char* site);

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
// Blend at the EARLIEST command after an evaluation, not at the decode dispatch.
//
// Inserting before the dispatch meant inserting after the addon had already bound its pipeline
// state, root signature and descriptor heaps -- and D3D12 offers no way to save or restore those,
// so its decode ran against ours and both eyes froze on stale images. Going in before the addon
// binds anything means it rebinds everything afterwards and never sees our state at all.
// The insertion point is a SEARCH, not a guess. Close is too late, the first state-setting call is
// too early (it is NGX's own, so NR overwrites us), and nothing the addon does announces the gap
// between NR's last write and its decode. So every eligible command after an evaluation is counted
// and the blend fires on the Nth -- sweep N live with the red debug ring on, and the value where
// red appears is the window. If red never appears at any N, no such window is reachable from here.
void blend_here_if_pending(ID3D12GraphicsCommandList* l, const char* site) {
    if (t_our_own_commands || !g_blend_pending) return;
    if (l != g_blend_list) return;              // never another list -- the vtable is shared
    if (!DlssNeuralRendering::get()->blend_enabled()) return;
    const int want = DlssNeuralRendering::get()->blend_slot();
    if (g_blend_seen++ < want) return;
    g_blend_pending = false;
    t_our_own_commands = true;
    const int r = record_blend(l, g_blend_eye, g_blend_box[0], g_blend_box[1], g_blend_box[2],
                               g_blend_box[3], DlssNeuralRendering::get()->blend_feather_px(),
                               g_blend_output,
                               DlssNeuralRendering::get()->blend_debug() ? 1u : 0u);
    t_our_own_commands = false;
    if (r == 0) {
        const auto n = g_blends.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n == 1 || (n % 600) == 0)
            spdlog::info("[DLSSNR-FOV] ring blend #{} at slot {} ({})", n, want, site);
    } else {
        static std::atomic<bool> s_said{false};
        bool e = false;
        if (s_said.compare_exchange_strong(e, true))
            spdlog::error("[DLSSNR-FOV] ring blend FAULTED while recording; disabling");
        g_blend_failed = true;
    }
}

using SetPSOFn      = void(__stdcall*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
using SetHeapsFn    = void(__stdcall*)(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap* const*);
using SetComputeRSFn= void(__stdcall*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);

void __stdcall hooked_set_pso(ID3D12GraphicsCommandList* l, ID3D12PipelineState* pso) {
    blend_here_if_pending(l, "SetPipelineState");
    reinterpret_cast<SetPSOFn>(g_list_orig_vt[kSetPSOSlot])(l, pso);
}
void __stdcall hooked_set_heaps(ID3D12GraphicsCommandList* l, UINT n, ID3D12DescriptorHeap* const* h) {
    blend_here_if_pending(l, "SetDescriptorHeaps");
    reinterpret_cast<SetHeapsFn>(g_list_orig_vt[kSetHeapsSlot])(l, n, h);
}
void __stdcall hooked_set_compute_rs(ID3D12GraphicsCommandList* l, ID3D12RootSignature* rs) {
    blend_here_if_pending(l, "SetComputeRootSignature");
    reinterpret_cast<SetComputeRSFn>(g_list_orig_vt[kSetComputeRSSlot])(l, rs);
}

void __stdcall hooked_dispatch(ID3D12GraphicsCommandList* l, UINT x, UINT y, UINT z) {
    if (op_should_log()) spdlog::info("[DLSSNR-FOV][op] Dispatch {}x{}x{}", x, y, z);
    // Fallback only: if the addon bound its state before evaluating, none of the setters above
    // fired and this is the last chance -- at the cost of the clobber this change exists to avoid.
    blend_here_if_pending(l, "Dispatch");
    reinterpret_cast<DispatchFn>(g_list_orig_vt[kDispatchSlot])(l, x, y, z);
}
void __stdcall hooked_copy_tex(ID3D12GraphicsCommandList* l, const D3D12_TEXTURE_COPY_LOCATION* d,
                               UINT x, UINT y, UINT z, const D3D12_TEXTURE_COPY_LOCATION* s,
                               const D3D12_BOX* box) {
    if (op_should_log())
        log_op("CopyTextureRegion", d ? d->pResource : nullptr, s ? s->pResource : nullptr);
    blend_here_if_pending(l, "CopyTextureRegion");
    reinterpret_cast<CopyTexFn>(g_list_orig_vt[kCopyTexSlot])(l, d, x, y, z, s, box);
}
void __stdcall hooked_copy_res(ID3D12GraphicsCommandList* l, ID3D12Resource* d, ID3D12Resource* s) {
    if (op_should_log()) log_op("CopyResource", d, s);
    blend_here_if_pending(l, "CopyResource");
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
        g_list_our_vt[kSetPSOSlot]       = reinterpret_cast<void*>(&hooked_set_pso);
        g_list_our_vt[kSetHeapsSlot]     = reinterpret_cast<void*>(&hooked_set_heaps);
        g_list_our_vt[kSetComputeRSSlot] = reinterpret_cast<void*>(&hooked_set_compute_rs);
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
        g_nr_last_eval_ms.store(GetTickCount64(), std::memory_order_relaxed);
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
            // Box first: the periphery copy needs to know where the region is so it can avoid it.
            //
            // Center Box applies the percentage to BOTH axes and is identical in both eyes, so the
            // two views agree. Stereo Slab keeps full height and anchors toward the nose per eye.
            const float pct = mod->preset_percent();
            const bool slab = mod->preset_is_slab();
            apply_region(params, pct, slab ? 1.0f : pct, /*nasal=*/slab, mod->swap_eyes());
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
            if (mod->blend_enabled()) {
                ID3D12Resource* c = recorded_resource("color");
                ID3D12Resource* o = recorded_resource("out");
                auto* dev = reinterpret_cast<ID3D12Device*>(g_native_dev);
                const uint32_t bw = g_nr_box_w.load(std::memory_order_relaxed);
                const uint32_t bh = g_nr_box_h.load(std::memory_order_relaxed);
                if (c && o && dev && bw && bh && init_blend(dev)) {
                    g_blend_eye = g_last_first_eye.load(std::memory_order_relaxed) ? 0 : 1;
                    if (describe_pair(dev, g_blend_eye, c, o)) {
                        g_blend_box[0] = g_nr_box_x.load(std::memory_order_relaxed);
                        g_blend_box[1] = g_nr_box_y.load(std::memory_order_relaxed);
                        g_blend_box[2] = bw;
                        g_blend_box[3] = bh;
                        g_blend_output = o;
                        g_blend_pending = true;
                        g_blend_seen = 0;
                        g_blend_list = list;
                        hook_list_close(list);
                    }
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

// ---- model resolution -------------------------------------------------------------------------
//
// The strongest lever against NR's cost, and the one OptiScaler exposes as a slider: run the model
// BELOW output resolution. NVIDIA quote 50-60% frame cost for neural rendering at output res, and we
// measured it at ~4.5 ms of a ~21 ms frame here. Foveation caps out at ~19% because it can only
// remove the part of the frame you are not looking at; dropping the model to 75% removes ~44% of its
// cost everywhere, with no seam, no stereo geometry and no eye identity to get wrong.
//
// The working resolution is fixed at CreateFeature time via DLSSNR.Width/Height, so it has to be
// changed there rather than per evaluation. The addon's own "Enable Upscaling" is a hardcoded 33%
// version of exactly this (848x864 into 2544x2592); this makes the ratio a setting.
std::atomic<uint32_t> g_model_res_w{0}, g_model_res_h{0};
std::atomic<uint64_t> g_model_scaled{0};

void create_prehook(uint32_t feature, void* params) {
    try {
        // MEASURED NO-OP, kept as a record rather than a control. Writing DLSSNR.Width/Height at
        // CreateFeature succeeds and changes nothing: the addon allocates the model's input
        // RESOURCES at its chosen size -- "created inline NR resources 848x864 -> 2544x2592" -- and
        // the snippet takes its working resolution from those, not from these parameters. We set
        // 1904x1944 and the feature was still built at 2544x2592 with no frame-time change.
        // OptiScaler can offer a slider because it owns the resource allocation; hosting a closed
        // addon, we do not. The only ratio available here is the addon's fixed Enable Upscaling.
        return;
        if (feature != 18) return;
        const uint32_t pct = DlssNeuralRendering::get()->model_resolution_pct();
        if (pct >= 100) return;

        // The addon's own "Enable Upscaling" already runs the model below output -- hardcoded at
        // the game's render resolution, a third here. Setting both meant telling the model to run
        // at 1904x1944 while the addon had built an 848x864 contract: two answers to one question,
        // and a good explanation for the shimmering that produced. One owner at a time.
        {
            std::lock_guard lock(g_mtx);
            for (const auto& e : g_entries) {
                if (_stricmp(e.key.c_str(), "NREnableUpscaling") == 0 && e.value == "1") {
                    static std::atomic<bool> s_said{false};
                    bool ex = false;
                    if (s_said.compare_exchange_strong(ex, true))
                        spdlog::warn("[DLSSNR-FOV] the addon's upscaling is on, which already runs "
                                     "the model below output; model resolution ignored");
                    return;
                }
            }
        }

        uint32_t w = 0, h = 0;
        if (!nr_get_uint(params, "DLSSNR.Width", &w) || !nr_get_uint(params, "DLSSNR.Height", &h))
            return;
        if (w < 64 || h < 64) return;

        // Multiples of eight: the model is happier on aligned dimensions and the guides divide down.
        const uint32_t nw = align8_down((uint32_t)((uint64_t)w * pct / 100));
        const uint32_t nh = align8_down((uint32_t)((uint64_t)h * pct / 100));
        if (nw < 64 || nh < 64 || nw >= w) return;

        nr_set_uint(params, "DLSSNR.Width", nw);
        nr_set_uint(params, "DLSSNR.Height", nh);
        g_model_res_w.store(nw, std::memory_order_relaxed);
        g_model_res_h.store(nh, std::memory_order_relaxed);

        const auto n = g_model_scaled.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 3)
            spdlog::info("[DLSSNR-FOV] model resolution {}%: {}x{} -> {}x{} at CreateFeature",
                         pct, w, h, nw, nh);
    } catch (...) {
    }
}

// Same shape as the evaluate thunk -- JMP to the trampoline so the addon's return address survives
// the signed runtime's caller check -- but it hands the prehook the FEATURE id (rdx) and the
// parameter block (r8) instead of the command list.
constexpr size_t kCreateStubSize = 0x4D;
constexpr size_t kCreateStubPrehookImm = 0x1F;
constexpr size_t kCreateStubTargetImm  = 0x43;
constexpr uint8_t kCreateStubCode[kCreateStubSize] = {
    0x48,0x83,0xEC,0x48,
    0x48,0x89,0x4C,0x24,0x20,
    0x48,0x89,0x54,0x24,0x28,
    0x4C,0x89,0x44,0x24,0x30,
    0x4C,0x89,0x4C,0x24,0x38,
    0x89,0xD1,                          // mov ecx, edx   (feature)
    0x4C,0x89,0xC2,                     // mov rdx, r8    (params)
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

safetyhook::InlineHook g_nr_create_hook{};

uint8_t* build_create_stub() {
    auto* stub = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, kCreateStubSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (stub == nullptr) return nullptr;
    memcpy(stub, kCreateStubCode, kCreateStubSize);
    const auto fn = reinterpret_cast<uint64_t>(&create_prehook);
    memcpy(stub + kCreateStubPrehookImm, &fn, sizeof(fn));
    return stub;
}

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
    for (auto& e : g_events) if (e.id == ev) { e.fns.push_back(cb); rebuild_exec_subscribers(); return; }
    g_events.push_back(EventSub{ev, {cb}});
    rebuild_exec_subscribers();
    spdlog::info("[DLSSNR] addon subscribed to event {} ({})", ev,
                 ev == 0 ? "init_device" : ev == 1 ? "destroy_device" : ev == 74 ? "present" : "?");
}

extern "C" __declspec(dllexport) void ReShadeUnregisterEvent(uint32_t ev, void* cb) {
    if (!hosting()) { if (auto fn = forwarded<void(*)(uint32_t, void*)>("ReShadeUnregisterEvent")) fn(ev, cb); return; }
    std::lock_guard lock(g_mtx);
    for (auto& e : g_events) if (e.id == ev) std::erase(e.fns, cb);
    rebuild_exec_subscribers();
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
    if (contains_ci(text, "captured first loaded-module D3D12 reconstruction evaluation"))
        g_sr_evaluation_observed.store(true, std::memory_order_relaxed);

    // The thunk gives up the return value to keep the addon's return address on the stack, so the
    // addon's own reporting is where evaluation results come from now. It says either
    // "inline feature 18 evaluation succeeded" or "feature 18 evaluate failed with 0xbad00002".
    if (contains_ci(text, "0xbad00002")) {
        if (g_nr_bad.fetch_add(1, std::memory_order_relaxed) == 0) {
            g_nr_tamper_suspected.store(true, std::memory_order_relaxed);
            spdlog::error("[DLSSNR-FOV] the runtime still refuses us as the caller even through the "
                          "tail-jump thunk. Neural rendering is being skipped; turn ProbeNGX off.");
        }
    } else if (contains_ci(text, "feature 18 created")) {
        // The addon states the geometry it built; showing that beats showing what we asked for.
        if (const char* p1 = strstr(text, "NR input ")) {
            unsigned iw = 0, ih = 0, ow = 0, oh = 0;
            if (sscanf_s(p1, "NR input %ux%u -> output %ux%u", &iw, &ih, &ow, &oh) == 4) {
                g_built_in_w.store(iw, std::memory_order_relaxed);
                g_built_in_h.store(ih, std::memory_order_relaxed);
                g_built_out_w.store(ow, std::memory_order_relaxed);
                g_built_out_h.store(oh, std::memory_order_relaxed);
            }
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

// Added by the 2026-09-02 addon build, which uses it to site its NR screenshot pairs. It is the
// ONLY new entry point that build needs -- the other ten are unchanged -- so hosting it costs one
// function rather than a rethink. ReShade's signature writes into a caller buffer and reports the
// required size when the buffer is null.
extern "C" __declspec(dllexport) void ReShadeGetBasePath(char* path, size_t* path_size) {
    if (!hosting()) {
        if (auto fn = forwarded<void(*)(char*, size_t*)>("ReShadeGetBasePath")) fn(path, path_size);
        return;
    }
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto dir = std::filesystem::path{exe}.parent_path().string();
    if (path == nullptr) { if (path_size) *path_size = dir.size() + 1; return; }
    const size_t cap = path_size ? *path_size : 0;
    if (cap == 0) return;
    const size_t n = std::min(dir.size(), cap - 1);
    memcpy(path, dir.c_str(), n);
    path[n] = '\0';
    if (path_size) *path_size = n;
}

extern "C" __declspec(dllexport) const void* ReShadeGetImGuiFunctionTable(uint32_t version) {
    if (!hosting()) {
        if (auto fn = forwarded<const void*(*)(uint32_t)>("ReShadeGetImGuiFunctionTable"))
            return fn(version);
        return nullptr;
    }
    // The version has been recorded and never acted on. ReShade ships a DIFFERENT table layout per
    // ImGui version (imgui_function_table_18600, _18971, _19000, _19040, _19180 ...), and returning
    // one fixed table regardless is why an addon built against another version lands its calls on
    // the wrong members. The measured slots here -- Separator 80, Checkbox 115, SliderFloat 144 --
    // match none of the published tables, whose Separator sits at 131, so the layout in use is
    // older than anything currently in ReShade's tree. Log it so the right header can be fetched
    // rather than the layout being re-measured widget by widget.
    if (g_imgui_version_asked != version || g_imgui_table[0] == nullptr) build_imgui_table(version);
    g_imgui_version_asked = version;
    return g_imgui_table;                       // never null while hosting; see build_imgui_table
}

// ================================================================================================

std::optional<std::string> DlssNeuralRendering::on_initialize() {
    // NOT building the ImGui table here. It allocates ~180 KB of executable thunk memory, and doing
    // that at startup in EVERY game -- including ones where this mod is switched off and will never
    // host anything -- is both wasteful and a needless perturbation of UEVR's startup, which is a
    // documented race in some titles. The table is built when an addon first asks for it.
    fill_vts(0);
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
    // Framework only dispatches mod reset once game-data initialization is
    // complete. Do not retain backbuffers before that teardown path exists.
    if (!g_framework->is_game_data_intialized()) return;
    if (!m_enabled->value() || m_device_delivered) return;
    if (!GetModuleHandleW(L"_nvngx.dll") && !GetModuleHandleW(L"nvngx_dlssnr.dll")) return;

    auto& hook = g_framework->get_d3d12_hook();
    auto* dev = hook->get_device();
    if (dev == nullptr) return;

    if (forward_target() != nullptr) {
        // ONCE, not every frame. This runs on the present path, so warning here unconditionally
        // wrote a line per present -- measured in TOW2 with a real ReShade installed: 3,871 lines,
        // 36 per second, 40% of the entire log, and a synchronous file write in the frame loop.
        // The condition never changes within a session: either a ReShade is forwarding or it is not.
        strncpy_s(g_last_error, "a real ReShade is loaded; standing down so addons are not hosted twice", _TRUNCATE);
        static std::atomic<bool> s_said{false};
        bool expected = false;
        if (s_said.compare_exchange_strong(expected, true)) {
            spdlog::warn("[DLSSNR] {}", g_last_error);
        }
        return;
    }

    g_native_dev   = reinterpret_cast<uint64_t>(dev);
    g_native_queue = reinterpret_cast<uint64_t>(hook->get_command_queue());
    g_native_swap  = reinterpret_cast<uint64_t>(hook->get_swap_chain());
    {
        std::lock_guard lock(g_mtx);
        if (!g_swapchain.bind(hook->get_swap_chain())) {
            strncpy_s(g_last_error, "cannot acquire native swapchain buffers", _TRUNCATE);
            return;
        }
        spdlog::info("[DLSSNR] native swapchain snapshot: {} buffers, current {}",
                     g_swapchain.count(), g_swapchain.index());
    }
    // Gated on the toggle at INSTALL time, not just at dispatch. Leaving the vtable swapped and
    // only skipping the callback still routes every submission through our thunk, so the toggle
    // could never actually clear the submit path for an isolation test.
    if (m_exec_event->value())
        hook_queue_execute(reinterpret_cast<void*>(g_native_queue));
    else
        spdlog::info("[DLSSNR] execute-command-list event OFF; queue left unhooked");

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
        HMODULE addon = GetModuleHandleW(f.path().c_str());
        spdlog::info("[DLSSNR] loaded {} -- {}", f.path().filename().string(),
                     g_addons_registered > before ? "registered" : "did NOT register");

        if (addon != nullptr) {
            if (auto init = reinterpret_cast<AddonInitFn>(GetProcAddress(addon, "AddonInit"))) {
                HMODULE self{nullptr};
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(&forward_target), &self);
                bool ok = false;
                if (!invoke_addon_init_guarded(init, addon, self, &ok))
                    spdlog::error("[DLSSNR] AddonInit faulted in {}", f.path().filename().string());
                else
                    spdlog::info("[DLSSNR] AddonInit returned {}", ok ? "true" : "false");
                if (ok) g_addon_uninit_targets.push_back(addon);
            }
        }
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
    // CreateFeature too, so the model resolution can be set where it is actually fixed.
    if (auto* ct = reinterpret_cast<void*>(GetProcAddress(snippet, "NVSDK_NGX_D3D12_CreateFeature"))) {
        if (uint8_t* cstub = build_create_stub()) {
            auto ch = safetyhook::InlineHook::create(ct, cstub, safetyhook::InlineHook::StartDisabled);
            if (ch) {
                g_nr_create_hook = std::move(*ch);
                const auto ctramp = reinterpret_cast<uint64_t>(g_nr_create_hook.original<void*>());
                memcpy(cstub + kCreateStubTargetImm, &ctramp, sizeof(ctramp));
                DWORD o = 0;
                if (VirtualProtect(cstub, kCreateStubSize, PAGE_EXECUTE_READ, &o)) {
                    FlushInstructionCache(GetCurrentProcess(), cstub, kCreateStubSize);
                    if (auto en = g_nr_create_hook.enable(); en)
                        spdlog::info("[DLSSNR-FOV] CreateFeature detoured; model resolution is settable");
                    else
                        spdlog::error("[DLSSNR-FOV] could not enable the CreateFeature detour");
                }
            } else {
                spdlog::error("[DLSSNR-FOV] could not detour CreateFeature");
            }
        }
    }

    g_nr_hooked.store(true, std::memory_order_relaxed);
    spdlog::info("[DLSSNR-FOV] EvaluateFeature detoured via tail-jump thunk at {} -> trampoline {:#x}. "
                 "The addon's return address is preserved, so the caller check should not fire.",
                 (void*)stub, trampoline);
}

// Delivered on device reset and at teardown. Guarded twice over: once so it cannot fire without a
// matching init_device, and once by SEH, because by teardown the addon may already be partway
// through its own unload.
void DlssNeuralRendering::deliver_addon_uninit() {
    for (HMODULE m : g_addon_uninit_targets) {
        if (auto un = reinterpret_cast<AddonUninitFn>(GetProcAddress(m, "AddonUninit"))) {
            HMODULE self{nullptr};
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&forward_target), &self);
            if (!invoke_addon_uninit_guarded(un, m, self))
                spdlog::error("[DLSSNR] AddonUninit faulted");
        }
    }
    g_addon_uninit_targets.clear();
}

void DlssNeuralRendering::deliver_destroy_device() {
    destroy_lifecycle(true);
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
    g_swapchain.reset();
}

DlssNeuralRendering::~DlssNeuralRendering() {
    destroy_lifecycle(false);
    deliver_addon_uninit();
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
    g_nr_last_eval_ms.store(0, std::memory_order_relaxed);
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
    // Also release a snapshot acquired before device initialization completed.
    { std::lock_guard lock(g_mtx); g_swapchain.reset(); }
    g_native_dev = g_native_queue = g_native_swap = 0;
    g_present_faulted = false;
}

void DlssNeuralRendering::on_present() {
    load_addons_once();
    if (m_device_delivered && !initialize_lifecycle()) return;
    install_nr_ngx_gate_probes();
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
        if (n == 600 || n % 3600 == 0)
            spdlog::info("[DLSSNR-NGX-GATE] totals entry={} mode={} inputs={} native-wrapper={} wrapper-rejected={}",
                g_nr_ngx_counts[0].load(), g_nr_ngx_counts[1].load(), g_nr_ngx_counts[2].load(),
                g_nr_ngx_counts[3].load(), g_nr_ngx_tag_rejected.load());
        if (n == 1 || n == 60 || n == 600)
            spdlog::info("[DLSSNR] present delivered {} time(s)", n);
    }
}

// MENU STRUCTURE -- the rule, so it stops drifting every time something is added:
//
//   "Neural rendering"   ONLY things owned by the addon: what registered, what it built, its own
//                        settings page, and the values we persist on its behalf.
//   "Foveated rendering" ONLY things owned by us: the detour, the region, the measurement, and our
//                        own diagnostics.
//
// No control appears on both pages. No text says "below" or "above" for something that lives on the
// other page -- it names the page instead. Anything gated says which page unlocks it. Instructions
// that pointed "below" at controls which had moved pages is exactly how this got confusing.
void DlssNeuralRendering::on_draw_sidebar_entry(std::string_view entry) {
    const bool addon_page = (entry == kPageNeural);
    const bool hooked = g_nr_hooked.load(std::memory_order_relaxed);
    const bool refused = g_nr_tamper_suspected.load(std::memory_order_relaxed);
    const ImVec4 green(0.4f, 0.95f, 0.5f, 1.0f);
    const ImVec4 amber(1.0f, 0.8f, 0.3f, 1.0f);
    const bool sr_seen = g_sr_evaluation_observed.load(std::memory_order_relaxed);
    ImGui::TextColored(sr_seen ? green : amber, "%s", sr_seen
        ? "DLSS SR/AA/RR: reconstruction hook observed an evaluation this session"
        : "DLSS SR/AA/RR: no intercepted evaluation observed");
    ImGui::TextDisabled("SR status is session evidence, not proof an override changed the image.");
    const auto evaluations = g_nr_evals.load(std::memory_order_relaxed);
    const auto last_eval = g_nr_last_eval_ms.load(std::memory_order_relaxed);
    const bool recent = last_eval != 0 && GetTickCount64() - last_eval < 2000;
    if (refused || g_lifecycle_failed || g_present_faulted) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "DLSS NR: blocked/faulted -- see diagnostics");
    } else if (recent) {
        ImGui::TextColored(green, "DLSS NR: evaluation calls active (%llu)",
                           static_cast<unsigned long long>(evaluations));
    } else if (evaluations != 0) {
        ImGui::TextColored(amber, "DLSS NR: evaluated earlier; no recent activity (%llu calls)",
                           static_cast<unsigned long long>(evaluations));
    } else {
        ImGui::TextColored(amber, "%s", hooked
            ? "DLSS NR: hook attached, but ZERO evaluation calls"
            : "DLSS NR: evaluation hook not attached");
    }
    ImGui::TextDisabled("NR counts calls, not successful output. DLL loading alone never turns it green.");
    ImGui::Separator();

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
        // OURS, not the addon's. NeuralUplift is served from our store and read by the addon once at
        // registration, so this works even when the addon's own page has faulted and cannot be
        // drawn -- which is exactly when being unable to switch neural rendering on hurts most.
        // Only when the addon cannot draw its own live toggle -- two checkboxes for one setting,
        // one of them next-launch, was needlessly confusing.
        if (g_overlay_faulted || g_overlay_cb == nullptr) {
            std::lock_guard lock(g_mtx);
            Entry& e = touch(kAddonSection, "NeuralUplift");
            bool on = (e.value == "1");
            if (ImGui::Checkbox("Neural rendering enabled (next launch)", &on)) {
                e.value = on ? "1" : "0";
                e.set = true;
                g_config_dirty = true;
                save_addon_config();
            }
            ImGui::TextDisabled("Takes effect on the next launch: the addon reads this once, when "
                                "it loads. Independent of its own page below.");

            // Both resolution controls together: they are the same lever and were fighting when
            // they lived on separate pages.
            Entry& up = touch(kAddonSection, "NREnableUpscaling");
            bool ups = (up.value == "1");
            if (ImGui::Checkbox("Enable upscaling at next launch", &ups)) {
                up.value = ups ? "1" : "0";
                up.set = true;
                g_config_dirty = true;
                save_addon_config();
            }
            ImGui::TextDisabled("Saved here, read by the addon once when it loads. To change it NOW, "
                                "use Enable Upscaling on the addon's own page below -- that mutates "
                                "its live variable. This runs the model at the game's render "
                                "resolution, a third of output here -- the only ratio on offer.");

            ImGui::Spacing();
        {
            const uint32_t iw = g_built_in_w.load(std::memory_order_relaxed);
            const uint32_t ow = g_built_out_w.load(std::memory_order_relaxed);
            if (ow != 0) {
                ImGui::Text("Model runs at %ux%u -> %ux%u%s", iw,
                            g_built_in_h.load(std::memory_order_relaxed), ow,
                            g_built_out_h.load(std::memory_order_relaxed),
                            iw < ow ? "  (upscaling)" : "  (native)");
            }
            ImGui::TextDisabled("Model resolution is fixed by the addon: it allocates the input "
                                "resources, so the only ratio available is its own Enable "
                                "Upscaling. Setting the NGX parameters from here has no effect.");
        }
        }

        // ---- the addon's own settings, on their own page ---------------------------------------
        ImGui::Separator();
        if (m_draw_addon_ui->value() && g_overlay_cb != nullptr && !g_overlay_faulted) {
            if (!invoke_overlay_guarded(reinterpret_cast<void(*)(void*)>(g_overlay_cb))) {
                g_overlay_faulted = true;
                spdlog::error("[DLSSNR] the addon's settings page faulted; not called again. Last "
                              "unimplemented ImGui slot entered was {} (0xFFFFFFFF = none, so the "
                              "fault was inside one of our own forwarders)",
                              (uint32_t)g_last_imgui_slot);
            }
        } else if (g_overlay_faulted) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                               "The addon's settings page faulted and will not be drawn again this "
                               "session. Use the checkbox above to control neural rendering; every "
                               "other setting keeps its stored value.");
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
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Region controls unavailable.");
        if (!m_probe_ngx->value()) {
            ImGui::TextWrapped(
                "\"Detour NGX for our foveal region\" is off -- see Setup below. It takes effect on "
                "the next launch, and nothing here can work without it. Leave it off if you are "
                "running an addon that does its own foveation.");
        } else {
            ImGui::TextWrapped(
                "Waiting for neural rendering to start. nvngx_dlssnr.dll only loads once NR is "
                "running, and there is nothing to detour until then. Switch it on from the addon's "
                "own controls on the \"Neural rendering\" page; the detour installs a second later "
                "and these controls appear. No restart needed.");
        }
    } else {
        m_foveate->draw("Restrict neural rendering to a region");

        ImGui::Text("Neural Rendering coverage");
        ImGui::SetNextItemWidth(330.0f);
        m_foveal_preset->draw("##coverage");

        const float pct = preset_percent();
        if (preset_is_slab()) {
            ImGui::TextDisabled("Full height, anchored toward the nose so both eyes cover the same "
                                "world and fuse into one region. %.0f%% of the pixels; the eyes "
                                "share the middle %.0f%%.", pct * 100.0f, (2.0f * pct - 1.0f) * 100.0f);
        } else {
            ImGui::TextDisabled("Centred in each eye. %.0f%% x %.0f%% = %.0f%% of the pixels -- the "
                                "cheapest option. Below 50%% the two eyes cannot share one region, "
                                "so expect to see it per eye rather than fused.",
                                pct * 100.0f, pct * 100.0f, pct * pct * 100.0f);

            ImGui::TextDisabled("Mirrored nudge toward the nose. Helps a little; it cannot make "
                                "small regions fuse.");
        }
        m_swap_eyes->draw("Swap eyes");
        ImGui::TextDisabled("Should not be needed: the eye is taken from UEVR directly. Only use it "
                            "if the untreated area appears in the middle instead of the edges.");

        const auto bw = g_nr_box_w.load(std::memory_order_relaxed);
        if (bw != 0) ImGui::Text("Region: %ux%u", bw, g_nr_box_h.load(std::memory_order_relaxed));







        // ---- is the box actually cheaper? ----
        ImGui::Separator();
        ImGui::TextDisabled("Rolling %d-frame window. Alternate off/on every ~10 s while standing "
                            "still; both buckets stay recent so the comparison is fair.", kAbWindow);
        for (int i = 0; i < 2; ++i) {
            const double avg = g_ms_n[i]
                ? g_ms_sum[i] / (double)std::min<uint64_t>(g_ms_n[i], kAbWindow) : 0.0;
            if (g_ms_n[i] == 0) ImGui::TextDisabled("Box %s: no samples", i ? "ON " : "OFF");
            else ImGui::Text("Box %s: %.2f ms (%.1f fps) over %llu frames", i ? "ON " : "OFF",
                             avg, avg > 0.0 ? 1000.0 / avg : 0.0, (unsigned long long)g_ms_n[i]);
        }
        if (g_ms_n[0] && g_ms_n[1]) {
            const double off = g_ms_sum[0] / (double)std::min<uint64_t>(g_ms_n[0], kAbWindow);
            const double on = g_ms_sum[1] / (double)std::min<uint64_t>(g_ms_n[1], kAbWindow);
            ImGui::TextColored(on < off ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.6f, 0.5f, 1.0f),
                               "Box saves %.2f ms/frame (%+.1f%%)", off - on,
                               off > 0.0 ? (off - on) / off * 100.0 : 0.0);
        }
        if (ImGui::Button("Reset measurement")) {
            memset(g_ms_ring, 0, sizeof(g_ms_ring));
            g_ms_sum[0] = g_ms_sum[1] = 0.0;
            g_ms_head[0] = g_ms_head[1] = 0;
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
        m_probe_ngx->draw("Detour NGX for our foveal region");
        ImGui::TextDisabled("Required for the region -- it is how the subrects are rewritten. Turn "
                            "it OFF when hosting an addon that hooks NGX itself, or two detours "
                            "land on the same export.");
        ImGui::TextDisabled("Both take effect on the next launch. The detour is what the box needs.");
        m_refresh_periphery->draw("Keep the area outside the box live");
        ImGui::TextDisabled("Without it the periphery shows a frozen frame.");
        m_dispatch_present->draw("Deliver the per-frame present event");
        m_exec_event->draw("Deliver the execute-command-list event");
        ImGui::TextDisabled("Needed by addons that add work to the game's command lists. Fires on "
                            "every submission -- switch it off if the game stalls.");
        m_draw_addon_ui->draw("Show the addon's own settings below");
    }

    // ---- diagnostics --------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Diagnostics")) {
        ImGui::Text("Frames delivered: %llu", (unsigned long long)g_present_calls);
        ImGui::Text("Periphery copies: %llu", (unsigned long long)g_copies.load(std::memory_order_relaxed));
        ImGui::Text("execute_command_list dispatches: %llu%s",
                    (unsigned long long)g_exec_dispatches.load(std::memory_order_relaxed),
                    g_exec_event_faulted ? "  (faulted, disabled)" : "");
        ImGui::Text("Addon reported succeeded %llu, refused %llu",
                    (unsigned long long)g_nr_ok.load(std::memory_order_relaxed),
                    (unsigned long long)g_nr_bad.load(std::memory_order_relaxed));
        if (g_copy_faulted.load(std::memory_order_relaxed))
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Periphery refresh disabled itself.");
        if (g_slot_list[0]) ImGui::TextDisabled("ImGui slots used: %s", g_slot_list);
        if (g_addon_desc[0]) ImGui::TextWrapped("%s", g_addon_desc);
    }

}
