#pragma once

#include <windows.h>

#include <string>
#include <vector>
#include <debugapi.h>

#define altlog(msg) { std::wstringstream ss; ss << "[ALTLog] " << msg << std::endl; OutputDebugStringW(ss.str().c_str()); }
