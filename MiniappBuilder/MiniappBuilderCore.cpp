#include <windowsx.h>
#include "MiniappBuilderCore.h"
#include <strsafe.h>
#include <Guiddef.h>

#include "AppleAPI.hpp"
#include "ConnectionManager.hpp"
#include "InstallError.hpp"
#include "Signer.hpp"
#include "DeviceManager.hpp"
#include "Archiver.hpp"
#include "ServerError.hpp"

#include "AnisetteDataManager.h"

#include <cpprest/http_client.h>
#include <cpprest/filestream.h>

#include <filesystem>
#include <regex>
#include <numeric>

#include <plist/plist.h>

#include <WS2tcpip.h>
#include <ShlObj_core.h>

#pragma comment( lib, "gdiplus.lib" ) 
#include <gdiplus.h> 
#include <strsafe.h>

#include "resource.h"

#include <winsparkle.h>

#define stdoutlog(msg) {  std::cout << msg << std::endl; }
#define stderrlog(msg) {  std::cerr << msg << std::endl; }

using namespace utility;                    // Common utilities like string conversions
using namespace web;                        // Common features like URIs.
using namespace web::http;                  // Common HTTP functionality
using namespace web::http::client;          // HTTP client features
using namespace concurrency::streams;       // Asynchronous streams

namespace fs = std::filesystem;

extern std::string temporary_directory();
extern std::string make_uuid();
extern std::vector<unsigned char> readFile(const char* filename);

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

const char* REGISTRY_ROOT_KEY = "SOFTWARE\\RileyTestut\\MiniAppBuilder";
const char* DID_LAUNCH_KEY = "Launched";
const char* PRESENTED_RUNNING_NOTIFICATION_KEY = "PresentedRunningNotification";
const char* SERVER_ID_KEY = "ServerID";
const char* REPROVISIONED_DEVICE_KEY = "ReprovisionedDevice";
const char* APPLE_FOLDER_KEY = "AppleFolder";


std::string _verificationCode;


HKEY OpenRegistryKey()
{
	HKEY hKey;
	LONG nError = RegOpenKeyExA(HKEY_CURRENT_USER, REGISTRY_ROOT_KEY, NULL, KEY_ALL_ACCESS, &hKey);

	if (nError == ERROR_FILE_NOT_FOUND)
	{
		nError = RegCreateKeyExA(HKEY_CURRENT_USER, REGISTRY_ROOT_KEY, NULL, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL);
	}

	if (nError)
	{
		stderrlog("Error finding/creating registry value. " << nError);
	}

	return hKey;
}

void SetRegistryBoolValue(const char *lpValue, bool data)
{
	int32_t value = data ? 1 : 0;

	HKEY rootKey = OpenRegistryKey();
	LONG nError = RegSetValueExA(rootKey, lpValue, NULL, REG_DWORD, (BYTE *)&value, sizeof(int32_t));

	if (nError)
	{
		stderrlog("Error setting registry value. " << nError);
	}

	RegCloseKey(rootKey);
}

void SetRegistryStringValue(const char* lpValue, std::string string)
{
	HKEY rootKey = OpenRegistryKey();
	LONG nError = RegSetValueExA(rootKey, lpValue, NULL, REG_SZ, (const BYTE *)string.c_str(), string.size() + 1);

	if (nError)
	{
		stderrlog("Error setting registry value. " << nError);
	}

	RegCloseKey(rootKey);
}

bool GetRegistryBoolValue(const char *lpValue)
{
	HKEY rootKey = OpenRegistryKey();

	int32_t data;
	DWORD size = sizeof(int32_t);
	DWORD type = REG_DWORD;
	LONG nError = RegQueryValueExA(rootKey, lpValue, NULL, &type, (BYTE *)& data, &size);

	if (nError == ERROR_FILE_NOT_FOUND)
	{
		data = 0;
	}
	else if (nError)
	{
		stderrlog("Could not get registry value. " << nError);
	}

	RegCloseKey(rootKey);

	return (bool)data;
}

std::string GetRegistryStringValue(const char* lpValue)
{
	HKEY rootKey = OpenRegistryKey();

	char value[1024];
	DWORD length = sizeof(value);

	DWORD type = REG_SZ;
	LONG nError = RegQueryValueExA(rootKey, lpValue, NULL, &type, (LPBYTE)& value, &length);

	if (nError == ERROR_FILE_NOT_FOUND)
	{
		value[0] = 0;
	}
	else if (nError)
	{
		stderrlog("Could not get registry value. " << nError);
	}

	RegCloseKey(rootKey);

	std::string string(value);
	return string;
}



// Observes all exceptions that occurred in all tasks in the given range.
template<class T, class InIt>
void observe_all_exceptions(InIt first, InIt last)
{
	std::for_each(first, last, [](concurrency::task<T> t)
		{
			t.then([](concurrency::task<T> previousTask)
				{
					try
					{
						previousTask.get();
					}
					catch (const std::exception&)
					{
						// Swallow the exception.
					}
				});
		});
}

BOOL CALLBACK InstallDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
	case WM_INITDIALOG:
	{
		std::map<std::string, std::wstring>* parameters = (std::map<std::string, std::wstring>*)lParam;

		std::wstring title = (*parameters)["title"];
		std::wstring message = (*parameters)["message"];

		SetWindowText(hwnd, title.c_str());

		HWND descriptionText = GetDlgItem(hwnd, IDC_DESCRIPTION);
		SetWindowText(descriptionText, message.c_str());

		HWND downloadButton = GetDlgItem(hwnd, IDOK);
		PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)downloadButton, TRUE);

		return TRUE;
	}

	case WM_CTLCOLORSTATIC:
	{
		if (GetDlgCtrlID((HWND)lParam) == IDC_DESCRIPTION)
		{
			HBRUSH success = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
			SetBkMode((HDC)wParam, TRANSPARENT);
			return (BOOL)success;
		}

		return TRUE;
	}

	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case IDOK:
		case IDCANCEL:
		case ID_FOLDER:
			EndDialog(hwnd, LOWORD(wParam));
			return TRUE;
		}
	}

	default: break;
	}

	return FALSE;
}

BOOL CALLBACK TwoFactorDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	HWND verificationCodeTextField = GetDlgItem(hwnd, IDC_EDIT1);
	HWND submitButton = GetDlgItem(hwnd, IDOK);

	switch (Message)
	{
	case WM_INITDIALOG:
	{
		Edit_SetCueBannerText(verificationCodeTextField, L"123456");

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

			int codeLength = Edit_GetTextLength(verificationCodeTextField);
			if (codeLength == 6)
			{
				Button_Enable(submitButton, true);
			}
			else
			{
				Button_Enable(submitButton, false);
			}

			break;
		}
		}

		switch (LOWORD(wParam))
		{
		case IDOK:
		{
			wchar_t verificationCode[512];
			Edit_GetText(verificationCodeTextField, verificationCode, 512);

			stdoutlog("Verification Code:" << verificationCode);

			_verificationCode = StringFromWideString(verificationCode);

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

MiniappBuilderCore* MiniappBuilderCore::_instance = nullptr;

MiniappBuilderCore* MiniappBuilderCore::instance()
{
	if (_instance == 0)
	{
		_instance = new MiniappBuilderCore();
	}

	return _instance;
}

MiniappBuilderCore::MiniappBuilderCore() : _appGroupSemaphore(1)
{
	HRESULT result = CoCreateGuid(&_notificationIconGUID);
	if (result != S_OK)
	{
		//TODO: Better error handling?
		assert(false);
	}
}

MiniappBuilderCore::~MiniappBuilderCore()
{
}

static int CALLBACK BrowseFolderCallback(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
	if (uMsg == BFFM_INITIALIZED)
	{
		std::string tmp = (const char*)lpData;
		stdoutlog("Browser Path:" << tmp);
		SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
	}

	return 0;
}

std::string MiniappBuilderCore::BrowseForFolder(std::wstring title, std::string folderPath)
{
	BROWSEINFO browseInfo = { 0 };
	browseInfo.lpszTitle = title.c_str();
	browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
	browseInfo.lpfn = BrowseFolderCallback;
	browseInfo.lParam = (LPARAM)folderPath.c_str();

	LPITEMIDLIST pidList = SHBrowseForFolder(&browseInfo);
	if (pidList == 0)
	{
		return "";
	}

	TCHAR path[MAX_PATH];
	SHGetPathFromIDList(pidList, path);

	IMalloc* imalloc = NULL;
	if (SUCCEEDED(SHGetMalloc(&imalloc)))
	{
		imalloc->Free(pidList);
		imalloc->Release();
	}

	return StringFromWideString(path);
}

void MiniappBuilderCore::Start()
{

	win_sparkle_init();

	bool didLaunch = GetRegistryBoolValue(DID_LAUNCH_KEY);
	if (!didLaunch)
	{
		// First launch.

		auto serverID = make_uuid();
		this->setServerID(serverID);

		SetRegistryBoolValue(DID_LAUNCH_KEY, true);
	}

	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

	ConnectionManager::instance()->Start();

	try
	{
		this->CheckDependencies();
		AnisetteDataManager::instance()->LoadDependencies();

#if SPOOF_MAC
		if (!this->CheckiCloudDependencies())
		{
			stderrlog("iCloud Not Installed. iCloud must be installed from Apple's website (not the Microsoft Store) in order to use the Miniapp Assistant.");
		}
#endif
	}
	catch (AnisetteError &error)
	{
		this->HandleAnisetteError(error);
	}
	catch (LocalizedError& error)
	{
		stderrlog("Failed to Start MiniappBuilder: " + error.localizedDescription());
	}
	catch (std::exception& exception)
	{
		stderrlog("Failed to Start MiniappBuilder", exception.what());
	}

	if (!this->presentedRunningNotification())
	{
		stderrlog("MiniappBuilder will continue to run in the background listening for the Miniapp Assistant.");
		this->setPresentedRunningNotification(true);
	}
	else
	{
		// Make MiniappBuilder appear in notification area.

	}

	DeviceManager::instance()->Start();
}

void MiniappBuilderCore::Stop()
{
	win_sparkle_cleanup();
}


pplx::task<void> MiniappBuilderCore::InstallApplication(std::string filepath, std::shared_ptr<Device> installDevice, std::optional<std::set<std::string>> activeProfiles)
{
    auto appName =fs::path(filepath).filename().string();
	stdoutlog("Install the app... ");
	return DeviceManager::instance()->InstallApp(filepath, installDevice->identifier(), activeProfiles, [](double progress) {
		stdoutlog("Installation Progress: " << progress);
	})
	.then([=](pplx::task<void> task) -> void {
		try
		{
			task.get();
			std::stringstream ss;
			ss << "the ipa was successfully installed on " << installDevice->name() << ".";
			stdoutlog("Installation Succeeded:", ss.str());
		}
		catch (InstallError& error)
		{
			if ((InstallErrorCode)error.code() == InstallErrorCode::Cancelled)
			{
				// Ignore
			}
			else
			{
                // stderrlog(error.localizedDescription());
                throw;
			}
		}
		catch (APIError& error)
		{
			if ((APIErrorCode)error.code() == APIErrorCode::InvalidAnisetteData)
			{
				// Our attempt to re-provision the device as a Mac failed, so reset provisioning and try one more time.
				// This appears to happen when iCloud is running simultaneously, and just happens to provision device at same time as MiniappBuilder.
				AnisetteDataManager::instance()->ResetProvisioning();

				stdoutlog("Registering PC with Apple, This may take a few seconds.");

				// Provisioning device can fail if attempted too soon after previous attempt.
				// As a hack around this, we wait a bit before trying again.
				// 10-11 seconds appears to be too short, so wait for 12 seconds instead.
				Sleep(12000);

				this->InstallApplication(filepath, installDevice, activeProfiles);
				return;
			}
			else if ((APIErrorCode)error.code() == APIErrorCode::InvalidAnisetteData)
			{
				AnisetteDataManager::instance()->ResetProvisioning();
			}

            // stderrlog(error.localizedDescription());
            throw;
		}
		catch (AnisetteError& error)
		{
			this->HandleAnisetteError(error);
            throw;
		}
		catch (LocalizedError& error)
		{
            throw;
		}
		catch (std::exception& exception)
		{
			throw;
		}
	});
}



pplx::task<SignResult> MiniappBuilderCore::SignWithAppleId(std::string ipapath, std::shared_ptr<Device> installDevice,std::string appleID, std::string password, std::string bundleId, std::unordered_map<std::string, std::string> entitlements)
{
    fs::path destinationDirectoryPath(temporary_directory());
    destinationDirectoryPath.append(make_uuid());
    
	auto account = std::make_shared<Account>();
	auto app = std::make_shared<Application>();
	auto team = std::make_shared<Team>();
	auto device = std::make_shared<Device>();
	auto appID = std::make_shared<AppID>();
	auto certificate = std::make_shared<Certificate>();
	auto session = std::make_shared<AppleAPISession>();

	return pplx::create_task([=]() {
		auto anisetteData = AnisetteDataManager::instance()->FetchAnisetteData();
		return this->Authenticate(appleID, password, anisetteData);
	})
    .then([=](std::pair<std::shared_ptr<Account>, std::shared_ptr<AppleAPISession>> pair)
          {
              *account = *(pair.first);
			  *session = *(pair.second);

			  stdoutlog("Fetching team...");

              return this->FetchTeam(account, session);
          })
    .then([=](std::shared_ptr<Team> tempTeam)
          {
			stdoutlog("Registering device...");

              *team = *tempTeam;
              return this->RegisterDevice(installDevice, team, session);
          })
    .then([=](std::shared_ptr<Device> tempDevice)
          {
				stdoutlog("Fetching certificate...");

				tempDevice->setOSVersion(installDevice->osVersion());
				*device = *tempDevice;

				return this->FetchCertificate(team, session);
          })
    .then([=](std::shared_ptr<Certificate> tempCertificate)
          {
				*certificate = *tempCertificate;

				stdoutlog("Preparing device...");				
                return this->PrepareDevice(device).then([=](pplx::task<void> task) {
                    try
                    {
                        // Don't rethrow error, and instead continue installing app even if we couldn't install Developer disk image.
                        task.get();
                    }
                    catch (Error& error)
                    {
                        stderrlog("Failed to install DeveloperDiskImage.dmg to " << *device << ". " << error.localizedDescription());
                    }
                    catch (std::exception& exception)
                    {
                        stderrlog("Failed to install DeveloperDiskImage.dmg to " << *device << ". " << exception.what());
                    }

					return pplx::create_task([ipapath] {
						return fs::path(ipapath);
						});
                });
          })
    .then([=](fs::path appFilePath)
          {
              fs::create_directory(destinationDirectoryPath);
              auto appBundlePath = UnzipAppBundle(appFilePath.string(), destinationDirectoryPath.string());

			  auto app = std::make_shared<Application>(appBundlePath);
              return app;
          })
    .then([=](std::shared_ptr<Application> tempApp)
          {
              *app = *tempApp;
			  return this->PrepareAllProvisioningProfiles(app, device, team, bundleId, session);
          })
   .then([=](std::map<std::string, std::shared_ptr<ProvisioningProfile>> profiles)
         {
             return this->SignCore(app, certificate, profiles, entitlements);
         })
   .then([=](pplx::task<std::optional<std::set<std::string>>> task)
          { 
			try
			{
				auto activeProfiles = task.get();
				SignResult result;
				result.application = *app.get();
				result.activeProfiles = activeProfiles;
				return result;
			}
			catch (LocalizedError& error)
			{
				if (error.code() == -22421)
				{
					// Don't know what API call returns this error code, so assume any LocalizedError with -22421 error code
					// means invalid anisette data, then throw the correct APIError.
					throw APIError(APIErrorCode::InvalidAnisetteData);
				}
				else if (error.code() == -29004)
				{
					// Same with -29004, "Environment Mismatch"
					throw APIError(APIErrorCode::InvalidAnisetteData);
				}
				else
				{
					throw;
				}
			}
        });
}


pplx::task<SignResult> MiniappBuilderCore::SignWithCertificate(std::string ipapath, std::string certificatePath, std::optional<std::string> certificatePassword,std::string profilePath, std::unordered_map<std::string, std::string> entitlements)
{
	fs::path destinationDirectoryPath(temporary_directory());
	destinationDirectoryPath.append(make_uuid());
	auto app = std::make_shared<Application>();

	return pplx::create_task([=]() {
		try
		{
			auto appBundlePath = UnzipAppBundle(ipapath, destinationDirectoryPath.string());
			auto tempApp = std::make_shared<Application>(appBundlePath);
			*app = *tempApp;

			auto data = readFile(certificatePath.c_str());
			std::shared_ptr<Certificate> certificate;
			if (certificatePassword.has_value()) {
				certificate = std::make_shared<Certificate>(data, certificatePassword.value());
			} else {
				certificate = std::make_shared<Certificate>(data);
			}

			// Manually set machineIdentifier so we can encrypt + embed certificate if needed.
			certificate->setMachineIdentifier(certificatePassword);
			auto profile = std::make_shared<ProvisioningProfile>(profilePath);
			std::map<std::string, std::shared_ptr<ProvisioningProfile>> profiles; 
			profiles[tempApp->bundleIdentifier()] = profile;
			return this->SignCore(tempApp, certificate, profiles, entitlements);
		}
		catch(std::exception &e)
		{
			// Ignore cached certificate errors.
			stderrlog("Failed to load cached certificate:" << certificatePath << ". " << e.what())
		}
	}).then([=](pplx::task<std::optional<std::set<std::string>>> task)
          {
			// if (fs::exists(destinationDirectoryPath))
			// {
			// 	fs::remove_all(destinationDirectoryPath);
			// }     
			try
			{
				auto activeProfiles = task.get();
				SignResult result;
				result.application = *app.get();
				result.activeProfiles = activeProfiles;
				return result;
			}
			catch (LocalizedError& error)
			{
				if (error.code() == -22421)
				{
					// Don't know what API call returns this error code, so assume any LocalizedError with -22421 error code
					// means invalid anisette data, then throw the correct APIError.
					throw APIError(APIErrorCode::InvalidAnisetteData);
				}
				else if (error.code() == -29004)
				{
					// Same with -29004, "Environment Mismatch"
					throw APIError(APIErrorCode::InvalidAnisetteData);
				}
				else
				{
					throw;
				}
			}
        });

}

pplx::task<void> MiniappBuilderCore::PrepareDevice(std::shared_ptr<Device> device)
{
	return DeviceManager::instance()->IsDeveloperDiskImageMounted(device)
	.then([=](bool isMounted) {
		if (isMounted)
		{
			return pplx::create_task([] { return; });
		}
		else
		{
			return this->_developerDiskManager.DownloadDeveloperDisk(device)
			.then([=](std::pair<std::string, std::string> paths) {
				return DeviceManager::instance()->InstallDeveloperDiskImage(paths.first, paths.second, device);
			})
			.then([=](pplx::task<void> task) {
				try
				{
					task.get();

					// No error thrown, so assume disk is compatible.
					this->_developerDiskManager.SetDeveloperDiskCompatible(true, device);
				}
				catch (ServerError& serverError)
				{
					if (serverError.code() == (int)ServerErrorCode::IncompatibleDeveloperDisk)
					{
						// Developer disk is not compatible with this device, so mark it as incompatible.
						this->_developerDiskManager.SetDeveloperDiskCompatible(false, device);
					}
					else
					{
						// Don't mark developer disk as incompatible because it probably failed for a different reason.
					}

					throw;
				}
			});
		}		
	});
}


pplx::task<std::pair<std::shared_ptr<Account>, std::shared_ptr<AppleAPISession>>> MiniappBuilderCore::Authenticate(std::string appleID, std::string password, std::shared_ptr<AnisetteData> anisetteData)
{
	auto verificationHandler = [=](void)->pplx::task<std::optional<std::string>> {
		return pplx::create_task([=]() -> std::optional<std::string> {

			int result = DialogBox(NULL, MAKEINTRESOURCE(ID_TWOFACTOR), NULL, TwoFactorDlgProc);
			if (result == IDCANCEL)
			{
				return std::nullopt;
			}

			auto verificationCode = std::make_optional<std::string>(_verificationCode);
			_verificationCode = "";

			return verificationCode;
		});
	};

	return pplx::create_task([=]() {
		if (anisetteData == NULL)
		{
			throw ServerError(ServerErrorCode::InvalidAnisetteData);
		}

		return AppleAPI::getInstance()->Authenticate(appleID, password, anisetteData, verificationHandler);
	});
}

pplx::task<std::shared_ptr<Team>> MiniappBuilderCore::FetchTeam(std::shared_ptr<Account> account, std::shared_ptr<AppleAPISession> session)
{
    auto task = AppleAPI::getInstance()->FetchTeams(account, session)
    .then([](std::vector<std::shared_ptr<Team>> teams) {

		for (auto& team : teams)
		{
			if (team->type() == Team::Type::Individual)
			{
				return team;
			}
		}

		for (auto& team : teams)
		{
			if (team->type() == Team::Type::Free)
			{
				return team;
			}
		}

		for (auto& team : teams)
		{
			return team;
		}

		throw InstallError(InstallErrorCode::NoTeam);
    });
    
    return task;
}

pplx::task<std::shared_ptr<Certificate>> MiniappBuilderCore::FetchCertificate(std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    auto task = AppleAPI::getInstance()->FetchCertificates(team, session)
    .then([this, team, session](std::vector<std::shared_ptr<Certificate>> certificates)
          {
			auto certificatesDirectoryPath = this->certificatesDirectoryPath();
			auto cachedCertificatePath = certificatesDirectoryPath.append(team->identifier() + ".p12");

			std::shared_ptr<Certificate> preferredCertificate = nullptr;

			for (auto& certificate : certificates)
			{
				if (!certificate->machineName().has_value())
				{
					continue;
				}

				std::string prefix("MiniAppBuilder");

				if (certificate->machineName()->size() < prefix.size())
				{
					// Machine name doesn't begin with "MiniAppBuilder", so ignore.
					continue;
				}
				else
				{
					auto result = std::mismatch(prefix.begin(), prefix.end(), certificate->machineName()->begin());
					if (result.first != prefix.end())
					{
						// Machine name doesn't begin with "MiniAppBuilder", so ignore.
						continue;
					}
				}

				if (fs::exists(cachedCertificatePath) && certificate->machineIdentifier().has_value())
				{
					try
					{
						auto data = readFile(cachedCertificatePath.string().c_str());
						auto cachedCertificate = std::make_shared<Certificate>(data, *certificate->machineIdentifier());

						// Manually set machineIdentifier so we can encrypt + embed certificate if needed.
						cachedCertificate->setMachineIdentifier(*certificate->machineIdentifier());

						return pplx::create_task([cachedCertificate] {
							return cachedCertificate;
						});
					}
					catch(std::exception &e)
					{
						// Ignore cached certificate errors.
						stderrlog("Failed to load cached certificate:" << cachedCertificatePath << ". " << e.what())
					}
				}

				preferredCertificate = certificate;

				break;
			}

              if (preferredCertificate != nullptr)
              {
                  return AppleAPI::getInstance()->RevokeCertificate(preferredCertificate, team, session).then([this, team, session](bool success)
                                                                                            {
                                                                                                return this->FetchCertificate(team, session);
                                                                                            });
              }
              else
              {
                  std::string machineName = "MiniAppBuilder";
                  
                  return AppleAPI::getInstance()->AddCertificate(machineName, team, session)
					  .then([team, session, cachedCertificatePath](std::shared_ptr<Certificate> addedCertificate)
                        {
                            auto privateKey = addedCertificate->privateKey();
                            if (privateKey == std::nullopt)
                            {
                                throw InstallError(InstallErrorCode::MissingPrivateKey);
                            }
                                                                                             
                            return AppleAPI::getInstance()->FetchCertificates(team, session)
                            .then([privateKey, addedCertificate, cachedCertificatePath](std::vector<std::shared_ptr<Certificate>> certificates)
                                {
                                    std::shared_ptr<Certificate> certificate = nullptr;
                                                                                                       
                                    for (auto tempCertificate : certificates)
                                    {
                                        if (tempCertificate->serialNumber() == addedCertificate->serialNumber())
                                        {
                                            certificate = tempCertificate;
                                            break;
                                        }
                                    }
                                                                                                       
                                    if (certificate == nullptr)
                                    {
                                        throw InstallError(InstallErrorCode::MissingCertificate);
                                    }
                                                                                                       
                                    certificate->setPrivateKey(privateKey);

									try
									{
										if (certificate->machineIdentifier().has_value())
										{
											auto machineIdentifier = certificate->machineIdentifier();

											auto encryptedData = certificate->encryptedP12Data(*machineIdentifier);
											if (encryptedData.has_value())
											{
												std::ofstream fout(cachedCertificatePath.string(), std::ios::out | std::ios::binary);
												fout.write((const char*)encryptedData->data(), encryptedData->size());
												fout.close();
											}
										}
									}
									catch (std::exception& e)
									{
										// Ignore caching certificate errors.
										stderrlog("Failed to cache certificate:" << cachedCertificatePath << ". " << e.what())
									}

                                    return certificate;
                                });
                        });
              }
          });
    
    return task;
}

pplx::task<std::map<std::string, std::shared_ptr<ProvisioningProfile>>> MiniappBuilderCore::PrepareAllProvisioningProfiles(
	std::shared_ptr<Application> application,
	std::shared_ptr<Device> device,
	std::shared_ptr<Team> team,
	std::string bundleId,
	std::shared_ptr<AppleAPISession> session)
{
	return this->PrepareProvisioningProfile(application, std::nullopt, device, team, bundleId, session)
	.then([=](std::shared_ptr<ProvisioningProfile> profile) {
		std::vector<pplx::task<std::pair<std::string, std::shared_ptr<ProvisioningProfile>>>> tasks;

		auto task = pplx::create_task([application, profile]() -> std::pair<std::string, std::shared_ptr<ProvisioningProfile>> { 
			return std::make_pair(application->bundleIdentifier(), profile); 
		});
		tasks.push_back(task);

		for (auto appExtension : application->appExtensions())
		{
			auto task = this->PrepareProvisioningProfile(appExtension, application, device, team, bundleId, session)
			.then([appExtension](std::shared_ptr<ProvisioningProfile> profile) {
				return std::make_pair(appExtension->bundleIdentifier(), profile);
			});
			tasks.push_back(task);
		}

		return pplx::when_all(tasks.begin(), tasks.end())
			.then([tasks](pplx::task<std::vector<std::pair<std::string, std::shared_ptr<ProvisioningProfile>>>> task) {
				try
				{
					auto pairs = task.get();

					std::map<std::string, std::shared_ptr<ProvisioningProfile>> profiles;
					for (auto& pair : pairs)
					{
						profiles[pair.first] = pair.second;
					}

					observe_all_exceptions<std::pair<std::string, std::shared_ptr<ProvisioningProfile>>>(tasks.begin(), tasks.end());
					return profiles;
				}
				catch (std::exception& e)
				{
					observe_all_exceptions<std::pair<std::string, std::shared_ptr<ProvisioningProfile>>>(tasks.begin(), tasks.end());
					throw;
				}
		});
	});
}

pplx::task<std::shared_ptr<ProvisioningProfile>> MiniappBuilderCore::PrepareProvisioningProfile(
	std::shared_ptr<Application> app,
	std::optional<std::shared_ptr<Application>> parentApp,
	std::shared_ptr<Device> device,
	std::shared_ptr<Team> team,
	std::string bundleId,
	std::shared_ptr<AppleAPISession> session)
{
	std::string preferredName;
	std::string parentBundleID;

	if (parentApp.has_value())
	{
		parentBundleID = (*parentApp)->bundleIdentifier();
		preferredName = (*parentApp)->name() + " " + app->name();
	}
	else
	{
		parentBundleID = app->bundleIdentifier();
		preferredName = app->name();
	}

	std::string updatedParentBundleID;
	if (bundleId == "same") {
		updatedParentBundleID = parentBundleID;
	} else if (bundleId == "auto") {
		updatedParentBundleID = parentBundleID + "." + team->identifier();
	} else {
		updatedParentBundleID = bundleId;
	}
	
	std::string bundleID = std::regex_replace(app->bundleIdentifier(), std::regex(parentBundleID), updatedParentBundleID);

	return this->RegisterAppID(preferredName, bundleID, team, session)
	.then([=](std::shared_ptr<AppID> appID)
	{
		return this->UpdateAppIDFeatures(appID, app, team, session);
	})
	.then([=](std::shared_ptr<AppID> appID)
	{
		return this->UpdateAppIDAppGroups(appID, app, team, session);
	})
	.then([=](std::shared_ptr<AppID> appID)
	{
		return this->FetchProvisioningProfile(appID, device, team, session);
	})
	.then([=](std::shared_ptr<ProvisioningProfile> profile)
	{
		return profile;
	});
}

pplx::task<std::shared_ptr<AppID>> MiniappBuilderCore::RegisterAppID(std::string appName, std::string bundleID, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    auto task = AppleAPI::getInstance()->FetchAppIDs(team, session)
    .then([bundleID, appName, team, session](std::vector<std::shared_ptr<AppID>> appIDs)
          {
              std::shared_ptr<AppID> appID = nullptr;
              
              for (auto tempAppID : appIDs)
              {
                  if (tempAppID->bundleIdentifier() == bundleID)
                  {
                      appID = tempAppID;
                      break;
                  }
              }
              
              if (appID != nullptr)
              {
                  return pplx::task<std::shared_ptr<AppID>>([appID]()
                                                            {
                                                                return appID;
                                                            });
              }
              else
              {
                  return AppleAPI::getInstance()->AddAppID(appName, bundleID, team, session);
              }
          });
    
    return task;
}

pplx::task<std::shared_ptr<AppID>> MiniappBuilderCore::UpdateAppIDFeatures(std::shared_ptr<AppID> appID, std::shared_ptr<Application> app, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
	auto entitlements = app->entitlements();

	std::map<std::string, plist_t> features;
	for (auto& pair : entitlements)
	{
		auto feature = ALTFeatureForEntitlement(pair.first);
		if (feature.has_value())
		{
			features[*feature] = pair.second;
		}
	}

	auto appGroups = entitlements[ALTEntitlementAppGroups];
	plist_t appGroupNode = NULL;
	
	if (appGroups != NULL && plist_array_get_size(appGroups) > 0)
	{
		// App uses app groups, so assign `true` to enable the feature.
		appGroupNode = plist_new_bool(true);
	}
	else
	{
		// App has no app groups, so assign `false` to disable the feature.
		appGroupNode = plist_new_bool(false);
	}

	features[ALTFeatureAppGroups] = appGroupNode;

	bool updateFeatures = false;

	// Determine whether the required features are already enabled for the AppID.
	for (auto& pair : features)
	{
		plist_t currentValue = appID->features()[pair.first];
		plist_t newValue = pair.second;

		std::optional<bool> newBoolValue = std::nullopt;
		if (PLIST_IS_BOOLEAN(newValue))
		{
			uint8_t isEnabled = false;
			plist_get_bool_val(newValue, &isEnabled);
			newBoolValue = isEnabled;
		}

		if (currentValue != NULL && plist_compare_node_value(currentValue, newValue))
		{
			// AppID already has this feature enabled and the values are the same.
			continue;
		}
		else if (currentValue == NULL && newBoolValue == false)
		{
			// AppID doesn't already have this feature enabled, but we want it disabled anyway.
			continue;
		}
		else
		{
			// AppID either doesn't have this feature enabled or the value has changed,
			// so we need to update it to reflect new values.
			updateFeatures = true;
			break;
		}
	}

	if (updateFeatures)
	{
		std::shared_ptr<AppID> copiedAppID = std::make_shared<AppID>(*appID);
		copiedAppID->setFeatures(features);

		plist_free(appGroupNode);

		return AppleAPI::getInstance()->UpdateAppID(copiedAppID, team, session);
	}
	else
	{
		plist_free(appGroupNode);

		return pplx::create_task([appID]() {
			return appID;
		});
	}
}

pplx::task<std::shared_ptr<AppID>> MiniappBuilderCore::UpdateAppIDAppGroups(std::shared_ptr<AppID> appID, std::shared_ptr<Application> app, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
	return pplx::create_task([=]() -> pplx::task<std::shared_ptr<AppID>> {
		auto applicationGroupsNode = app->entitlements()[ALTEntitlementAppGroups];
		std::vector<std::string> applicationGroups;

		if (applicationGroupsNode != nullptr)
		{
			for (int i = 0; i < plist_array_get_size(applicationGroupsNode); i++)
			{
				auto groupNode = plist_array_get_item(applicationGroupsNode, i);

				char* groupName = nullptr;
				plist_get_string_val(groupNode, &groupName);

				applicationGroups.push_back(groupName);
			}
		}

		if (applicationGroups.size() == 0)
		{
			// Assigning an App ID to an empty app group array fails,
			// so just do nothing if there are no app groups.
			return pplx::create_task([appID]() {
				return appID;
			});
		}

		this->_appGroupSemaphore.wait();

		return AppleAPI::getInstance()->FetchAppGroups(team, session)
		.then([=](std::vector<std::shared_ptr<AppGroup>> fetchedGroups) {

			std::vector<pplx::task<std::shared_ptr<AppGroup>>> tasks;

			for (auto groupIdentifier : applicationGroups)
			{
				std::string adjustedGroupIdentifier = groupIdentifier + "." + team->identifier();
				std::optional<std::shared_ptr<AppGroup>> matchingGroup;

				for (auto group : fetchedGroups)
				{
					if (group->groupIdentifier() == adjustedGroupIdentifier)
					{
						matchingGroup = group;
						break;
					}
				}

				if (matchingGroup.has_value())
				{
					auto task = pplx::create_task([matchingGroup]() { return *matchingGroup; });
					tasks.push_back(task);
				}
				else
				{
					std::string name = std::regex_replace("MiniAppBuilder " + groupIdentifier, std::regex("\\."), " ");

					auto task = AppleAPI::getInstance()->AddAppGroup(name, adjustedGroupIdentifier, team, session);
					tasks.push_back(task);
				}				
			}

			return pplx::when_all(tasks.begin(), tasks.end()).then([=](pplx::task<std::vector<std::shared_ptr<AppGroup>>> task) {
				try
				{
					auto groups = task.get();
					observe_all_exceptions<std::shared_ptr<AppGroup>>(tasks.begin(), tasks.end());
					return groups;
				}
				catch (std::exception& e)
				{
					observe_all_exceptions<std::shared_ptr<AppGroup>>(tasks.begin(), tasks.end());
					throw;
				}
			});
		})
		.then([=](std::vector<std::shared_ptr<AppGroup>> groups) {
			return AppleAPI::getInstance()->AssignAppIDToGroups(appID, groups, team, session);
		})
		.then([appID](bool result) {
			return appID;
		})
		.then([this](pplx::task<std::shared_ptr<AppID>> task) {
			this->_appGroupSemaphore.notify();

			auto appID = task.get();
			return appID;
		});
	});
}

pplx::task<std::shared_ptr<Device>> MiniappBuilderCore::RegisterDevice(std::shared_ptr<Device> device, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    auto task = AppleAPI::getInstance()->FetchDevices(team, device->type(), session)
    .then([device, team, session](std::vector<std::shared_ptr<Device>> devices)
          {
              std::shared_ptr<Device> matchingDevice = nullptr;
              
              for (auto tempDevice : devices)
              {
                  if (tempDevice->identifier() == device->identifier())
                  {
                      matchingDevice = tempDevice;
                      break;
                  }
              }
              
              if (matchingDevice != nullptr)
              {
                  return pplx::task<std::shared_ptr<Device>>([matchingDevice]()
                                                             {
                                                                 return matchingDevice;
                                                             });
              }
              else
              {
                  return AppleAPI::getInstance()->RegisterDevice(device->name(), device->identifier(), device->type(), team, session);
              }
          });
    
    return task;
}

pplx::task<std::shared_ptr<ProvisioningProfile>> MiniappBuilderCore::FetchProvisioningProfile(std::shared_ptr<AppID> appID, std::shared_ptr<Device> device, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    return AppleAPI::getInstance()->FetchProvisioningProfile(appID, device->type(), team, session);
}

pplx::task<std::optional<std::set<std::string>>> MiniappBuilderCore::SignCore(std::shared_ptr<Application> app,
                            std::shared_ptr<Certificate> certificate,
                            std::map<std::string, std::shared_ptr<ProvisioningProfile>> profilesByBundleID,
							std::unordered_map<std::string, std::string> entitlements)
{
	auto prepareInfoPlist = [profilesByBundleID](std::shared_ptr<Application> app, plist_t additionalValues){
		auto profile = profilesByBundleID.at(app->bundleIdentifier());

		fs::path infoPlistPath(app->path());
		infoPlistPath.append("Info.plist");

		auto data = readFile(infoPlistPath.string().c_str());

		plist_t plist = nullptr;
		plist_from_memory((const char*)data.data(), (int)data.size(), &plist);
		if (plist == nullptr)
		{
			throw InstallError(InstallErrorCode::MissingInfoPlist);
		}

		plist_dict_set_item(plist, "CFBundleIdentifier", plist_new_string(profile->bundleIdentifier().c_str()));
		plist_dict_set_item(plist, "MiniAppBundleIdentifier", plist_new_string(app->bundleIdentifier().c_str()));

		if (additionalValues != NULL)
		{
			plist_dict_merge(&plist, additionalValues);
		}

		plist_t entitlements = profile->entitlements();
		if (entitlements != nullptr && plist_dict_get_item(entitlements, ALTEntitlementAppGroups) != nullptr)
		{
            plist_t appGroups = plist_copy(plist_dict_get_item(entitlements, ALTEntitlementAppGroups));
            plist_dict_set_item(plist, "ALTAppGroups", appGroups);
		}

		char* plistXML = nullptr;
		uint32_t length = 0;
		plist_to_xml(plist, &plistXML, &length);

		std::ofstream fout(infoPlistPath.string(), std::ios::out | std::ios::binary);
		fout.write(plistXML, length);
		fout.close();
	};

    return pplx::task<std::optional<std::set<std::string>>>([=]() {

		stdoutlog("Signing the app...");
        fs::path infoPlistPath(app->path());
        infoPlistPath.append("Info.plist");
        
        auto data = readFile(infoPlistPath.string().c_str());
        
        plist_t plist = nullptr;
        plist_from_memory((const char *)data.data(), (int)data.size(), &plist);
        if (plist == nullptr)
        {
            throw InstallError(InstallErrorCode::MissingInfoPlist);
        }
        
		plist_t additionalValues = plist_new_dict();
		prepareInfoPlist(app, additionalValues);

		for (auto appExtension : app->appExtensions())
		{
			prepareInfoPlist(appExtension, NULL);
		}

		std::vector<std::shared_ptr<ProvisioningProfile>> profiles;
		std::set<std::string> profileIdentifiers;
		for (auto pair : profilesByBundleID)
		{
			profiles.push_back(pair.second);
			profileIdentifiers.insert(pair.second->bundleIdentifier());
		}
        
        Signer signer(certificate);
        signer.SignApp(app->path(), profiles, entitlements);
		        
		return profileIdentifiers;
    });
}


bool MiniappBuilderCore::CheckDependencies()
{
	wchar_t* programFilesCommonDirectory;
	SHGetKnownFolderPath(FOLDERID_ProgramFilesCommon, 0, NULL, &programFilesCommonDirectory);

	fs::path deviceDriverDirectoryPath(programFilesCommonDirectory);
	deviceDriverDirectoryPath.append("Apple").append("Mobile Device Support");

	if (!fs::exists(deviceDriverDirectoryPath))
	{
		return false;
	}

	wchar_t* programFilesDirectory;
	SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &programFilesDirectory);

	fs::path bonjourDirectoryPath(programFilesDirectory);
	bonjourDirectoryPath.append("Bonjour");

	if (!fs::exists(bonjourDirectoryPath))
	{
		return false;
	}

	return true;
}

bool MiniappBuilderCore::CheckiCloudDependencies()
{
	wchar_t* programFilesCommonDirectory;
	SHGetKnownFolderPath(FOLDERID_ProgramFilesCommon, 0, NULL, &programFilesCommonDirectory);

	fs::path deviceDriverDirectoryPath(programFilesCommonDirectory);
	deviceDriverDirectoryPath.append("Apple").append("Internet Services");

	fs::path aosKitPath(deviceDriverDirectoryPath);
	aosKitPath.append("AOSKit.dll");

	if (!fs::exists(aosKitPath))
	{
		return false;
	}

	return true;
}

void MiniappBuilderCore::HandleAnisetteError(AnisetteError& error)
{
	switch ((AnisetteErrorCode)error.code())
	{
	case AnisetteErrorCode::iTunesNotInstalled:
	case AnisetteErrorCode::iCloudNotInstalled:
	{
		wchar_t* title = NULL;
		wchar_t *message = NULL;
		std::string downloadURL;

		switch ((AnisetteErrorCode)error.code())
		{
		case AnisetteErrorCode::iTunesNotInstalled: 
		{
			title = (wchar_t *)L"iTunes Not Found";
			message = (wchar_t*)LR"(Download the latest version of iTunes from apple.com (not the Microsoft Store) in order to continue using MiniappBuilder.

If you already have iTunes installed, please locate the "Apple" folder that was installed with iTunes. This can normally be found at:

)";

			BOOL is64Bit = false;

			if (GetProcAddress(GetModuleHandle(TEXT("kernel32")), "IsWow64Process2") != NULL)
			{
				USHORT pProcessMachine = 0;
				USHORT pNativeMachine = 0;

				if (IsWow64Process2(GetCurrentProcess(), &pProcessMachine, &pNativeMachine) != 0 && pProcessMachine != IMAGE_FILE_MACHINE_UNKNOWN)
				{
					is64Bit = true;
				}
				else
				{
					is64Bit = false;
				}
			}
			else if (GetProcAddress(GetModuleHandle(TEXT("kernel32")), "IsWow64Process") != NULL)
			{
				IsWow64Process(GetCurrentProcess(), &is64Bit);
			}
			else
			{
				is64Bit = false;
			}

			if (is64Bit)
			{
				// 64-bit
				downloadURL = "https://www.apple.com/itunes/download/win64";
			}
			else
			{
				// 32-bit
				downloadURL = "https://www.apple.com/itunes/download/win32";
			}

			break;
		}

		case AnisetteErrorCode::iCloudNotInstalled: 
			title = (wchar_t*)L"iCloud Not Found";
			message = (wchar_t*)LR"(Download the latest version of iCloud from apple.com (not the Microsoft Store) in order to continue using MiniappBuilder.

If you already have iCloud installed, please locate the "Apple" folder that was installed with iCloud. This can normally be found at:

)";
			downloadURL = "https://secure-appldnld.apple.com/windows/061-91601-20200323-974a39d0-41fc-4761-b571-318b7d9205ed/iCloudSetup.exe";
			break;
		}

		std::wstring completeMessage(message);
		completeMessage += WideStringFromString(this->defaultAppleFolderPath());

		std::map<std::string, std::wstring> parameters = { {"title", title}, {"message", completeMessage} };

		int result = DialogBoxParam(NULL, MAKEINTRESOURCE(ID_ICLOUD_MISSING_64), NULL, InstallDlgProc, (LPARAM)&parameters);
		if (result == IDOK)
		{
			ShellExecuteA(NULL, "open", downloadURL.c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
		else if (result == ID_FOLDER)
		{
			std::string folderPath = this->BrowseForFolder(L"Choose the 'Apple' folder that contains both 'Apple Application Support' and 'Internet Services'. This can normally be found at: " + WideStringFromString(this->defaultAppleFolderPath()), this->appleFolderPath());
			if (folderPath.size() == 0)
			{
				return;
			}

			stdoutlog("Chose Apple folder: " << folderPath);

			this->setAppleFolderPath(folderPath);
		}

		break;
	}

	case AnisetteErrorCode::MissingApplicationSupportFolder:
	case AnisetteErrorCode::MissingAOSKit:
	case AnisetteErrorCode::MissingFoundation:
	case AnisetteErrorCode::MissingObjc:
	{
		std::wstring message = L"Please locate the 'Apple' folder installed with iTunes to continue using MiniappBuilder.\n\nThis can normally be found at:\n";
		message += WideStringFromString(this->defaultAppleFolderPath());

		int result = MessageBoxW(NULL, message.c_str(), WideStringFromString(error.localizedDescription()).c_str(), MB_OKCANCEL);
		if (result != IDOK)
		{
			return;
		}

		std::string folderPath = this->BrowseForFolder(L"Choose the 'Apple' folder that contains both 'Apple Application Support' and 'Internet Services'. This can normally be found at: " + WideStringFromString(this->defaultAppleFolderPath()), this->appleFolderPath());
		if (folderPath.size() == 0)
		{
			return;
		}

		stdoutlog("Chose Apple folder: " << folderPath);

		this->setAppleFolderPath(folderPath);

		break;
	}

	case AnisetteErrorCode::InvalidiTunesInstallation:
	{
		stderrlog("Invalid iTunes Installation " +error.localizedDescription());
		break;
	}
	}
}


bool MiniappBuilderCore::boolValueForRegistryKey(std::string key) const
{
	auto value = GetRegistryBoolValue(key.c_str());
	return value;
}

void MiniappBuilderCore::setBoolValueForRegistryKey(bool value, std::string key)
{
	SetRegistryBoolValue(key.c_str(), value);
}

std::string MiniappBuilderCore::serverID() const
{
	auto serverID = GetRegistryStringValue(SERVER_ID_KEY);
	return serverID;
}

void MiniappBuilderCore::setServerID(std::string serverID)
{
	SetRegistryStringValue(SERVER_ID_KEY, serverID);
}

bool MiniappBuilderCore::presentedRunningNotification() const
{
	auto presentedRunningNotification = GetRegistryBoolValue(PRESENTED_RUNNING_NOTIFICATION_KEY);
	return presentedRunningNotification;
}

void MiniappBuilderCore::setPresentedRunningNotification(bool presentedRunningNotification)
{
	SetRegistryBoolValue(PRESENTED_RUNNING_NOTIFICATION_KEY, presentedRunningNotification);
}


std::string MiniappBuilderCore::defaultAppleFolderPath() const
{
	wchar_t* programFilesCommonDirectory;
	SHGetKnownFolderPath(FOLDERID_ProgramFilesCommon, 0, NULL, &programFilesCommonDirectory);

	fs::path appleDirectoryPath(programFilesCommonDirectory);
	appleDirectoryPath.append("Apple");

	return appleDirectoryPath.string();
}

std::string MiniappBuilderCore::appleFolderPath() const
{
	auto appleFolderPath = GetRegistryStringValue(APPLE_FOLDER_KEY);
	if (appleFolderPath.size() != 0)
	{
		return appleFolderPath;
	}

	return this->defaultAppleFolderPath();
}

void MiniappBuilderCore::setAppleFolderPath(std::string appleFolderPath)
{
	SetRegistryStringValue(APPLE_FOLDER_KEY, appleFolderPath);
}

std::string MiniappBuilderCore::internetServicesFolderPath() const
{
	fs::path internetServicesDirectoryPath(this->appleFolderPath());
	internetServicesDirectoryPath.append("Internet Services");
	return internetServicesDirectoryPath.string();
}

std::string MiniappBuilderCore::applicationSupportFolderPath() const
{
	fs::path applicationSupportDirectoryPath(this->appleFolderPath());
	applicationSupportDirectoryPath.append("Apple Application Support");
	return applicationSupportDirectoryPath.string();
}

fs::path MiniappBuilderCore::appDataDirectoryPath() const
{
	wchar_t* programDataDirectory;
	SHGetKnownFolderPath(FOLDERID_ProgramData, 0, NULL, &programDataDirectory);

	fs::path miniappBuilderDirectoryPath(programDataDirectory);
	miniappBuilderDirectoryPath.append("MiniappBuilder");

	if (!fs::exists(miniappBuilderDirectoryPath))
	{
		fs::create_directory(miniappBuilderDirectoryPath);
	}

	return miniappBuilderDirectoryPath;
}

fs::path MiniappBuilderCore::certificatesDirectoryPath() const
{
	auto appDataPath = this->appDataDirectoryPath();
	auto certificatesDirectoryPath = appDataPath.append("Certificates");

	if (!fs::exists(certificatesDirectoryPath))
	{
		fs::create_directory(certificatesDirectoryPath);
	}

	return certificatesDirectoryPath;
}

fs::path MiniappBuilderCore::developerDisksDirectoryPath() const
{
	auto appDataPath = this->appDataDirectoryPath();
	auto developerDisksDirectoryPath = appDataPath.append("DeveloperDiskImages");

	if (!fs::exists(developerDisksDirectoryPath))
	{
		fs::create_directory(developerDisksDirectoryPath);
	}

	return developerDisksDirectoryPath;
}