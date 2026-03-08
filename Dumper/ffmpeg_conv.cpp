#include "pch.h"
#include <string>
#include <vector>
#include "ffmpeg_conv.h"

extern void PrintLog(const std::wstring& msg);

bool start_ffmpeg(PROCESS_INFORMATION& pi, HANDLE& ffmpeg_stdin_write, const std::wstring& cmd) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdin_read = NULL;
    HANDLE stdin_write = NULL;

    // Create pipe for FFmpeg stdin
    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)) {
        PrintLog(L"[-] create pipe failed");
        return false;
    }

    // Ensure the write handle is NOT inherited
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdin_read;     // FFmpeg reads from here
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    std::vector<wchar_t> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back(L'\0');
    BOOL ok = CreateProcessW(NULL, cmdline.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    // Parent no longer needs read end
    CloseHandle(stdin_read);

    if (!ok) {
        PrintLog(L"[-] create ffmpeg process failed");
        CloseHandle(stdin_write);
        return false;
    }

    ffmpeg_stdin_write = stdin_write;
    return true;
}

bool ffmpeg_write_frame(HANDLE pipe, const uint8_t* data, const size_t size) {
    DWORD written = 0;
    return WriteFile(pipe, data, size, &written, NULL) && written == size;
}
