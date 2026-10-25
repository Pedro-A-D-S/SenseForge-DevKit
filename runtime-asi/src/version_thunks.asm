; version_thunks.asm — x64 export thunks for the version.dll proxy (SF-3).
;
; MSVC does not support __declspec(naked) on x64, so forwarding goes through
; these one-instruction indirect jumps (FF /4, RIP-relative) into
; g_versionProcs, which version_proxy.cpp resolves from
; C:\Windows\System32\version.dll during DLL_PROCESS_ATTACH — before the
; loader resolves any import thunk against this module (imports fire only
; after DllMain returns TRUE).
;
; The jump is signature-agnostic: registers/stack are untouched, so every
; calling convention passes through byte-for-byte.

EXTERN g_versionProcs:QWORD          ; FARPROC[17], filled in DllMain

.code

PUBLIC  GetFileVersionInfoA
PUBLIC  GetFileVersionInfoByHandle
PUBLIC  GetFileVersionInfoExA
PUBLIC  GetFileVersionInfoExW
PUBLIC  GetFileVersionInfoSizeA
PUBLIC  GetFileVersionInfoSizeExA
PUBLIC  GetFileVersionInfoSizeExW
PUBLIC  GetFileVersionInfoSizeW
PUBLIC  GetFileVersionInfoW
PUBLIC  VerFindFileA
PUBLIC  VerFindFileW
PUBLIC  VerInstallFileA
PUBLIC  VerInstallFileW
PUBLIC  VerLanguageNameA
PUBLIC  VerLanguageNameW
PUBLIC  VerQueryValueA
PUBLIC  VerQueryValueW

GetFileVersionInfoA PROC
    jmp QWORD PTR [g_versionProcs + 8*0]
GetFileVersionInfoA ENDP

GetFileVersionInfoByHandle PROC
    jmp QWORD PTR [g_versionProcs + 8*1]
GetFileVersionInfoByHandle ENDP

GetFileVersionInfoExA PROC
    jmp QWORD PTR [g_versionProcs + 8*2]
GetFileVersionInfoExA ENDP

GetFileVersionInfoExW PROC
    jmp QWORD PTR [g_versionProcs + 8*3]
GetFileVersionInfoExW ENDP

GetFileVersionInfoSizeA PROC
    jmp QWORD PTR [g_versionProcs + 8*4]
GetFileVersionInfoSizeA ENDP

GetFileVersionInfoSizeExA PROC
    jmp QWORD PTR [g_versionProcs + 8*5]
GetFileVersionInfoSizeExA ENDP

GetFileVersionInfoSizeExW PROC
    jmp QWORD PTR [g_versionProcs + 8*6]
GetFileVersionInfoSizeExW ENDP

GetFileVersionInfoSizeW PROC
    jmp QWORD PTR [g_versionProcs + 8*7]
GetFileVersionInfoSizeW ENDP

GetFileVersionInfoW PROC
    jmp QWORD PTR [g_versionProcs + 8*8]
GetFileVersionInfoW ENDP

VerFindFileA PROC
    jmp QWORD PTR [g_versionProcs + 8*9]
VerFindFileA ENDP

VerFindFileW PROC
    jmp QWORD PTR [g_versionProcs + 8*10]
VerFindFileW ENDP

VerInstallFileA PROC
    jmp QWORD PTR [g_versionProcs + 8*11]
VerInstallFileA ENDP

VerInstallFileW PROC
    jmp QWORD PTR [g_versionProcs + 8*12]
VerInstallFileW ENDP

VerLanguageNameA PROC
    jmp QWORD PTR [g_versionProcs + 8*13]
VerLanguageNameA ENDP

VerLanguageNameW PROC
    jmp QWORD PTR [g_versionProcs + 8*14]
VerLanguageNameW ENDP

VerQueryValueA PROC
    jmp QWORD PTR [g_versionProcs + 8*15]
VerQueryValueA ENDP

VerQueryValueW PROC
    jmp QWORD PTR [g_versionProcs + 8*16]
VerQueryValueW ENDP

END
