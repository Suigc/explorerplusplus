// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "Tab.h"
#include "Config.h"
#include "MainResource.h"
#include "ModelessDialogList.h"
#include "ResourceLoader.h"
#include "ShellBrowser/FolderSettings.h"
#include "ShellBrowser/NavigationEvents.h"
#include "ShellBrowser/ShellBrowserImpl.h"
#include "ShellBrowser/ShellNavigationController.h"
#include "TabEvents.h"
#include "TabStorage.h"
#include "../Helper/PidlHelper.h"
#include "../Helper/WindowSubclass.h"
#include <boost/algorithm/string/predicate.hpp>

namespace
{

constexpr UINT WM_RECONNECT_DEVICE = WM_APP + 215;

}

bool IsPortableDeviceTab(const TabStorageData &tab)
{
	const bool shellPath =
		boost::starts_with(tab.directory, L"::") || boost::starts_with(tab.directory, L"\\\\?\\");
	return tab.portableDevice
		|| (shellPath && boost::icontains(tab.directory, PORTABLE_DEVICE_INTERFACE_GUID))
		|| IsPortableDevicePidl(tab.pidl.Raw());
}

Tab::Tab(std::unique_ptr<ShellBrowser> shellBrowser, BrowserWindow *browser, TabBar *tabBar,
	TabEvents *tabEvents) :
	Tab(std::move(shellBrowser), browser, tabBar, tabEvents, {})
{
}

// Note the use of std::dynamic_pointer_cast below. There are a number of places where the caller
// needs to be able to retrieve the ShellBrowser implementation class (ShellBrowserImpl), so that's
// something this class has to provide.
// In tests, however, it's not feasible to provide the concrete implementation instance. While the
// instance can be set to null (and is in some tests), doing that greatly limits the way in which
// this class can be used when testing. In turn, that can make it infeasible to test classes that
// rely on this class.
// By accepting a ShellBrowser instance and dynamically casting it to ShellBrowserImpl, this class
// can continue to provide access to the implementation in normal circumstances (outside of tests).
// Within tests, the implementation won't be available, however GetShellBrowser() will still work
// (provided the test has supplied a ShellBrowser instance). That's useful, since other classes may
// only need to access the ShellBrowser through its base interface, rather than via the
// implementation class.
// If the ShellBrowser interface expands to cover all the necessary functionality, or
// ShellBrowserImpl is simplified enough to make it usable in tests, the casting here can be
// removed.
Tab::Tab(std::unique_ptr<ShellBrowser> shellBrowser, BrowserWindow *browser, TabBar *tabBar,
	TabEvents *tabEvents, const InitialData &initialData) :
	m_id(idCounter++),
	m_shellBrowser(std::move(shellBrowser)),
	m_shellBrowserImpl(dynamic_cast<ShellBrowserImpl *>(m_shellBrowser.get())),
	m_browser(browser),
	m_tabBar(tabBar),
	m_tabEvents(tabEvents),
	m_useCustomName(initialData.useCustomName),
	m_customName(initialData.customName),
	m_lockState(LockState::NotLocked)
{
	m_shellBrowser->SetTab(this);

	ApplyLockState(initialData.lockState, NotificationMode::DontNotify);
}

Tab::~Tab()
{
	if (m_shellBrowserImpl)
	{
		m_shellBrowserImpl->SetDeviceConnectionDeferred(false);
	}
	if (m_deviceDialogs)
	{
		m_deviceDialogs->RemoveDialog(L"device-connection-" + std::to_wstring(m_id));
	}
}

int Tab::GetId() const
{
	return m_id;
}

ShellBrowser *Tab::GetShellBrowser() const
{
	return m_shellBrowser.get();
}

ShellBrowserImpl *Tab::GetShellBrowserImpl() const
{
	return m_shellBrowserImpl;
}

BrowserWindow *Tab::GetBrowser() const
{
	return m_browser;
}

TabBar *Tab::GetTabBar() const
{
	return m_tabBar;
}

// If a custom name has been set, that will be returned. Otherwise, the
// display name of the current directory will be returned.
std::wstring Tab::GetName() const
{
	if (m_useCustomName)
	{
		return m_customName;
	}
	if (m_deferredDevice)
	{
		return m_deferredDevice->displayName;
	}

	auto *entry = m_shellBrowser->GetNavigationController()->GetCurrentEntry();
	return GetDisplayNameWithFallback(entry->GetPidl().Raw(), SHGDN_INFOLDER);
}

bool Tab::GetUseCustomName() const
{
	return m_useCustomName;
}

void Tab::SetCustomName(const std::wstring &name)
{
	if (name.empty())
	{
		return;
	}

	m_useCustomName = true;
	m_customName = name;

	m_tabEvents->NotifyUpdated(*this, PropertyType::Name);
}

void Tab::ClearCustomName()
{
	m_useCustomName = false;
	m_customName.erase();

	m_tabEvents->NotifyUpdated(*this, PropertyType::Name);
}

Tab::LockState Tab::GetLockState() const
{
	return m_lockState;
}

void Tab::SetLockState(LockState lockState)
{
	ApplyLockState(lockState, NotificationMode::Notify);
}

void Tab::ApplyLockState(LockState lockState, NotificationMode notificationMode)
{
	if (lockState == m_lockState)
	{
		return;
	}

	m_lockState = lockState;

	auto navigationTargetMode = (lockState == LockState::AddressLocked)
		? NavigationTargetMode::ForceNewTab
		: NavigationTargetMode::Normal;
	m_shellBrowser->GetNavigationController()->SetNavigationTargetMode(navigationTargetMode);

	if (notificationMode == NotificationMode::Notify)
	{
		m_tabEvents->NotifyUpdated(*this, PropertyType::LockState);
	}
}

bool Tab::IsLocked() const
{
	return m_lockState == LockState::Locked || m_lockState == LockState::AddressLocked;
}

TabStorageData Tab::GetStorageData() const
{
	TabStorageData storageData;
	if (m_deferredDevice)
	{
		storageData = *m_deferredDevice;
	}
	else
	{
		storageData.pidl = m_shellBrowser->GetDirectory();
		storageData.directory = m_shellBrowser->GetDirectoryPath();
		storageData.folderSettings = m_shellBrowser->GetFolderSettings();
		storageData.columns = m_shellBrowser->GetAllColumnSets();
		storageData.portableDevice = IsPortableDeviceTab(storageData);
		if (storageData.portableDevice)
		{
			storageData.displayName = GetName();
		}
	}

	TabSettings tabSettings;

	if (m_useCustomName)
	{
		tabSettings.name = m_customName;
	}

	tabSettings.lockState = m_lockState;

	storageData.tabSettings = tabSettings;

	return storageData;
}

const TabStorageData *Tab::GetDeferredDevice() const
{
	return m_deferredDevice.get();
}

void Tab::DeferDeviceConnection(const TabStorageData &storage, const ResourceLoader *resourceLoader,
	NavigationEvents *navigationEvents, ModelessDialogList *dialogs)
{
	m_deferredDevice = std::make_unique<TabStorageData>(storage);
	m_deferredDevice->portableDevice = true;
	m_deviceConnectionError = resourceLoader->LoadString(IDS_DEVICE_CONNECTION_FAILED);
	if (m_deferredDevice->displayName.empty())
	{
		m_deferredDevice->displayName = resourceLoader->LoadString(IDS_DEVICE_TAB_TITLE);
	}

	if (m_shellBrowserImpl)
	{
		m_shellBrowserImpl->SetDeviceConnectionDeferred(true);
		auto listView = m_shellBrowserImpl->GetListView();
		m_devicePage.reset(resourceLoader->CreateModelessDialog(IDD_DEVICE_CONNECTION, listView,
			[this, listView](HWND, UINT message, WPARAM wParam, LPARAM)
			{
				if (message == WM_COMMAND && LOWORD(wParam) == IDC_DEVICE_RECONNECT)
				{
					PostMessage(listView, WM_RECONNECT_DEVICE, 0, 0);
					return static_cast<INT_PTR>(TRUE);
				}
				return static_cast<INT_PTR>(FALSE);
			}));
		CHECK(m_devicePage);
		m_deviceDialogs = dialogs;
		if (m_deviceDialogs)
		{
			m_deviceDialogs->AddDialog(L"device-connection-" + std::to_wstring(m_id),
				m_devicePage.get());
		}
		m_devicePageLayout = std::make_unique<WindowSubclass>(listView,
			[this](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT
			{
				if (message == WM_RECONNECT_DEVICE)
				{
					ReconnectDevice();
					return 0;
				}
				auto result = DefSubclassProc(hwnd, message, wParam, lParam);
				if (message == WM_SIZE)
				{
					LayoutDevicePage();
				}
				return result;
			});
		LayoutDevicePage();
	}

	auto scope = NavigationEventScope::ForShellBrowser(*m_shellBrowser);
	auto *initialNavigation = m_shellBrowser->MaybeGetLatestActiveNavigation();
	if (initialNavigation && m_devicePage)
	{
		EnableWindow(GetDlgItem(m_devicePage.get(), IDC_DEVICE_RECONNECT), FALSE);
	}
	m_deviceConnections.push_back(navigationEvents->AddCommittedObserver(
		[this, initialNavigation, initialPending = initialNavigation != nullptr](
			const NavigationRequest *request) mutable
		{
			if (initialPending && request == initialNavigation)
			{
				initialPending = false;
				if (m_devicePage)
				{
					EnableWindow(GetDlgItem(m_devicePage.get(), IDC_DEVICE_RECONNECT), TRUE);
				}
				return;
			}
			ClearDeferredDevice();
		},
		scope, boost::signals2::at_front, SlotGroup::HighPriority));
	m_deviceConnections.push_back(navigationEvents->AddFailedObserver(
		[this](const NavigationRequest *) { ShowDeviceConnectionError(); }, scope));
	m_deviceConnections.push_back(navigationEvents->AddCancelledObserver(
		[this](const NavigationRequest *)
		{
			if (m_devicePage)
			{
				EnableWindow(GetDlgItem(m_devicePage.get(), IDC_DEVICE_RECONNECT), TRUE);
			}
		},
		scope));
	m_tabEvents->NotifyUpdated(*this, PropertyType::Name);
}

void Tab::LayoutDevicePage()
{
	if (!m_devicePage)
	{
		return;
	}
	RECT bounds;
	GetClientRect(m_shellBrowserImpl->GetListView(), &bounds);
	SetWindowPos(m_devicePage.get(), HWND_TOP, 0, 0, bounds.right, bounds.bottom, SWP_NOACTIVATE);
	RECT button;
	GetWindowRect(GetDlgItem(m_devicePage.get(), IDC_DEVICE_RECONNECT), &button);
	auto width = button.right - button.left;
	auto height = button.bottom - button.top;
	SetWindowPos(GetDlgItem(m_devicePage.get(), IDC_DEVICE_MESSAGE), nullptr, 12,
		std::max(12L, bounds.bottom / 2 - height * 2), std::max(0L, bounds.right - 24), height * 2,
		SWP_NOZORDER | SWP_NOACTIVATE);
	SetWindowPos(GetDlgItem(m_devicePage.get(), IDC_DEVICE_RECONNECT), nullptr,
		std::max(0L, (bounds.right - width) / 2), bounds.bottom / 2 + height, width, height,
		SWP_NOZORDER | SWP_NOACTIVATE);
}

void Tab::ClearDeferredDevice()
{
	if (!m_deferredDevice)
	{
		return;
	}
	m_deferredDevice.reset();
	if (m_shellBrowserImpl)
	{
		m_shellBrowserImpl->SetDeviceConnectionDeferred(false);
	}
	if (m_deviceDialogs)
	{
		m_deviceDialogs->RemoveDialog(L"device-connection-" + std::to_wstring(m_id));
		m_deviceDialogs = nullptr;
	}
	m_devicePage.reset();
	m_tabEvents->NotifyUpdated(*this, PropertyType::Name);
}

void Tab::Focus() const
{
	if (m_devicePage)
	{
		SetFocus(GetDlgItem(m_devicePage.get(), IDC_DEVICE_RECONNECT));
	}
	else if (m_shellBrowserImpl)
	{
		SetFocus(m_shellBrowserImpl->GetListView());
	}
}

void Tab::ReconnectDevice()
{
	if (!m_deferredDevice || m_shellBrowser->MaybeGetLatestActiveNavigation())
	{
		return;
	}
	auto pidl = m_deferredDevice->pidl;
	if (!pidl.HasValue())
	{
		if (FAILED(SHParseDisplayName(m_deferredDevice->directory.c_str(), nullptr,
				PidlOutParam(pidl), 0, nullptr)))
		{
			ShowDeviceConnectionError();
			return;
		}
	}
	auto params = NavigateParams::Normal(pidl.Raw(), HistoryEntryType::ReplaceCurrentEntry);
	params.overrideNavigationTargetMode = true;
	params.itemsToSelect = m_deferredDevice->itemsToSelect;
	if (m_devicePage)
	{
		EnableWindow(GetDlgItem(m_devicePage.get(), IDC_DEVICE_RECONNECT), FALSE);
	}
	m_shellBrowser->GetNavigationController()->Navigate(params);
}

void Tab::ShowDeviceConnectionError()
{
	if (m_devicePage)
	{
		SetDlgItemText(m_devicePage.get(), IDC_DEVICE_MESSAGE, m_deviceConnectionError.c_str());
		EnableWindow(GetDlgItem(m_devicePage.get(), IDC_DEVICE_RECONNECT), TRUE);
	}
}

WeakPtr<Tab> Tab::GetWeakPtr()
{
	return m_weakPtrFactory.GetWeakPtr();
}
