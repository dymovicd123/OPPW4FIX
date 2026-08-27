#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "user32.lib")

static FILE* g_log = nullptr;
static constexpr UINT W = 64;
static constexpr UINT H = 64;
static constexpr D3DFORMAT FMT_NV12 = (D3DFORMAT)MAKEFOURCC('N','V','1','2');

static void log_hr(const char* what, HRESULT hr) {
  std::fprintf(g_log, "%s: hr=0x%08lx\n", what, (unsigned long)hr);
  std::fflush(g_log);
}

static DXVA2_Fixed32 opaque_alpha() {
  DXVA2_Fixed32 a{};
  a.Value = 1;
  a.Fraction = 0;
  return a;
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

static bool fill_yuy2(IDirect3DSurface9* surface) {
  D3DLOCKED_RECT lr{};
  HRESULT hr = surface->LockRect(&lr, nullptr, D3DLOCK_DISCARD);
  log_hr("YUY2 LockRect", hr);
  if (FAILED(hr)) return false;
  for (UINT y = 0; y < H; ++y) {
    uint8_t* row = reinterpret_cast<uint8_t*>(lr.pBits) + y * lr.Pitch;
    for (UINT x = 0; x < W; x += 2) {
      row[x * 2 + 0] = 200;
      row[x * 2 + 1] = 128;
      row[x * 2 + 2] = 200;
      row[x * 2 + 3] = 128;
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

static bool bright_gray(const uint8_t p[4]) {
  int a = p[0], b = p[1], c = p[2];
  int mn = a < b ? (a < c ? a : c) : (b < c ? b : c);
  int mx = a > b ? (a > c ? a : c) : (b > c ? b : c);
  return mn > 120 && (mx - mn) < 35;
}

static bool guid_equal(const GUID& a, const GUID& b) {
  return InlineIsEqualGUID(a, b) != 0;
}

static bool run_case(IDirect3DDevice9Ex* dev,
                     IDirect3DSurface9* backbuffer,
                     IDirectXVideoProcessorService* service,
                     const char* name,
                     D3DFORMAT fmt,
                     bool (*fill)(IDirect3DSurface9*)) {
  std::fprintf(g_log, "\n=== %s ===\n", name);

  DXVA2_VideoDesc desc{};
  desc.SampleWidth = W;
  desc.SampleHeight = H;
  desc.SampleFormat.SampleFormat = DXVA2_SampleProgressiveFrame;
  desc.Format = fmt;
  desc.InputSampleFreq.Numerator = 30;
  desc.InputSampleFreq.Denominator = 1;
  desc.OutputFrameFreq.Numerator = 30;
  desc.OutputFrameFreq.Denominator = 1;

  UINT guid_count = 0;
  GUID* guids = nullptr;
  HRESULT hr = service->GetVideoProcessorDeviceGuids(&desc, &guid_count, &guids);
  log_hr("GetVideoProcessorDeviceGuids", hr);
  std::fprintf(g_log, "processor guid count=%u\n", guid_count);
  if (FAILED(hr) || !guid_count || !guids) return false;

  GUID chosen = guids[0];
  for (UINT i = 0; i < guid_count; ++i) {
    if (guid_equal(guids[i], DXVA2_VideoProcProgressiveDevice)) {
      chosen = guids[i];
      break;
    }
  }
  CoTaskMemFree(guids);

  UINT rt_count = 0;
  D3DFORMAT* rt_formats = nullptr;
  hr = service->GetVideoProcessorRenderTargets(chosen, &desc, &rt_count, &rt_formats);
  log_hr("GetVideoProcessorRenderTargets", hr);
  std::fprintf(g_log, "render target count=%u\n", rt_count);
  if (SUCCEEDED(hr) && rt_formats) CoTaskMemFree(rt_formats);

  IDirectXVideoProcessor* processor = nullptr;
  hr = service->CreateVideoProcessor(chosen, &desc, D3DFMT_X8R8G8B8, 0, &processor);
  log_hr("CreateVideoProcessor", hr);
  if (FAILED(hr) || !processor) return false;

  IDirect3DSurface9* src = nullptr;
  hr = service->CreateSurface(W, H, 0, fmt, D3DPOOL_DEFAULT, 0,
                              DXVA2_VideoProcessorRenderTarget, &src, nullptr);
  log_hr("DXVA2 CreateSurface(source)", hr);
  if (FAILED(hr) || !src) { processor->Release(); return false; }

  IDirect3DSurface9* rt = nullptr;
  hr = service->CreateSurface(W, H, 0, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, 0,
                              DXVA2_VideoProcessorRenderTarget, &rt, nullptr);
  log_hr("DXVA2 CreateSurface(output)", hr);
  if (FAILED(hr) || !rt) { src->Release(); processor->Release(); return false; }

  if (!fill(src)) {
    rt->Release(); src->Release(); processor->Release();
    return false;
  }

  RECT full{0, 0, (LONG)W, (LONG)H};
  DXVA2_VideoProcessBltParams params{};
  params.TargetFrame = 0;
  params.TargetRect = full;
  params.ConstrictionSize.cx = W;
  params.ConstrictionSize.cy = H;
  params.BackgroundColor.Alpha = 0xffff;
  params.BackgroundColor.Y = 0x1000;
  params.BackgroundColor.Cb = 0x8000;
  params.BackgroundColor.Cr = 0x8000;
  params.DestFormat.SampleFormat = DXVA2_SampleProgressiveFrame;
  params.Alpha = opaque_alpha();

  DXVA2_VideoSample sample{};
  sample.Start = 0;
  sample.End = 333333;
  sample.SampleFormat = desc.SampleFormat;
  sample.SrcSurface = src;
  sample.SrcRect = full;
  sample.DstRect = full;
  sample.PlanarAlpha = opaque_alpha();

  hr = processor->VideoProcessBlt(rt, &params, &sample, 1, nullptr);
  log_hr("IDirectXVideoProcessor::VideoProcessBlt", hr);
  if (FAILED(hr)) {
    rt->Release(); src->Release(); processor->Release();
    return false;
  }

  dev->ColorFill(backbuffer, nullptr, D3DCOLOR_XRGB(0,0,0));
  hr = dev->StretchRect(rt, nullptr, backbuffer, nullptr, D3DTEXF_POINT);
  log_hr("StretchRect(DXVA2 output -> backbuffer)", hr);

  uint8_t p[4]{};
  bool read_ok = SUCCEEDED(hr) && read_backbuffer_pixel(dev, backbuffer, p);
  bool ok = read_ok && bright_gray(p);
  std::fprintf(g_log, "%s backbuffer BGRA=%u,%u,%u,%u pixel_ok=%d\n",
               name, p[0], p[1], p[2], p[3], ok ? 1 : 0);
  std::fprintf(g_log, "%s RESULT: %s\n", name, ok ? "PASS" : "FAIL");
  std::fflush(g_log);

  rt->Release();
  src->Release();
  processor->Release();
  return ok;
}

int main() {
  g_log = std::fopen("OPPW4_dxva2_evr_processor_test.txt", "wb");
  if (!g_log) return 100;
  std::fprintf(g_log, "OPPW4 Wine DXVA2 EVR processor verifier v1\n");

  HWND hwnd = CreateWindowExA(0, "STATIC", "OPPW4 DXVA2 EVR test", WS_OVERLAPPEDWINDOW,
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

  UINT token = 0;
  IDirect3DDeviceManager9* manager = nullptr;
  hr = DXVA2CreateDirect3DDeviceManager9(&token, &manager);
  log_hr("DXVA2CreateDirect3DDeviceManager9", hr);
  if (FAILED(hr) || !manager) return 105;

  hr = manager->ResetDevice(dev, token);
  log_hr("IDirect3DDeviceManager9::ResetDevice", hr);
  if (FAILED(hr)) return 106;

  HANDLE hdevice = nullptr;
  hr = manager->OpenDeviceHandle(&hdevice);
  log_hr("IDirect3DDeviceManager9::OpenDeviceHandle", hr);
  if (FAILED(hr)) return 107;

  IDirectXVideoProcessorService* service = nullptr;
  hr = manager->GetVideoService(hdevice, IID_IDirectXVideoProcessorService, (void**)&service);
  log_hr("IDirect3DDeviceManager9::GetVideoService", hr);
  if (FAILED(hr) || !service) return 108;

  const bool yuy2 = run_case(dev, backbuffer, service, "DXVA2_YUY2", D3DFMT_YUY2, fill_yuy2);
  const bool nv12 = run_case(dev, backbuffer, service, "DXVA2_NV12", FMT_NV12, fill_nv12);

  const char* result = (yuy2 && nv12) ? "PASS_ALL" : "DXVA2_PATH_FAIL";
  std::fprintf(g_log, "\nRESULT: %s (YUY2=%d NV12=%d)\n", result, yuy2 ? 1 : 0, nv12 ? 1 : 0);
  std::fflush(g_log);

  char msg[256];
  std::snprintf(msg, sizeof(msg), "%s\nYUY2=%d  NV12=%d\nSee OPPW4_dxva2_evr_processor_test.txt next to the EXE.",
                result, yuy2 ? 1 : 0, nv12 ? 1 : 0);
  MessageBoxA(hwnd, msg, "OPPW4 DXVA2 EVR test", MB_OK | ((yuy2 && nv12) ? MB_ICONINFORMATION : MB_ICONWARNING));

  service->Release();
  manager->CloseDeviceHandle(hdevice);
  manager->Release();
  backbuffer->Release();
  dev->Release();
  d3d9->Release();
  DestroyWindow(hwnd);
  std::fclose(g_log);
  return (yuy2 && nv12) ? 0 : 2;
}
