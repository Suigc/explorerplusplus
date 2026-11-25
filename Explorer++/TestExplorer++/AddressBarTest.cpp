// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "pch.h"
#include "AddressBar.h"
#include "AddressBarView.h"
#include "BrowserTestBase.h"
#include "BrowserWindowFake.h"
#include "KeyboardStateFake.h"
#include "PidlTestHelper.h"
#include "ShellBrowser/ShellBrowser.h"
#include "ShellBrowser/ShellNavigationController.h"
#include "Tab.h"
#include "TabStorage.h"
#include "ToolbarView.h"
#include "../Helper/ShellHelper.h"
#include <gtest/gtest.h>
#include <memory>

using namespace testing;

class AddressBarTest : public BrowserTestBase
{
protected:
	AddressBarTest() :
		m_browser(AddBrowser()),
		m_addressBarView(std::make_unique<AddressBarView>(m_browser->GetView(), &m_config,
			&m_resourceLoader, &m_themeManager)),
		m_addressBar(std::make_unique<AddressBar>(m_addressBarView.get(), m_browser, &m_tabEvents,
			&m_shellBrowserEvents, &m_navigationEvents, &m_runtime, &m_iconFetcher,
			m_platformContext.GetKeyboardState()))
	{
	}

	void ShowAddressBar()
	{
		EnableWindow(m_browser->GetHWND(), true);
		ShowWindow(m_browser->GetHWND(), SW_SHOWNOACTIVATE);
		SetWindowPos(m_addressBarView->GetHWND(), nullptr, 0, 0, 1000,
			m_addressBarView->GetHeight(), SWP_NOZORDER | SWP_SHOWWINDOW);
	}

	void ProcessAddressMessages()
	{
		MSG message;
		while (PeekMessage(&message, m_addressBarView->GetHWND(), WM_APP, WM_APP + 4, PM_REMOVE))
		{
			DispatchMessage(&message);
		}
	}

	BrowserWindowFake *const m_browser;
	std::unique_ptr<AddressBarView> m_addressBarView;
	std::unique_ptr<AddressBar> m_addressBar;
};

TEST_F(AddressBarTest, DeferredDeviceHasInertBreadcrumb)
{
	TabStorageData storage;
	storage.directory = L"unavailable-device";
	storage.displayName = L"Phone files";
	storage.portableDevice = true;
	storage.tabSettings.selected = true;
	auto &tab = m_browser->GetActiveTabBar()->CreateDeferredDeviceTab(storage);

	EXPECT_EQ(m_addressBarView->GetText(), storage.displayName);
	const auto &crumbs = m_addressBarView->GetBreadcrumbsForTesting();
	ASSERT_EQ(crumbs.size(), 1);
	EXPECT_EQ(crumbs[0].text, storage.displayName);
	EXPECT_FALSE(crumbs[0].pidl.HasValue());
	m_addressBarView->GetDelegateForTesting()->OnBreadcrumbClicked(crumbs[0].pidl,
		MouseEvent({}, false, false));
	EXPECT_NE(tab.GetDeferredDevice(), nullptr);
}

TEST_F(AddressBarTest, DisplayUpdateAfterNavigation)
{
	auto *tab1 = m_browser->AddTab(L"c:\\");
	auto *tab2 = m_browser->AddTab(L"c:\\");

	auto *browser2 = AddBrowser();
	auto *tab3 = browser2->AddTab(L"c:\\");

	std::wstring path = L"c:\\path\\to\\folder";
	NavigateTab(tab1, path);
	EXPECT_THAT(m_addressBarView->GetText(), StrCaseEq(path));

	// tab2 isn't active, so navigations in it should be ignored by the address bar. The path shown
	// should still be the original path.
	NavigateTab(tab2, L"d:\\");
	EXPECT_THAT(m_addressBarView->GetText(), StrCaseEq(path));

	// tab3 is in a different browser window, so navigations in it should also be ignored.
	NavigateTab(tab3, L"e:\\");
	EXPECT_THAT(m_addressBarView->GetText(), StrCaseEq(path));
}

TEST_F(AddressBarTest, DisplayUpdateAfterTabSwitch)
{
	std::wstring path1 = L"c:\\path1";
	m_browser->AddTab(path1);

	std::wstring path2 = L"c:\\path2";
	m_browser->AddTab(path2);

	EXPECT_THAT(m_addressBarView->GetText(), StrCaseEq(path1));
	m_browser->GetActiveTabBar()->SelectTabAtIndex(1);
	EXPECT_THAT(m_addressBarView->GetText(), StrCaseEq(path2));
}

TEST_F(AddressBarTest, OpenItemOnEnter)
{
	auto *tab1 = m_browser->AddTab(L"c:\\original\\path");

	std::wstring updatedPath = L"c:\\updated\\path";
	m_addressBarView->SetTextForTesting(updatedPath);
	auto *delegate = m_addressBarView->GetDelegateForTesting();
	delegate->OnKeyPressed(VK_RETURN);

	// The key press above should have resulted in a navigation in the active tab.
	EXPECT_EQ(tab1->GetShellBrowser()->GetNavigationController()->GetNumHistoryEntries(), 2);

	auto *currentEntry = tab1->GetShellBrowser()->GetNavigationController()->GetCurrentEntry();
	ASSERT_NE(currentEntry, nullptr);
	EXPECT_EQ(currentEntry->GetPidl(), CreateSimplePidlForTest(updatedPath));
}

TEST_F(AddressBarTest, RevertTextOnEscape)
{
	std::wstring path = L"c:\\path";
	m_browser->AddTab(path);

	m_addressBarView->SetTextForTesting(L"c:\\path\\to\\second\\folder");

	// This should result in the text being reverted back to the original text.
	auto *delegate = m_addressBarView->GetDelegateForTesting();
	delegate->OnKeyPressed(VK_ESCAPE);
	EXPECT_THAT(m_addressBarView->GetText(), StrCaseEq(path));
}

TEST_F(AddressBarTest, ActiveTargetOnFocus)
{
	auto *commandTargetManager = m_browser->GetCommandTargetManager();
	EXPECT_NE(commandTargetManager->GetCurrentTarget(), m_addressBar.get());

	// When the address bar receives focus, it should mark itself as the active target.
	auto *delegate = m_addressBarView->GetDelegateForTesting();
	delegate->OnFocused();
	EXPECT_EQ(commandTargetManager->GetCurrentTarget(), m_addressBar.get());
}

TEST_F(AddressBarTest, IconRetrievalAfterDestruction)
{
	// Doing this will cause the address bar to queue at least one icon retrieval task.
	m_browser->AddTab(L"c:\\");
	EXPECT_TRUE(m_iconFetcher.HasPendingRequests());

	m_addressBar.reset();
	m_addressBarView.reset();

	// This will cause the pending icon retrieval task(s) to return. This should be safe, even
	// though the address bar has been destroyed.
	m_iconFetcher.CompletePendingRequests();
}

TEST_F(AddressBarTest, BreadcrumbParents)
{
	m_browser->AddTab(L"c:\\path\\to\\folder");
	const auto &items = m_addressBarView->GetBreadcrumbsForTesting();
	ASSERT_EQ(items.size(), 4);
	EXPECT_EQ(items[0].pidl, CreateSimplePidlForTest(L"c:\\"));
	EXPECT_EQ(items[1].pidl, CreateSimplePidlForTest(L"c:\\path"));
	EXPECT_EQ(items[2].pidl, CreateSimplePidlForTest(L"c:\\path\\to"));
	EXPECT_EQ(items[3].pidl, CreateSimplePidlForTest(L"c:\\path\\to\\folder"));
}

TEST_F(AddressBarTest, BreadcrumbsFollowSelectedTab)
{
	m_browser->AddTab(L"c:\\first");
	m_browser->AddTab(L"d:\\second\\child");
	m_browser->GetActiveTabBar()->SelectTabAtIndex(1);
	const auto &items = m_addressBarView->GetBreadcrumbsForTesting();
	ASSERT_EQ(items.size(), 3);
	EXPECT_EQ(items[0].pidl, CreateSimplePidlForTest(L"d:\\"));
	EXPECT_EQ(items[2].pidl, CreateSimplePidlForTest(L"d:\\second\\child"));
}

TEST_F(AddressBarTest, VirtualFolderBreadcrumbs)
{
	PidlAbsolute computer;
	ASSERT_HRESULT_SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_ComputerFolder, KF_FLAG_DEFAULT,
		nullptr, PidlOutParam(computer)));
	m_addressBarView->UpdateDirectory(computer);
	const auto &items = m_addressBarView->GetBreadcrumbsForTesting();
	ASSERT_FALSE(items.empty());
	EXPECT_EQ(items.back().pidl, computer);
	for (size_t i = 1; i < items.size(); ++i)
	{
		EXPECT_TRUE(items[i - 1].pidl.IsParent(items[i].pidl));
	}

	PidlAbsolute desktop;
	ASSERT_HRESULT_SUCCEEDED(
		SHGetKnownFolderIDList(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, PidlOutParam(desktop)));
	m_addressBarView->UpdateDirectory(desktop);
	const auto &desktopItems = m_addressBarView->GetBreadcrumbsForTesting();
	ASSERT_EQ(desktopItems.size(), 1);
	EXPECT_EQ(desktopItems[0].pidl, desktop);
}

TEST_F(AddressBarTest, BreadcrumbClickNavigatesToParent)
{
	auto *tab = m_browser->AddTab(L"c:\\path\\to\\folder");
	auto *toolbar = m_addressBarView->GetBreadcrumbToolbarForTesting();
	// The first button is the overflow menu; the next two are the drive and parent.
	toolbar->GetButtonAtIndex(2)->OnClicked({ {}, false, false });
	MSG message;
	while (PeekMessage(&message, m_addressBarView->GetHWND(), WM_APP, WM_APP + 2, PM_REMOVE))
	{
		DispatchMessage(&message);
	}
	EXPECT_EQ(tab->GetShellBrowser()->GetDirectory(), CreateSimplePidlForTest(L"c:\\path"));
}

TEST_F(AddressBarTest, BreadcrumbClickRetainsTargetAcrossTabSwitch)
{
	m_browser->AddTab(L"c:\\original\\child");
	auto *secondTab = m_browser->AddTab(L"d:\\other");
	auto *toolbar = m_addressBarView->GetBreadcrumbToolbarForTesting();
	toolbar->GetButtonAtIndex(2)->OnClicked({ {}, false, false });
	m_browser->GetActiveTabBar()->SelectTabAtIndex(1);
	MSG message;
	while (PeekMessage(&message, m_addressBarView->GetHWND(), WM_APP, WM_APP + 2, PM_REMOVE))
	{
		DispatchMessage(&message);
	}
	EXPECT_EQ(secondTab->GetShellBrowser()->GetDirectory(),
		CreateSimplePidlForTest(L"c:\\original"));
}

TEST_F(AddressBarTest, CtrlClickBreadcrumbOpensNewTab)
{
	m_browser->AddTab(L"c:\\parent\\child");
	auto *toolbar = m_addressBarView->GetBreadcrumbToolbarForTesting();
	toolbar->GetButtonAtIndex(2)->OnClicked({ {}, false, true });
	MSG message;
	while (PeekMessage(&message, m_addressBarView->GetHWND(), WM_APP, WM_APP + 2, PM_REMOVE))
	{
		DispatchMessage(&message);
	}
	EXPECT_EQ(m_browser->GetActiveTabBar()->GetNumTabs(), 2);
}

TEST_F(AddressBarTest, OverflowRestoresAncestorsWhenWidened)
{
	m_browser->AddTab(L"c:\\long-parent-directory\\another-long-directory\\child");
	auto hwnd = m_addressBarView->GetHWND();
	auto *toolbar = m_addressBarView->GetBreadcrumbToolbarForTesting();
	auto isHidden = [toolbar](int index)
	{
		TBBUTTON button;
		SendMessage(toolbar->GetHWND(), TB_GETBUTTON, index, reinterpret_cast<LPARAM>(&button));
		return (SendMessage(toolbar->GetHWND(), TB_GETSTATE, button.idCommand, 0) & TBSTATE_HIDDEN)
			!= 0;
	};
	SetWindowPos(hwnd, nullptr, 0, 0, 300, m_addressBarView->GetHeight(), SWP_NOZORDER);
	EXPECT_FALSE(isHidden(0));
	EXPECT_TRUE(isHidden(1));
	EXPECT_FALSE(isHidden(4));
	SetWindowPos(hwnd, nullptr, 0, 0, 1600, m_addressBarView->GetHeight(), SWP_NOZORDER);
	EXPECT_TRUE(isHidden(0));
	for (int i = 1; i <= 4; ++i)
	{
		EXPECT_FALSE(isHidden(i));
	}
}

TEST_F(AddressBarTest, NavigationButtonStatesFollowHistory)
{
	auto *tab = m_browser->AddTab(L"c:\\first");
	auto *toolbar = m_addressBarView->GetNavigationToolbarForTesting();
	EXPECT_FALSE(toolbar->GetButtonAtIndex(0)->IsEnabled());
	EXPECT_FALSE(toolbar->GetButtonAtIndex(1)->IsEnabled());
	EXPECT_TRUE(toolbar->GetButtonAtIndex(2)->IsEnabled());
	NavigateTab(tab, L"c:\\second");
	EXPECT_TRUE(toolbar->GetButtonAtIndex(0)->IsEnabled());
	EXPECT_FALSE(toolbar->GetButtonAtIndex(1)->IsEnabled());
}

TEST_F(AddressBarTest, BlankClickEntersEditingAndSelectsPath)
{
	std::wstring path = L"c:\\parent\\child";
	m_browser->AddTab(path);
	ShowAddressBar();
	auto toolbar = m_addressBarView->GetBreadcrumbToolbarForTesting()->GetHWND();
	RECT rect;
	GetClientRect(toolbar, &rect);
	SendMessage(toolbar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(rect.right - 8, rect.bottom / 2));
	ASSERT_TRUE(m_addressBarView->IsEditingForTesting());
	EXPECT_THAT(m_addressBarView->GetText(), StrCaseEq(path));
	auto combo = FindWindowEx(m_addressBarView->GetHWND(), nullptr, WC_COMBOBOXEX, nullptr);
	auto edit = reinterpret_cast<HWND>(SendMessage(combo, CBEM_GETEDITCONTROL, 0, 0));
	EXPECT_EQ(GetFocus(), edit);
	DWORD start = 0;
	DWORD end = 0;
	SendMessage(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
	EXPECT_EQ(start, 0u);
	EXPECT_EQ(end, path.size());
}

TEST_F(AddressBarTest, EscapeExitsEditing)
{
	m_browser->AddTab(L"c:\\original");
	ShowAddressBar();
	m_addressBarView->Focus();
	ASSERT_TRUE(m_addressBarView->IsEditingForTesting());
	m_addressBarView->SetTextForTesting(L"c:\\modified");
	m_addressBarView->GetDelegateForTesting()->OnKeyPressed(VK_ESCAPE);
	EXPECT_FALSE(m_addressBarView->IsEditingForTesting());
	EXPECT_THAT(m_addressBarView->GetText(), StrCaseEq(L"c:\\original"));
}

TEST_F(AddressBarTest, EnterNavigatesAndExitsEditing)
{
	auto *tab = m_browser->AddTab(L"c:\\original");
	ShowAddressBar();
	m_addressBarView->Focus();
	m_addressBarView->SetTextForTesting(L"c:\\updated");
	m_addressBarView->GetDelegateForTesting()->OnKeyPressed(VK_RETURN);
	EXPECT_FALSE(m_addressBarView->IsEditingForTesting());
	EXPECT_EQ(tab->GetShellBrowser()->GetDirectory(), CreateSimplePidlForTest(L"c:\\updated"));
}

TEST_F(AddressBarTest, FocusLossCancelsEditing)
{
	m_browser->AddTab(L"c:\\original");
	ShowAddressBar();
	m_addressBarView->Focus();
	m_addressBarView->SetTextForTesting(L"c:\\modified");
	SetFocus(m_browser->GetHWND());
	ProcessAddressMessages();
	EXPECT_FALSE(m_addressBarView->IsEditingForTesting());
	EXPECT_THAT(m_addressBarView->GetText(), StrCaseEq(L"c:\\original"));
}

TEST_F(AddressBarTest, FontChangeRecalculatesBreadcrumbLayout)
{
	m_browser->AddTab(L"c:\\long-parent-directory\\another-long-directory\\child");
	auto hwnd = m_addressBarView->GetHWND();
	SetWindowPos(hwnd, nullptr, 0, 0, 300, m_addressBarView->GetHeight(), SWP_NOZORDER);
	auto originalHeight = m_addressBarView->GetHeight();
	m_config.mainFont = CustomFont(L"Segoe UI", 20_pt);
	ProcessAddressMessages();
	EXPECT_GT(m_addressBarView->GetHeight(), originalHeight);
	auto toolbar = m_addressBarView->GetBreadcrumbToolbarForTesting()->GetHWND();
	TBBUTTON overflow;
	SendMessage(toolbar, TB_GETBUTTON, 0, reinterpret_cast<LPARAM>(&overflow));
	EXPECT_EQ(SendMessage(toolbar, TB_GETSTATE, overflow.idCommand, 0) & TBSTATE_HIDDEN, 0);
	TBBUTTON drive;
	SendMessage(toolbar, TB_GETBUTTON, 1, reinterpret_cast<LPARAM>(&drive));
	EXPECT_NE(SendMessage(toolbar, TB_GETSTATE, drive.idCommand, 0) & TBSTATE_HIDDEN, 0);
}

TEST_F(AddressBarTest, LongFolderNameLeavesBlankEditingTarget)
{
	m_browser->AddTab(L"c:\\" + std::wstring(100, L'x'));
	SetWindowPos(m_addressBarView->GetHWND(), nullptr, 0, 0, 320, m_addressBarView->GetHeight(),
		SWP_NOZORDER);
	auto toolbar = m_addressBarView->GetBreadcrumbToolbarForTesting()->GetHWND();
	RECT client;
	GetClientRect(toolbar, &client);
	RECT last;
	SendMessage(toolbar, TB_GETITEMRECT, 2, reinterpret_cast<LPARAM>(&last));
	EXPECT_LE(last.right, client.right - 32);
}

TEST_F(AddressBarTest, QueuedClickAfterControllerDestruction)
{
	auto *tab = m_browser->AddTab(L"c:\\parent\\child");
	m_addressBarView->GetBreadcrumbToolbarForTesting()->GetButtonAtIndex(2)->OnClicked(
		{ {}, false, false });
	m_addressBar.reset();
	ProcessAddressMessages();
	EXPECT_EQ(m_addressBarView->GetDelegateForTesting(), nullptr);
	EXPECT_EQ(tab->GetShellBrowser()->GetDirectory(),
		CreateSimplePidlForTest(L"c:\\parent\\child"));
}

TEST_F(AddressBarTest, CtrlEnterOpensNewTab)
{
	m_browser->AddTab(L"c:\\original");
	m_addressBarView->SetTextForTesting(L"c:\\updated");
	m_platformContext.GetKeyboardState()->SetCtrlDown(true);
	m_addressBarView->GetDelegateForTesting()->OnKeyPressed(VK_RETURN);
	EXPECT_EQ(m_browser->GetActiveTabBar()->GetNumTabs(), 2);
}
