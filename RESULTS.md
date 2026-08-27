# OPPW4FIX — experimental results

## 2026-08-27

### Patched Proton 10 ARM64EC (sharedgpures PR 312 + 313)

Build: `proton-10.0.4.313-arm64ec` / patched `sharedgpures.sys`.

Device result:

- OPPW4 no longer immediately skips the Gallery/story WMV.
- The cutscene timeline runs.
- Audio is present and mostly normal, with occasional lag/dropouts.
- Subtitles are visible and advance normally.
- Video image itself remains black.

Interpretation: media decoding is alive and the two sharedgpures fixes materially changed the failure mode. The remaining fault is in shared video-surface/resource transfer or opening, not WMV/WMA decoding.

### Diagnostic Proton 11 ARM64EC D3DKMT build + DXVK 2.7.1

Build: `proton-11.0-3-arm64ec-opppw4-d3dkmt-sdk35.wcp`.

The build verified that the Proton 11 payload contains implemented Wine D3DKMT resource-sharing APIs (`NtGdiDdDDIQueryResourceInfo`, `NtGdiDdDDIOpenResource`, etc.) and `winedmo`.

Device result with DXVK 2.7.1 ARM64EC:

- OPPW4 simply skips the cutscene again.
- It does **not** reach the Proton 10 patched behavior of black video + audio + subtitles.

DXVK 2.7.1 still uses Proton's legacy `\\.\SharedGpuResource` device/IOCTL path for shared-resource metadata, so this combination did not exercise the modern Wine 11 D3DKMT sharing path end-to-end.

### Diagnostic Proton 11 ARM64EC D3DKMT build + DXVK 3.0.2 ARM64EC

Same Proton 11 diagnostic build, same Wrapper/Turnip/game files, only DXVK changed from 2.7.1 to 3.0.2 ARM64EC.

Device result:

- The cutscene now starts instead of being skipped.
- Timeline/subtitles run normally.
- Voice/audio is present and noticeably cleaner/more stable than in the patched Proton 10 test.
- The actual video image is still completely black.

Interpretation:

- Moving from DXVK 2.7.1 to 3.0.2 materially changes the failure mode on Proton 11, consistent with the modern upstream Wine D3DKMT resource-sharing path being used.
- Basic media decode/audio is not the blocker.
- The remaining failure is now likely **after shared-resource metadata/open succeeds**: actual shared image contents, visibility/synchronization, or a video-surface/format-specific path.
- Do not return to codec/MF experiments.

### Standalone D3D9 -> D3D11 shared-texture verifier

The standalone verifier (`tests/d3d9_d3d11_share_test.cpp`) was run inside the same Proton 11 + DXVK 3.0.2 container.

Result: **PASS**.

Observed details:

- D3D9 shared texture creation succeeded.
- D3D11 `OpenSharedResource` succeeded for handle `0x40000002`.
- MAGENTA, GREEN and CYAN writes made by D3D9 were read back correctly through both D3D9 and D3D11.
- Therefore ordinary D3D9-created -> D3D11-opened shared-image contents and visibility work in this stack.

This closes the hypothesis that generic D3D9 -> D3D11 shared memory is fundamentally broken.

### Important directionality finding after PASS

The verifier tested **D3D9 creates the resource -> D3D11 opens it**.

The Media Foundation / D3D9 device-manager path used by OPPW4 may exercise the reverse import direction: **D3D11-created resource -> D3D9 imports/validates/writes it -> D3D11 consumes it**.

Very recent upstream DXVK commit `d647fdf3c554ba917f88e01374602e06f664491d` (2026-08-26), titled:

`[d3d9] Pass validation for imported D3D11 textures.`

changes `D3D9DeviceEx::ValidateSharedTexture` specifically so D3D9 accepts D3D11 runtime descriptors (`version == 4`) instead of rejecting them as invalid D3D9 descriptors.

That commit is newer than DXVK 3.0.2 and is therefore absent from the tested stable WCP.

A targeted ARM64EC DXVK build workflow has been added:

`.github/workflows/build-dxvk-opppw4-imported-d3d11.yml`

It pins exactly commit `d647fdf3...` and packages it as an experimental `dxvk-arm64ec-3.0.99.wcp` for a one-variable A/B test against DXVK 3.0.2.

Next device test after the build succeeds:

1. Keep the same diagnostic Proton 11 container.
2. Keep Wrapper, Turnip, VKD3D, resolution and game files unchanged.
3. Change only DXVK 3.0.2 -> targeted `3.0.99` build.
4. Re-test the same Gallery cutscene.
