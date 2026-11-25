// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "AddressBar.h"
#include "AsyncIconFetcher.h"
#include "BrowserCommandController.h"
#include "BrowserWindow.h"
#include "NavigationHelper.h"
#include "RuntimeHelper.h"
#include "ShellBrowser/NavigationEvents.h"
#include "ShellBrowser/ShellBrowser.h"
#include "ShellBrowser/ShellBrowserEvents.h"
#include "ShellBrowser/ShellNavigationController.h"
#include "Tab.h"
#include "TabEvents.h"
#include "../Helper/DragDropHelper.h"
#include "../Helper/Helper.h"
#include "../Helper/KeyboardState.h"
#include "../Helper/ShellHelper.h"
#include <glog/logging.h>

AddressBar::AddressBar(AddressBarView *view, BrowserWindow *browser, TabEvents *tabEvents,
	ShellBrowserEvents *shellBrowserEvents, NavigationEvents *navigationEvents,
	const Runtime *runtime, AsyncIconFetcher *iconFetcher, const KeyboardState *keyboardState) :
	m_view(view),
	m_browser(browser),
	m_runtime(runtime),
	m_iconFetcher(iconFetcher),
	m_keyboardState(keyboardState),
	m_commandTarget(browser->GetCommandTargetManager(), this),
	m_weakPtrFactory(this)
{
	Initialize(tabEvents, shellBrowserEvents, navigationEvents);
}

AddressBar::~AddressBar()
{
	m_view->SetDelegate(nullptr);
}

void AddressBar::Initialize(TabEvents *tabEvents, ShellBrowserEvents *shellBrowserEvents,
	NavigationEvents *navigationEvents)
{
	m_view->SetDelegate(this);
	m_connections.push_back(tabEvents->AddUpdatedObserver(
		[this](const Tab &tab, Tab::PropertyType)
		{
			if (m_browser->IsShellBrowserActive(tab.GetShellBrowser()))
			{
				UpdateTextAndIcon(tab.GetShellBrowser(), IconUpdateType::AlwaysFetch);
			}
		},
		TabEventScope::ForBrowser(*m_browser)));

	m_connections.push_back(tabEvents->AddSelectedObserver(
		std::bind_front(&AddressBar::OnTabSelected, this), TabEventScope::ForBrowser(*m_browser)));

	m_connections.push_back(shellBrowserEvents->AddDirectoryPropertiesChangedObserver(
		std::bind_front(&AddressBar::OnDirectoryPropertiesChanged, this),
		ShellBrowserEventScope::ForActiveShellBrowser(*m_browser)));

	m_connections.push_back(navigationEvents->AddCommittedObserver(
		std::bind_front(&AddressBar::OnNavigationCommitted, this),
		NavigationEventScope::ForActiveShellBrowser(*m_browser)));
}

AddressBarView *AddressBar::GetView()
{
	return m_view;
}

bool AddressBar::OnKeyPressed(UINT key)
{
	switch (key)
	{
	case VK_RETURN:
		OnEnterPressed();
		return true;

	case VK_ESCAPE:
		OnEscapePressed();
		return true;
	}

	return false;
}

void AddressBar::OnEnterPressed()
{
	std::wstring path = m_view->GetText();

	const auto *shellBrowser = m_browser->GetActiveShellBrowser();
	std::wstring currentDirectory = shellBrowser->GetDirectoryPath();

	// When entering a path in the address bar in Windows Explorer, environment variables will be
	// expanded. The behavior here is designed to match that.
	// Note that this does result in potential ambiguity. '%' is a valid character in a filename.
	// That means, for example, it's valid to have a file or folder called %windir%. In cases like
	// that, entering the text %windir% would be ambiguous - the path could refer either to the
	// file/folder or environment variable. Explorer treats it as an environment variable, which is
	// also the behavior here.
	// Additionally, it appears that Explorer doesn't normalize "." in paths (though ".." is
	// normalized). For example, entering "c:\windows\.\" results in an error. Whereas here, the
	// path is normalized before navigation, meaning entering "c:\windows\.\" will result in a
	// navigation to "c:\windows". That also means that entering the relative path ".\" works as
	// expected.
	auto absolutePath = TransformUserEnteredPathToAbsolutePathAndNormalize(path, currentDirectory,
		EnvVarsExpansion::Expand);

	if (!absolutePath)
	{
		// TODO: Should possibly display an error here (perhaps in the status bar).
		return;
	}

	/* TODO: Could keep text user has entered and only revert if navigation fails. */
	// Whether a file or folder is being opened, the address bar text should be reverted to the
	// original text. If the item being opened is a folder, the text will be updated once the
	// navigation commits.
	// Note that if the above call to TransformUserEnteredPathToAbsolutePathAndNormalize() fails,
	// the text won't be reverted. That gives the user the chance to update the text and try again.
	m_view->RevertText();

	m_browser->OpenItem(*absolutePath,
		DetermineOpenDisposition(false, m_keyboardState->IsCtrlDown(),
			m_keyboardState->IsShiftDown()));
	m_browser->FocusActiveTab();
}

void AddressBar::OnEscapePressed()
{
	m_view->RevertText();
	m_browser->FocusActiveTab();
}

void AddressBar::OnBeginDrag()
{
	const auto *shellBrowser = m_browser->GetActiveShellBrowser();
	const auto &pidl = shellBrowser->GetDirectory();
	if (auto *tab = shellBrowser->GetTab(); tab && tab->GetDeferredDevice())
	{
		return;
	}
	StartDragForShellItems({ pidl.Raw() }, DROPEFFECT_LINK);
}

void AddressBar::OnFocused()
{
	m_commandTarget.TargetFocused();
}

void AddressBar::OnBreadcrumbClicked(const PidlAbsolute &pidl, const MouseEvent &event)
{
	if (!pidl.HasValue())
	{
		return;
	}
	m_browser->OpenItem(pidl.Raw(), DetermineOpenDisposition(false, event.ctrlKey, event.shiftKey));
	m_browser->FocusActiveTab();
}

void AddressBar::OnNavigationCommand(int command)
{
	auto *controller = m_browser->GetCommandController();
	if (controller->IsCommandEnabled(command))
	{
		controller->ExecuteCommand(command);
		m_browser->FocusActiveTab();
	}
}

void AddressBar::OnTabSelected(const Tab &tab)
{
	UpdateTextAndIcon(tab.GetShellBrowser());
}

void AddressBar::OnNavigationCommitted(const NavigationRequest *request)
{
	UpdateTextAndIcon(request->GetShellBrowser());
}

void AddressBar::OnDirectoryPropertiesChanged(const ShellBrowser *shellBrowser)
{
	// Since the directory properties have changed, it's possible that the icon has changed.
	// Therefore, the updated icon should always be retrieved.
	UpdateTextAndIcon(shellBrowser, IconUpdateType::AlwaysFetch);
}

void AddressBar::UpdateTextAndIcon(const ShellBrowser *shellBrowser, IconUpdateType iconUpdateType)
{
	// Resetting this here ensures that any previous icon requests that are still ongoing will be
	// ignored once they complete.
	m_scopedStopSource = std::make_unique<ScopedStopSource>();

	const auto &pidl = shellBrowser->GetDirectory();
	if (auto *tab = shellBrowser->GetTab(); tab && tab->GetDeferredDevice())
	{
		m_view->UpdateTextAndIcon(tab->GetName(), m_iconFetcher->GetDefaultIconIndex(pidl.Raw()));
		m_view->UpdateDeferredDevice(tab->GetName());
		m_view->UpdateNavigationButtons(false, false, false);
		return;
	}

	auto cachedIconIndex = m_iconFetcher->MaybeGetCachedIconIndex(pidl.Raw());
	int iconIndex;

	if (cachedIconIndex)
	{
		iconIndex = *cachedIconIndex;
	}
	else
	{
		iconIndex = m_iconFetcher->GetDefaultIconIndex(pidl.Raw());
	}

	if (iconUpdateType == IconUpdateType::AlwaysFetch || !cachedIconIndex)
	{
		RetrieveUpdatedIcon(m_weakPtrFactory.GetWeakPtr(), pidl);
	}

	auto fullPathForDisplay = GetFolderPathForDisplayWithFallback(pidl.Raw());
	m_view->UpdateTextAndIcon(fullPathForDisplay, iconIndex);
	m_view->UpdateDirectory(pidl);
	auto *navigation = shellBrowser->GetNavigationController();
	m_view->UpdateNavigationButtons(navigation->CanGoBack(), navigation->CanGoForward(),
		navigation->CanGoUp());
}

concurrencpp::null_result AddressBar::RetrieveUpdatedIcon(WeakPtr<AddressBar> weakSelf,
	PidlAbsolute pidl)
{
	auto *runtime = weakSelf->m_runtime;
	auto iconFetcher = weakSelf->m_iconFetcher;
	auto stopToken = weakSelf->m_scopedStopSource->GetToken();

	auto iconInfo = co_await iconFetcher->GetIconIndexAsync(pidl.Raw(), stopToken);

	if (!iconInfo)
	{
		co_return;
	}

	co_await ResumeOnUiThread(runtime);

	if (stopToken.stop_requested() || !weakSelf)
	{
		co_return;
	}

	weakSelf->m_view->UpdateTextAndIcon(std::nullopt, iconInfo->iconIndex);
}

bool AddressBar::IsCommandEnabled(int command) const
{
	UNREFERENCED_PARAMETER(command);

	return false;
}

void AddressBar::ExecuteCommand(int command)
{
	UNREFERENCED_PARAMETER(command);
}
