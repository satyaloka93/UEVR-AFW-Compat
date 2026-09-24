---
type: release
title: UEVR AFW compatibility alpha.5
description: Backend-only experimental checkpoint for validated CVars, controller scrolling, browser safety, AFW copy guards and Native timing diagnostics.
timestamp: 2026-09-24
tags: [release, afw, cvars, diagnostics]
resource: https://github.com/satyaloka93/UEVR-AFW-Compat/releases/tag/afw-beta4-compat-v0.1.0-alpha.5
---

# Scope

Runtime source is `832bff79db09304c1bc68512f8fd3c9ec60dec06` and UESDK
is `2f201aaeb8fbc213078afe3b0a7c6e80feb0a3f8`. Full user-facing changes,
installation, rollback and limitations are in repository `docs/RELEASE_ALPHA5.md`.

The isolated export excludes unrelated working-tree haptics/melee/profile
changes. It uses existing pinned dependency source caches, a new build directory,
the checked-in RenderDoc SDK patch, and explicit generated source/version
identification. No RenderDoc runtime, game deployment or profile replacement
is part of publication. The archive-export path required applying the recorded
SDK patch explicitly because configure-time Git patch discovery did not apply it.

# Runtime requirement

Official beta.4 PDAFW SHA-256:
`76bbc4d7a5370ba0d6a1a50de6b81d45223e42b764d2d7c79f0c6c48a59f6b64`.
It is not the amended `b129118b...` binary bundled with alpha.4. Alpha.5
does not redistribute PDAFW or its generated no-op build stub. Preserve the
backend/runtime ABI pair; obtain the matching runtime under its distribution terms.

# Validation boundary

Isolated Windows Release build and CVar ABI fixtures passed. Portable catalog,
script policy, object-array, OpenXR policy, AFW copy/border and actual-ImGui
scroll regressions passed. Embedded binary identification matches `832bff79`
and alpha.5. Backend SHA-256:
`70ccbaaf4c357deaacc3990fd01a911a029d31dccf0e5abc01c7a9073e4f50c1`.
PDB SHA-256:
`25977a41f1ab3fa9f53af74cf29c121bd5c7679ebdcc1e61f271184c2ca4d716`.

Build/test and release hashes are recorded with the published assets.
Prior user runs support restored CVar access and stereo image correction,
but do not establish a general FPS benefit. AFW ghosting, final-eye diagnostic
outline visuals, headset scrolling confirmation and broad game regression
remain open. Successful build/tests are not fresh gameplay validation.

# Related

- [Validated CVar access](../fixes/tow2-validated-cvar-access.md)
- [Script and performance controls](../fixes/cvar-script-controls-shf.md)
- [AFW copy guards](../fixes/tow2-afw-gpu-copy-guard.md)
- [Native cost attribution](../fixes/native-openxr-cost-attribution.md)
- [Integration history](puredark-afw-integration.md)
