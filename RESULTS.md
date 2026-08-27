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

Interpretation: media decoding is alive and the two sharedgpures fixes materially changed the failure mode. This is the strongest positive result so far. The remaining fault is in shared video-surface/resource transfer or opening, not WMV/WMA decoding.

### Diagnostic Proton 11 ARM64EC D3DKMT build

Build: `proton-11.0-3-arm64ec-opppw4-d3dkmt-sdk35.wcp`.

The build verified that the Proton 11 payload contains implemented Wine D3DKMT resource-sharing APIs (`NtGdiDdDDIQueryResourceInfo`, `NtGdiDdDDIOpenResource`, etc.) and `winedmo`.

Device result with the existing DXVK 2.7.1 ARM64EC setup:

- OPPW4 simply skips the cutscene again.
- It does **not** reach the Proton 10 patched behavior of black video + audio + subtitles.

### Important compatibility finding after the P11 test

DXVK 2.7.1 still uses Proton's legacy `\\.\SharedGpuResource` device/IOCTL path for shared-resource metadata. DXVK 3.0 introduced support for Wine's upstream D3DKMT shared-resource implementation and explicitly states that shared resources no longer require Proton-specific patches.

Therefore the P11 + DXVK 2.7.1 test did **not** actually exercise the intended modern Wine 11 D3DKMT sharing path end-to-end. The next controlled A/B test is:

1. Keep the diagnostic Proton 11 container/build unchanged.
2. Keep Wrapper/Turnip/game files unchanged.
3. Change only DXVK from 2.7.1 ARM64EC to DXVK 3.0.2 ARM64EC (or newer master if 3.0.2 is insufficient).
4. Re-test the exact same OPPW4 Gallery cutscene.

Do not return to codec/MF experiments unless new evidence points there.
