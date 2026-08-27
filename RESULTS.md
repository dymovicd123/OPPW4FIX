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

### Next diagnostic

A standalone Windows x64 verifier was added to `tests/d3d9_d3d11_share_test.cpp` with workflow `Build OPPW4 D3D9-D3D11 share verifier`.

It performs a direct D3D9Ex -> D3D11 shared-texture test independent of OPPW4/Media Foundation:

1. Creates a shared D3D9 render-target texture.
2. Opens the same handle through D3D11.
3. Writes three colors from D3D9.
4. Verifies the producer pixels through D3D9 readback.
5. Verifies the consumer pixels through D3D11 readback after each update.

Possible results:

- `PASS`: generic D3D9 -> D3D11 shared image contents work; focus next on the media/video-specific surface path (NV12/conversion/synchronization).
- `SHARE_CONTENT_FAIL`: the shared handle opens but the D3D11 side does not see D3D9 writes; confirms a lower shared-memory/visibility/synchronization defect.
- `OPEN_FAIL`: shared-resource opening is still failing in the generic path.
- `PRODUCER_FAIL`: D3D9 itself failed to create/write/read the test resource.
