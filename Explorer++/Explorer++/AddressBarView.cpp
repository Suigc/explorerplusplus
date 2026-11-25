// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "AddressBarView.h"
#include "AddressBarViewDelegate.h"
#include "MainResource.h"
#include "ResourceLoader.h"
#include "TestHelper.h"
#include "ThemeManager.h"
#include "ToolbarView.h"
#include "../Helper/DpiCompatibility.h"
#include "../Helper/ShellHelper.h"
#include "../Helper/WindowHelper.h"
#include "../Helper/WindowSubclass.h"
#include <Shlwapi.h>
#include <algorithm>
#include <array>

namespace
{
constexpr UINT WM_OPEN_BREADCRUMB = WM_APP + 1;
constexpr UINT WM_FINISH_EDITING = WM_APP + 2;
constexpr UINT WM_REFRESH_BREADCRUMBS = WM_APP + 3;
constexpr UINT WM_SHOW_ANCESTORS = WM_APP + 4;
constexpr DWORD TOOLBAR_STYLE = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_TOOLTIPS
	| TBSTYLE_LIST | TBSTYLE_FLAT | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN;

struct NavigationButtonDefinition
{
	int command;
	UINT textId;
	Icon icon;
};

constexpr auto NAVIGATION_BUTTONS = std::to_array<NavigationButtonDefinition>({
	{ IDM_GO_BACK, IDS_TOOLBAR_BACK, Icon::Back },
	{ IDM_GO_FORWARD, IDS_TOOLBAR_FORWARD, Icon::Forward },
	{ IDM_GO_UP, IDS_TOOLBAR_UP, Icon::Up },
	{ IDM_VIEW_REFRESH, IDS_TOOLBAR_REFRESH, Icon::Refresh },
});

class AddressButton : public ToolbarButton
{
public:
	AddressButton(std::wstring text, std::optional<int> image, bool showText,
		MouseEventCallback callback) :
		ToolbarButton(std::move(callback)),
		m_text(std::move(text)),
		m_image(image),
		m_showText(showText)
	{
	}

	std::wstring GetText() const override
	{
		return m_text;
	}
	std::wstring GetTooltipText() const override
	{
		return m_text;
	}
	std::optional<int> GetImageIndex() const override
	{
		return m_image;
	}
	bool ShouldShowText() const override
	{
		return m_showText;
	}
	bool IsEnabled() const override
	{
		return m_enabled;
	}
	void SetEnabled(bool enabled)
	{
		m_enabled = enabled;
		NotifyParentOfUpdate();
	}

private:
	std::wstring m_text;
	std::optional<int> m_image;
	bool m_showText;
	bool m_enabled = true;
};
}

AddressBarView::AddressBarView(ViewBase *parent, const Config *config,
	const ResourceLoader *resourceLoader, const ThemeManager *themeManager) :
	ViewBase(CreateAddressBar(parent)),
	m_resourceLoader(resourceLoader),
	m_themeManager(themeManager),
	m_comboBox(CreateWindowEx(0, WC_COMBOBOXEX, L"",
		WS_CHILD | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		0, 0, 0, 200, GetHWND(), nullptr, GetModuleHandle(nullptr), nullptr)),
	m_fontSetter(m_comboBox, config)
{
	CHECK(m_comboBox);
	HIMAGELIST smallIcons;
	CHECK(Shell_GetImageLists(nullptr, &smallIcons));
	SendMessage(m_comboBox, CBEM_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(smallIcons));

	m_navigationToolbar = std::make_unique<ToolbarView>(this, TOOLBAR_STYLE,
		TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER, nullptr, config);
	m_breadcrumbToolbar = std::make_unique<ToolbarView>(this, TOOLBAR_STYLE,
		TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER, nullptr, config);
	CreateNavigationImages();
	CreateNavigationButtons();
	SendMessage(m_breadcrumbToolbar->GetHWND(), TB_SETMAXTEXTROWS, 1, 0);

	AddSubclass(std::make_unique<WindowSubclass>(GetHWND(),
		std::bind_front(&AddressBarView::WndProc, this)));
	AddSubclass(std::make_unique<WindowSubclass>(m_comboBox,
		std::bind_front(&AddressBarView::ComboBoxExSubclass, this)));
	AddSubclass(std::make_unique<WindowSubclass>(GetEditControl(),
		std::bind_front(&AddressBarView::EditSubclass, this)));
	AddSubclass(std::make_unique<WindowSubclass>(m_breadcrumbToolbar->GetHWND(),
		std::bind_front(&AddressBarView::BreadcrumbSubclass, this)));

	HRESULT hr = SHAutoComplete(GetEditControl(), SHACF_FILESYSTEM | SHACF_AUTOSUGGEST_FORCE_ON);
	DCHECK(SUCCEEDED(hr));
	m_fontSetter.fontUpdatedSignal.AddObserver(
		std::bind(&AddressBarView::OnFontOrDpiUpdated, this));
}

AddressBarView::~AddressBarView() = default;

HWND AddressBarView::CreateAddressBar(ViewBase *parent)
{
	static const auto atom = []
	{
		WNDCLASS windowClass = {};
		windowClass.lpfnWndProc = DefWindowProc;
		windowClass.hInstance = GetModuleHandle(nullptr);
		windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
		windowClass.lpszClassName = L"Explorer++AddressBar";
		return RegisterClass(&windowClass);
	}();
	CHECK(atom);
	return CreateWindowEx(WS_EX_CONTROLPARENT, MAKEINTATOM(atom), L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
		parent->GetHWND(), nullptr, GetModuleHandle(nullptr), nullptr);
}

LRESULT AddressBarView::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_SIZE:
		Layout();
		return 0;
	case WM_SETFOCUS:
		Focus();
		return 0;
	case WM_DPICHANGED_AFTERPARENT:
		CreateNavigationImages();
		OnFontOrDpiUpdated();
		break;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		BeginPaint(hwnd, &ps);
		Paint(ps.hdc);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_PRINTCLIENT:
		Paint(reinterpret_cast<HDC>(wParam));
		return 0;
	case WM_LBUTTONDOWN:
		if (PtInRect(&m_pathRect, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
		{
			Focus();
			SelectAllText();
		}
		break;
	case WM_NOTIFY:
		if (auto *header = reinterpret_cast<NMHDR *>(lParam);
			header->hwndFrom == m_comboBox && header->code == CBEN_DRAGBEGIN && m_delegate)
		{
			m_delegate->OnBeginDrag();
		}
		break;
	case WM_OPEN_BREADCRUMB:
		if (!m_pendingNavigations.empty())
		{
			auto navigation = std::move(m_pendingNavigations.front());
			m_pendingNavigations.pop_front();
			if (m_delegate)
			{
				m_delegate->OnBreadcrumbClicked(navigation.first, navigation.second);
			}
		}
		return 0;
	case WM_FINISH_EDITING:
		if (GetFocus() != GetEditControl() && !IsChild(m_comboBox, GetFocus())
			&& !SendMessage(m_comboBox, CB_GETDROPPEDSTATE, 0, 0))
		{
			RevertText();
		}
		return 0;
	case WM_REFRESH_BREADCRUMBS:
		RebuildBreadcrumbButtons();
		Layout();
		NotifySizeRequirementsChanged();
		return 0;
	case WM_SHOW_ANCESTORS:
		if (m_pendingMenuEvent)
		{
			auto event = *m_pendingMenuEvent;
			m_pendingMenuEvent.reset();
			ShowAncestorsMenu(event);
		}
		return 0;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT AddressBarView::ComboBoxExSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_DPICHANGED_AFTERPARENT)
	{
		OnFontOrDpiUpdated();
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT AddressBarView::EditSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		if (m_delegate && m_delegate->OnKeyPressed(static_cast<UINT>(wParam)))
		{
			return 0;
		}
		break;
	case WM_SETFOCUS:
		if (m_delegate)
		{
			m_delegate->OnFocused();
		}
		break;
	case WM_KILLFOCUS:
		PostMessage(GetHWND(), WM_FINISH_EDITING, 0, 0);
		break;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT AddressBarView::BreadcrumbSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_ERASEBKGND)
	{
		RECT rect;
		GetClientRect(hwnd, &rect);
		FillRect(reinterpret_cast<HDC>(wParam), &rect,
			m_themeManager->GetCurrentColorProvider()->GetBackgroundBrush());
		return 1;
	}
	if (msg == WM_LBUTTONDOWN
		&& !m_breadcrumbToolbar->MaybeGetIndexOfButtonAtPoint(
			{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
	{
		Focus();
		SelectAllText();
		return 0;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void AddressBarView::SetDelegate(AddressBarViewDelegate *delegate)
{
	m_delegate = delegate;
}

HWND AddressBarView::GetHWND() const
{
	return ViewBase::GetHWND();
}

int AddressBarView::Scale(int value) const
{
	return DpiCompatibility::GetInstance().ScaleValue(GetHWND(), value);
}

int AddressBarView::GetHeight() const
{
	RECT comboRect;
	GetWindowRect(m_comboBox, &comboRect);
	return std::max({ Scale(28), GetRectHeight(&comboRect) + Scale(4),
		m_navigationToolbar->GetHeight() + Scale(4), m_breadcrumbToolbar->GetHeight() + Scale(4) });
}

void AddressBarView::CreateNavigationImages()
{
	int size = Scale(16);
	wil::unique_himagelist images(ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK,
		static_cast<int>(NAVIGATION_BUTTONS.size()) + 1, 0));
	CHECK(images);
	for (const auto &definition : NAVIGATION_BUTTONS)
	{
		auto bitmap = m_resourceLoader->LoadBitmapFromPNGAndScale(definition.icon, size, size);
		CHECK(ImageList_Add(images.get(), bitmap.get(), nullptr) != -1);
	}
	auto folderBitmap = m_resourceLoader->LoadBitmapFromPNGAndScale(Icon::Folder, size, size);
	CHECK(ImageList_Add(images.get(), folderBitmap.get(), nullptr) != -1);
	m_navigationToolbar->SetImageList(images.get());
	m_breadcrumbToolbar->SetImageList(images.get());
	m_navigationImages = std::move(images);
}

void AddressBarView::CreateNavigationButtons()
{
	for (size_t i = 0; i < NAVIGATION_BUTTONS.size(); ++i)
	{
		auto definition = NAVIGATION_BUTTONS[i];
		m_navigationToolbar->AddButton(
			std::make_unique<AddressButton>(m_resourceLoader->LoadString(definition.textId),
				static_cast<int>(i), false,
				[this, command = definition.command](const MouseEvent &)
				{
					if (m_delegate)
					{
						m_delegate->OnNavigationCommand(command);
					}
				}),
			i);
	}
}

void AddressBarView::UpdateNavigationButtons(bool canGoBack, bool canGoForward, bool canGoUp)
{
	std::array enabled{ canGoBack, canGoForward, canGoUp };
	for (size_t i = 0; i < enabled.size(); ++i)
	{
		static_cast<AddressButton *>(m_navigationToolbar->GetButtonAtIndex(i))
			->SetEnabled(enabled[i]);
	}
}

void AddressBarView::Layout()
{
	RECT rect;
	GetClientRect(GetHWND(), &rect);
	int padding = Scale(2);
	int navWidth = std::min(m_navigationToolbar->MaybeGetIdealWidth().value_or(0),
		std::max(0, static_cast<int>(rect.right) - Scale(64)));
	int navHeight = m_navigationToolbar->GetHeight();
	SetWindowPos(m_navigationToolbar->GetHWND(), nullptr, 0,
		std::max(0L, (rect.bottom - navHeight) / 2), navWidth, navHeight, SWP_NOZORDER);
	m_pathRect = { navWidth + padding, padding,
		std::max(navWidth + padding, static_cast<int>(rect.right) - padding),
		std::max(padding, static_cast<int>(rect.bottom) - padding) };
	int width = GetRectWidth(&m_pathRect);
	RECT comboRect;
	GetWindowRect(m_comboBox, &comboRect);
	int comboTop = std::max(0L, (rect.bottom - GetRectHeight(&comboRect)) / 2);
	// ComboBoxEx treats the supplied height as edit + dropdown height.
	SetWindowPos(m_comboBox, nullptr, m_pathRect.left, comboTop, width, Scale(200), SWP_NOZORDER);
	SetWindowPos(m_breadcrumbToolbar->GetHWND(), nullptr, m_pathRect.left + 1, m_pathRect.top + 1,
		std::max(0, width - 2), std::max(0, GetRectHeight(&m_pathRect) - 2), SWP_NOZORDER);
	LayoutBreadcrumbs(std::max(0, width - 2));
	InvalidateRect(GetHWND(), nullptr, true);
}

void AddressBarView::Paint(HDC hdc)
{
	RECT rect;
	GetClientRect(GetHWND(), &rect);
	auto *colors = m_themeManager->GetCurrentColorProvider();
	FillRect(hdc, &rect, colors->GetTabBarBackgroundBrush());
	FillRect(hdc, &m_pathRect, colors->GetBackgroundBrush());
	FrameRect(hdc, &m_pathRect, colors->GetBorderBrush());
}

void AddressBarView::UpdateDirectory(const PidlAbsolute &pidl)
{
	m_breadcrumbs.clear();
	auto ancestor = pidl;
	do
	{
		m_breadcrumbs.push_back(
			{ GetDisplayNameWithFallback(ancestor.Raw(), SHGDN_INFOLDER), ancestor });
		auto path = GetDisplayNameWithFallback(ancestor.Raw(), SHGDN_FORPARSING);
		if (PathIsRoot(path.c_str()))
		{
			break;
		}
	} while (ancestor.RemoveLastItem());
	std::ranges::reverse(m_breadcrumbs);
	RebuildBreadcrumbButtons();
	Layout();
}

void AddressBarView::UpdateDeferredDevice(const std::wstring &name)
{
	m_breadcrumbs = { { name, {} } };
	RebuildBreadcrumbButtons();
	Layout();
}

void AddressBarView::RebuildBreadcrumbButtons()
{
	for (const auto &item : m_breadcrumbToolbar->GetItems())
	{
		m_breadcrumbToolbar->RemoveButton(item.GetButton());
	}
	m_breadcrumbToolbar->AddButton(std::make_unique<AddressButton>(L"\u2026",
									   static_cast<int>(NAVIGATION_BUTTONS.size()), true,
									   [this](const MouseEvent &event)
									   {
										   m_pendingMenuEvent.emplace(event);
										   PostMessage(GetHWND(), WM_SHOW_ANCESTORS, 0, 0);
									   }),
		0);
	for (size_t i = 0; i < m_breadcrumbs.size(); ++i)
	{
		auto text = m_breadcrumbs[i].text;
		if (i + 1 < m_breadcrumbs.size())
		{
			text += L"  \u203a";
		}
		std::optional<int> image =
			i == 0 ? std::optional(static_cast<int>(NAVIGATION_BUTTONS.size())) : std::nullopt;
		auto button = std::make_unique<AddressButton>(text, image, true,
			[this, pidl = m_breadcrumbs[i].pidl](const MouseEvent &event)
			{ QueueNavigation(pidl, event); });
		button->SetMiddleClickedCallback(
			[this, pidl = m_breadcrumbs[i].pidl](const MouseEvent &event)
			{ QueueNavigation(pidl, { event.ptScreen, event.shiftKey, true }); });
		m_breadcrumbToolbar->AddButton(std::move(button), i + 1);
	}
	m_breadcrumbWidths.clear();
	for (const auto &item : m_breadcrumbToolbar->GetItems())
	{
		auto rect = m_breadcrumbToolbar->GetButtonRect(item.GetButton());
		m_breadcrumbWidths.push_back(GetRectWidth(&rect));
	}
}

void AddressBarView::LayoutBreadcrumbs(int width)
{
	if (m_breadcrumbs.empty())
	{
		return;
	}
	// Reserve a blank target for entering a path, even when the path overflows.
	int available = std::max(0, width - Scale(32));
	int total = 0;
	for (size_t i = 1; i < m_breadcrumbWidths.size(); ++i)
	{
		total += m_breadcrumbWidths[i];
	}
	m_firstVisibleBreadcrumb = 0;
	if (total > available && m_breadcrumbs.size() > 1)
	{
		total += m_breadcrumbWidths[0];
		while (total > available && m_firstVisibleBreadcrumb + 1 < m_breadcrumbs.size())
		{
			total -= m_breadcrumbWidths[++m_firstVisibleBreadcrumb];
		}
	}
	auto hwnd = m_breadcrumbToolbar->GetHWND();
	for (size_t i = 0; i < m_breadcrumbWidths.size(); ++i)
	{
		TBBUTTON button;
		SendMessage(hwnd, TB_GETBUTTON, i, reinterpret_cast<LPARAM>(&button));
		bool visible = i == 0 ? m_firstVisibleBreadcrumb > 0 : i > m_firstVisibleBreadcrumb;
		SendMessage(hwnd, TB_HIDEBUTTON, button.idCommand, MAKELPARAM(!visible, 0));
		TBBUTTONINFO info = {};
		info.cbSize = sizeof(info);
		info.dwMask = TBIF_BYINDEX | TBIF_SIZE | TBIF_STYLE;
		info.fsStyle = BTNS_SHOWTEXT | BTNS_NOPREFIX;
		int buttonWidth = m_breadcrumbWidths[i];
		if (i == m_breadcrumbs.size())
		{
			buttonWidth = std::max(0, buttonWidth - std::max(0, total - available));
		}
		info.cx = static_cast<WORD>(std::min(buttonWidth, 65535));
		SendMessage(hwnd, TB_SETBUTTONINFO, i, reinterpret_cast<LPARAM>(&info));
	}
}

void AddressBarView::ShowAncestorsMenu(const MouseEvent &event)
{
	// A shell notification can navigate while TrackPopupMenu runs its modal loop.
	auto ancestors = m_breadcrumbs;
	auto count = m_firstVisibleBreadcrumb;
	if (count == 0)
	{
		return;
	}
	wil::unique_hmenu menu(CreatePopupMenu());
	for (size_t i = 0; i < count; ++i)
	{
		std::wstring label;
		for (auto ch : ancestors[i].text)
		{
			label += ch;
			if (ch == L'&')
			{
				label += ch;
			}
		}
		AppendMenu(menu.get(), MF_STRING, i + 1, label.c_str());
	}
	int selected = TrackPopupMenu(menu.get(), TPM_RETURNCMD | TPM_NONOTIFY, event.ptScreen.x,
		event.ptScreen.y, 0, GetHWND(), nullptr);
	if (selected > 0 && static_cast<size_t>(selected) <= count)
	{
		QueueNavigation(ancestors[selected - 1].pidl, event);
	}
}

void AddressBarView::QueueNavigation(const PidlAbsolute &pidl, const MouseEvent &event)
{
	// Navigation can synchronously replace the buttons. Leave their click
	// handlers before navigating, and retain the target across tab changes.
	m_pendingNavigations.emplace_back(pidl, event);
	PostMessage(GetHWND(), WM_OPEN_BREADCRUMB, 0, 0);
}

void AddressBarView::SetEditing(bool editing)
{
	m_editing = editing;
	ShowWindow(m_comboBox, editing ? SW_SHOW : SW_HIDE);
	ShowWindow(m_breadcrumbToolbar->GetHWND(), editing ? SW_HIDE : SW_SHOW);
}

void AddressBarView::Focus()
{
	if (IsWindowVisible(GetHWND()))
	{
		SetEditing(true);
		SetFocus(GetEditControl());
		SelectAllText();
	}
}

std::wstring AddressBarView::GetText() const
{
	return GetWindowString(m_comboBox);
}

bool AddressBarView::IsTextModified() const
{
	return SendMessage(GetEditControl(), EM_GETMODIFY, 0, 0);
}

void AddressBarView::SelectAllText()
{
	SendMessage(GetEditControl(), EM_SETSEL, 0, -1);
}

void AddressBarView::UpdateTextAndIcon(const std::optional<std::wstring> &optionalText,
	int iconIndex)
{
	COMBOBOXEXITEM item = {};
	item.mask = CBEIF_IMAGE | CBEIF_SELECTEDIMAGE | CBEIF_INDENT;
	item.iItem = -1;
	item.iImage = iconIndex;
	item.iSelectedImage = iconIndex;
	item.iIndent = 1;
	if (optionalText)
	{
		item.mask |= CBEIF_TEXT;
		item.pszText = const_cast<LPWSTR>(optionalText->c_str());
		m_currentText = *optionalText;
	}
	SendMessage(m_comboBox, CBEM_SETITEM, 0, reinterpret_cast<LPARAM>(&item));
}

void AddressBarView::RevertText()
{
	SetWindowText(m_comboBox, m_currentText.c_str());
	SetEditing(false);
}

void AddressBarView::ShowDropDown()
{
	if (!IsWindowVisible(GetHWND()))
	{
		return;
	}
	Focus();
	SendMessage(m_comboBox, CB_SHOWDROPDOWN, true, 0);
}

HWND AddressBarView::GetEditControl() const
{
	return reinterpret_cast<HWND>(SendMessage(m_comboBox, CBEM_GETEDITCONTROL, 0, 0));
}

void AddressBarView::OnFontOrDpiUpdated()
{
	// All child font setters must finish before measuring the new button widths.
	PostMessage(GetHWND(), WM_REFRESH_BREADCRUMBS, 0, 0);
}

AddressBarViewDelegate *AddressBarView::GetDelegateForTesting()
{
	CHECK(IsInTest());
	return m_delegate;
}

void AddressBarView::SetTextForTesting(const std::wstring &text)
{
	CHECK(IsInTest());
	SetWindowText(m_comboBox, text.c_str());
	SendMessage(GetEditControl(), EM_SETMODIFY, TRUE, 0);
}

const std::vector<AddressBarView::Breadcrumb> &AddressBarView::GetBreadcrumbsForTesting() const
{
	CHECK(IsInTest());
	return m_breadcrumbs;
}

bool AddressBarView::IsEditingForTesting() const
{
	CHECK(IsInTest());
	return m_editing;
}

ToolbarView *AddressBarView::GetBreadcrumbToolbarForTesting()
{
	CHECK(IsInTest());
	return m_breadcrumbToolbar.get();
}

ToolbarView *AddressBarView::GetNavigationToolbarForTesting()
{
	CHECK(IsInTest());
	return m_navigationToolbar.get();
}
