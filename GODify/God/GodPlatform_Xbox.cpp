//==========================================================================================================================
// GodPlatform_Xbox.cpp - Xbox 360 (XDK) implementation of GodPlatform.h
//==========================================================================================================================
#include "stdafx.h"
#include "God/GodPlatform.h"

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

GodFile GodOpenRead(const char* Path, god_u64* OutSize)
{
	HANDLE h = CreateFile(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return NULL;
	if (OutSize)
	{
		DWORD high = 0;
		DWORD low = GetFileSize(h, &high);
		*OutSize = ((god_u64)high << 32) | low;
	}
	return (GodFile)h;
}

GodFile GodCreateWrite(const char* Path)
{
	HANDLE h = CreateFile(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	return h == INVALID_HANDLE_VALUE ? NULL : (GodFile)h;
}

GodFile GodOpenWriteExisting(const char* Path)
{
	HANDLE h = CreateFile(Path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	return h == INVALID_HANDLE_VALUE ? NULL : (GodFile)h;
}

god_u32 GodRead(GodFile File, void* Buf, god_u32 Size)
{
	DWORD got = 0;
	if (!ReadFile((HANDLE)File, Buf, Size, &got, NULL)) return 0;
	return got;
}

bool GodWrite(GodFile File, const void* Buf, god_u32 Size)
{
	const BYTE* p = (const BYTE*)Buf;
	while (Size)
	{
		DWORD done = 0;
		if (!WriteFile((HANDLE)File, p, Size, &done, NULL) || done == 0) return false;
		p += done; Size -= done;
	}
	return true;
}

void GodClose(GodFile File)
{
	if (File) CloseHandle((HANDLE)File);
}

bool GodListDir(const char* Path, std::vector<GodDirItem>& Out)
{
	std::string pattern = Path;
	if (!pattern.empty() && pattern[pattern.size() - 1] != '\\') pattern += "\\";
	pattern += "*";

	WIN32_FIND_DATA wfd;
	HANDLE h = FindFirstFile(pattern.c_str(), &wfd);
	if (h == INVALID_HANDLE_VALUE)
	{
		// An empty folder also fails FindFirstFile - only report failure if the folder itself is missing.
		DWORD a = GetFileAttributes(Path);
		return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
	}
	do
	{
		GodDirItem it;
		it.Name  = wfd.cFileName;
		it.IsDir = (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		it.Size  = it.IsDir ? 0 : (((god_u64)wfd.nFileSizeHigh << 32) | wfd.nFileSizeLow);
		if (it.Name != "." && it.Name != "..") Out.push_back(it);
	} while (FindNextFile(h, &wfd));
	FindClose(h);
	return true;
}

bool GodPathExists(const char* Path)
{
	return GetFileAttributes(Path) != INVALID_FILE_ATTRIBUTES;
}

bool GodMakeDir(const char* Path)
{
	CreateDirectory(Path, NULL);
	DWORD a = GetFileAttributes(Path);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool GodDeleteFile(const char* Path)
{
	return DeleteFile(Path) != FALSE;
}

bool GodDeleteTree(const char* Path)
{
	DWORD a = GetFileAttributes(Path);
	if (a == INVALID_FILE_ATTRIBUTES) return true;
	if (!(a & FILE_ATTRIBUTE_DIRECTORY)) return DeleteFile(Path) != FALSE;

	std::vector<GodDirItem> items;
	GodListDir(Path, items);
	for (size_t i = 0; i < items.size(); i++)
	{
		std::string child = std::string(Path) + "\\" + items[i].Name;
		if (items[i].IsDir) GodDeleteTree(child.c_str());
		else DeleteFile(child.c_str());
	}
	return RemoveDirectory(Path) != FALSE;
}

bool GodGetFreeSpace(const char* PathOnDevice, god_u64* OutFree)
{
	// "Hdd:\Content\..." -> "Hdd:\"
	std::string root = PathOnDevice;
	size_t colon = root.find(':');
	if (colon == std::string::npos) return false;
	root = root.substr(0, colon + 1) + "\\";

	ULARGE_INTEGER freeToCaller, total, totalFree;
	if (!GetDiskFreeSpaceEx(root.c_str(), &freeToCaller, &total, &totalFree)) return false;
	*OutFree = freeToCaller.QuadPart;
	return true;
}

void GodSha1(const void* Data, god_u32 Size, god_u8 Out[20])
{
	XeCryptSha((PBYTE)Data, Size, NULL, 0, NULL, 0, Out, 20);
}
