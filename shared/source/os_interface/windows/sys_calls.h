/*
 * Copyright (C) 2018-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/os_interface/sys_calls_common.h"
#include "shared/source/os_interface/windows/windows_wrapper.h"

namespace NEO {

namespace SysCalls {

DWORD getLastError();
HANDLE createEvent(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName);
BOOL closeHandle(HANDLE hObject);
BOOL getSystemPowerStatus(LPSYSTEM_POWER_STATUS systemPowerStatusPtr);
BOOL getModuleHandle(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE *phModule);
DWORD getModuleFileName(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
UINT getTempFileNameA(LPCSTR lpPathName, LPCSTR lpPrefixString, UINT uUnique, LPSTR lpTempFileName);
BOOL moveFileExA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, DWORD dwFlags);
BOOL lockFileEx(HANDLE hFile, DWORD dwFlags, DWORD dwReserved, DWORD nNumberOfBytesToLockLow, DWORD nNumberOfBytesToLockHigh, LPOVERLAPPED lpOverlapped);
BOOL unlockFileEx(HANDLE hFile, DWORD dwReserved, DWORD nNumberOfBytesToLockLow, DWORD nNumberOfBytesToLockHigh, LPOVERLAPPED lpOverlapped);
BOOL getOverlappedResult(HANDLE hFile, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred, BOOL bWait);
HANDLE createFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
BOOL deleteFileA(LPCSTR lpFileName);
BOOL readFileEx(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPOVERLAPPED lpOverlapped, LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
BOOL writeFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
HANDLE findFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);
BOOL findNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);
BOOL findClose(HANDLE hFindFile);
DWORD getFileAttributesA(LPCSTR lpFileName);

LSTATUS regOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
LSTATUS regQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);

} // namespace SysCalls

} // namespace NEO
