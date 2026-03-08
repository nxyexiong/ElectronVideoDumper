// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include "Detours/detours.h"
#include "video_frame.h"
#include "av_frame.h"
#include "ffmpeg_conv.h"

#pragma comment(lib, "Detours/detours.lib")

// void DecryptingVideoDecoder::DeliverFrame(Decryptor::Status status, scoped_refptr<VideoFrame> frame)
using DecryptingVideoDecoderDeliverFrame_t = void (*)(void* this_ptr, int status, VideoFrame* frame);

// bool FFmpegAudioDecoder::OnNewFrame(const DecoderBuffer& buffer, bool* decoded_frame_this_loop, AVFrame* frame)
using FFmpegAudioDecoderOnNewFrame_t = bool(*)(void* this_ptr, void* buffer, bool* decoded_frame_this_loop, AVFrame* frame);

constexpr const int kKeyQuit = VK_DELETE;

void Entry();
void Hook();
void Unhook();
void HookedDecryptingVideoDecoderDeliverFrame(void* this_ptr, int status, VideoFrame* frame);
bool HookedFFmpegAudioDecoderOnNewFrame(void* this_ptr, void* buffer, bool* decoded_frame_this_loop, AVFrame* frame);

static DecryptingVideoDecoderDeliverFrame_t g_real_DecryptingVideoDecoderDeliverFrame = nullptr;
static FFmpegAudioDecoderOnNewFrame_t g_real_FFmpegAudioDecoderOnNewFrame = nullptr;
static PROCESS_INFORMATION g_video_ffmpeg_pi = {};
static HANDLE g_video_ffmpeg_stdin = NULL;
static PROCESS_INFORMATION g_audio_ffmpeg_pi = {};
static HANDLE g_audio_ffmpeg_stdin = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        std::wstring cmd = GetCommandLineW();
        if (cmd.find(L"--type=renderer") == std::wstring::npos)
            break;
        std::thread dump_thread(Entry);
        dump_thread.detach();
        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

void PrintLog(const std::wstring& msg) {
    std::wcout << L"[DMMVideoDumper]" << msg << std::endl;
}

void Entry() {
    PrintLog(L"[+] dumper attached");

    Hook();

    while (!GetAsyncKeyState(kKeyQuit))
        Sleep(100);

    Unhook();

    if (g_video_ffmpeg_stdin) {
        CloseHandle(g_video_ffmpeg_stdin);
        WaitForSingleObject(g_video_ffmpeg_pi.hProcess, INFINITE);
        CloseHandle(g_video_ffmpeg_pi.hProcess);
        CloseHandle(g_video_ffmpeg_pi.hThread);
    }

    if (g_audio_ffmpeg_stdin) {
        CloseHandle(g_audio_ffmpeg_stdin);
        WaitForSingleObject(g_audio_ffmpeg_pi.hProcess, INFINITE);
        CloseHandle(g_audio_ffmpeg_pi.hProcess);
        CloseHandle(g_audio_ffmpeg_pi.hThread);
    }

    PrintLog(L"[+] exit dumpping");
}

void Hook() {
    wchar_t exe_name[MAX_PATH] = {};
    auto rst = GetModuleFileNameW(nullptr, exe_name, MAX_PATH);
    if (rst == 0 || rst == MAX_PATH) {
        PrintLog(L"[-] cannot get exe name");
        return;
    }
    auto mod = GetModuleHandleW(exe_name);
    if (!mod) {
        PrintLog(L"[-] cannot load exe module");
        return;
    }

    /*
    to find DecryptingVideoDecoder::DeliverFrame:
    search string "decrypting_video_decoder" and "DeliverFrame" to find the function that has something like this:
    FUN_144556670(&local_178,"DeliverFrame","..\\..\\media\\filters\\decrypting_video_decoder.cc ",0x141);

    to find FFmpegAudioDecoder::OnNewFrame:
    search string "Detected midstream configuration change"
    find the offsets of AVFrame via reversing the function

    TODO: find more generic hook point
    */

    // store original
    void* ptr = (uint8_t*)mod + 0x560d7e0;
    g_real_DecryptingVideoDecoderDeliverFrame = reinterpret_cast<DecryptingVideoDecoderDeliverFrame_t>(ptr);
    ptr = (uint8_t*)mod + 0xdf7540;
    g_real_FFmpegAudioDecoderOnNewFrame = reinterpret_cast<FFmpegAudioDecoderOnNewFrame_t>(ptr);

    // hook
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    DetourAttach(reinterpret_cast<void**>(&g_real_DecryptingVideoDecoderDeliverFrame),
        reinterpret_cast<void*>(HookedDecryptingVideoDecoderDeliverFrame));
    DetourAttach(reinterpret_cast<void**>(&g_real_FFmpegAudioDecoderOnNewFrame),
        reinterpret_cast<void*>(HookedFFmpegAudioDecoderOnNewFrame));

    DetourTransactionCommit();
}

void Unhook() {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    DetourDetach(reinterpret_cast<void**>(&g_real_FFmpegAudioDecoderOnNewFrame),
        reinterpret_cast<void*>(HookedFFmpegAudioDecoderOnNewFrame));
    DetourDetach(reinterpret_cast<void**>(&g_real_DecryptingVideoDecoderDeliverFrame),
        reinterpret_cast<void*>(HookedDecryptingVideoDecoderDeliverFrame));

    DetourTransactionCommit();
}

void HookedDecryptingVideoDecoderDeliverFrame(void* this_ptr, int status, VideoFrame* frame) {
    if (!g_real_DecryptingVideoDecoderDeliverFrame)
        return;

    if (status != 0) // kSuccess
        return g_real_DecryptingVideoDecoderDeliverFrame(this_ptr, status, frame);

    const int format = frame->layout.format; // should be PIXEL_FORMAT_I420 == 1
    if (format != 1) {
        PrintLog(L"[-] cannot process formats other then PIXEL_FORMAT_I420 == 1");
        return g_real_DecryptingVideoDecoderDeliverFrame(this_ptr, status, frame);
    }

    const int width = frame->natural_size.width;
    const int height = frame->natural_size.height;

    // start ffmpeg if not already
    if (!g_video_ffmpeg_stdin) {
        std::wstring cmd =
            L"ffmpeg -y "
            L"-f rawvideo "
            L"-pixel_format yuv420p "
            L"-video_size " + std::to_wstring(width) + L"x" + std::to_wstring(height) + L" "
            L"-r 30000/1001 "
            L"-i - "
            L"-c:v libx264 -preset slow -crf 18 "
            L"video.mp4";
        if (!start_ffmpeg(g_video_ffmpeg_pi, g_video_ffmpeg_stdin, cmd))
            return g_real_DecryptingVideoDecoderDeliverFrame(this_ptr, status, frame);
    }

    for (int i = 0; i < 3; i++) {
        if (!frame->data[i].ptr || !ffmpeg_write_frame(g_video_ffmpeg_stdin, frame->data[i].ptr, frame->data[i].size)) {
            PrintLog(L"[-] cannot write video frame");
            return g_real_DecryptingVideoDecoderDeliverFrame(this_ptr, status, frame);
        }
    }

    return g_real_DecryptingVideoDecoderDeliverFrame(this_ptr, status, frame);
}

bool HookedFFmpegAudioDecoderOnNewFrame(void* this_ptr, void* buffer, bool* decoded_frame_this_loop, AVFrame* frame) {
    if (!g_real_FFmpegAudioDecoderOnNewFrame)
        return false;

    const int format = frame->format; // should be AV_SAMPLE_FMT_FLTP == 8
    if (format != 8) {
        PrintLog(L"[-] cannot process formats other then AV_SAMPLE_FMT_FLTP == 8");
        return g_real_FFmpegAudioDecoderOnNewFrame(this_ptr, buffer, decoded_frame_this_loop, frame);
    }

    const int channels = frame->nb_channels;
    const int samples = frame->nb_samples;
    const int sample_rate = frame->sample_rate;

    // start ffmpeg if not already
    if (!g_audio_ffmpeg_stdin) {
        std::wstring cmd =
            L"ffmpeg -y "
            L"-f f32le "
            L"-ac " + std::to_wstring(channels) + L" "
            L"-ar " + std::to_wstring(sample_rate) + L" "
            L"-channel_layout stereo "
            L"-i - "
            L"-c:a aac "
            L"audio.m4a";
        if (!start_ffmpeg(g_audio_ffmpeg_pi, g_audio_ffmpeg_stdin, cmd))
            return g_real_FFmpegAudioDecoderOnNewFrame(this_ptr, buffer, decoded_frame_this_loop, frame);
    }

    float** src = (float**)frame->data;
    for (int i = 0; i < samples; i++) {
        for (int ch = 0; ch < channels; ch++)
            ffmpeg_write_frame(g_audio_ffmpeg_stdin, (uint8_t*)&src[ch][i], sizeof(float));
    }

    return g_real_FFmpegAudioDecoderOnNewFrame(this_ptr, buffer, decoded_frame_this_loop, frame);
}
