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

### Targeted DXVK imported-D3D11 validation build

Experimental ARM64EC DXVK `3.0.99` was built from upstream commit `d647fdf3c554ba917f88e01374602e06f664491d` (`[d3d9] Pass validation for imported D3D11 textures.`).

Device result with the same Proton 11 container:

- HUD confirms `DXVK v3.0.99-arm64ec` is actually loaded.
- Cutscene still starts.
- Audio and subtitles still work.
- Video remains completely black.
- No meaningful visual change versus DXVK 3.0.2.

Conclusion: rejecting D3D11 runtime descriptors during D3D9 reverse import is **not** the remaining OPPW4 black-video cause in this configuration.

### Stronger EVR / DXVA2 YUV hypothesis

Wine's EVR path is D3D9 based. Decoded frames can enter EVR as RGB32, YUY2 or NV12. The EVR mixer uses `IDirectXVideoProcessor::VideoProcessBlt`, and Wine's DXVA2 implementation performs the actual conversion/copy with `IDirect3DDevice9::StretchRect` from the video surface to an RGB render target. The presenter then performs another D3D9 `StretchRect` to its swapchain backbuffer and presents it.

Important behavior: the DXVA2 `VideoProcessBlt` implementation logs an individual `StretchRect` failure but still returns `S_OK`; the EVR presenter also does not use the return value of its final `StretchRect`. Therefore a failed YUY2/NV12 -> RGB D3D9 conversion can plausibly produce exactly the observed symptom: the media timeline and audio continue normally while the video area remains black.

DXVK does contain D3D9 YUY2 and NV12 mappings/conversion helpers, so the next question is whether that path actually works with this Android/Turnip ARM64EC stack.

A focused standalone verifier was added:

- source: `tests/d3d9_evr_yuv_stretch_test.cpp`
- workflow: `.github/workflows/build-evr-yuv-verifier.yml`

It tests three D3D9 `StretchRect` cases to an X8R8G8B8 backbuffer with pixel readback:

1. RGB32 baseline
2. YUY2 -> RGB
3. NV12 -> RGB

Interpretation:

- RGB32 passes, YUY2/NV12 fail: strong evidence that the missing FMV image is the EVR/DXVA2 YUV conversion path.
- all three pass: move higher into the EVR mixer/output-surface/presenter chain instead of generic YUV conversion.
- RGB32 baseline fails: broader D3D9 offscreen -> backbuffer path problem.
