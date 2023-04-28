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
#include "Archiver.hpp"
#include <combaseapi.h>


// AltSign
#include "DeviceManager.hpp"
#include "Error.hpp"

#include "MiniappBuilderCore.h"

#include <pplx/pplxtasks.h>

#include <boost/program_options.hpp>

namespace fs = std::filesystem;
namespace po = boost::program_options;

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


std::string _input_appleid;
std::string _input_password;

BOOL CALLBACK LoginDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	HWND appleIDTextField = GetDlgItem(hwnd, IDC_EDIT1);
	HWND passwordTextField = GetDlgItem(hwnd, IDC_EDIT2);
	HWND installButton = GetDlgItem(hwnd, IDOK);

	switch (Message)
	{
	case WM_INITDIALOG:
	{
		Edit_SetCueBannerText(appleIDTextField, _T("Apple ID"));
		Edit_SetCueBannerText(passwordTextField, _T("Password"));

		Button_Enable(installButton, false);

		break;
	}

	case WM_CTLCOLORSTATIC:
	{
		if (GetDlgCtrlID((HWND)lParam) == IDC_DESCRIPTION)
		{
			HBRUSH success = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
			SetBkMode((HDC)wParam, TRANSPARENT);
			return (BOOL)success;
		}

		break;
	}

	case WM_COMMAND:
		switch (HIWORD(wParam))
		{
		case EN_CHANGE:
		{
			/*PostMessage(hWnd, WM_CLOSE, 0, 0);
			break;*/

			int appleIDLength = Edit_GetTextLength(appleIDTextField);
			int passwordLength = Edit_GetTextLength(passwordTextField);

			if (appleIDLength == 0 || passwordLength == 0)
			{
				Button_Enable(installButton, false);
			}
			else
			{
				Button_Enable(installButton, true);
			}

			break;
		}
		}

		switch (LOWORD(wParam))
		{
		case IDOK:
		{
			wchar_t appleID[512];
			wchar_t password[512];

			Edit_GetText(appleIDTextField, appleID, 512);
			Edit_GetText(passwordTextField, password, 512);

			_input_appleid = StringFromWideString(appleID);
			_input_password = StringFromWideString(password);
			EndDialog(hwnd, IDOK);

			break;
		}

		case IDCANCEL:
			EndDialog(hwnd, IDCANCEL);
			break;
		}

	default:
		return FALSE;
	}
	return TRUE;
}

pplx::task<void> signCallback(SignResult signResult, std::shared_ptr<Device> selectedDevice, std::string outputDir,  bool install) {
	Application app = signResult.application;
	fs::path appBundlePath = app.path();
	if (!outputDir.empty()) {
		std::string ipaPath = ZipAppBundle(app.path());
		fs::path src_path(ipaPath);
		fs::path dist_path(outputDir);

		fs::path filename = src_path.filename(); // 获取文件名
    	fs::path extension = src_path.extension(); // 获取扩展名

    	fs::path target_path = dist_path / filename; // 将文件名添加到目标路径中
    	target_path.replace_extension(extension); // 将扩展名添加到目标路径中
		
		if (fs::exists(target_path)) {
			fs::remove(target_path);
		}
		fs::rename(ipaPath, target_path);
	}
	if (install) {
		return MiniappBuilderCore::instance()->InstallApplication(app.path(), selectedDevice, signResult.activeProfiles)
			.then([=]() {
				if (fs::exists(appBundlePath.parent_path())) {
					fs::remove_all(appBundlePath.parent_path());
				}
			});
	} else {
		if (fs::exists(appBundlePath.parent_path())) {
			fs::remove_all(appBundlePath.parent_path());
		}
	}
}

//MiniappBuilder.exe  <appleId> <password> <ipaFile>
int main(int argc, char* argv[])
{

	po::options_description desc("Allowed options");
	desc.add_options()
		("help", "produce help message")
		("ipa", po::value<std::string>()->default_value(""), "ipa path")
		("type", po::value<std::string>()->default_value("appleId"), "apple sign type: appleId or certificate")
		("appleId", po::value<std::string>()->default_value(""), "apple ID")
		("password", po::value<std::string>()->default_value(""), "apple password")
		("deviceId", po::value<std::string>()->default_value(""), "device udid")
		("deviceName", po::value<std::string>()->default_value("your phone"), "device name")
		("bundleId", po::value<std::string>()->default_value("same"), "the bundleId, same|auto|xx.xx.xx(specified bundleId)")
		("certificatePath", po::value<std::string>()->default_value(""), "certificate path")
		("certificatePassword", po::value<std::string>()->default_value(""), "certificate password")
		("profilePath", po::value<std::string>()->default_value(""), "profile path")
		("output", po::value<std::string>()->default_value(""), "output dir")
		("install", po::value<bool>()->default_value(false), "whether if install instantly to device");

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	if (vm.count("help")) {
		std::cout << desc << std::endl;
		return 0;
	}

	std::string ipaFilepath = vm["ipa"].as<std::string>();
	std::string signType = vm["type"].as<std::string>();

	std::string appleID = vm["appleId"].as<std::string>();
	std::string password = vm["password"].as<std::string>();
	std::string deviceId = vm["deviceId"].as<std::string>();
	std::string deviceName = vm["deviceName"].as<std::string>();
	std::string bundleId = vm["bundleId"].as<std::string>();

	std::string certificatePath = vm["certificatePath"].as<std::string>();
	std::string certificatePassword = vm["certificatePassword"].as<std::string>();
	std::string profilePath = vm["profilePath"].as<std::string>();

	std::string outputDir = vm["output"].as<std::string>();
	bool install = vm["install"].as<bool>();

	if (ipaFilepath.empty()) {
		stderrlog("Error: ipaFilepath is undefined");
		return -1;
	}

	if (signType == "appleId") {
		if (appleID.empty() || password.empty()) {
			int result = DialogBox(NULL, MAKEINTRESOURCE(ID_LOGIN), NULL, LoginDlgProc);
			if (result == IDCANCEL)
			{
				stderrlog("Error: appleID or password is undefined");
				return -1;
			}

			appleID = _input_appleid;
			password = _input_password;
			_input_appleid = "";
			_input_password = "";
		}
	}
	else {
		if (certificatePath.empty() || profilePath.empty()) {
			stderrlog("Error: profile or profile is undefined");
			return -1;
		}
		if (!fs::exists(certificatePath)) {
			stderrlog("Error: certificate file not found");
			return -1;
		}
		if (!fs::exists(profilePath)) {
			stderrlog("Error: profile file not found");
			return -1;
		}
	}

	// output��install����һ��
	if ( outputDir.empty() && !install) {
		stderrlog("Error: output or install all not found");
		return -1;
	}

	//appleIdǩ������installʱ����Ҫ��device
	std::shared_ptr<Device> selectedDevice;
	if ((signType == "appleId") || install) {
		if (deviceId.empty()) {
			auto devices = DeviceManager::instance()->availableDevices();
			if (devices.size() == 0) {
				stderrlog("Error: no connected device");
				return -1;
			}
			selectedDevice = devices[0];
		}
		else {
			selectedDevice = std::make_shared<Device>(deviceName, deviceId, Device::Type::iPhone);
		}
	}


	pplx::task<void> task;
	MiniappBuilderCore::instance()->Start();
	if (signType == "appleId") {
		task = MiniappBuilderCore::instance()->SignWithAppleId(ipaFilepath, selectedDevice, appleID, password, bundleId)
			.then([=](SignResult signResult) {
				return signCallback(signResult, selectedDevice, outputDir, install);
			});
	} else {
		task = MiniappBuilderCore::instance()->SignWithCertificate(ipaFilepath, certificatePath, certificatePassword, profilePath)
			.then([=](SignResult signResult) {
				return signCallback(signResult, selectedDevice, outputDir, install);
			});
	}
	try
	{
		task.get();
	}
	catch (Error& error)
	{
		stderrlog("Error: " << error.domain() << " (" << error.localizedDescription() << ").")
		return -1;
	}
	catch (std::exception& exception)
	{
		stderrlog("Exception: " << exception.what());
		return -1;
	}

	stdoutlog("All is Finished!");

}
