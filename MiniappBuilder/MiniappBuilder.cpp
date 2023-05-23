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
extern void SetRegistryBoolValue(const char *lpValue, bool data);
extern void SetRegistryStringValue(const char* lpValue, std::string string);
extern bool GetRegistryBoolValue(const char *lpValue);
extern std::string GetRegistryStringValue(const char* lpValue);

#define stdoutlog(msg) {  std::cout << msg << std::endl; }
#define stderrlog(msg) {  std::cerr << msg << std::endl; }

std::string _input_appleid;
std::string _input_password;
bool _input_remember_appleIdInfo;

std::string _input_certificate_path;
std::string _input_certificate_password;
std::string _input_profile_path;
bool _input_remember_certificate;

std::string BrowseForFile(std::wstring title, std::wstring filter, std::wstring initialDir)
{
    OPENFILENAME ofn = {0};
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = NULL;
    // ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = new TCHAR[MAX_PATH];
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title.c_str();
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER | OFN_ENABLEHOOK;
    ofn.lpTemplateName = NULL;

    std::string filePath = "";

    if (GetOpenFileName(&ofn))
    {
        filePath = StringFromWideString(ofn.lpstrFile);
    }

    delete[] ofn.lpstrFile;

    return filePath;
}


BOOL CALLBACK LoginDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	HWND appleIDTextField = GetDlgItem(hwnd, IDC_EDIT1);
	HWND passwordTextField = GetDlgItem(hwnd, IDC_EDIT2);
	HWND rememberAppleIdInfoCheckbox = GetDlgItem(hwnd, IDC_CHECK1);
	HWND installButton = GetDlgItem(hwnd, IDOK);

	switch (Message)
	{
	case WM_INITDIALOG:
	{
		Edit_SetCueBannerText(appleIDTextField, _T("Apple ID"));
		Edit_SetCueBannerText(passwordTextField, _T("Password"));
		Edit_SetCueBannerText(rememberAppleIdInfoCheckbox, _T("Remember"));

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
			int nCheck = SendMessage(rememberAppleIdInfoCheckbox, BM_GETCHECK, 0, 0); // 获取IDC_CHECK1控件的值
			_input_remember_appleIdInfo = (nCheck == BST_CHECKED);
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


BOOL CALLBACK CertificateDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
            {
                // 初始化对话框
                return TRUE;
            }
            break;
        case WM_COMMAND:
            {
                switch (LOWORD(wParam))
                {
                    case IDC_BROWSE_CERT:
                        {
                           std::string folderPath = BrowseForFile(L"Please Choose an Apple certificate(p12)", L"p12 文件 (*.p12;)、*.p12;", L"~/Desktop");
							if (folderPath.size() == 0)
							{
								return -1;
							}
							SetDlgItemText(hwndDlg, IDC_CERT_PATH, WideStringFromString(folderPath).c_str());
                            return TRUE;
                        }
                        break;
                    case IDC_BROWSE_PROFILE:
                        {
                           std::string folderPath = BrowseForFile(L"Please Choose an Apple profile(mobileprovision)", L"mobileprovision Files (*.mobileprovision)\0*.mobileprovision\0All Files (*.*)\0*.*\0", L"~/Desktop");
							if (folderPath.size() == 0)
							{
								return -1;
							}
							SetDlgItemText(hwndDlg, IDC_PROFILE, WideStringFromString(folderPath).c_str());
                            return TRUE;
                        }
                        break;
                    case IDOK:
                        {
                            // 获取输入项的值
                            TCHAR szCertPath[MAX_PATH] = {0};
                            TCHAR szCertPassword[256] = {0};
                            TCHAR szProfile[MAX_PATH] = {0};
                            GetDlgItemText(hwndDlg, IDC_CERT_PATH, szCertPath, MAX_PATH);
                            GetDlgItemText(hwndDlg, IDC_CERT_PASSWORD, szCertPassword, 256);
                            GetDlgItemText(hwndDlg, IDC_PROFILE, szProfile, MAX_PATH);
							_input_certificate_path =  StringFromWideString(szCertPath);
							_input_certificate_password =  StringFromWideString(szCertPassword);
							_input_profile_path =  StringFromWideString(szProfile);
							int nCheck = SendMessage( GetDlgItem(hwndDlg, IDC_CHECK2), BM_GETCHECK, 0, 0); // 获取IDC_CHECK1控件的值
							_input_remember_certificate = (nCheck == BST_CHECKED);
                            EndDialog(hwndDlg, IDOK);
                            return TRUE;
                        }
                        break;
                    case IDCANCEL:
                        {
                            EndDialog(hwndDlg, IDCANCEL);
                        }
                        break;
                }
            }
            break;
		default:
			return FALSE;
    }
    return FALSE;
}

std::map<std::string, std::string> queryStringToDictionary(const std::string& queryString) {
	std::map<std::string, std::string> dict;
	std::vector<std::string> pairs;
	size_t start = 0;
	size_t end = 0;
	while (end != std::string::npos) {
		end = queryString.find('&', start);
		pairs.push_back(queryString.substr(start, end - start));
		start = end + 1;
	}
	for (const auto& pair : pairs) {
		size_t pos = pair.find('=');
		if (pos != std::string::npos) {
			std::string key = pair.substr(0, pos);
			std::string value = pair.substr(pos + 1);
			dict[key] = value;
		}
	}
	return dict;
}

pplx::task<void> signCallback(SignResult signResult, std::shared_ptr<Device> selectedDevice, std::string outputDir,  bool install) {
	Application app = signResult.application;
	fs::path appBundlePath = app.path();
	if (!outputDir.empty()) {
		std::string ipaPath = ZipAppBundle(appBundlePath.string());
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
		("action", po::value<std::string>()->default_value("getDevices"), "sign|getDevices|clear")
		("ipa", po::value<std::string>()->default_value(""), "ipa path")
		("type", po::value<std::string>()->default_value("appleId"), "apple sign type: appleId or certificate")
		("appleId", po::value<std::string>()->default_value(""), "apple ID")
		("password", po::value<std::string>()->default_value(""), "apple password")
		("deviceId", po::value<std::string>()->default_value(""), "device udid")
		("deviceName", po::value<std::string>()->default_value("your phone"), "device name")
		("bundleId", po::value<std::string>()->default_value("same"), "the bundleId, same|auto|xx.xx.xx(specified bundleId)")
		("entitlements", po::value<std::string>()->default_value(""), "the emtitlement, A=xxx&B=xxx")
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

	std::string action = vm["action"].as<std::string>();

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

	std::string entitlementsStr = vm["entitlements"].as<std::string>();

	std::string outputDir = vm["output"].as<std::string>();
	bool install = vm["install"].as<bool>();

	if (action == "getDevices") {
		auto devices = DeviceManager::instance()->availableDevices();
		if (devices.size() == 0) {
			return 0;
		}
		for (std::shared_ptr<Device> device: devices)
		{
			if (device->type() != Device::Type::iPhone) {
				continue;
			}
			std::string versionString = device->osVersion().stringValue();
			std::string result = "iphone|" + device->name() + "|" + versionString + "|" + device->identifier();
			stdoutlog(result);
		}
		return 0;
	}

	if (action == "clear") {
		SetRegistryBoolValue("rememberAppleId", false);
		SetRegistryBoolValue("rememberCertificate", false);
		return 0;
	}

	if (ipaFilepath.empty()) {
		stderrlog("Error: ipaFilepath is undefined");
		return -1;
	}

	MiniappBuilderCore::instance()->Start();
	if (signType == "appleId") {
		bool rememberAppleId = GetRegistryBoolValue("rememberAppleId");
		if ((appleID.empty() || password.empty()) && rememberAppleId) {
			appleID = GetRegistryStringValue("appleID");
			password = GetRegistryStringValue("applePassword");
		}
		if (appleID.empty() || password.empty()) {
			int result = DialogBox(NULL, MAKEINTRESOURCE(ID_LOGIN), NULL, LoginDlgProc);
			if (result == IDCANCEL)
			{
				stderrlog("Error: appleID or password is undefined");
				return -1;
			}

			appleID = _input_appleid;
			password = _input_password;
			rememberAppleId = _input_remember_appleIdInfo;
			_input_appleid = "";
			_input_password = "";
			_input_remember_appleIdInfo = false;
		}
		if (rememberAppleId) {
			SetRegistryBoolValue("rememberAppleId", true);
			SetRegistryStringValue("appleID", appleID);
			SetRegistryStringValue("applePassword", password);
		} else {
			SetRegistryBoolValue("rememberAppleId", false);
		}
	}
	else {
		bool rememberCertificate = GetRegistryBoolValue("rememberCertificate");
		if ((certificatePath.empty() || profilePath.empty()) && rememberCertificate) {
			certificatePath = GetRegistryStringValue("certificatePath");
			certificatePassword = GetRegistryStringValue("certificatePassword");
			profilePath = GetRegistryStringValue("profilePath");
		}
		if (certificatePath.empty() || profilePath.empty() || !fs::exists(certificatePath) || !fs::exists(profilePath)) {
			int result = DialogBox(NULL, MAKEINTRESOURCE(ID_CERTIFICATE), NULL, CertificateDlgProc);
			if (result == IDCANCEL)
			{
				stderrlog("Error: certificatePath or profilePath is undefined");
				return -1;
			}
			certificatePath = _input_certificate_path;
			certificatePassword = _input_certificate_password;
			profilePath = _input_profile_path;
			rememberCertificate = _input_remember_certificate;
		}
		if (rememberCertificate) {
			SetRegistryBoolValue("rememberCertificate", true);
			SetRegistryStringValue("certificatePath", certificatePath);
			SetRegistryStringValue("certificatePassword", certificatePassword);
			SetRegistryStringValue("profilePath", profilePath);
		}
		else {
			SetRegistryBoolValue("rememberCertificate", false);
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

	// entitlements
	std::map<std::string, std::string> entitlements = queryStringToDictionary(entitlementsStr);


	pplx::task<void> task;
	if (signType == "appleId") {
		task = MiniappBuilderCore::instance()->SignWithAppleId(ipaFilepath, selectedDevice, appleID, password, bundleId, entitlements)
			.then([=](SignResult signResult) {
				return signCallback(signResult, selectedDevice, outputDir, install);
			});
	} else {
		task = MiniappBuilderCore::instance()->SignWithCertificate(ipaFilepath, certificatePath, certificatePassword, profilePath, entitlements)
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
