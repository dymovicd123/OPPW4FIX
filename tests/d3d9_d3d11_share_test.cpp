#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")

static FILE* g_log = nullptr;

static void log_hr(const char* what, HRESULT hr) {
  if (g_log) {
    std::fprintf(g_log, "%s: hr=0x%08lx\n", what, (unsigned long)hr);
    std::fflush(g_log);
  }
}

static bool read_d3d9_pixel(IDirect3DDevice9Ex* dev9, IDirect3DSurface9* src, uint8_t out[4]) {
  D3DSURFACE_DESC desc{};
  HRESULT hr = src->GetDesc(&desc);
  log_hr("D3D9 GetDesc", hr);
  if (FAILED(hr)) return false;

  IDirect3DSurface9* sys = nullptr;
  hr = dev9->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr);
  log_hr("D3D9 CreateOffscreenPlainSurface", hr);
  if (FAILED(hr)) return false;

  hr = dev9->GetRenderTargetData(src, sys);
  log_hr("D3D9 GetRenderTargetData", hr);
  if (FAILED(hr)) { sys->Release(); return false; }

  D3DLOCKED_RECT lr{};
  hr = sys->LockRect(&lr, nullptr, D3DLOCK_READONLY);
  log_hr("D3D9 LockRect", hr);
  if (FAILED(hr)) { sys->Release(); return false; }

  std::memcpy(out, lr.pBits, 4);
  sys->UnlockRect();
  sys->Release();
  return true;
}

static bool read_d3d11_pixel(ID3D11Device* dev11, ID3D11DeviceContext* ctx11, ID3D11Texture2D* shared, uint8_t out[4]) {
  D3D11_TEXTURE2D_DESC desc{};
  shared->GetDesc(&desc);
  if (g_log) {
    std::fprintf(g_log,
      "D3D11 shared desc: %ux%u mip=%u array=%u fmt=%u usage=%u bind=0x%x cpu=0x%x misc=0x%x\n",
      desc.Width, desc.Height, desc.MipLevels, desc.ArraySize, (unsigned)desc.Format,
      (unsigned)desc.Usage, desc.BindFlags, desc.CPUAccessFlags, desc.MiscFlags);
    std::fflush(g_log);
  }

  D3D11_TEXTURE2D_DESC st = desc;
  st.Usage = D3D11_USAGE_STAGING;
  st.BindFlags = 0;
  st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  st.MiscFlags = 0;

  ID3D11Texture2D* staging = nullptr;
  HRESULT hr = dev11->CreateTexture2D(&st, nullptr, &staging);
  log_hr("D3D11 CreateTexture2D(staging)", hr);
  if (FAILED(hr)) return false;

  ctx11->CopyResource(staging, shared);
  ctx11->Flush();

  D3D11_MAPPED_SUBRESOURCE map{};
  hr = ctx11->Map(staging, 0, D3D11_MAP_READ, 0, &map);
  log_hr("D3D11 Map(staging)", hr);
  if (FAILED(hr)) { staging->Release(); return false; }

  std::memcpy(out, map.pData, 4);
  ctx11->Unmap(staging, 0);
  staging->Release();
  return true;
}

static bool approx_bgra(const uint8_t p[4], uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
  const int tol = 8;
  return std::abs((int)p[0] - b) <= tol &&
         std::abs((int)p[1] - g) <= tol &&
         std::abs((int)p[2] - r) <= tol &&
         std::abs((int)p[3] - a) <= tol;
}

int main() {
  g_log = std::fopen("OPPW4_share_test.txt", "wb");
  if (!g_log) return 100;

  std::fprintf(g_log, "OPPW4 D3D9->D3D11 shared texture verifier v1\n");
  std::fflush(g_log);

  HWND hwnd = CreateWindowExA(0, "STATIC", "OPPW4 share test", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 320, 240,
                              nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
  if (!hwnd) {
    std::fprintf(g_log, "CreateWindowEx failed: %lu\n", GetLastError());
    std::fclose(g_log);
    return 101;
  }

  IDirect3D9Ex* d3d9 = nullptr;
  HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9);
  log_hr("Direct3DCreate9Ex", hr);
  if (FAILED(hr)) return 102;

  D3DPRESENT_PARAMETERS pp{};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.hDeviceWindow = hwnd;
  pp.BackBufferFormat = D3DFMT_UNKNOWN;
  pp.BackBufferWidth = 64;
  pp.BackBufferHeight = 64;
  pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

  IDirect3DDevice9Ex* dev9 = nullptr;
  hr = d3d9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                            &pp, nullptr, &dev9);
  log_hr("D3D9 CreateDeviceEx", hr);
  if (FAILED(hr)) return 103;

  D3D_FEATURE_LEVEL flOut{};
  ID3D11Device* dev11 = nullptr;
  ID3D11DeviceContext* ctx11 = nullptr;
  const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
  hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                         levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                         &dev11, &flOut, &ctx11);
  log_hr("D3D11CreateDevice", hr);
  if (FAILED(hr)) return 104;

  HANDLE sharedHandle = nullptr;
  IDirect3DTexture9* tex9 = nullptr;
  hr = dev9->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET,
                           D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                           &tex9, &sharedHandle);
  log_hr("D3D9 CreateTexture(shared)", hr);
  if (g_log) std::fprintf(g_log, "shared handle = %p\n", sharedHandle);
  if (FAILED(hr) || !sharedHandle) return 105;

  IDirect3DSurface9* surf9 = nullptr;
  hr = tex9->GetSurfaceLevel(0, &surf9);
  log_hr("D3D9 GetSurfaceLevel", hr);
  if (FAILED(hr)) return 106;

  ID3D11Texture2D* tex11 = nullptr;
  hr = dev11->OpenSharedResource(sharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex11));
  log_hr("D3D11 OpenSharedResource", hr);
  if (FAILED(hr) || !tex11) {
    std::fprintf(g_log, "RESULT: OPEN_FAIL\n");
    std::fclose(g_log);
    MessageBoxA(hwnd, "OPEN_FAIL - see OPPW4_share_test.txt", "OPPW4 share test", MB_OK | MB_ICONERROR);
    return 107;
  }

  struct TestColor { D3DCOLOR color; const char* name; uint8_t b,g,r,a; } tests[] = {
    { D3DCOLOR_ARGB(255,255,0,255), "MAGENTA", 255,0,255,255 },
    { D3DCOLOR_ARGB(255,0,255,0),   "GREEN",     0,255,0,255 },
    { D3DCOLOR_ARGB(255,0,255,255), "CYAN",    255,255,0,255 },
  };

  bool allProducer = true;
  bool allConsumer = true;

  for (const auto& t : tests) {
    hr = dev9->ColorFill(surf9, nullptr, t.color);
    log_hr(t.name, hr);
    if (FAILED(hr)) { allProducer = allConsumer = false; continue; }

    dev9->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);

    uint8_t p9[4]{};
    uint8_t p11[4]{};
    bool ok9 = read_d3d9_pixel(dev9, surf9, p9);
    bool ok11 = read_d3d11_pixel(dev11, ctx11, tex11, p11);
    bool match9 = ok9 && approx_bgra(p9, t.b,t.g,t.r,t.a);
    bool match11 = ok11 && approx_bgra(p11, t.b,t.g,t.r,t.a);
    allProducer &= match9;
    allConsumer &= match11;

    std::fprintf(g_log,
      "%s producer BGRA=%u,%u,%u,%u match=%d | consumer BGRA=%u,%u,%u,%u match=%d\n",
      t.name,
      p9[0],p9[1],p9[2],p9[3], match9 ? 1 : 0,
      p11[0],p11[1],p11[2],p11[3], match11 ? 1 : 0);
    std::fflush(g_log);
  }

  const char* result = nullptr;
  if (!allProducer) result = "PRODUCER_FAIL";
  else if (!allConsumer) result = "SHARE_CONTENT_FAIL";
  else result = "PASS";

  std::fprintf(g_log, "RESULT: %s\n", result);
  std::fflush(g_log);

  tex11->Release();
  surf9->Release();
  tex9->Release();
  ctx11->Release();
  dev11->Release();
  dev9->Release();
  d3d9->Release();
  DestroyWindow(hwnd);
  std::fclose(g_log);

  char msg[256];
  std::snprintf(msg, sizeof(msg), "%s\nSee OPPW4_share_test.txt next to the EXE.", result);
  MessageBoxA(nullptr, msg, "OPPW4 D3D9->D3D11 share test", MB_OK | (std::strcmp(result,"PASS") == 0 ? MB_ICONINFORMATION : MB_ICONWARNING));
  return std::strcmp(result, "PASS") == 0 ? 0 : 2;
}
