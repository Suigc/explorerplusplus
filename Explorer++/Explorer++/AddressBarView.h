// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include "MainFontSetter.h"
#include "MouseEvent.h"
#include "RebarBandView.h"
#include "ViewBase.h"
#include "../Helper/Pidl.h"
#include <deque>
#include <optional>
#include <string>
#include <vector>

class AddressBarViewDelegate;
class ResourceLoader;
class ThemeManager;
class ToolbarView;
struct Config;

class AddressBarView : public ViewBase, public RebarBandView
{
public:
	struct Breadcrumb
	{
		std::wstring text;
		PidlAbsolute pidl;
	};

	AddressBarView(ViewBase *parent, const Config *config, const ResourceLoader *resourceLoader,
		const ThemeManager *themeManager);
	~AddressBarView();

	void SetDelegate(AddressBarViewDelegate *delegate);

	// RebarBandView
	HWND GetHWND() const override;
	int GetHeight() const override;

	void Focus();
	std::wstring GetText() const;
	bool IsTextModified() const;
	void SelectAllText();
	void UpdateTextAndIcon(const std::optional<std::wstring> &optionalText, int iconIndex);
	void RevertText();
	void ShowDropDown();
	void UpdateDirectory(const PidlAbsolute &pidl);
	void UpdateDeferredDevice(const std::wstring &name);
	void UpdateNavigationButtons(bool canGoBack, bool canGoForward, bool canGoUp);

	AddressBarViewDelegate *GetDelegateForTesting();
	void SetTextForTesting(const std::wstring &text);
	const std::vector<Breadcrumb> &GetBreadcrumbsForTesting() const;
	bool IsEditingForTesting() const;
	ToolbarView *GetBreadcrumbToolbarForTesting();
	ToolbarView *GetNavigationToolbarForTesting();

private:
	static HWND CreateAddressBar(ViewBase *parent);
	LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT BreadcrumbSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	LRESULT ComboBoxExSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT EditSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	HWND GetEditControl() const;
	void OnFontOrDpiUpdated();
	void Layout();
	void LayoutBreadcrumbs(int width);
	void SetEditing(bool editing);
	void CreateNavigationButtons();
	void CreateNavigationImages();
	void RebuildBreadcrumbButtons();
	void ShowAncestorsMenu(const MouseEvent &event);
	void QueueNavigation(const PidlAbsolute &pidl, const MouseEvent &event);
	void Paint(HDC hdc);
	int Scale(int value) const;

	AddressBarViewDelegate *m_delegate = nullptr;
	const ResourceLoader *const m_resourceLoader;
	const ThemeManager *const m_themeManager;
	HWND m_comboBox;
	MainFontSetter m_fontSetter;
	std::unique_ptr<ToolbarView> m_navigationToolbar;
	std::unique_ptr<ToolbarView> m_breadcrumbToolbar;
	wil::unique_himagelist m_navigationImages;
	std::vector<Breadcrumb> m_breadcrumbs;
	std::vector<int> m_breadcrumbWidths;
	std::deque<std::pair<PidlAbsolute, MouseEvent>> m_pendingNavigations;
	std::optional<MouseEvent> m_pendingMenuEvent;
	size_t m_firstVisibleBreadcrumb = 0;
	bool m_editing = false;
	RECT m_pathRect = {};
	std::wstring m_currentText;
};
