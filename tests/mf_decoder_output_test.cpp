#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

static FILE *g_log;

static void log_hr(const char *what, HRESULT hr)
{
    std::fprintf(g_log, "%s: hr=0x%08lx\n", what, (unsigned long)hr);
    std::fflush(g_log);
}

static void log_guid(const char *what, const GUID &guid)
{
    wchar_t wbuf[64] = {};
    char buf[128] = {};
    StringFromGUID2(guid, wbuf, 64);
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, sizeof(buf), nullptr, nullptr);
    std::fprintf(g_log, "%s: %s (Data1=0x%08lx)\n", what, buf, (unsigned long)guid.Data1);
}

static bool pick_file(wchar_t path[MAX_PATH])
{
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Video files\0*.wmv;*.asf;*.avi;*.mp4;*.mkv\0All files\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&ofn) != FALSE;
}

struct frame_stats
{
    unsigned minv = 255;
    unsigned maxv = 0;
    double mean = 0.0;
    uint64_t hash = 1469598103934665603ull;
    size_t count = 0;
};

static void add_value(frame_stats &s, uint8_t v)
{
    if (v < s.minv) s.minv = v;
    if (v > s.maxv) s.maxv = v;
    s.mean += v;
    s.hash ^= v;
    s.hash *= 1099511628211ull;
    ++s.count;
}

static frame_stats analyze_buffer(const GUID &subtype, const uint8_t *data, DWORD len, UINT32 width, UINT32 height)
{
    frame_stats s{};
    size_t pixels = (size_t)width * height;

    if (IsEqualGUID(subtype, MFVideoFormat_NV12) || IsEqualGUID(subtype, MFVideoFormat_YV12)
            || IsEqualGUID(subtype, MFVideoFormat_I420) || IsEqualGUID(subtype, MFVideoFormat_IYUV))
    {
        size_t n = len < pixels ? len : pixels;
        for (size_t i = 0; i < n; ++i) add_value(s, data[i]);
    }
    else if (IsEqualGUID(subtype, MFVideoFormat_YUY2) || IsEqualGUID(subtype, MFVideoFormat_UYVY))
    {
        bool uyvy = IsEqualGUID(subtype, MFVideoFormat_UYVY);
        size_t n = len < pixels * 2 ? len : pixels * 2;
        for (size_t i = uyvy ? 1 : 0; i < n; i += 2) add_value(s, data[i]);
    }
    else if (IsEqualGUID(subtype, MFVideoFormat_RGB32))
    {
        size_t n = len < pixels * 4 ? len : pixels * 4;
        for (size_t i = 0; i + 3 < n; i += 4)
        {
            unsigned b = data[i + 0], g = data[i + 1], r = data[i + 2];
            add_value(s, (uint8_t)((29 * b + 150 * g + 77 * r) >> 8));
        }
    }
    else
    {
        for (DWORD i = 0; i < len; ++i) add_value(s, data[i]);
    }

    if (s.count) s.mean /= (double)s.count;
    else s.minv = 0;
    return s;
}

static HRESULT try_output_type(IMFSourceReader *reader, const GUID &subtype, const char *name)
{
    IMFMediaType *type = nullptr;
    HRESULT hr = MFCreateMediaType(&type);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, subtype);
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type);
    std::fprintf(g_log, "Try output %s: hr=0x%08lx\n", name, (unsigned long)hr);
    if (type) type->Release();
    return hr;
}

int wmain()
{
    g_log = std::fopen("OPPW4_mf_decoder_output_test.txt", "wb");
    if (!g_log) return 100;
    std::fprintf(g_log, "OPPW4 actual media decoder output verifier v1\n");

    wchar_t path[MAX_PATH] = {};
    if (!pick_file(path))
    {
        std::fprintf(g_log, "No file selected. GetLastError=%lu\n", GetLastError());
        std::fclose(g_log);
        return 101;
    }

    char utf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
    std::fprintf(g_log, "Selected file: %s\n", utf8);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    log_hr("CoInitializeEx", hr);
    bool coinit = SUCCEEDED(hr);

    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    log_hr("MFStartup", hr);
    if (FAILED(hr)) return 102;

    IMFSourceReader *reader = nullptr;
    hr = MFCreateSourceReaderFromURL(path, nullptr, &reader);
    log_hr("MFCreateSourceReaderFromURL", hr);
    if (FAILED(hr) || !reader)
    {
        MessageBoxA(nullptr, "MFCreateSourceReaderFromURL failed. Send the log file.", "OPPW4 decoder test", MB_OK | MB_ICONWARNING);
        MFShutdown();
        if (coinit) CoUninitialize();
        std::fclose(g_log);
        return 103;
    }

    IMFMediaType *native_type = nullptr;
    hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &native_type);
    log_hr("GetNativeMediaType(video,0)", hr);
    if (SUCCEEDED(hr) && native_type)
    {
        GUID major{}, subtype{};
        if (SUCCEEDED(native_type->GetGUID(MF_MT_MAJOR_TYPE, &major))) log_guid("Native major", major);
        if (SUCCEEDED(native_type->GetGUID(MF_MT_SUBTYPE, &subtype))) log_guid("Native subtype", subtype);
        UINT32 w = 0, h = 0;
        if (SUCCEEDED(MFGetAttributeSize(native_type, MF_MT_FRAME_SIZE, &w, &h)))
            std::fprintf(g_log, "Native frame size: %ux%u\n", w, h);
        native_type->Release();
    }

    const GUID *selected = nullptr;
    if (SUCCEEDED(try_output_type(reader, MFVideoFormat_NV12, "NV12"))) selected = &MFVideoFormat_NV12;
    else if (SUCCEEDED(try_output_type(reader, MFVideoFormat_YUY2, "YUY2"))) selected = &MFVideoFormat_YUY2;
    else if (SUCCEEDED(try_output_type(reader, MFVideoFormat_RGB32, "RGB32"))) selected = &MFVideoFormat_RGB32;

    if (!selected)
    {
        MessageBoxA(nullptr, "No supported uncompressed output type. Send the log file.", "OPPW4 decoder test", MB_OK | MB_ICONWARNING);
        reader->Release();
        MFShutdown();
        if (coinit) CoUninitialize();
        std::fclose(g_log);
        return 104;
    }

    IMFMediaType *current = nullptr;
    UINT32 width = 0, height = 0;
    GUID output_subtype = *selected;
    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current);
    log_hr("GetCurrentMediaType", hr);
    if (SUCCEEDED(hr) && current)
    {
        current->GetGUID(MF_MT_SUBTYPE, &output_subtype);
        log_guid("Current output subtype", output_subtype);
        if (SUCCEEDED(MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &width, &height)))
            std::fprintf(g_log, "Current frame size: %ux%u\n", width, height);
        current->Release();
    }

    unsigned sample_count = 0, data_samples = 0, dynamic_samples = 0, changed_hashes = 0;
    uint64_t previous_hash = 0;
    bool have_previous_hash = false;
    LONGLONG first_ts = -1, last_ts = -1;

    for (unsigned iteration = 0; iteration < 360; ++iteration)
    {
        DWORD actual_stream = 0, flags = 0;
        LONGLONG ts = 0;
        IMFSample *sample = nullptr;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &actual_stream, &flags, &ts, &sample);
        if (FAILED(hr))
        {
            log_hr("ReadSample", hr);
            break;
        }
        ++sample_count;
        if (flags)
            std::fprintf(g_log, "ReadSample[%u] flags=0x%08lx ts=%lld sample=%p\n", iteration, (unsigned long)flags, (long long)ts, sample);
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            if (sample) sample->Release();
            break;
        }
        if (!sample) continue;

        if (first_ts < 0) first_ts = ts;
        last_ts = ts;

        IMFMediaBuffer *buffer = nullptr;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (SUCCEEDED(hr) && buffer)
        {
            BYTE *data = nullptr;
            DWORD max_len = 0, cur_len = 0;
            hr = buffer->Lock(&data, &max_len, &cur_len);
            if (SUCCEEDED(hr))
            {
                frame_stats stats = analyze_buffer(output_subtype, data, cur_len, width, height);
                bool dynamic = stats.count && (stats.maxv - stats.minv >= 8);
                if (dynamic) ++dynamic_samples;
                if (have_previous_hash && stats.hash != previous_hash) ++changed_hashes;
                previous_hash = stats.hash;
                have_previous_hash = true;
                ++data_samples;

                if (data_samples <= 20 || (data_samples % 30) == 0)
                    std::fprintf(g_log, "Frame %u ts=%lld bytes=%lu min=%u max=%u mean=%.2f range=%u hash=%016llx dynamic=%d\n",
                                 data_samples, (long long)ts, (unsigned long)cur_len, stats.minv, stats.maxv,
                                 stats.mean, stats.maxv - stats.minv, (unsigned long long)stats.hash, dynamic ? 1 : 0);
                buffer->Unlock();
            }
            else log_hr("IMFMediaBuffer::Lock", hr);
            buffer->Release();
        }
        else log_hr("ConvertToContiguousBuffer", hr);

        sample->Release();
        if (first_ts >= 0 && ts - first_ts >= 100000000ll) break; /* ~10 seconds */
    }

    bool has_picture_data = dynamic_samples > 0 && changed_hashes > 0;
    std::fprintf(g_log, "\nSUMMARY samples=%u data_samples=%u dynamic_samples=%u changed_hashes=%u first_ts=%lld last_ts=%lld\n",
                 sample_count, data_samples, dynamic_samples, changed_hashes, (long long)first_ts, (long long)last_ts);
    std::fprintf(g_log, "RESULT: %s\n", has_picture_data ? "DECODE_HAS_IMAGE_DATA" : "DECODE_FLAT_OR_EMPTY");
    std::fflush(g_log);

    char msg[512];
    std::snprintf(msg, sizeof(msg), "%s\nframes=%u dynamic=%u changed=%u\nSee OPPW4_mf_decoder_output_test.txt next to the EXE.",
                  has_picture_data ? "DECODE_HAS_IMAGE_DATA" : "DECODE_FLAT_OR_EMPTY",
                  data_samples, dynamic_samples, changed_hashes);
    MessageBoxA(nullptr, msg, "OPPW4 decoder output test", MB_OK | (has_picture_data ? MB_ICONINFORMATION : MB_ICONWARNING));

    reader->Release();
    MFShutdown();
    if (coinit) CoUninitialize();
    std::fclose(g_log);
    return has_picture_data ? 0 : 2;
}
