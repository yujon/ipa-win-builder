//
//  MiniappBuilderCore.hpp
//  
//
//  Created by Riley Testut on 8/30/19.
//  Copyright (c) 2019 Riley Testut. All rights reserved.
//

#pragma once

#include <string>
#include <set>
#include "Account.hpp"
#include "AppID.hpp"
#include "Application.hpp"
#include "Certificate.hpp"
#include "Device.hpp"
#include "ProvisioningProfile.hpp"
#include "Team.hpp"

#include "AppleAPISession.h"
#include "AnisetteDataManager.h"

#include "DeveloperDiskManager.h"

#include "Semaphore.h"

#include "InstalledApp.h"

#include <pplx/pplxtasks.h>

#ifdef _WIN32
#include <filesystem>
#undef _WINSOCKAPI_
#define _WINSOCKAPI_  /* prevents <winsock.h> inclusion by <windows.h> */
#include <windows.h>
namespace fs = std::filesystem;
#else
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#endif

struct SignResult {
    Application application;
    std::optional<std::set<std::string>> activeProfiles;

};

class MiniappBuilderCore
{
public:
	static MiniappBuilderCore *instance();

	void Start();
	void Stop();
	void CheckForUpdates();
    
	pplx::task<void> InstallApplication(std::string filepath, std::shared_ptr<Device> installDevice, std::optional<std::set<std::string>> activeProfiles);
	
	pplx::task<SignResult> SignWithAppleId(std::string filepath, std::shared_ptr<Device> installDevice, std::string appleID, std::string password, std::string bundleId);
	pplx::task<SignResult> SignWithCertificate(std::string filepath, std::string certificatePath, std::optional<std::string> certificatePassword,std::string profilePath);


	
	pplx::task<void> PrepareDevice(std::shared_ptr<Device> device);

	bool automaticallyLaunchAtLogin() const;
	void setAutomaticallyLaunchAtLogin(bool launch);

	std::string serverID() const;
	void setServerID(std::string serverID);

	bool reprovisionedDevice() const;
	void setReprovisionedDevice(bool reprovisionedDevice);

	std::string appleFolderPath() const;
	std::string internetServicesFolderPath() const;
	std::string applicationSupportFolderPath() const;

	fs::path appDataDirectoryPath() const;
	fs::path certificatesDirectoryPath() const;
	fs::path developerDisksDirectoryPath() const;

	bool boolValueForRegistryKey(std::string key) const;
	void setBoolValueForRegistryKey(bool value, std::string key);

private:
	MiniappBuilderCore();
	~MiniappBuilderCore();

	static MiniappBuilderCore *_instance;

	bool CheckDependencies();
	bool CheckiCloudDependencies();

	std::string BrowseForFolder(std::wstring title, std::string folderPath);

	bool _presentedNotification;
	GUID _notificationIconGUID;

	HWND _windowHandle;
	HINSTANCE _instanceHandle;

	Semaphore _appGroupSemaphore;

	DeveloperDiskManager _developerDiskManager;

	bool presentedRunningNotification() const;
	void setPresentedRunningNotification(bool presentedRunningNotification);

	void setAppleFolderPath(std::string appleFolderPath);
	std::string defaultAppleFolderPath() const;

	void HandleAnisetteError(AnisetteError& error);
    
	pplx::task<std::pair<std::shared_ptr<Account>, std::shared_ptr<AppleAPISession>>>  Authenticate(std::string appleID, std::string password, std::shared_ptr<AnisetteData> anisetteData);
    pplx::task<std::shared_ptr<Team>> FetchTeam(std::shared_ptr<Account> account, std::shared_ptr<AppleAPISession> session);
    pplx::task<std::shared_ptr<Certificate>> FetchCertificate(std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
	pplx::task<std::map<std::string, std::shared_ptr<ProvisioningProfile>>> PrepareAllProvisioningProfiles(
		std::shared_ptr<Application> application,
		std::shared_ptr<Device> device,
		std::shared_ptr<Team> team,
		std::string bundleId,
		std::shared_ptr<AppleAPISession> session);
	pplx::task<std::shared_ptr<ProvisioningProfile>> PrepareProvisioningProfile(
		std::shared_ptr<Application> application,
		std::optional<std::shared_ptr<Application>> parentApp,
		std::shared_ptr<Device> device,
		std::shared_ptr<Team> team,
	std::string bundleId,
		std::shared_ptr<AppleAPISession> session);
    pplx::task<std::shared_ptr<AppID>> RegisterAppID(std::string appName, std::string identifier, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
	pplx::task<std::shared_ptr<AppID>> UpdateAppIDFeatures(std::shared_ptr<AppID> appID, std::shared_ptr<Application> app, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
	pplx::task<std::shared_ptr<AppID>> UpdateAppIDAppGroups(std::shared_ptr<AppID> appID, std::shared_ptr<Application> app, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    pplx::task<std::shared_ptr<Device>> RegisterDevice(std::shared_ptr<Device> device, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    pplx::task<std::shared_ptr<ProvisioningProfile>> FetchProvisioningProfile(std::shared_ptr<AppID> appID, std::shared_ptr<Device> device, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    
	pplx::task<std::optional<std::set<std::string>>> SignCore(std::shared_ptr<Application> app,
		std::shared_ptr<Certificate> certificate,
		std::map<std::string, std::shared_ptr<ProvisioningProfile>> profilesByBundleID);
};
