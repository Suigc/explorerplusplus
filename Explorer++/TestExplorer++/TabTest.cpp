// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "pch.h"
#include "Tab.h"
#include "BrowserWindowMock.h"
#include "PidlTestHelper.h"
#include "ResourceLoaderFake.h"
#include "ShellBrowser/NavigationEvents.h"
#include "ShellBrowser/ShellNavigationController.h"
#include "ShellBrowserFake.h"
#include "TabEvents.h"
#include "TabStorage.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstring>

using namespace testing;

class TabTest : public Test
{
protected:
	Tab BuildTab(const Tab::InitialData &initialData = {})
	{
		return Tab(std::make_unique<ShellBrowserFake>(&m_browser, &m_navigationEvents), &m_browser,
			nullptr, &m_tabEvents, initialData);
	}

	NavigationEvents m_navigationEvents;
	TabEvents m_tabEvents;
	BrowserWindowMock m_browser;
};

TEST_F(TabTest, InitialData)
{
	Tab::InitialData initialData;
	initialData.useCustomName = true;
	initialData.customName = L"Custom tab name";
	initialData.lockState = Tab::LockState::AddressLocked;

	auto tab = BuildTab(initialData);
	EXPECT_EQ(tab.GetUseCustomName(), initialData.useCustomName);
	EXPECT_EQ(tab.GetName(), initialData.customName);
	EXPECT_EQ(tab.GetLockState(), initialData.lockState);
}

TEST_F(TabTest, CustomName)
{
	auto tab = BuildTab();

	std::wstring folderName = L"fake";
	PidlAbsolute pidl = CreateSimplePidlForTest(L"c:\\" + folderName);
	auto navigateParams = NavigateParams::Normal(pidl.Raw());
	tab.GetShellBrowser()->GetNavigationController()->Navigate(navigateParams);

	EXPECT_FALSE(tab.GetUseCustomName());
	EXPECT_EQ(tab.GetName(), folderName);

	std::wstring customName = L"Name";
	tab.SetCustomName(customName);
	EXPECT_TRUE(tab.GetUseCustomName());
	EXPECT_EQ(tab.GetName(), customName);

	tab.ClearCustomName();
	EXPECT_FALSE(tab.GetUseCustomName());
	EXPECT_EQ(tab.GetName(), folderName);
}

TEST_F(TabTest, EmptyName)
{
	auto tab = BuildTab();

	std::wstring folderName = L"fake";
	PidlAbsolute pidl = CreateSimplePidlForTest(L"c:\\" + folderName);
	auto navigateParams = NavigateParams::Normal(pidl.Raw());
	tab.GetShellBrowser()->GetNavigationController()->Navigate(navigateParams);

	// An empty string isn't counted as a valid name and should be ignored.
	tab.SetCustomName(L"");
	EXPECT_FALSE(tab.GetUseCustomName());
	EXPECT_EQ(tab.GetName(), folderName);
}

TEST_F(TabTest, InitialLockState)
{
	auto tab1 = BuildTab({ .lockState = Tab::LockState::NotLocked });
	EXPECT_EQ(tab1.GetShellBrowser()->GetNavigationController()->GetNavigationTargetMode(),
		NavigationTargetMode::Normal);

	auto tab2 = BuildTab({ .lockState = Tab::LockState::Locked });
	EXPECT_EQ(tab2.GetShellBrowser()->GetNavigationController()->GetNavigationTargetMode(),
		NavigationTargetMode::Normal);

	auto tab3 = BuildTab({ .lockState = Tab::LockState::AddressLocked });
	EXPECT_EQ(tab3.GetShellBrowser()->GetNavigationController()->GetNavigationTargetMode(),
		NavigationTargetMode::ForceNewTab);
}

TEST_F(TabTest, LockState)
{
	auto tab = BuildTab();

	EXPECT_EQ(tab.GetLockState(), Tab::LockState::NotLocked);
	EXPECT_FALSE(tab.IsLocked());
	EXPECT_EQ(tab.GetShellBrowser()->GetNavigationController()->GetNavigationTargetMode(),
		NavigationTargetMode::Normal);

	tab.SetLockState(Tab::LockState::Locked);
	EXPECT_EQ(tab.GetLockState(), Tab::LockState::Locked);
	EXPECT_TRUE(tab.IsLocked());
	EXPECT_EQ(tab.GetShellBrowser()->GetNavigationController()->GetNavigationTargetMode(),
		NavigationTargetMode::Normal);

	tab.SetLockState(Tab::LockState::AddressLocked);
	EXPECT_EQ(tab.GetLockState(), Tab::LockState::AddressLocked);
	EXPECT_TRUE(tab.IsLocked());
	EXPECT_EQ(tab.GetShellBrowser()->GetNavigationController()->GetNavigationTargetMode(),
		NavigationTargetMode::ForceNewTab);
}

TEST_F(TabTest, Update)
{
	auto tab = BuildTab();

	MockFunction<void(const Tab &tab, Tab::PropertyType propertyType)> tabUpdatedCallback;
	m_tabEvents.AddUpdatedObserver(tabUpdatedCallback.AsStdFunction(), TabEventScope::Global());

	InSequence seq;

	EXPECT_CALL(tabUpdatedCallback, Call(Ref(tab), Tab::PropertyType::Name)).Times(2);
	EXPECT_CALL(tabUpdatedCallback, Call(Ref(tab), Tab::PropertyType::LockState)).Times(3);

	tab.SetCustomName(L"Name");
	tab.ClearCustomName();

	tab.SetLockState(Tab::LockState::Locked);
	tab.SetLockState(Tab::LockState::AddressLocked);
	tab.SetLockState(Tab::LockState::NotLocked);
}

TEST_F(TabTest, NoUpdateOnCreation)
{
	MockFunction<void(const Tab &tab, Tab::PropertyType propertyType)> tabUpdatedCallback;
	m_tabEvents.AddUpdatedObserver(tabUpdatedCallback.AsStdFunction(), TabEventScope::Global());

	// Specifying a lock state during construction shouldn't result in an update notification being
	// triggered.
	EXPECT_CALL(tabUpdatedCallback, Call(_, _)).Times(0);

	auto tab1 = BuildTab({ .lockState = Tab::LockState::NotLocked });
	auto tab2 = BuildTab({ .lockState = Tab::LockState::Locked });
	auto tab3 = BuildTab({ .lockState = Tab::LockState::AddressLocked });
}

TEST_F(TabTest, GetBrowser)
{
	auto tab = BuildTab();

	EXPECT_EQ(tab.GetBrowser(), &m_browser);
}

TEST_F(TabTest, DeferredDeviceSurvivesSavingAndSelection)
{
	auto tab = BuildTab();
	TabStorageData storage;
	storage.pidl = CreateSimplePidlForTest(L"c:\\phone");
	storage.portableDevice = true;
	storage.displayName = L"Phone storage";
	storage.tabSettings.lockState = Tab::LockState::NotLocked;
	ResourceLoaderFake resources;
	tab.DeferDeviceConnection(storage, &resources, &m_navigationEvents);

	m_tabEvents.NotifySelected(tab);
	EXPECT_NE(tab.GetDeferredDevice(), nullptr);
	EXPECT_EQ(tab.GetName(), storage.displayName);
	EXPECT_EQ(tab.GetStorageData(), storage);

	tab.SetCustomName(L"Work phone");
	EXPECT_EQ(tab.GetName(), L"Work phone");
	EXPECT_EQ(tab.GetStorageData().pidl, storage.pidl);
	EXPECT_TRUE(tab.GetStorageData().portableDevice);
}

TEST_F(TabTest, ReconnectDeferredAddressLockedDeviceInSameTab)
{
	auto tab = BuildTab({ .lockState = Tab::LockState::AddressLocked });
	TabStorageData storage;
	storage.pidl = CreateSimplePidlForTest(L"c:\\phone");
	storage.displayName = L"Phone";
	ResourceLoaderFake resources;
	tab.DeferDeviceConnection(storage, &resources, &m_navigationEvents);
	tab.ReconnectDevice();

	EXPECT_EQ(tab.GetDeferredDevice(), nullptr);
	EXPECT_EQ(tab.GetShellBrowser()->GetDirectory(), storage.pidl);
	EXPECT_EQ(tab.GetLockState(), Tab::LockState::AddressLocked);
}

TEST_F(TabTest, NavigateAwayFromDeferredDevice)
{
	auto tab = BuildTab();
	TabStorageData storage;
	storage.displayName = L"Phone";
	ResourceLoaderFake resources;
	tab.DeferDeviceConnection(storage, &resources, &m_navigationEvents);
	auto directory = CreateSimplePidlForTest(L"c:\\work");
	auto params = NavigateParams::Normal(directory.Raw());
	tab.GetShellBrowser()->GetNavigationController()->Navigate(params);

	EXPECT_EQ(tab.GetDeferredDevice(), nullptr);
	EXPECT_EQ(tab.GetStorageData().pidl, directory);
}

TEST_F(TabTest, FailedDevicePathKeepsDeferredTab)
{
	auto tab = BuildTab();
	TabStorageData storage;
	storage.directory = L"c:\\__ExplorerPP_unavailable_device_784fde21";
	storage.displayName = L"Phone";
	ResourceLoaderFake resources;
	tab.DeferDeviceConnection(storage, &resources, &m_navigationEvents);
	tab.ReconnectDevice();
	EXPECT_NE(tab.GetDeferredDevice(), nullptr);
	EXPECT_EQ(tab.GetStorageData().directory, storage.directory);
}

TEST_F(TabTest, DetectPortableDeviceWithoutShellQueries)
{
	TabStorageData storage;
	storage.directory = L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\"
						L"\\\\?\\usb#vid_1234#{6AC27878-A6FA-4155-BA85-F98F491D4F33}\\storage";
	EXPECT_TRUE(IsPortableDeviceTab(storage));
	storage.directory = L"c:\\work";
	storage.pidl = CreateSimplePidlForTest(storage.directory);
	EXPECT_FALSE(IsPortableDeviceTab(storage));
	storage.directory = L"c:\\{6ac27878-a6fa-4155-ba85-f98f491d4f33}";
	storage.pidl = CreateSimplePidlForTest(storage.directory);
	EXPECT_FALSE(IsPortableDeviceTab(storage));
	storage.portableDevice = true;
	EXPECT_TRUE(IsPortableDeviceTab(storage));
}

TEST_F(TabTest, DetectLegacyDevicePidlWithUnalignedInterfacePath)
{
	const std::wstring path = L"\\\\?\\usb#vid_1234#{6ac27878-a6fa-4155-ba85-f98f491d4f33}";
	for (size_t padding : { 0, 1 })
	{
		USHORT itemSize =
			static_cast<USHORT>(sizeof(USHORT) + padding + (path.size() + 1) * sizeof(wchar_t));
		std::vector<unsigned char> bytes(itemSize + sizeof(USHORT), 0);
		memcpy(bytes.data(), &itemSize, sizeof(itemSize));
		memcpy(bytes.data() + sizeof(USHORT) + padding, path.data(), path.size() * sizeof(wchar_t));
		TabStorageData storage;
		storage.pidl = reinterpret_cast<PCIDLIST_ABSOLUTE>(bytes.data());
		EXPECT_TRUE(IsPortableDeviceTab(storage));
	}
}
