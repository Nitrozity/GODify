#include"stdafx.h"
#include <time.h>

HMODULE hAPP = NULL;
BOOL IsDevkit = FALSE;

// =========================  ENABLE DEBUG PRINTS  ====================================
#ifdef _DEBUG
VOID Byrom_Print(const CHAR* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stdout, fmt, args);
	va_end(args);
}

#endif

VOID XNotify(LPCWSTR text)
{
	XNotifyQueueUI(XNOTIFYUI_TYPE_PREFERRED_REVIEW, 0, 2, (PWCHAR)text, NULL);
}

PWCHAR LinkWChar(PWCHAR Text, ...) {
	static WCHAR Buffer[0x400];   // static: the old version returned a pointer to a stack buffer

	va_list pArgList;
	va_start(pArgList, Text);
	vswprintf_s(Buffer, 0x400, Text, pArgList);
	va_end(pArgList);

	return Buffer;
}

BOOL FileExists(PCHAR path)
{
	OBJECT_ATTRIBUTES obAtrib;
	FILE_NETWORK_OPEN_INFORMATION netInfo;
	STRING filePath;
	RtlInitAnsiString(&filePath, path); //  = 0x10
	InitializeObjectAttributes(&obAtrib, &filePath, 0x40, NULL);
	if (path[0] != '\\')
		obAtrib.RootDirectory = (HANDLE)0xFFFFFFFD;
	if (NT_SUCCESS(NtQueryFullAttributesFile(&obAtrib, &netInfo)))
	{
		// filter out directories from the result
		if ((netInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			return TRUE;
	}
	return FALSE;
}

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif
BOOL DirectoryExists(LPCTSTR szPath)
{
	DWORD dwAttrib = GetFileAttributes(szPath);

	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

BOOL CWriteFile(const CHAR* FilePath, const VOID* Data, DWORD Size)
{
	HANDLE fHandle = CreateFile(FilePath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fHandle == INVALID_HANDLE_VALUE)
	{
		DbgPrint("[CWriteFile] INVALID_HANDLE_VALUE. Error: %X", GetLastError());
		return FALSE;
	}

	DWORD writeSize = Size;
	if (WriteFile(fHandle, Data, writeSize, &writeSize, NULL) != TRUE)
	{
		DbgPrint("[CWriteFile] WriteFile failed. Error: %X", GetLastError());
		CloseHandle(fHandle);
		return FALSE;
	}
	CloseHandle(fHandle);

	return TRUE;
}

HRESULT CreateMount(const char* szDrive, const char* szDevice, BOOL System) {
	STRING DeviceName, LinkName;
	CHAR szDestinationDrive[MAX_PATH];
	sprintf_s(szDestinationDrive, MAX_PATH, System ? OBJ_SYS_STRING : OBJ_USR_STRING, szDrive);
	RtlInitAnsiString(&DeviceName, szDevice);
	RtlInitAnsiString(&LinkName, szDestinationDrive);
	ObDeleteSymbolicLink(&LinkName);
	return (HRESULT)ObCreateSymbolicLink(&LinkName, &DeviceName);
}
