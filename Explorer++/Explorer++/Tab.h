// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include "../Helper/WeakPtrFactory.h"
#include <boost/core/noncopyable.hpp>
#include <boost/signals2.hpp>
#include <wil/resource.h>
#include <memory>
#include <vector>

class BrowserWindow;
class ShellBrowser;
class ShellBrowserImpl;
class TabBar;
class TabEvents;
struct TabStorageData;
class NavigationEvents;
class ResourceLoader;
class WindowSubclass;
class ModelessDialogList;

class Tab : private boost::noncopyable
{
public:
	enum class PropertyType
	{
		Name,
		LockState
	};

	enum class LockState
	{
		// The tab isn't locked; it can be navigated freely and closed.
		NotLocked,

		// The tab is locked. It can be navigated freely, but not closed.
		Locked,

		// Both the tab and address are locked. The tab can't be navigated or closed. All
		// navigations will proceed in a new tab.
		AddressLocked
	};

	struct InitialData
	{
		bool useCustomName = false;
		std::wstring customName;
		LockState lockState = LockState::NotLocked;
	};

	Tab(std::unique_ptr<ShellBrowser> shellBrowser, BrowserWindow *browser, TabBar *tabBar,
		TabEvents *tabEvents);
	Tab(std::unique_ptr<ShellBrowser> shellBrowser, BrowserWindow *browser, TabBar *tabBar,
		TabEvents *tabEvents, const InitialData &initialData);
	~Tab();

	int GetId() const;

	ShellBrowser *GetShellBrowser() const;
	ShellBrowserImpl *GetShellBrowserImpl() const;

	BrowserWindow *GetBrowser() const;
	TabBar *GetTabBar() const;

	std::wstring GetName() const;
	bool GetUseCustomName() const;
	void SetCustomName(const std::wstring &name);
	void ClearCustomName();

	LockState GetLockState() const;
	void SetLockState(LockState lockState);

	// Returns true if the tab is locked, or address locked.
	bool IsLocked() const;

	TabStorageData GetStorageData() const;
	const TabStorageData *GetDeferredDevice() const;
	void DeferDeviceConnection(const TabStorageData &storage, const ResourceLoader *resourceLoader,
		NavigationEvents *navigationEvents, ModelessDialogList *dialogs = nullptr);
	void ReconnectDevice();
	void Focus() const;

	WeakPtr<Tab> GetWeakPtr();

	/* Although each tab manages its
	own columns, it does not know
	about any column defaults.
	Therefore, it makes more sense
	for this setting to remain here. */
	// BOOL	bUsingDefaultColumns;

private:
	enum class NotificationMode
	{
		DontNotify,
		Notify
	};

	void ApplyLockState(LockState lockState, NotificationMode notificationMode);
	void LayoutDevicePage();
	void ClearDeferredDevice();
	void ShowDeviceConnectionError();

	static inline int idCounter = 1;
	const int m_id;

	const std::unique_ptr<ShellBrowser> m_shellBrowser;
	ShellBrowserImpl *const m_shellBrowserImpl;

	BrowserWindow *const m_browser;
	TabBar *const m_tabBar;
	TabEvents *const m_tabEvents;

	bool m_useCustomName;
	std::wstring m_customName;
	LockState m_lockState;
	std::unique_ptr<TabStorageData> m_deferredDevice;
	wil::unique_hwnd m_devicePage;
	std::unique_ptr<WindowSubclass> m_devicePageLayout;
	std::vector<boost::signals2::scoped_connection> m_deviceConnections;
	ModelessDialogList *m_deviceDialogs = nullptr;
	std::wstring m_deviceConnectionError;

	WeakPtrFactory<Tab> m_weakPtrFactory{ this };
};
