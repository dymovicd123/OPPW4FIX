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

### EVR / D3D9 YUV StretchRect verifier

Focused verifier: `tests/d3d9_evr_yuv_stretch_test.cpp`.

Device result in the same Proton 11 + DXVK 3.0.99 container: **PASS_ALL**.

Observed readback:

- RGB32 baseline: PASS, BGRA `255,0,255,255`.
- YUY2 -> X8R8G8B8: PASS, BGRA `185,185,185,255`.
- NV12 -> X8R8G8B8: PASS, BGRA `214,214,214,255`.
- Every `CreateOffscreenPlainSurface`, `LockRect`, `StretchRect`, `GetRenderTargetData` and readback call returned `S_OK`.

Conclusion: generic D3D9 YUY2/NV12 surface creation, CPU fill, YUV -> RGB `StretchRect`, RGB backbuffer transfer and readback all work on this ARM64EC + DXVK + Turnip stack. The black OPPW4 FMV is therefore **not** caused by generic DXVK D3D9 YUV conversion.

This pushes the fault higher into the real Wine EVR path. The remaining candidates are now much narrower:

1. Wine `IMFVideoSampleAllocator` / D3D9 video sample surfaces.
2. Wine DXVA2 `IDirectXVideoProcessor::VideoProcessBlt` and its EVR-specific source/output surfaces or rectangles.
3. Copying the decoded `IMediaSample` into the allocated D3D9 surface (`evr_copy_sample_buffer`) including actual media subtype/stride.
4. EVR mixer -> presenter handoff / final presentation behavior.

Next diagnostic should exercise the Wine DXVA2 device-manager/video-processor path and, ideally, the MF video sample allocator rather than another raw D3D9 `StretchRect` test.
