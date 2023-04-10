// HelloWindowsDesktop.cpp
// compile with: /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /c
#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <CommCtrl.h>
#include <ShObjIdl.h>

#include <strsafe.h>

#include "resource.h"

#include <fstream>
#include <iterator>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <codecvt>

#include <combaseapi.h>

#pragma comment( lib, "gdiplus.lib" ) 
#include <gdiplus.h> 

// AltSign
#include "DeviceManager.hpp"
#include "Error.hpp"

#include "MiniappBuilderCore.h"

#include <pplx/pplxtasks.h>

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

std::string make_uuid()
{
	GUID guid;
	CoCreateGuid(&guid);

	std::ostringstream os;
	os << std::hex << std::setw(8) << std::setfill('0') << guid.Data1;
	os << '-';
	os << std::hex << std::setw(4) << std::setfill('0') << guid.Data2;
	os << '-';
	os << std::hex << std::setw(4) << std::setfill('0') << guid.Data3;
	os << '-';
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[0]);
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[1]);
	os << '-';
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[2]);
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[3]);
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[4]);
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[5]);
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[6]);
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[7]);

	std::string s(os.str());
	return s;
}

std::string temporary_directory()
{
	wchar_t rawTempDirectory[1024];

	int length = GetTempPath(1024, rawTempDirectory);

	std::wstring wideString(rawTempDirectory);

	std::wstring_convert<std::codecvt_utf8<wchar_t>> conv1;
	std::string tempDirectory = conv1.to_bytes(wideString);

	return tempDirectory;
}

std::vector<unsigned char> readFile(const char* filename)
{
	// open the file:
	std::ifstream file(filename, std::ios::binary);

	// Stop eating new lines in binary mode!!!
	file.unsetf(std::ios::skipws);

	// get its size:
	std::streampos fileSize;

	file.seekg(0, std::ios::end);
	fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	// reserve capacity
	std::vector<unsigned char> vec;
	vec.reserve(fileSize);

	// read the data:
	vec.insert(vec.begin(),
		std::istream_iterator<unsigned char>(file),
		std::istream_iterator<unsigned char>());

	return vec;
}


int CALLBACK WinMain(
	_In_ HINSTANCE hInstance,
	_In_ HINSTANCE hPrevInstance,
	_In_ LPSTR     lpCmdLine,
	_In_ int       nCmdShow
)
{
	MiniappBuilderCore::instance()->Start();
	std::string appleID = "8618819259301";
	std::string password = "8618819259301";
	std::optional<std::string> _ipaFilepath = "C:\\Users\\coverguo\\Desktop\\demo.ipa";
	auto devices = DeviceManager::instance()->availableDevices();
	std::shared_ptr<Device> _selectedDevice = devices[0];
	auto task = MiniappBuilderCore::instance()->InstallApplication(_ipaFilepath, _selectedDevice, appleID, password);

	try
	{
		task.get();
	}
	catch (Error& error)
	{
		odslog("Error: " << error.domain() << " (" << error.code() << ").")
	}
	catch (std::exception& exception)
	{
		odslog("Exception: " << exception.what());
	}

	odslog("Finished!");

}
