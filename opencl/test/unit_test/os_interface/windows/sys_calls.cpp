/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "sys_calls.h"

namespace NEO {

namespace SysCalls {

constexpr uintptr_t dummyHandle = static_cast<uintptr_t>(0x7);

BOOL systemPowerStatusRetVal = 1;
BYTE systemPowerStatusACLineStatusOverride = 1;
HMODULE handleValue = reinterpret_cast<HMODULE>(dummyHandle);
const wchar_t *currentLibraryPath = L"";

HANDLE createEvent(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName) {
    return reinterpret_cast<HANDLE>(dummyHandle);
}

BOOL closeHandle(HANDLE hObject) {
    return (reinterpret_cast<HANDLE>(dummyHandle) == hObject) ? TRUE : FALSE;
}

BOOL getSystemPowerStatus(LPSYSTEM_POWER_STATUS systemPowerStatusPtr) {
    systemPowerStatusPtr->ACLineStatus = systemPowerStatusACLineStatusOverride;
    return systemPowerStatusRetVal;
}
BOOL getModuleHandle(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE *phModule) {
    constexpr auto expectedFlags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (dwFlags != expectedFlags) {
        return FALSE;
    }
    *phModule = handleValue;
    return TRUE;
}
DWORD getModuleFileName(HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    if (hModule != handleValue) {
        return FALSE;
    }
    lstrcpyW(lpFilename, currentLibraryPath);
    return TRUE;
}

char *openCLDriverName = "igdrcl.dll";

char *getenv(const char *variableName) {
    if (strcmp(variableName, "OpenCLDriverName") == 0) {
        return openCLDriverName;
    }
    return ::getenv(variableName);
}
} // namespace SysCalls

} // namespace NEO
