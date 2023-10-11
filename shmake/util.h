#pragma once

#include <string>
#include "windows.h"

std::wstring str_to_wstr(const std::string& str);

std::string wstr_to_str(const std::wstring& wstr);

/// <summary>
///  Throws runtime_error with win32 error code and human readable message. Intended to be the last thing the program does.
/// </summary>
void throw_win32_le(const std::wstring& function_name = L"");

bool is_console_exe(const std::wstring& image_path);

std::wstring sys_path_search(const std::wstring& image_path);

// Physically patches PE header to change subsystem (GUI/terminal)
void patch_exe_subsystem(const std::wstring& image_path, bool gui);