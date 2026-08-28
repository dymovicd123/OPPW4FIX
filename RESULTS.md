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

### DXVA2 / EVR processor verifier

Focused verifier: `tests/dxva2_evr_processor_test.cpp`.

Device result in the same Proton 11 + DXVK 3.0.99 container: **PASS_ALL**.

- D3D9Ex device manager and `GetVideoService` succeeded.
- Progressive DXVA2 video processor creation succeeded.
- YUY2 source/output surface path, `VideoProcessBlt`, `StretchRect` and readback all succeeded; BGRA `185,185,185,255`.
- NV12 source/output surface path, `VideoProcessBlt`, `StretchRect` and readback all succeeded; BGRA `214,214,214,255`.

Conclusion: generic Wine DXVA2 video-processor operation is not fundamentally broken on this stack.

### Actual OPPW4 WMV decoder output verifier

Focused verifier: `tests/mf_decoder_output_test.cpp`.

Tested the exact problem asset:

`File/REGION/WW/Movie/JK035.wmv`

Observed:

- Native video subtype is I420 (`0x30323449`).
- Native frame size is 1920x1080.
- SourceReader successfully negotiates NV12 output.
- First frame is flat black luma, as expected for the opening frame.
- Subsequent decoded frames contain strongly changing image data; 300 of the next 300 samples were dynamic with changing hashes.
- Final result: `DECODE_HAS_IMAGE_DATA`.

Conclusion: the exact OPPW4 WMV asset decodes to real image data inside Wine/Media Foundation. Decoder output is not the black-screen cause.

### EVR planar-negotiation rejection experiment

Diagnostic Proton 11 build `11.0.3.2-arm64ec` changed `dlls/evr/evr.c` so DirectShow EVR input negotiation rejects I420/IYUV/YV12 and should force NV12/YUY2 when possible.

Device result with DXVK 3.0.99:

- Cutscene still starts.
- Audio/subtitles still work.
- Video remains black.

Conclusion: the simple theory that OPPW4 connects I420 directly to `evr_render()` and hits its missing planar-copy branch is not sufficient.

### Real-game EVR input trace v3b

Diagnostic Proton 11 build `11.0.3.4-arm64ec` placed `ERR()` traces directly in DirectShow `evr_render()` / copy / mixer notification path.

The Bannerlator `wine_debug.log` was captured with `WINEDEBUG=+err`.

Result:

- There are **zero** `OPPW4TRACE` records from `evr_render()`.
- The game and `DXVK v3.0.99-arm64ec` are definitely active in the same log.
- During video initialization the log repeatedly shows `mfplat:stream_handler_BeginCreateObject`, Proton/WineGStreamer activity, followed by DXVA2 `GetVideoProcessorDeviceGuids` and `CreateVideoProcessor` for the progressive processor GUID.

Interpretation: OPPW4 is very likely not using the DirectShow `CLSID_EnhancedVideoRenderer -> evr_render()` wrapper path we instrumented. The more likely real path is direct Media Foundation use of `MFVideoMixer9` / `MFVideoPresenter9` plus DXVA2.

Next trace target is therefore:

`MF sample -> MFVideoMixer9 ProcessInput -> VideoProcessBlt -> MFVideoPresenter9 -> StretchRect -> SwapChain Present`

Workflow prepared for this: `.github/workflows/build-opppw4-p11-evr-trace-v4.yml`.

## 2026-08-28

### GitHub Actions runner/billing blocker

The v4 workflow failed before runner assignment. To distinguish a bad workflow from an account/runner problem, a temporary smoke workflow was created with only `runs-on: ubuntu-latest` and `echo runner-ok`.

That trivial job also failed immediately with no steps and no assigned runner. At the same time GitHub's public status page reported Actions operational.

This strongly indicates an account-side Actions usage/billing/budget block rather than a v4 YAML or Proton compile failure. The repository is private, so GitHub-hosted Actions consumes the account's included monthly minutes.

A separate waste source was found: `build-opppw4-sharedgpures-proton.yml` had an unrestricted `push` trigger and therefore started a full Proton build on every commit. Its run number had already reached the mid-30s. The trigger has now been restricted to changes to that workflow itself plus manual `workflow_dispatch`, preventing future unrelated commits from launching expensive Proton 10 builds.

Temporary smoke workflow was removed after diagnosis.

The billing page confirmed `2000 / 2000` included Actions minutes used. The repository was then switched to public, after which the same v4 job immediately received a GitHub-hosted runner and completed successfully.

### EVR mixer/presenter trace v4 — build success, first device capture INVALID

Workflow: `.github/workflows/build-opppw4-p11-evr-trace-v4.yml`.

Run `33120271314` completed successfully after the repository became public. Artifact `opppw4-p11-evr-trace-v4-arm64ec-sdk35` contains the real WCP:

- expected WCP size: `108611183` bytes;
- expected SHA-256: `f8da0c2c32729de7ba7abb25a739c2881c47e41fdc0f341a38bd19ddcc0d28ab`.

The first WCP file handed to the device through ChatGPT was accidentally truncated locally to exactly `2621440` bytes and had SHA-256 `3d8a466730161591db821565c8486507ab74f728e43c61fe4c8f120cc80a6843`.

Therefore the subsequent device log is **not a valid v4 result**. It contains no `OPPW4TRACE` markers, while DXVK 3.0.99 and the normal MF/DXVA2 initialization are visible, but that absence must not be interpreted until the full 108611183-byte WCP is installed.

Next action: install the correctly extracted full WCP from the Actions artifact, keep `WINEDEBUG=+err`, reproduce the black cutscene for 5–10 seconds, then export a fresh `wine_debug.log` from Bannerlator Log Manager.
