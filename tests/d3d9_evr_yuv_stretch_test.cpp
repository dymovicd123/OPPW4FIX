#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "user32.lib")

static FILE* g_log = nullptr;
static constexpr UINT W = 64;
static constexpr UINT H = 64;
static constexpr D3DFORMAT FMT_NV12 = (D3DFORMAT)MAKEFOURCC('N','V','1','2');

static void log_hr(const char* what, HRESULT hr) {
  std::fprintf(g_log, "%s: hr=0x%08lx\n", what, (unsigned long)hr);
  std::fflush(g_log);
}

static bool read_backbuffer_pixel(IDirect3DDevice9Ex* dev, IDirect3DSurface9* backbuffer, uint8_t out[4]) {
  IDirect3DSurface9* sys = nullptr;
  HRESULT hr = dev->CreateOffscreenPlainSurface(W, H, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr);
  log_hr("CreateOffscreenPlainSurface(readback)", hr);
  if (FAILED(hr)) return false;

  hr = dev->GetRenderTargetData(backbuffer, sys);
  log_hr("GetRenderTargetData(backbuffer)", hr);
  if (FAILED(hr)) { sys->Release(); return false; }

  D3DLOCKED_RECT lr{};
  hr = sys->LockRect(&lr, nullptr, D3DLOCK_READONLY);
  log_hr("LockRect(readback)", hr);
  if (FAILED(hr)) { sys->Release(); return false; }

  const uint8_t* p = reinterpret_cast<const uint8_t*>(lr.pBits) + (H / 2) * lr.Pitch + (W / 2) * 4;
  std::memcpy(out, p, 4);
  sys->UnlockRect();
  sys->Release();
  return true;
}

static bool fill_rgb32(IDirect3DSurface9* surface) {
  D3DLOCKED_RECT lr{};
  HRESULT hr = surface->LockRect(&lr, nullptr, D3DLOCK_DISCARD);
  log_hr("RGB32 LockRect", hr);
  if (FAILED(hr)) return false;

  for (UINT y = 0; y < H; ++y) {
    uint8_t* row = reinterpret_cast<uint8_t*>(lr.pBits) + y * lr.Pitch;
    for (UINT x = 0; x < W; ++x) {
      row[x * 4 + 0] = 255; // B
      row[x * 4 + 1] = 0;   // G
      row[x * 4 + 2] = 255; // R
      row[x * 4 + 3] = 255;
    }
  }
  surface->UnlockRect();
  return true;
}

static bool fill_yuy2(IDirect3DSurface9* surface) {
  D3DLOCKED_RECT lr{};
  HRESULT hr = surface->LockRect(&lr, nullptr, D3DLOCK_DISCARD);
  log_hr("YUY2 LockRect", hr);
  if (FAILED(hr)) return false;

  for (UINT y = 0; y < H; ++y) {
    uint8_t* row = reinterpret_cast<uint8_t*>(lr.pBits) + y * lr.Pitch;
    for (UINT x = 0; x < W; x += 2) {
      row[x * 2 + 0] = 200; // Y0
      row[x * 2 + 1] = 128; // U
      row[x * 2 + 2] = 200; // Y1
      row[x * 2 + 3] = 128; // V
    }
  }
  surface->UnlockRect();
  return true;
}

static bool fill_nv12(IDirect3DSurface9* surface) {
  D3DLOCKED_RECT lr{};
  HRESULT hr = surface->LockRect(&lr, nullptr, D3DLOCK_DISCARD);
  log_hr("NV12 LockRect", hr);
  if (FAILED(hr)) return false;

  uint8_t* base = reinterpret_cast<uint8_t*>(lr.pBits);
  for (UINT y = 0; y < H; ++y)
    std::memset(base + y * lr.Pitch, 200, W);

  uint8_t* uv = base + H * lr.Pitch;
  for (UINT y = 0; y < H / 2; ++y) {
    uint8_t* row = uv + y * lr.Pitch;
    for (UINT x = 0; x < W; x += 2) {
      row[x + 0] = 128;
      row[x + 1] = 128;
    }
  }

  surface->UnlockRect();
  return true;
}

static bool pixel_is_magenta(const uint8_t p[4]) {
  return p[0] > 240 && p[1] < 16 && p[2] > 240;
}

static bool pixel_is_bright_gray(const uint8_t p[4]) {
  int mn = std::min({(int)p[0], (int)p[1], (int)p[2]});
  int mx = std::max({(int)p[0], (int)p[1], (int)p[2]});
  return mn > 120 && (mx - mn) < 35;
}

static bool run_case(IDirect3DDevice9Ex* dev, IDirect3DSurface9* backbuffer,
                     const char* name, D3DFORMAT fmt, bool (*fill)(IDirect3DSurface9*), bool expect_magenta) {
  std::fprintf(g_log, "\n=== %s ===\n", name);
  std::fflush(g_log);

  IDirect3DSurface9* src = nullptr;
  HRESULT hr = dev->CreateOffscreenPlainSurface(W, H, fmt, D3DPOOL_DEFAULT, &src, nullptr);
  log_hr("CreateOffscreenPlainSurface(source)", hr);
  if (FAILED(hr) || !src) {
    std::fprintf(g_log, "%s RESULT: CREATE_FAIL\n", name);
    return false;
  }

  D3DSURFACE_DESC desc{};
  hr = src->GetDesc(&desc);
  log_hr("source GetDesc", hr);
  if (SUCCEEDED(hr)) {
    std::fprintf(g_log, "source desc: %ux%u fmt=0x%08x pool=%u usage=0x%lx\n",
                 desc.Width, desc.Height, (unsigned)desc.Format, (unsigned)desc.Pool, desc.Usage);
  }

  bool filled = fill(src);
  if (!filled) {
    std::fprintf(g_log, "%s RESULT: LOCK_OR_FILL_FAIL\n", name);
    src->Release();
    return false;
  }

  dev->ColorFill(backbuffer, nullptr, D3DCOLOR_XRGB(0,0,0));
  hr = dev->StretchRect(src, nullptr, backbuffer, nullptr, D3DTEXF_POINT);
  log_hr("StretchRect(source -> X8R8G8B8 backbuffer)", hr);
  if (FAILED(hr)) {
    std::fprintf(g_log, "%s RESULT: STRETCH_FAIL\n", name);
    src->Release();
    return false;
  }

  uint8_t p[4]{};
  bool read_ok = read_backbuffer_pixel(dev, backbuffer, p);
  bool pixel_ok = read_ok && (expect_magenta ? pixel_is_magenta(p) : pixel_is_bright_gray(p));
  std::fprintf(g_log, "%s backbuffer BGRA=%u,%u,%u,%u pixel_ok=%d\n",
               name, p[0], p[1], p[2], p[3], pixel_ok ? 1 : 0);
  std::fprintf(g_log, "%s RESULT: %s\n", name, pixel_ok ? "PASS" : "BLACK_OR_BAD_PIXELS");
  std::fflush(g_log);

  src->Release();
  return pixel_ok;
}

int main() {
  g_log = std::fopen("OPPW4_evr_yuv_stretch_test.txt", "wb");
  if (!g_log) return 100;
  std::fprintf(g_log, "OPPW4 EVR-path D3D9 YUV StretchRect verifier v1\n");

  HWND hwnd = CreateWindowExA(0, "STATIC", "OPPW4 EVR YUV test", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 320, 240,
                              nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
  if (!hwnd) return 101;
  ShowWindow(hwnd, SW_SHOW);

  IDirect3D9Ex* d3d9 = nullptr;
  HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9);
  log_hr("Direct3DCreate9Ex", hr);
  if (FAILED(hr)) return 102;

  D3DPRESENT_PARAMETERS pp{};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.hDeviceWindow = hwnd;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8;
  pp.BackBufferWidth = W;
  pp.BackBufferHeight = H;
  pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

  IDirect3DDevice9Ex* dev = nullptr;
  hr = d3d9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                            &pp, nullptr, &dev);
  log_hr("CreateDeviceEx", hr);
  if (FAILED(hr)) return 103;

  IDirect3DSurface9* backbuffer = nullptr;
  hr = dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
  log_hr("GetBackBuffer", hr);
  if (FAILED(hr)) return 104;

  const bool rgb = run_case(dev, backbuffer, "RGB32_BASELINE", D3DFMT_A8R8G8B8, fill_rgb32, true);
  const bool yuy2 = run_case(dev, backbuffer, "YUY2", D3DFMT_YUY2, fill_yuy2, false);
  const bool nv12 = run_case(dev, backbuffer, "NV12", FMT_NV12, fill_nv12, false);

  const char* result = nullptr;
  if (!rgb) result = "BASELINE_FAIL";
  else if (!yuy2 || !nv12) result = "YUV_PATH_FAIL";
  else result = "PASS_ALL";

  std::fprintf(g_log, "\nRESULT: %s (RGB32=%d YUY2=%d NV12=%d)\n", result, rgb ? 1 : 0, yuy2 ? 1 : 0, nv12 ? 1 : 0);
  std::fflush(g_log);

  dev->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);

  char msg[256];
  std::snprintf(msg, sizeof(msg), "%s\nRGB32=%d  YUY2=%d  NV12=%d\nSee OPPW4_evr_yuv_stretch_test.txt next to the EXE.",
                result, rgb ? 1 : 0, yuy2 ? 1 : 0, nv12 ? 1 : 0);
  MessageBoxA(hwnd, msg, "OPPW4 EVR YUV test", MB_OK | (std::strcmp(result, "PASS_ALL") == 0 ? MB_ICONINFORMATION : MB_ICONWARNING));

  backbuffer->Release();
  dev->Release();
  d3d9->Release();
  DestroyWindow(hwnd);
  std::fclose(g_log);
  return std::strcmp(result, "PASS_ALL") == 0 ? 0 : 2;
}
