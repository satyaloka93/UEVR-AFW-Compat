#pragma once

// Hosting RenoDX-style ReShade addons inside UEVR, so DLSS 5 Neural Rendering can run without
// ReShade in the process.
//
// WHY NOT JUST USE RESHADE. ReShade and UEVR do not coexist here: with ReShade installed as
// bin\dxgi.dll, UEVR never injected at all in The Outer Worlds 2, and removing the proxy fixed it
// immediately. ReShade's other entry point -- the OpenXR API layer -- is no better, because it
// declines any session created without a device it wrapped, which is exactly what a VR mod does.
// It logged "Skipping OpenXR session because it was created without a proxy Direct3D 11 device".
//
// So the host takes ReShade's place. An addon does not link against ReShade: it calls
// K32EnumProcessModules, finds the first module exporting ReShadeRegisterAddon, and binds to it.
// Exporting those ten entry points is sufficient to be that module.
//
// WHY THIS IS WORTH DOING IN UEVR SPECIFICALLY. The addon keeps its inline resource set and its
// visible codec in SINGLE-SLOT caches at fixed addresses, so two render targets can only share one
// set or thrash rebuilding it. In a port that renders each eye into its own resource that means
// neural rendering reaches exactly one eye, permanently. UEVR native stereo renders both eyes into
// one double-wide target -- confirmed in TOW2 as `RenderTargetSize After: 6120x3120` -- which is a
// single resource, a single contract, and therefore both eyes from one cache entry.
//
// That is the hypothesis this mod exists to test. It is off by default.

#include <cstdint>
#include <string>
#include <vector>
#include "Mod.hpp"

class DlssNeuralRendering : public Mod {
public:
    // Shared by get_sidebar_entries() and the page router. They were separate string literals and
    // drifted apart in a rename, which left the second page unreachable -- and with it the only way
    // to switch neural rendering on.
    static constexpr const char* kPageFoveated = "Foveated rendering";
    static constexpr const char* kPageNeural   = "Neural rendering";

    static std::shared_ptr<DlssNeuralRendering>& get();

    DlssNeuralRendering() {
        m_options = { *m_enabled, *m_dispatch_present, *m_draw_addon_ui,
                      *m_probe_ngx, *m_foveate, *m_foveal_fraction, *m_refresh_periphery,
                      *m_convergence_pct, *m_swap_eyes, *m_close_probe, *m_cmd_probe };
    }

    std::string_view get_name() const override { return "DLSS 5"; }
    // get_name() also feeds generate_name(), which builds every config key. Pin the original
    // prefix so renaming the sidebar heading does not orphan the saved settings.
    std::string generate_name(std::string_view name) override {
        return std::string{"DlssNeuralRendering_"} + name.data();
    }

    std::vector<SidebarEntryInfo> get_sidebar_entries() override {
        // The sidebar prints the MOD NAME as a heading above its entries (Framework.cpp), so a
        // single entry called "DLSS Neural Rendering" sat under a heading reading
        // "DlssNeuralRendering" and looked like an empty duplicate of itself. Two clearly-named
        // sub-pages read as what they are.
        // Neural rendering first: it is the prerequisite -- the foveated page stays gated until NR is
        // running -- and listing it second made it read as a child of "Foveated rendering". The
        // framework draws a flat list of centre-aligned selectables, so order is the only control.
        return { { kPageNeural, false }, { kPageFoveated, false } };
    }

    ~DlssNeuralRendering() override;

    std::optional<std::string> on_initialize() override;
    // The addon subscribes to destroy_device and uses it to release its feature registry and
    // detach its NGX hooks. Without it those outlive the device they were built against.
    void on_device_reset() override;
    void on_present() override;                                   // addon_event 74
    void on_draw_sidebar_entry(std::string_view entry) override;
    void on_config_load(const utility::Config& cfg, bool set_defaults) override;
    void on_config_save(utility::Config& cfg) override;

    // Consumed by the exported ReShade entry points and the NGX detour, none of which can be
    // members: the first are C exports, the second runs on the render thread from a hook.
    bool hosting_enabled() const { return m_enabled->value(); }
    bool foveation_enabled() const { return m_foveate->value(); }
    float foveal_fraction() const { return m_foveal_fraction->value(); }
    bool refresh_periphery_enabled() const { return m_refresh_periphery->value(); }
    float convergence_pct() const { return m_convergence_pct->value(); }
    bool close_probe_enabled() const { return m_close_probe->value(); }
    bool cmd_probe_enabled() const { return m_cmd_probe->value(); }
    bool swap_eyes() const { return m_swap_eyes->value(); }

private:
    void load_addons_once();
    void deliver_destroy_device();
    // Detours nvngx_dlssnr!NVSDK_NGX_D3D12_EvaluateFeature. Separate from hosting because it may
    // not be survivable -- see the foveation section of the .cpp for the anti-tamper result that
    // this is written to test.
    void install_ngx_probe();
    void reset_ngx_probe();

    ModToggle::Ptr m_enabled{ ModToggle::create(generate_name("Enabled"), false) };
    ModToggle::Ptr m_dispatch_present{ ModToggle::create(generate_name("DispatchPresent"), true) };
    ModToggle::Ptr m_draw_addon_ui{ ModToggle::create(generate_name("DrawAddonUI"), true) };

    // Off by default and deliberately so: hooking the signed snippet is the open question, not the
    // foveal box. Enable this alone first and confirm NR still renders before enabling Foveate.
    ModToggle::Ptr m_probe_ngx{ ModToggle::create(generate_name("ProbeNGX"), false) };
    ModToggle::Ptr m_foveate{ ModToggle::create(generate_name("Foveate"), false) };
    // Fraction of each subrect's width and height kept. 0.5 keeps a quarter of the pixels.
    ModSlider::Ptr m_foveal_fraction{ ModSlider::create(generate_name("FovealFraction"),
                                                        0.25f, 1.0f, 0.5f) };
    // Without this the region outside the box shows a frozen frame, because NR no longer writes it
    // and the addon blits its persistent output back whole. Off only as an escape hatch: it issues
    // D3D12 barriers against an ASSUMED resource state.
    ModToggle::Ptr m_refresh_periphery{ ModToggle::create(generate_name("RefreshPeriphery"), true) };


    // Converges the two boxes so they cover the same world region. Without it each box sits at the
    // same coordinates in its own eye image, which is a different piece of the world per eye.
    // As a FRACTION OF EYE WIDTH, mirrored per eye. OpenXR-Toolkit's fixed-foveated path uses
    // exactly this shape -- a hardcoded +0.04 applied as +x to the left eye and -x to the right --
    // so 4% is a proven starting point. Expressed in pixels this was tested at 21 px against a
    // 2544 px eye, which is 0.8%: far too small to align anything.
    ModSlider::Ptr m_convergence_pct{ ModSlider::create(generate_name("ConvergencePct"),
                                                        0.0f, 0.12f, 0.04f) };
    // Settles whether Close() is a usable insertion point before any shader is written.
    ModToggle::Ptr m_cmd_probe{ ModToggle::create(generate_name("CmdProbe"), false) };
    ModToggle::Ptr m_close_probe{ ModToggle::create(generate_name("CloseProbe"), false) };
    ModToggle::Ptr m_swap_eyes{ ModToggle::create(generate_name("SwapEyes"), false) };



    bool m_load_attempted{false};    // the .addon64 has been LoadLibrary'd
    bool m_device_delivered{false};  // init_device has been sent for the current device
    bool m_probe_attempted{false};   // the evaluate detour has been tried once this session
};
