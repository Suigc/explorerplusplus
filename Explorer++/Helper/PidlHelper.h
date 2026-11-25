// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include <ShObjIdl.h>
#include <string>
#include <string_view>

class PidlAbsolute;

enum class ShellItemType
{
	File,
	Folder
};

enum class ShellItemExtraAttributes
{
	None = 0,
	Hidden = FILE_ATTRIBUTE_HIDDEN
};

std::string EncodePidlToBase64(const PidlAbsolute &pidl);
PidlAbsolute DecodePidlFromBase64(const std::string &encodedPidl);

inline constexpr std::wstring_view PORTABLE_DEVICE_INTERFACE_GUID =
	L"{6ac27878-a6fa-4155-ba85-f98f491d4f33}";

// Recognizes saved WPD identifiers without binding to a shell folder.
bool IsPortableDevicePidl(PCIDLIST_ABSOLUTE pidl);

HRESULT CreateSimplePidl(const std::wstring &path, PidlAbsolute &outputPidl,
	IShellFolder *parent = nullptr, ShellItemType itemType = ShellItemType::File,
	ShellItemExtraAttributes extraAttributes = ShellItemExtraAttributes::None);
