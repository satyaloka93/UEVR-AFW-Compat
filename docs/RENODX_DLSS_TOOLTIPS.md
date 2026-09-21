# RenoDX DLSS tooltip reference

Transcribed from the user's GTA San Andreas flat-mode screenshots dated 2026-09-09. These are the addon's claims, not independently verified rendering behavior. Screenshot root: `C:\Users\gthom\Pictures\Screenshots\`; filenames below use `Screenshot 2026-09-09 HHMMSS.png`. The visible build stamp is `2026-09-04T08:48:41` (073656).

The host forwards the addon's original hover text, rather than replacing it with this transcription. Tooltip windows wrap at approximately 35 font-width units, bounded by display width. No game or stereo settings are changed.

## Hook Method — 073319, 073908

Visible choices: **Off / Auto / Upscaled / Present**. The screenshot shows `Off: Disabled`; it does not establish successful NR execution in GTA. Numeric config values require separate verification: the earlier inference that `DirectNeuralRenderingHookPoint=1` means Upscaled omitted Off and must not be relied upon.

> Present supports D3D9, D3D11, and D3D12 presentation. D3D9 and D3D11 use a same-adapter, device-only D3D12 endpoint.
> With DLSS-G active, Present processes only the application-rendered frame and does not rewrite the game's Streamline composition tags.

## Require DLSS — 073426

> On waits for matching DLSS SR/AA/RR temporal inputs before Auto or Present can use the presentation source.
> Off allows Auto to fall back to Present and makes Present use dummy temporal inputs.

## Encoding — 073438

> Auto treats native DLSS SR/AA/RR output as linear BT.709 and uses the active swapchain color space for downstream sources.
> srgb_nonlinear selects sRGB, extended_srgb_linear selects linear scRGB, and hdr10_st2084 selects BT.2100 PQ.
> scRGB-nl remains an explicit compatibility option and is never inferred.
> Selecting the wrong source convention can produce invalid color.

## Diffuse White (nits) — 073450

> Diffuse-white balance used by N2. Automatic values are 100 nits for linear BT.709, 250 nits for BT.2100 PQ and linear scRGB, and 203 nits for scRGB-nl.
> Enter a custom value directly or use reset to restore the current encoding's automatic value.

## Overall Intensity — 073502

> DLSSNR.Intensity. The supplied binaries default to 1 and consume this value without range validation. The slider uses the normalized range 0 to 1; manually entered values are forwarded unchanged.

## Model — 073517

> Selects Neural Rendering Model A, Model B, or Model C through the prerelease DLSSNR.Style field.

## Structure Intensity — 073600

> DLSSNR.LocalStructureStrength. The supplied binaries default to 1 and consume this value without range validation. The slider uses the normalized range 0 to 1; manually entered values are forwarded unchanged.

## Global Tone Intensity — 073613, 073623

Both screenshots contain the same GlobalToneStrength tooltip, even though 073623 is positioned near the Local Tone row. Do not invent a separate Local Tone description from it.

> DLSSNR.GlobalToneStrength is a real field in the supplied Streamline ABI. Its effect is not visible in the currently recovered NGX path. The slider uses the normalized range 0 to 1; manually entered values are forwarded unchanged.

## Character Mask — 073632

> Allows independent control of characters via automatic masking (UseAutoMask).

## Skin Structure Strength — 073639

> Controls structure intensity of detected characters (SkinStructureStrength).

## Pass Count — 073656

> Runs 1 to 10 sequential Neural Rendering evaluations for each source.
> Additional passes consume the preceding Neural Rendering output directly; source preparation and output composition run only once.
> Changing this option resets Neural Rendering history.

## Not captured

No distinct UI Correction, Local Tone Intensity, Preset or Options Mode tooltip is readable in this set. They may still appear through the live addon tooltip bridge; do not substitute an invented description.
