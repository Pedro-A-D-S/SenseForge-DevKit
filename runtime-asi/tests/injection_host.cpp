// injection_host.cpp — deterministic SF-3 validation host.
//
// Statically imports version.dll, so when this exe runs from a directory
// that contains the version.dll proxy, the loader binds through the proxy's
// MASM thunks into System32\version.dll AND the proxy loads senseforge.asi
// next to the exe. The host then exercises a real version API through the
// forwarding chain and stays alive long enough for the ASI worker thread to
// emit telemetry_frame lines into \\.\pipe\senseforge_ipc.

#include <windows.h>

#include <cstdio>
#include <cstdlib>

int main() {
    // These calls are forwarded: proxy thunk -> System32\version.dll.
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeA(
        "C:\\Windows\\System32\\notepad.exe", &ignored);
    std::printf("GetFileVersionInfoSizeA(notepad) -> %lu\n",
                static_cast<unsigned long>(size));

    if (size > 0) {
        void* buffer = std::malloc(size);
        if (buffer != nullptr) {
            if (GetFileVersionInfoA("C:\\Windows\\System32\\notepad.exe", 0,
                                    size, buffer)) {
                void* block = nullptr;
                unsigned block_size = 0;
                if (VerQueryValueA(buffer, "\\VarFileInfo\\Translation",
                                   &block, &block_size)) {
                    std::printf("VerQueryValueA(Translation) -> %u bytes\n",
                                block_size);
                } else {
                    std::printf("VerQueryValueA -> FALSE (forwarded ok)\n");
                }
            }
            std::free(buffer);
        }
    }

    std::printf("host: sleeping 2500ms for ASI telemetry...\n");
    std::fflush(stdout);
    Sleep(2500);
    std::printf("host: done\n");
    return 0;
}
