//==========================================================================================================================
// GODifyLauncher - the tiny program inside GODify "App or Demo" shortcut packages.
//
// The package contains this file as default.xex plus launch.txt holding a full path such as
//     Hdd:\--- apps ---\Freestyle Dash 3\default.xex
// It starts that program, so the app runs from its real folder and can save settings there.
//
// v2: follows the same steps as the QuickBoot launcher (whose launches don't drop the Xbox LIVE connection):
//   - mount ONLY the device that holds the program, under the temporary name "tmp:", trying the device named in
//     launch.txt first and then the HDD and USB drives
//   - flush the utility drive / reset the file cache before launching
//   - launch "tmp:\<path>" with XLaunchNewImage(path, 0)
//==========================================================================================================================
#include <xtl.h>
#include <xfilecache.h>     // XFileCacheInit / XFileCacheShutdown
#include <stdio.h>
#include <string.h>
#include <xkelib.h>

static const char* g_Devices[] =
{
	"\\Device\\Harddisk0\\Partition1",   // Hdd:
	"\\Device\\Mass0",                   // Usb0:
	"\\Device\\Mass1",                   // Usb1:
	"\\Device\\Mass2",                   // Usb2:
};
static const char* g_DriveNames[] = { "Hdd:", "Usb0:", "Usb1:", "Usb2:" };

static void SetLink(const char* Name, const char* Device)
{
	STRING dev, link;
	char linkName[64];
	sprintf_s(linkName, sizeof(linkName), OBJ_USR_STRING, Name);
	RtlInitAnsiString(&link, linkName);
	ObDeleteSymbolicLink(&link);
	if (Device)
	{
		RtlInitAnsiString(&dev, Device);
		ObCreateSymbolicLink(&link, &dev);
	}
}

static void Fail(LPCWSTR Text)
{
	XNotifyQueueUI(XNOTIFYUI_TYPE_PREFERRED_REVIEW, 0, 2, (PWCHAR)Text, NULL);
	Sleep(4000);
	XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);   // back to the dashboard
}

void __cdecl main()
{
	char line[MAX_PATH + 1];
	memset(line, 0, sizeof(line));

	HANDLE h = CreateFile("game:\\launch.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) { Fail(L"GODify shortcut: launch.txt is missing"); return; }
	DWORD got = 0;
	ReadFile(h, line, MAX_PATH, &got, NULL);
	CloseHandle(h);
	line[got] = 0;
	int n = (int)strlen(line);
	while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n' || line[n - 1] == ' ')) line[--n] = 0;

	// split "Hdd:\folder\app.xex" into the drive name and "\folder\app.xex"
	const char* rel = strchr(line, ':');
	int preferred = -1;
	if (rel)
	{
		for (int i = 0; i < 4; i++)
			if (_strnicmp(line, g_DriveNames[i], strlen(g_DriveNames[i])) == 0) preferred = i;
		rel++;
	}
	else rel = line;
	if (!*rel) { Fail(L"GODify shortcut: launch.txt is empty"); return; }

	// tmp:\folder\app.xex
	char path[MAX_PATH + 8];
	sprintf_s(path, sizeof(path), "tmp:%s%s", rel[0] == '\\' ? "" : "\\", rel);

	// try the named device first, then the rest (like QuickBoot: HDD, USB0, USB1, USB2)
	int order[5]; int count = 0;
	if (preferred >= 0) order[count++] = preferred;
	for (int i = 0; i < 4; i++) if (i != preferred) order[count++] = i;

	for (int k = 0; k < count; k++)
	{
		SetLink("tmp:", g_Devices[order[k]]);
		if (GetFileAttributes(path) != (DWORD)-1)
		{
			// same clean-up QuickBoot does right before it launches
			XFlushUtilityDrive();
			XFileCacheInit(0xC0000000, 0, 5, 0, 1);
			XFileCacheShutdown();
			XFlushUtilityDrive();

			XLaunchNewImage(path, 0);   // does not return
		}
		SetLink("tmp:", NULL);
	}

	Fail(L"GODify shortcut: the app was moved or deleted - convert it again");
}
