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


// AltSign
#include "DeviceManager.hpp"
#include "Error.hpp"

#include "MiniappBuilderCore.h"

#include <pplx/pplxtasks.h>

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

#define stdoutlog(msg) {  std::cout << msg << std::endl; }
#define stderrlog(msg) {  std::cerr << msg << std::endl; }

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


//MiniappBuilder.exe  <appleId> <password> <ipaFile>
int main(int argc, char* argv[])
{
	std::map<std::string, std::wstring> parameters = { {"title", (wchar_t*)L"title text"}, {"message", (wchar_t*)L"message test"} };

	if (argc <= 3) {
		stderrlog("Error: the lenght of arguments should more than 3");
		return -1;
	}
	std::string appleID = argv[1];
	std::string password = argv[2];
	std::string ipaFilepath = argv[3];

	if (appleID.empty()) {
		stderrlog("Error: appleID is undefined");
		return -1;
	}
	if (password.empty()) {
		stderrlog("Error: password is undefined");
		return -1;
	}
	if (ipaFilepath.empty()) {
		stderrlog("Error: ipaFilepath is undefined");
		return -1;
	}
	auto devices = DeviceManager::instance()->availableDevices();
	if (devices.size() == 0) {
		stderrlog("Error: no connected device");
		return -1;
	}
	std::shared_ptr<Device> selectedDevice = devices[0];
	stdoutlog("appleID:" + appleID);
	stdoutlog("ipaPath:" + ipaFilepath);

	MiniappBuilderCore::instance()->Start();
	auto task = MiniappBuilderCore::instance()->InstallApplication(ipaFilepath, selectedDevice, appleID, password);

	try
	{
		task.get();
	}
	catch (Error& error)
	{
		stderrlog("Error: " << error.domain() << " (" << error.code() << ").")
		return -1;
	}
	catch (std::exception& exception)
	{
		stderrlog("Exception: " << exception.what());
		return -1;
	}

	stdoutlog("Finished!");

}
