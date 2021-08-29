#include "process_util.h"
#include <iostream>
#include <Psapi.h>
#include "pe-sieve\utils\ntddk.h"

HANDLE create_new_process(IN LPSTR exe_path, IN LPSTR cmd, OUT PROCESS_INFORMATION &pi, DWORD flags)
{
    std::string full_cmd = std::string(exe_path) + " " + std::string(cmd);
    STARTUPINFOA si;
    memset(&si, 0, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);

    memset(&pi, 0, sizeof(PROCESS_INFORMATION));
    std::cout << "Commandline: " << cmd << std::endl;
    if (!CreateProcessA(
        exe_path,
        (LPSTR)full_cmd.c_str(),
        NULL, //lpProcessAttributes
        NULL, //lpThreadAttributes
        FALSE, //bInheritHandles
        flags, //dwCreationFlags
        NULL, //lpEnvironment 
        NULL, //lpCurrentDirectory
        &si, //lpStartupInfo
        &pi //lpProcessInformation
    ))
    {
#ifdef _DEBUG
        std::cerr << "[ERROR] CreateProcess failed, Error = " << GetLastError() << std::endl;
#endif
        return NULL;
    }
    return pi.hProcess;
}

HANDLE make_new_process(char* targetPath, char* cmdLine, DWORD flags)
{
    //create target process:
    PROCESS_INFORMATION pi;
    if (!create_new_process(targetPath, cmdLine, pi, flags)) {
        return false;
    }
#ifdef _DEBUG
    std::cout << "PID: " << std::dec << pi.dwProcessId << std::endl;
#endif
    return pi.hProcess;
}

DWORD get_parent_pid(DWORD dwPID)
{
    NTSTATUS ntStatus;
    DWORD dwParentPID = INVALID_PID_VALUE;
    HANDLE hProcess;
    PROCESS_BASIC_INFORMATION pbi;
    ULONG ulRetLen;

    //  create entry point for 'NtQueryInformationProcess()'
    typedef NTSTATUS(__stdcall *FPTR_NtQueryInformationProcess) (HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

    FPTR_NtQueryInformationProcess NtQueryInformationProcess
        = (FPTR_NtQueryInformationProcess)GetProcAddress(GetModuleHandleA("ntdll"), "NtQueryInformationProcess");

    //  get process handle
    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION,
        FALSE,
        dwPID
    );
    //  could fail due to invalid PID or insufficiant privileges
    if (!hProcess)
        return  INVALID_PID_VALUE;
    //  gather information
    ntStatus = NtQueryInformationProcess(hProcess,
        ProcessBasicInformation,
        (void*)&pbi,
        sizeof(PROCESS_BASIC_INFORMATION),
        &ulRetLen
    );
    //  copy PID on success
    if (!ntStatus)
        dwParentPID = (DWORD)pbi.InheritedFromUniqueProcessId;
    CloseHandle(hProcess);
    return  (dwParentPID);
}

bool kill_pid(DWORD pid)
{
#ifdef _DEBUG
    std::cout << "[!] Killing PID: " << std::dec << pid << std::endl;
#endif
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProcess) {
        const DWORD err = GetLastError();
        if (err == ERROR_INVALID_PARAMETER) {
            return true; // already killed
        }
        return false;
    }
    bool is_killed = false;
    if (TerminateProcess(hProcess, 0)) {
        is_killed = true;
    }
    CloseHandle(hProcess);
    return is_killed;
}


/*
based on: https://support.microsoft.com/en-us/help/131065/how-to-obtain-a-handle-to-any-process-with-sedebugprivilege
*/
BOOL set_privilege(
    HANDLE hToken,          // token handle
    LPCTSTR Privilege,      // Privilege to enable/disable
    BOOL bEnablePrivilege   // TRUE to enable.  FALSE to disable
)
{
    TOKEN_PRIVILEGES tp;
    LUID luid;
    TOKEN_PRIVILEGES tpPrevious;
    DWORD cbPrevious = sizeof(TOKEN_PRIVILEGES);

    if (!LookupPrivilegeValueA(nullptr, Privilege, &luid)) {
        return FALSE;
    }
    // get current privilege
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = 0;

    AdjustTokenPrivileges(
        hToken,
        FALSE,
        &tp,
        sizeof(TOKEN_PRIVILEGES),
        &tpPrevious,
        &cbPrevious
    );

    if (GetLastError() != ERROR_SUCCESS) {
        return FALSE;
    }
    // set privilege based on previous setting
    tpPrevious.PrivilegeCount = 1;
    tpPrevious.Privileges[0].Luid = luid;

    if (bEnablePrivilege) {
        tpPrevious.Privileges[0].Attributes |= (SE_PRIVILEGE_ENABLED);
    }
    else {
        tpPrevious.Privileges[0].Attributes ^= (SE_PRIVILEGE_ENABLED & tpPrevious.Privileges[0].Attributes);
    }

    AdjustTokenPrivileges(
        hToken,
        FALSE,
        &tpPrevious,
        cbPrevious,
        NULL,
        NULL
    );

    if (GetLastError() != ERROR_SUCCESS) {
        return FALSE;
    }
    return TRUE;
}

bool set_debug_privilege()
{
    HANDLE hToken;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken)) {
        if (GetLastError() == ERROR_NO_TOKEN) {
            if (!ImpersonateSelf(SecurityImpersonation)) return false;
            if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken)) {
                std::cerr << "Error: cannot open the token" << std::endl;
                return false;
            }
        }
    }
    bool is_ok = false;
    // enable SeDebugPrivilege
    if (set_privilege(hToken, SE_DEBUG_NAME, TRUE)) {
        is_ok = true;
    }
    // close token handle
    CloseHandle(hToken);
    return is_ok;
}