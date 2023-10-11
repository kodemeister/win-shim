#include <stdio.h>
#include <string>
#include "windows.h"
#include "resource.h"

#define MAX_LOADSTRING 1024

using namespace std;

const wstring CMD_TOKEN{ L"%s" };

wstring load_string(UINT id)
{
    HINSTANCE hInstance = ::GetModuleHandle(NULL);
    WCHAR szs[MAX_LOADSTRING];
    ::LoadString(hInstance, id, szs, MAX_LOADSTRING);
    return wstring(szs);
}

void get_caps(const wstring& caps_str, bool& clipboard, bool& no_kill)
{
    auto detect_cap = [&clipboard, &no_kill](const wstring& cap_name)
    {
        if (cap_name == L"clipboard")
            clipboard = true;
        else if (cap_name == L"no-kill")
            no_kill = true;
    };

    wstring cap_name;
    for (wchar_t c : caps_str)
    {
        if (c == L';')
        {
            detect_cap(cap_name);
            cap_name.clear();
        }
        else
        {
            cap_name.push_back(c);
        }
    }
    detect_cap(cap_name);
}

wstring get_win32_last_error()
{
    DWORD dw = GetLastError();

    PVOID lpMsgBuf;

    ::FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0, NULL);
    wstring sdw((wchar_t*)lpMsgBuf);
    ::LocalFree(lpMsgBuf);
    return sdw;
}

// entry point has to be wWinMain (Unicode version of WinMain) instead of "main" - this allows us to change
// target subsystem to "windows" and turn off console allocation.
// note that "shmake" is still standard console executable.
int wmain(int argc, wchar_t* argv[], wchar_t *envp[])
{
    auto image_path = load_string(IDS_IMAGE_PATH);
    auto args_pattern = load_string(IDS_ARGS);
    auto caps_str = load_string(IDS_CAPABILITIES);

    bool cap_clipboard = false;
    bool cap_no_kill = false;
    get_caps(caps_str, cap_clipboard, cap_no_kill);

    // args
    auto args = args_pattern;

    // use the original command-line string from GetCommandLine() for token replacement
    // the string always starts with a path to the shim executable, so we need to remove it first
    wstring passed_arg = ::GetCommandLine();
    if (passed_arg[0] == L'"')
    {
        passed_arg.erase(0, wcslen(argv[0]) + 2);
    }
    else
    {
        passed_arg.erase(0, wcslen(argv[0]));
    }

    if (!args.empty())
    {
        // do token replacement regardless of whether we have an argument or not - if we do, it needs to be deleted anyway
        size_t pos = args.find(CMD_TOKEN);
        if (pos != string::npos)
        {
            args.replace(pos, CMD_TOKEN.size(), passed_arg);
        }
    }
    else
    {
        args = passed_arg;
    }

    // if the path to the target executable contains spaces, enclose it in double quotes
    wstring full_cmd;
    if (image_path.find(L" ") != wstring::npos && image_path[0] != L'"')
    {
        full_cmd = L'"' + image_path + L'"';
    }
    else
    {
        full_cmd = image_path;
    }
    if (!args.empty())
    {
        full_cmd += L" " + args;
    }

    STARTUPINFO si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(si);

    //wprintf(L"launching %s\n", full_cmd.c_str());

    if(!::CreateProcess(
        NULL, // lpApplicationName
        const_cast<LPWSTR>(full_cmd.c_str()),
        NULL, // lpProcessAttributes
        NULL, // lpThreadAttributes
        TRUE, // bInheritHandles
        // create process in suspended state in case we need to do some adjustments before it executes
        CREATE_SUSPENDED, // dwCreationFlags
        NULL, // lpEnvironment
        NULL, // lpCurrentDirectory
        &si,
        &pi))
    {
        wstring emsg = get_win32_last_error();
        wprintf(L"could not create process - %s\n", emsg.c_str());
        return 1;
    }
    else
    {
        HANDLE hJob = ::CreateJobObject(
            NULL,
            NULL // job name, not needed
        );

        // perfect place to create something like a job object - anything that happens just before process starts.
        ::AssignProcessToJobObject(hJob, pi.hProcess);

        JOBOBJECT_BASIC_UI_RESTRICTIONS jUI = { 0 };
        jUI.UIRestrictionsClass = JOB_OBJECT_UILIMIT_DESKTOP |
            JOB_OBJECT_UILIMIT_DISPLAYSETTINGS |
            JOB_OBJECT_UILIMIT_EXITWINDOWS |
            JOB_OBJECT_UILIMIT_GLOBALATOMS |
            JOB_OBJECT_UILIMIT_HANDLES |
            JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS;
        if (!cap_clipboard)
        {
            jUI.UIRestrictionsClass |= JOB_OBJECT_UILIMIT_READCLIPBOARD | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
        }
        // can limit clipboard access above if needed!
        if (!::SetInformationJobObject(hJob, JobObjectBasicUIRestrictions, &jUI, sizeof(jUI)))
        {
            wstring emsg = get_win32_last_error();
            wprintf(L"failed to set UI limits: %s\n", emsg.c_str());
        }

        if (!cap_no_kill)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jEli = { 0 };
            jEli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!::SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jEli, sizeof(jEli)))
            {
                wstring emsg = get_win32_last_error();
                wprintf(L"failed to set extended limits: %s\n", emsg.c_str());
            }
        }

        // now the process starts for real
        //wprintf(L"resuming\n");
        ::ResumeThread(pi.hThread);

		::WaitForSingleObject(pi.hProcess, INFINITE);

		DWORD exit_code = 0;
		// if next line fails, code is still 0
		::GetExitCodeProcess(pi.hProcess, &exit_code);
        //wprintf(L"exited with code %lu\n", exit_code);

		// free OS resources
		::CloseHandle(pi.hProcess);
		::CloseHandle(pi.hThread);
        ::CloseHandle(hJob); // required to apply termination job limits

		return exit_code;
    }
}