# OPPW4FIX — persistent context

This file exists so a new ChatGPT conversation can continue the OPPW4 FMV investigation without reconstructing the whole history.

## User / device / target

- Game: **One Piece: Pirate Warriors 4 (OPPW4)**, Windows version.
- Host: Android, Samsung flagship with **Snapdragon 8 Elite / Adreno 830**.
- Compatibility app: **Bannerlator 3.0.1**.
- Goal: make OPPW4's original in-game WMV cutscenes play normally inside the game. External playback is not an acceptable workaround.
- User prefers controlled A/B tests and does not want random setting changes or repeating already-closed experiments.

## Known-good baseline container

Return to this baseline after experiments unless a test explicitly says otherwise:

- Resolution: **1280x720**
- Proton: **GE-Proton 11.0-1 FULL winedmo ARM64EC SDK35 / 16 KB** (`proton-11.0-1-arm64ec-3` in Bannerlator)
- Graphics Driver: **Wrapper-original**
- DX wrapper: DXVK + VKD3D
- DXVK: **2.7.1 ARM64EC family**
- VKD3D: **2.8**
- Turnip: **Gen8 V34**
- GPU: Adreno 830

This baseline runs the game and Gallery, but WMV story cutscenes are automatically skipped.

## Media path — already proven working up to GPU sharing

Wine/Media Foundation logs proved the following:

- ASF/WMV opens and parses.
- Video codec is **WMV3**.
- Audio codec is **WMA Pro**.
- `winedmo` / FFmpeg recognizes both.
- Video decoder instantiates successfully.
- Audio decoder instantiates successfully.
- Video output negotiates **WMV3 -> NV12**, 1920x1080, about 29.97 fps.
- Media Foundation passes a **D3D9 device manager**.
- `IMFDXGIDeviceManager` QI fails, then `IDirect3DDeviceManager9` succeeds.

Therefore the current problem is **not missing codecs** and not a broken WMV file.

## Decisive DXVK failure

The repeated smoking-gun errors are:

```text
D3D9: Failed to write shared resource info for a texture
D3D11Device::OpenSharedResourceGeneric:
    Handle not found: 0x40000002
```

The same pair repeats with handles such as:

```text
0x40000002
0x40000042
0x40000082
...
```

DXVK reports these capabilities as present:

```text
VK_KHR_external_memory_win32
VK_KHR_external_semaphore_win32
VK_KHR_win32_keyed_mutex
```

So the failure is not simply “extension missing”. The working diagnosis is:

```text
WMV -> Media Foundation / winedmo / FFmpeg      OK
    -> WMV3/WMA Pro decoder setup              OK
    -> NV12 output                             OK
    -> D3D9 shared texture producer            FAILS
    -> Wine/sharedgpures handle bookkeeping    suspect
    -> D3D11 OpenSharedResource                Handle not found
    -> OPPW4 skips FMV
```

This closely matches old Koei Tecmo / Omega Force DXVK shared-resource cases such as Samurai Warriors 5.

## Tests already CLOSED — do not repeat without genuinely new evidence

1. GE FULL without winedmo — no fix.
2. Codec replacement / PCM-WMV experiments — no fix.
3. `d3d9=b` / WineD3D for D3D9 — whole output went black.
4. DXVK 2.3.1 ARM64EC GPLAsync — FMV still skipped.
5. DXVK 2.7.1-arm64ec-0 — FMV still skipped.
6. DXVK 2.7.1.1-sdk36-arm64ec — verified actual DXVK 2.7.1 family, FMV still skipped.
7. Upstream official x64 DXVK 2.7.1, local `dxgi.dll` + `d3d11.dll` beside OPPW4.exe — DLLs definitely loaded; FMV still skipped / black after.
8. Fresh stock Proton 11.0-2 ARM64EC — different media backend (`winegstreamer/protonmediaconverter`) but same shared-resource errors. Therefore not winedmo-specific.
9. `mf-install` / random native MF overrides — intentionally NOT done; logs already prove MF/codec setup works and native MF could destabilize the coherent GE video path.
10. Moving game E: -> C: — not indicated by logs; WMV reads/parses correctly.
11. VEGAS+VKD3D — no fix.
12. Bannerlator **3.0.1** update / new Native Rendering paths — no fix.
13. `Wrapper-gamenative` instead of `Wrapper-original` — no fix; exact same `Failed to write shared resource info` + `Handle not found` errors.
14. External Android player test: phone's stock player shows picture but no audio because it does not support WMA Pro. This is a separate Android player codec limitation and unrelated to the in-game shared-texture failure.

## Important external evidence

- The412Banner's GE FULL winedmo release notes explicitly warn that **D3D9<->D3D11 shared-texture FMV** can still fail on Android even though decoding works.
- On desktop Linux / Steam Deck the analogous Wine/Proton path can work, which points to the Android/Bionic/shared-resource bridge rather than an intrinsic OPPW4 codec problem.
- Alternative Winlator-like frontends often share the same basic stack (Wine/Proton -> DXVK -> Android Vulkan wrapper -> Turnip), so changing only the frontend is unlikely to fix this specific bug.

## Current self-fix plan

We found two still-relevant sharedgpures fixes in Wine/Valve-family development history and decided to test them ourselves.

Target patch ideas:

- sharedgpures handle lifetime / `NtDuplicateObject` fix (previously discussed as PR #312 / commit family around `1c778c0...`)
- sharedgpures failed-open / `FsContext` corruption protection (previously discussed as PR #313 / commit family around `63d1363...`)

The purpose is to test whether the current Android failure comes from **sharedgpures handle bookkeeping / lifetime** rather than DXVK codec or rendering configuration.

## Dedicated repository

All OPPW4 work must stay in:

**`dymovicd123/OPPW4FIX`**

Do NOT put OPPW4 experimental workflows or files in `Orders-app`.

The old accidental experimental branch in Orders-app was cleaned of OPPW4 files; production branches `main` / `branch2` were not used for the experiment.

## Current workflow in OPPW4FIX

File:

```text
.github/workflows/build-opppw4-sharedgpures-proton.yml
```

Workflow name:

```text
Build OPPW4 sharedgpures Proton
```

Trigger:

```text
workflow_dispatch
```

So the user manually opens GitHub Actions and presses **RUN WORKFLOW**.

The workflow currently attempts to:

1. Clone The412Banner `proton_10.0` source.
2. Verify `dlls/sharedgpures.sys/shared_resource.c` exists.
3. Apply both sharedgpures patches.
4. Hard-check expected markers before wasting full CI time.
5. Build **ARM64EC SDK35 / 16 KB** Proton 10.0-4.
6. Save a separate patched `sharedgpures.sys`.
7. Package a Bannerlator test `.wcp` named approximately:

```text
proton-10.0-4-arm64ec-opppw4-sharedgpures-sdk35.wcp
```

8. Upload one GitHub Actions artifact:

```text
opppw4-patched-proton-arm64ec-sdk35
```

The artifact should also include patch diff / hashes / driver diagnostics.

## Why Proton 10 source is used for the first build

The Proton 10 branch still contains the relevant `dlls/sharedgpures.sys/shared_resource.c` source in-tree. Proton 11's tree arrangement is different even though OPPW4 runtime logs clearly show sharedgpures behavior. Therefore Proton 10 is being used as the first controlled test bed for the two fixes.

If the patched Proton 10 build succeeds and changes the failure signature, the next likely experiment is to determine whether the resulting patched `sharedgpures.sys` can be used in a GE-Proton 11 FULL/winedmo-compatible hybrid or whether the same source fix should be ported into the exact GE FULL build pipeline.

## Next exact actions

### If the workflow has just been started

1. Query `dymovicd123/OPPW4FIX` GitHub Actions runs.
2. Identify the newest `Build OPPW4 sharedgpures Proton` run.
3. Inspect status, jobs and logs.
4. If it fails, fix the workflow/source patch in **OPPW4FIX only**, commit, then ask the user to re-run manually if connector-created commits do not trigger it.
5. If it succeeds, download the artifact and inspect its contents/hashes.

### If a valid `.wcp` exists

Install it in Bannerlator **as a separate Proton**, preserving the existing GE FULL baseline. Do not overwrite the known-good Proton.

For the first device test keep all other variables unchanged as much as possible:

- Wrapper-original
- same DXVK 2.7.1 ARM64EC
- same Turnip Gen8 V34
- same OPPW4 files
- same Gallery cutscene

Enable enough logging to tell whether these two errors change/disappear:

```text
D3D9: Failed to write shared resource info for a texture
D3D11Device::OpenSharedResourceGeneric: Handle not found
```

### Interpretation

- **If both errors disappear:** the sharedgpures patches affected the correct subsystem. Follow the next error or test whether video now renders.
- **If the wording/handles change:** still useful; compare old/new logs before changing anything else.
- **If identical errors remain:** this pair of fixes is insufficient; next suspect is lower Android/Bionic `D3DKMT <-> dma-buf <-> WineVulkan/Turnip` integration or another sharedgpures path. Do not go back to codec experiments.

## Working style for future chats

When continuing from this file:

- Do not restart diagnosis from “install codecs”.
- Do not recommend external VLC as the main solution; user wants in-game cutscenes.
- Prefer one-variable A/B experiments.
- Keep GE FULL baseline intact.
- After each test record the exact result here so the repository remains the canonical handoff context.
- If changing code/build workflow, use only `OPPW4FIX`.

## Current handoff marker

As of **2026-08-26**, the user is on the GitHub Mobile page for `Build OPPW4 sharedgpures Proton`, sees the **RUN WORKFLOW** button, and is about to start the first dedicated OPPW4FIX build.
