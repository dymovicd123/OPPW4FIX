from pathlib import Path


def find_or_die(text, needle, start=0, label=None):
    pos = text.find(needle, start)
    if pos < 0:
        raise SystemExit(f"anchor not found: {label or needle[:80]}")
    return pos


def inject_before_trace(text, signature, code):
    pos = find_or_die(text, signature, label=signature)
    trace = find_or_die(text, "    TRACE(", pos, "TRACE after " + signature)
    return text[:trace] + code + text[trace:]


def inject_before_return_after(text, signature, code):
    pos = find_or_die(text, signature, label=signature)
    anchor = "    LeaveCriticalSection(&mixer->cs);\n\n    return hr;"
    ret = find_or_die(text, anchor, pos, "return after " + signature)
    replacement = "    LeaveCriticalSection(&mixer->cs);\n\n" + code + "    return hr;"
    return text[:ret] + replacement + text[ret + len(anchor):]


# The v7 test reached DXVA2 CreateVideoProcessor but none of the mf/session.c
# or mf/evr.c markers. Trace the component that actually owns MFVideoMixer9.
p = Path("dlls/evr/mixer.c")
s = p.read_text()

sig = "static HRESULT WINAPI video_mixer_transform_SetInputType(IMFTransform *iface, DWORD id, IMFMediaType *media_type, DWORD flags)\n{"
s = inject_before_trace(s, sig, r'''    {
        GUID oppw4_subtype = GUID_NULL;
        UINT64 oppw4_size = 0;
        UINT32 oppw4_w = 0, oppw4_h = 0;
        if (media_type)
        {
            IMFMediaType_GetGUID(media_type, &MF_MT_SUBTYPE, &oppw4_subtype);
            if (SUCCEEDED(IMFMediaType_GetUINT64(media_type, &MF_MT_FRAME_SIZE, &oppw4_size)))
            {
                oppw4_w = (UINT32)(oppw4_size >> 32);
                oppw4_h = (UINT32)oppw4_size;
            }
        }
        ERR("OPPW4TRACE MIXER_SET_INPUT mixer=%p id=%lu type=%p subtype=%s size=%ux%u flags=%#lx.\n",
                mixer, id, media_type, debugstr_guid(&oppw4_subtype), oppw4_w, oppw4_h, flags);
    }
''')

sig = "static HRESULT WINAPI video_mixer_transform_SetOutputType(IMFTransform *iface, DWORD id, IMFMediaType *type, DWORD flags)\n{"
s = inject_before_trace(s, sig, r'''    {
        GUID oppw4_subtype = GUID_NULL;
        UINT64 oppw4_size = 0;
        UINT32 oppw4_w = 0, oppw4_h = 0;
        if (type)
        {
            IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &oppw4_subtype);
            if (SUCCEEDED(IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &oppw4_size)))
            {
                oppw4_w = (UINT32)(oppw4_size >> 32);
                oppw4_h = (UINT32)oppw4_size;
            }
        }
        ERR("OPPW4TRACE MIXER_SET_OUTPUT mixer=%p id=%lu type=%p subtype=%s size=%ux%u flags=%#lx.\n",
                mixer, id, type, debugstr_guid(&oppw4_subtype), oppw4_w, oppw4_h, flags);
    }
''')

sig = "static HRESULT WINAPI video_mixer_transform_ProcessInput(IMFTransform *iface, DWORD id, IMFSample *sample, DWORD flags)\n{"
s = inject_before_trace(s, sig, r'''    {
        static unsigned int oppw4_input_count;
        unsigned int oppw4_idx = ++oppw4_input_count;
        DWORD oppw4_bytes = 0, oppw4_buffers = 0;
        LONGLONG oppw4_time = -1, oppw4_dur = -1;
        if (sample)
        {
            IMFSample_GetTotalLength(sample, &oppw4_bytes);
            IMFSample_GetBufferCount(sample, &oppw4_buffers);
            IMFSample_GetSampleTime(sample, &oppw4_time);
            IMFSample_GetSampleDuration(sample, &oppw4_dur);
        }
        if (oppw4_idx <= 120)
            ERR("OPPW4TRACE MIXER_INPUT #%u mixer=%p id=%lu sample=%p bytes=%lu buffers=%lu time=%I64d dur=%I64d flags=%#lx.\n",
                    oppw4_idx, mixer, id, sample, oppw4_bytes, oppw4_buffers, oppw4_time, oppw4_dur, flags);
    }
''')
s = inject_before_return_after(s, sig, r'''    {
        static unsigned int oppw4_input_result_count;
        unsigned int oppw4_idx = ++oppw4_input_result_count;
        if (oppw4_idx <= 120)
            ERR("OPPW4TRACE MIXER_INPUT_RESULT #%u mixer=%p id=%lu hr=%#lx.\n", oppw4_idx, mixer, id, hr);
    }
''')

sig = "static HRESULT WINAPI video_mixer_transform_ProcessOutput(IMFTransform *iface, DWORD flags, DWORD count,\n        MFT_OUTPUT_DATA_BUFFER *buffers, DWORD *status)\n{"
s = inject_before_trace(s, sig, r'''    {
        static unsigned int oppw4_output_count;
        unsigned int oppw4_idx = ++oppw4_output_count;
        IMFSample *oppw4_sample = count && buffers ? buffers[0].pSample : NULL;
        DWORD oppw4_bytes = 0, oppw4_buffers = 0;
        if (oppw4_sample)
        {
            IMFSample_GetTotalLength(oppw4_sample, &oppw4_bytes);
            IMFSample_GetBufferCount(oppw4_sample, &oppw4_buffers);
        }
        if (oppw4_idx <= 120)
            ERR("OPPW4TRACE MIXER_OUTPUT #%u mixer=%p flags=%#lx count=%lu sample=%p bytes=%lu buffers=%lu.\n",
                    oppw4_idx, mixer, flags, count, oppw4_sample, oppw4_bytes, oppw4_buffers);
    }
''')
s = inject_before_return_after(s, sig, r'''    {
        static unsigned int oppw4_output_result_count;
        unsigned int oppw4_idx = ++oppw4_output_result_count;
        if (oppw4_idx <= 120)
            ERR("OPPW4TRACE MIXER_OUTPUT_RESULT #%u mixer=%p hr=%#lx status=%#lx.\n",
                    oppw4_idx, mixer, hr, status ? *status : 0);
    }
''')

sig = "static void video_mixer_render(struct video_mixer *mixer, IDirect3DSurface9 *rt)\n{"
pos = find_or_die(s, sig, label=sig)
decl = find_or_die(s, "    unsigned int i;\n", pos, "video_mixer_render declarations")
decl_end = decl + len("    unsigned int i;\n")
s = s[:decl_end] + r'''    static unsigned int oppw4_render_count;
    unsigned int oppw4_render_idx = ++oppw4_render_count;
''' + s[decl_end:]

call = "        if (FAILED(hr = IDirectXVideoProcessor_VideoProcessBlt(mixer->processor, rt, &params, samples,\n                mixer->input_count, NULL)))"
call_pos = find_or_die(s, call, pos, "MFVideoMixer9 VideoProcessBlt call")
render_log = r'''        if (oppw4_render_idx <= 120)
        {
            D3DSURFACE_DESC oppw4_rt_desc = {0};
            HRESULT oppw4_rt_hr = IDirect3DSurface9_GetDesc(rt, &oppw4_rt_desc);
            ERR("OPPW4TRACE MIXER_RENDER #%u mixer=%p processor=%p rt=%p rt_desc_hr=%#lx fmt=%#x size=%ux%u inputs=%u target=(%ld,%ld)-(%ld,%ld).\n",
                    oppw4_render_idx, mixer, mixer->processor, rt, oppw4_rt_hr, (unsigned int)oppw4_rt_desc.Format,
                    oppw4_rt_desc.Width, oppw4_rt_desc.Height, mixer->input_count,
                    params.TargetRect.left, params.TargetRect.top, params.TargetRect.right, params.TargetRect.bottom);
            for (i = 0; i < mixer->input_count; ++i)
            {
                D3DSURFACE_DESC oppw4_src_desc = {0};
                HRESULT oppw4_src_hr = samples[i].SrcSurface ? IDirect3DSurface9_GetDesc(samples[i].SrcSurface, &oppw4_src_desc) : E_POINTER;
                ERR("OPPW4TRACE MIXER_RENDER_SRC #%u i=%u surface=%p desc_hr=%#lx fmt=%#x size=%ux%u src=(%ld,%ld)-(%ld,%ld) dst=(%ld,%ld)-(%ld,%ld) samplefmt=%u.\n",
                        oppw4_render_idx, i, samples[i].SrcSurface, oppw4_src_hr, (unsigned int)oppw4_src_desc.Format,
                        oppw4_src_desc.Width, oppw4_src_desc.Height,
                        samples[i].SrcRect.left, samples[i].SrcRect.top, samples[i].SrcRect.right, samples[i].SrcRect.bottom,
                        samples[i].DstRect.left, samples[i].DstRect.top, samples[i].DstRect.right, samples[i].DstRect.bottom,
                        samples[i].SampleFormat.SampleFormat);
            }
        }

'''
s = s[:call_pos] + render_log + s[call_pos:]
p.write_text(s)

# Trace the exact DXVA2 implementation reached in the test log.
p = Path("dlls/dxva2/main.c")
s = p.read_text()

sig = "static HRESULT WINAPI device_manager_processor_service_CreateSurface(IDirectXVideoProcessorService *iface,\n        UINT width, UINT height, UINT backbuffers, D3DFORMAT format, D3DPOOL pool, DWORD usage, DWORD dxvaType,\n        IDirect3DSurface9 **surfaces, HANDLE *shared_handle)\n{"
s = inject_before_trace(s, sig, r'''    ERR("OPPW4TRACE DXVA_CREATE_SURFACE service=%p size=%ux%u backbuffers=%u format=%#x pool=%u usage=%#lx dxvaType=%lu shared=%p.\n",
            iface, width, height, backbuffers, (unsigned int)format, pool, usage, dxvaType, shared_handle);
''')

sig = "static HRESULT WINAPI device_manager_processor_service_CreateVideoProcessor(IDirectXVideoProcessorService *iface,\n        REFGUID device, const DXVA2_VideoDesc *video_desc, D3DFORMAT rt_format, UINT max_substreams,\n        IDirectXVideoProcessor **processor)\n{"
pos = find_or_die(s, sig, label=sig)
first_fixme = find_or_die(s, "    FIXME(", pos, "CreateVideoProcessor FIXME")
s = s[:first_fixme] + r'''    if (video_desc)
        ERR("OPPW4TRACE DXVA_CREATE_PROCESSOR service=%p device=%s input_fmt=%#x size=%ux%u rt_fmt=%#x max_substreams=%u in_freq=%u/%u out_freq=%u/%u.\n",
                iface, debugstr_guid(device), (unsigned int)video_desc->Format, video_desc->SampleWidth, video_desc->SampleHeight,
                (unsigned int)rt_format, max_substreams, video_desc->InputSampleFreq.Numerator,
                video_desc->InputSampleFreq.Denominator, video_desc->OutputFrameFreq.Numerator,
                video_desc->OutputFrameFreq.Denominator);

''' + s[first_fixme:]

sig = "static HRESULT WINAPI video_processor_VideoProcessBlt(IDirectXVideoProcessor *iface, IDirect3DSurface9 *rt,\n        const DXVA2_VideoProcessBltParams *params, const DXVA2_VideoSample *samples, UINT sample_count,\n        HANDLE *complete_handle)\n{"
pos = find_or_die(s, sig, label=sig)
trace = find_or_die(s, "    TRACE(", pos, "VideoProcessBlt TRACE")
s = s[:trace] + r'''    static unsigned int oppw4_blt_count;
    unsigned int oppw4_blt_idx = ++oppw4_blt_count;
    if (oppw4_blt_idx <= 120)
    {
        D3DSURFACE_DESC oppw4_rt_desc = {0};
        HRESULT oppw4_rt_hr = rt ? IDirect3DSurface9_GetDesc(rt, &oppw4_rt_desc) : E_POINTER;
        ERR("OPPW4TRACE DXVA_BLT #%u processor=%p rt=%p desc_hr=%#lx fmt=%#x size=%ux%u samples=%u target=(%ld,%ld)-(%ld,%ld).\n",
                oppw4_blt_idx, iface, rt, oppw4_rt_hr, (unsigned int)oppw4_rt_desc.Format,
                oppw4_rt_desc.Width, oppw4_rt_desc.Height, sample_count,
                params->TargetRect.left, params->TargetRect.top, params->TargetRect.right, params->TargetRect.bottom);
    }

''' + s[trace:]

old = r'''        if (FAILED(hr = IDirect3DDevice9_StretchRect(device, samples[i].SrcSurface, &samples[i].SrcRect,
                rt, &dst_rect, D3DTEXF_POINT)))
        {
            WARN("Failed to copy sample %u, hr %#lx.\n", i, hr);
        }'''
new = r'''        {
            D3DSURFACE_DESC oppw4_src_desc = {0};
            HRESULT oppw4_desc_hr = samples[i].SrcSurface ? IDirect3DSurface9_GetDesc(samples[i].SrcSurface, &oppw4_src_desc) : E_POINTER;
            hr = IDirect3DDevice9_StretchRect(device, samples[i].SrcSurface, &samples[i].SrcRect,
                    rt, &dst_rect, D3DTEXF_POINT);
            if (oppw4_blt_idx <= 120)
                ERR("OPPW4TRACE DXVA_STRETCH #%u i=%u src=%p desc_hr=%#lx fmt=%#x size=%ux%u src_rect=(%ld,%ld)-(%ld,%ld) dst_rect=(%ld,%ld)-(%ld,%ld) hr=%#lx.\n",
                        oppw4_blt_idx, i, samples[i].SrcSurface, oppw4_desc_hr, (unsigned int)oppw4_src_desc.Format,
                        oppw4_src_desc.Width, oppw4_src_desc.Height,
                        samples[i].SrcRect.left, samples[i].SrcRect.top, samples[i].SrcRect.right, samples[i].SrcRect.bottom,
                        dst_rect.left, dst_rect.top, dst_rect.right, dst_rect.bottom, hr);
            if (FAILED(hr))
                WARN("Failed to copy sample %u, hr %#lx.\n", i, hr);
        }'''
if old not in s:
    raise SystemExit("DXVA StretchRect anchor not found")
s = s.replace(old, new, 1)
p.write_text(s)

print("v8 mixer/DXVA tracing applied")
