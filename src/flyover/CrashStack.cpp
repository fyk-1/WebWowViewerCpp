//
// The stack of a crashing thread on stderr, for the unhandled-exception filter in main.cpp. Names come from the PDB the
// linker writes beside the exe (`/DEBUG`), public symbols only: a function and an offset, no line.
//

#include "CrashStack.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "dbghelp.lib")

void printCrashStack(void *exceptionPointers) {
    auto info = static_cast<EXCEPTION_POINTERS *>(exceptionPointers);
    auto record = info->ExceptionRecord;
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        auto kind = record->ExceptionInformation[0];
        fprintf(stderr, "crash: %s of 0x%llx\n", kind == 0 ? "read" : kind == 1 ? "write" : "execute",
                (unsigned long long) record->ExceptionInformation[1]);
    }

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    CONTEXT context = *info->ContextRecord;
    // A call through a null pointer faults at address 0, where nothing unwinds: start from the return address the call pushed.
    if (context.Rip == 0 && context.Rsp != 0) {
        context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
        context.Rsp += 8;
    }
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 40; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
        DWORD64 address = frame.AddrPC.Offset;
        if (address == 0) break;

        char moduleName[MAX_PATH] = "?";
        DWORD64 base = SymGetModuleBase64(process, address);
        // dbghelp misses modules loaded after its first enumeration; the loader still knows them.
        HMODULE owner = nullptr;
        if (base == 0 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                            reinterpret_cast<LPCSTR>(address), &owner)) {
            base = reinterpret_cast<DWORD64>(owner);
        }
        if (base != 0 && GetModuleFileNameA((HMODULE) base, moduleName, MAX_PATH) > 0) {
            if (auto slash = strrchr(moduleName, '\\')) memmove(moduleName, slash + 1, strlen(slash));
        }

        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512];
        auto symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, address, &displacement, symbol)) {
            fprintf(stderr, "crash: #%d %s!%s+0x%llx\n", i, moduleName, symbol->Name, (unsigned long long) displacement);
        } else {
            fprintf(stderr, "crash: #%d %s+0x%llx\n", i, moduleName, (unsigned long long) (address - base));
        }
    }
    fflush(stderr);
}
#else
void printCrashStack(void *) {}
#endif
