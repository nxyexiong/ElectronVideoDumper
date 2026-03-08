#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>

constexpr const wchar_t* kTargetProcessName = L"DMM Player v2.exe";
constexpr const wchar_t* kDumperDllName = L"Dumper.dll";

void Inject(DWORD pid) {
    std::wcout << L"[+] start injecting to " << pid << std::endl;

    // get process handle by pid
    auto process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!process) {
        std::wcout << L"[-] open process failed: " << GetLastError() << std::endl;
        return;
    }

    // write dll path into the process
    std::filesystem::path dumper_dll_path = kDumperDllName;
    dumper_dll_path = std::filesystem::absolute(dumper_dll_path);
    auto dumper_dll_path_str = dumper_dll_path.wstring();
    uint32_t dumper_dll_path_size = dumper_dll_path_str.size() * sizeof(wchar_t);
    auto dumper_dll_path_ptr = VirtualAllocEx(process, NULL, dumper_dll_path_size, MEM_COMMIT, PAGE_READWRITE);
    if (!dumper_dll_path_ptr) {
        std::wcout << L"[-] virtual alloc failed: " << GetLastError() << std::endl;
        return;
    }
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, dumper_dll_path_ptr, dumper_dll_path_str.c_str(), dumper_dll_path_size,
        &written)) {
        std::wcout << L"[-] write process memory failed: " << GetLastError() << std::endl;
        return;
    }

    // get LoadLibraryW address
    auto kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) {
        std::wcout << L"[-] get kernel32 handle failed: " << GetLastError() << std::endl;
        return;
    }
    auto load_library_ptr = GetProcAddress(kernel32, "LoadLibraryW");
    if (!load_library_ptr) {
        std::wcout << L"[-] get LoadLibraryW failed: " << GetLastError() << std::endl;
        return;
    }

    // create remote thread
    DWORD tid = 0;
    auto thread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)load_library_ptr, dumper_dll_path_ptr,
        0, &tid);
    if (!thread) {
        std::wcout << L"[-] create remote thread failed: " << GetLastError() << std::endl;
        return;
    }
    std::wcout << L"[+] create remote thread id " << tid << std::endl;

    // wait for thread to exit
    DWORD exit_code = 0;
    while (GetExitCodeThread(thread, &exit_code)) {
        if (exit_code != 0x103) break;
        Sleep(100);
    }

    std::wcout << L"[+] successfully injected to " << pid << std::endl;
}

int main() {
    // start process
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    wchar_t cmd[] = 
        L"\"C:\\UserData\\DMMPlayer\\DMM Player v2.exe\" "
        L"--remote-debugging-port=9222 "
        L"--remote-allow-origins=http://localhost:9222 "
        L"--disable-gpu --disable-software-rasterizer --no-sandbox";
    si.cb = sizeof(si);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        std::wcout << L"[-] cannot create process" << std::endl;
        return 1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    Sleep(3000);

    // query process info
    auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        std::wcout << L"[-] cannot create process snapshot" << std::endl;
        return 1;
    }

    // inject
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, kTargetProcessName) == 0)
                Inject(pe.th32ProcessID);
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return 0;
}
