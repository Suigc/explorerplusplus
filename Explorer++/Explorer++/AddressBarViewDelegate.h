// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

class PidlAbsolute;
struct MouseEvent;

// Allows the AddressBarView controller to be notified of events that occur within the view.
class AddressBarViewDelegate
{
public:
	virtual ~AddressBarViewDelegate() = default;

	virtual bool OnKeyPressed(UINT key) = 0;
	virtual void OnBeginDrag() = 0;
	virtual void OnFocused() = 0;
	virtual void OnBreadcrumbClicked(const PidlAbsolute &pidl, const MouseEvent &event) = 0;
	virtual void OnNavigationCommand(int command) = 0;
};
