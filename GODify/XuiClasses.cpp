#include "stdafx.h"

#pragma region APP
HRESULT CMyApp::RegisterXuiClasses()
{
	CMainScene::Register();
	return S_OK;
}

HRESULT CMyApp::UnregisterXuiClasses()
{
	CMainScene::Unregister();
	return S_OK;
}
#pragma endregion

extern BOOL bShouldExit;

static const DWORD YellowTextCol = 0xFFEBEB0F;
static const DWORD RedTextCol    = 0xFFFF0000;
static const DWORD GreenTextCol  = 0xFF00FF00;
static const DWORD WhiteTextCol  = 0xFFEBEBEB;

#define TIMER_POLL      1001
#define TIMER_BACK      1002

static GodProgress g_Progress;

// The dashboard keeps its list of installed content in this file and only Settings > System > Storage rebuilds
// it (found by comparing every file's date before/after a Storage visit). After converting we move it aside so the system has to
// rescan the drive; the old copy is kept as ContentCache.pkg.old.
#define DASH_CONTENT_CACHE      "Hdd:\\Content\\0000000000000000\\FFFE07DF\\00040000\\ContentCache.pkg"
#define DASH_CONTENT_CACHE_OLD  DASH_CONTENT_CACHE ".old"

// 1 = moved aside, 0 = not there (nothing to do), -1 = couldn't move it (probably in use)
static int ResetDashboardContentCache()
{
	if (GetFileAttributes(DASH_CONTENT_CACHE) == (DWORD)-1) return 0;
	DeleteFile(DASH_CONTENT_CACHE_OLD);
	if (MoveFile(DASH_CONTENT_CACHE, DASH_CONTENT_CACHE_OLD)) return 1;
	return DeleteFile(DASH_CONTENT_CACHE) ? 1 : -1;
}



// Devices the browser offers (mounted in main()).
static const char* g_Drives[] = { "Hdd:", "Usb0:", "Usb1:", "Usb2:" };

//--------------------------------------------------------------------------------------------------------------------------
// small helpers
//--------------------------------------------------------------------------------------------------------------------------
static wstring Widen(const string& s)
{
	wstring w;
	for (size_t i = 0; i < s.size(); i++) w += (WCHAR)(unsigned char)s[i];
	return w;
}

static wstring FromGod(const GodWString& g)
{
	wstring w;
	for (size_t i = 0; i < g.size(); i++) w += (WCHAR)g[i];
	return w;
}

static GodWString ToGod(const WCHAR* w)
{
	GodWString g;
	while (w && *w) g.push_back((god_u16)*w++);
	return g;
}

static wstring FormatSize(god_u64 Bytes)
{
	WCHAR buf[32];
	double v = (double)Bytes;
	if (v >= 1024.0 * 1024 * 1024) swprintf_s(buf, 32, L"%.2f GB", v / (1024.0 * 1024 * 1024));
	else if (v >= 1024.0 * 1024)   swprintf_s(buf, 32, L"%.1f MB", v / (1024.0 * 1024));
	else                           swprintf_s(buf, 32, L"%u KB", (unsigned)(Bytes / 1024));
	return buf;
}

static string JoinPath(const string& a, const string& b)
{
	if (a.empty()) return b;
	return a + "\\" + b;
}

static string LastPart(const string& p)
{
	size_t pos = p.find_last_of('\\');
	return pos == string::npos ? p : p.substr(pos + 1);
}

static BOOL PathIsDir(const string& p)
{
	DWORD a = GetFileAttributes(p.c_str());
	return a != (DWORD)-1 && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL PathIsFile(const string& p)
{
	DWORD a = GetFileAttributes(p.c_str());
	return a != (DWORD)-1 && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// A device counts as present if any of these work (an empty USB stick fails FindFirstFile, and root folder
// attributes are not reported the same way on every device).
static BOOL DriveExists(const char* Drive)
{
	string root = string(Drive) + "\\";
	god_u64 freeBytes = 0;
	if (GodGetFreeSpace(root.c_str(), &freeBytes)) return TRUE;
	if (PathIsDir(root)) return TRUE;
	WIN32_FIND_DATA wfd;
	HANDLE h = FindFirstFile((root + "*").c_str(), &wfd);
	if (h != INVALID_HANDLE_VALUE) { FindClose(h); return TRUE; }
	return FALSE;
}

static god_u64 NowFileTime()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	return ((god_u64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

// Sort like XeXMenu: names starting with symbols ("--- Games ---", "[Apps]", "_new") first, then digits,
// then letters; case-insensitive.
static int CharClass(unsigned char c)
{
	if (c >= '0' && c <= '9') return 1;
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return 2;
	return 0;
}

static bool NameLess(const CMainScene::BrowseEntry& a, const CMainScene::BrowseEntry& b)
{
	const string& x = a.Name; const string& y = b.Name;
	size_t n = x.size() < y.size() ? x.size() : y.size();
	for (size_t i = 0; i < n; i++)
	{
		unsigned char cx = (unsigned char)x[i], cy = (unsigned char)y[i];
		int kx = CharClass(cx), ky = CharClass(cy);
		if (kx != ky) return kx < ky;
		if (cx >= 'A' && cx <= 'Z') cx = (unsigned char)(cx + 32);
		if (cy >= 'A' && cy <= 'Z') cy = (unsigned char)(cy + 32);
		if (cx != cy) return cx < cy;
	}
	return x.size() < y.size();
}

//==============================================================================================================================================================================
//
//			CMainScene
//
//==============================================================================================================================================================================
HRESULT CMainScene::SetTextColour(HXUIOBJ TextObject, DWORD dwTextColour)
{
	HRESULT ret = S_FALSE;
	DWORD dwId; XUIElementPropVal pVal;
	ret = XuiObjectGetPropertyId(TextObject, L"TextColor", &dwId);
	if (ret == S_OK)
	{
		pVal.SetColorVal(dwTextColour);
		ret = XuiObjectSetProperty(TextObject, dwId, 0, &pVal);
	}
	return ret;
}

HRESULT CMainScene::OnInit(XUIMessageInit* pInitData, BOOL& bHandled)
{
	State = State_Main;
	hWorker = NULL;
	WorkerDone = FALSE;
	CurrentJob = 0;
	JobStartTick = 0;
	WorkerIsConvert = FALSE;
	PendingSel = -1;
	CatIdx = 0;
	memset(&g_Progress, 0, sizeof(g_Progress));
	memset(&KbOverlapped, 0, sizeof(KbOverlapped));
	KbResult[0] = 0;
	KbPlan = 0;
	KbReturn = 0;

	GetChildById(L"textPrompt", &textPrompt);
	GetChildById(L"textBuildVer", &textBuildVer);
	GetChildById(L"textPath", &textPath);
	GetChildById(L"btnA", &btnA);
	GetChildById(L"btnB", &btnB);
	GetChildById(L"btnX", &btnX);
	GetChildById(L"btnY", &btnY);
	GetChildById(L"btnLS", &btnLS);
	GetChildById(L"btnRS", &btnRS);
	GetChildById(L"FileList", &FileList);
	GetChildById(L"btnExitApp", &btnExitApp);
	GetChildById(L"ImgBackground", &ImgBackground);

	textBuildVer.SetText(BUILD_VER_W);
	CenterPromptText();
	btnLS.SetEnable(false); btnLS.SetShow(false);
	btnRS.SetEnable(false); btnRS.SetShow(false);

	ShowMain();
	return S_OK;
}

//--------------------------------------------------------------------------------------------------------------------------
// screen helpers
//--------------------------------------------------------------------------------------------------------------------------
// Center the prompt text horizontally and vertically in its box (XUI TextStyle alignment bits).
VOID CMainScene::CenterPromptText()
{
#if defined(XUI_FONT_STYLE_CENTER_ALIGN) && defined(XUI_FONT_STYLE_VERTICAL_CENTER)
	DWORD id; XUIElementPropVal val;
	if (XuiObjectGetPropertyId(textPrompt, L"TextStyle", &id) == S_OK && XuiObjectGetProperty(textPrompt, id, 0, &val) == S_OK)
	{
		UINT style = val.uVal;
	#ifdef XUI_FONT_STYLE_RIGHT_ALIGN
		style &= ~(UINT)XUI_FONT_STYLE_RIGHT_ALIGN;
	#endif
		style |= XUI_FONT_STYLE_CENTER_ALIGN | XUI_FONT_STYLE_VERTICAL_CENTER;
		val.uVal = style;   // same type as read back, so just change the value
		XuiObjectSetProperty(textPrompt, id, 0, &val);
	}
#endif
}

static VOID SetBtn(CXuiControl& b, LPCWSTR text)
{
	if (text) { b.SetText(text); b.SetEnable(true); b.SetShow(true); }
	else      { b.SetEnable(false); b.SetShow(false); }
}

VOID CMainScene::ShowButtons(LPCWSTR a, LPCWSTR b, LPCWSTR x, LPCWSTR y)
{
	SetBtn(btnA, a);
	SetBtn(btnB, b);
	SetBtn(btnX, x);
	SetBtn(btnY, y);
	BOOL exitBtn = (State != State_Working);         // Exit is always available except while converting
	btnExitApp.SetEnable(exitBtn ? true : false);
	btnExitApp.SetShow(exitBtn ? true : false);
}

VOID CMainScene::ShowPrompt(DWORD Colour, const wstring& Text)
{
	SetTextColour(textPrompt, Colour);
	textPrompt.SetText(Text.c_str());
	textPrompt.SetShow(true);
}

VOID CMainScene::HideList()
{
	FileList.SetEnable(false);
	FileList.SetShow(false);
}

VOID CMainScene::ShowMain()
{
	State = State_Main;
	HideList();
	textPath.SetShow(false);
	ShowButtons(L"Browse Games", NULL, NULL, NULL);
	btnA.SetFocus();

	wstring t =
		L"Welcome to GODify\n\n"
		L"Convert games and apps on your HDD/USB into packages\n"
		L"that show up on the dashboard (.xex and original Xbox default.xbe).\n\n"
		L"A: Open folders, then press A on the executable (>>) to convert it.\n"
		L"X: Add to the queue, Y: Convert the queue";
	ShowPrompt(YellowTextCol, t);
}

VOID CMainScene::ShowResult(BOOL Success, const wstring& ResultText)
{
	KillTimer(TIMER_POLL);
	State = State_Result;
	HideList();
	textPath.SetShow(false);
	ShowButtons(L"OK", NULL, NULL, NULL);
	btnA.SetFocus();
	ShowPrompt(Success ? GreenTextCol : RedTextCol, ResultText);
}

//--------------------------------------------------------------------------------------------------------------------------
// browser
//--------------------------------------------------------------------------------------------------------------------------
BOOL CMainScene::IsQueued(const string& ExePath)
{
	if (ExePath.empty()) return FALSE;
	for (size_t i = 0; i < Queue.size(); i++)
		if (_stricmp(Queue[i].c_str(), ExePath.c_str()) == 0) return TRUE;
	return FALSE;
}

wstring CMainScene::EntryText(const BrowseEntry& e)
{
	BOOL q = IsQueued(e.Exe);
	switch (e.Type)
	{
	case Entry_Drive:    return L"[" + Widen(e.Name) + L"]";
	case Entry_Dir:      return L"> " + Widen(e.Name);
	case Entry_Game360:
	case Entry_GameXbox:
	case Entry_AppDir:   return (q ? L"[QUEUED] " : L"> ") + Widen(e.Name);
	case Entry_Exe:      return wstring(q ? L"[QUEUED] " : L"") + L">> " + Widen(e.Name) + L"  - press A to convert";
	}
	return L"";
}

static BOOL HasExt(const string& Name, const char* Ext)
{
	size_t n = strlen(Ext);
	return Name.size() > n && _stricmp(Name.c_str() + Name.size() - n, Ext) == 0;
}

// Classifies a sub folder by the executables it contains.
static CMainScene::EntryType ClassifyFolder(const string& Folder, string& Exe)
{
	Exe = "";
	if (PathIsFile(JoinPath(Folder, "default.xex"))) { Exe = JoinPath(Folder, "default.xex"); return CMainScene::Entry_Game360; }
	if (PathIsFile(JoinPath(Folder, "default.xbe"))) { Exe = JoinPath(Folder, "default.xbe"); return CMainScene::Entry_GameXbox; }

	WIN32_FIND_DATA wfd;
	HANDLE h = FindFirstFile(JoinPath(Folder, "*.xex").c_str(), &wfd);
	if (h == INVALID_HANDLE_VALUE) return CMainScene::Entry_Dir;
	int count = 0;
	string first;
	do
	{
		if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		if (!count) first = wfd.cFileName;
		count++;
	} while (count < 2 && FindNextFile(h, &wfd));
	FindClose(h);
	if (!count) return CMainScene::Entry_Dir;
	if (count == 1) Exe = JoinPath(Folder, first);    // several .xex files -> open the folder and pick one
	return CMainScene::Entry_AppDir;
}

BOOL CMainScene::FillEntries()
{
	Entries.clear();

	if (CurPath.empty())
	{
		for (int i = 0; i < (int)(sizeof(g_Drives) / sizeof(g_Drives[0])); i++)
		{
			if (!DriveExists(g_Drives[i])) continue;
			BrowseEntry e; e.Type = Entry_Drive; e.Name = g_Drives[i]; e.Path = g_Drives[i];
			Entries.push_back(e);
		}
		return !Entries.empty();
	}

	vector<GodDirItem> items;
	GodListDir((CurPath + "\\").c_str(), items);

	vector<BrowseEntry> dirs, exes;
	for (size_t i = 0; i < items.size(); i++)
	{
		BrowseEntry e;
		e.Name = items[i].Name;
		e.Path = JoinPath(CurPath, items[i].Name);
		if (items[i].IsDir)
		{
			e.Type = ClassifyFolder(e.Path, e.Exe);
			dirs.push_back(e);
		}
		else if (HasExt(e.Name, ".xex") || _stricmp(e.Name.c_str(), "default.xbe") == 0)
		{
			e.Type = Entry_Exe;
			e.Exe = e.Path;
			exes.push_back(e);
		}
	}
	sort(dirs.begin(), dirs.end(), NameLess);
	sort(exes.begin(), exes.end(), NameLess);

	// folders first, then files (like XeXMenu)
	for (size_t i = 0; i < dirs.size(); i++) Entries.push_back(dirs[i]);
	for (size_t i = 0; i < exes.size(); i++) Entries.push_back(exes[i]);
	return !Entries.empty();
}

VOID CMainScene::ShowBrowser(const string& SelectName)
{
	State = State_Browse;
	textPrompt.SetShow(false);

	BOOL any = FillEntries();

	WCHAR hdr[512];
	swprintf_s(hdr, 512, L"%s    (queue: %d)", CurPath.empty() ? L"Select a drive" : Widen(CurPath).c_str(), (int)Queue.size());
	textPath.SetText(hdr);
	textPath.SetShow(true);

	if (!any)
	{
		HideList();
		ShowButtons(NULL, L"Back", NULL, Queue.empty() ? NULL : L"Convert Queue");
		btnB.SetFocus();
		ShowPrompt(RedTextCol, CurPath.empty() ? L"No drives found." : L"This folder is empty.");
		return;
	}

	// Never delete all rows: deleting the highlighted row makes the console move the focus to another
	// control (e.g. the Exit button) and the list loses its highlight. Park the selection on row 0,
	// resize the list by the difference only, then rewrite every row's text.
	int oldCount = FileList.GetItemCount();
	int newCount = (int)Entries.size();
	if (oldCount > 0) FileList.SetCurSel(0);
	if (newCount > oldCount) FileList.InsertItems(oldCount, newCount - oldCount);
	else if (newCount < oldCount) FileList.DeleteItems(newCount, oldCount - newCount);

	int sel = -1, exeSel = -1;
	for (int i = 0; i < (int)Entries.size(); i++)
	{
		FileList.SetText(i, EntryText(Entries[i]).c_str());
		if (!SelectName.empty() && _stricmp(Entries[i].Name.c_str(), SelectName.c_str()) == 0) sel = i;
		if (exeSel < 0 && Entries[i].Type == Entry_Exe &&
			(_stricmp(Entries[i].Name.c_str(), "default.xex") == 0 || _stricmp(Entries[i].Name.c_str(), "default.xbe") == 0)) exeSel = i;
	}
	BOOL atDriveRoot = CurPath.find('\\') == string::npos;   // "Hdd:" vs "Hdd:\Games"
	if (sel < 0) sel = (exeSel >= 0 && !atDriveRoot) ? exeSel : 0;   // inside a game folder, start on its default.xex

	// Exactly the v1.10 sequence (select the row, then focus the list) - this highlights the row correctly.
	FileList.SetEnable(true);
	FileList.SetShow(true);
	FileList.SetCurSel(sel);
	FileList.SetTopItem(sel > 3 ? sel - 3 : 0);    // SetCurSel doesn't scroll: keep the highlighted row on screen
	FileList.SetFocus();

	ShowButtons(L"Open / Select", L"Back", CurPath.empty() ? NULL : L"Queue +/-", Queue.empty() ? NULL : L"Convert Queue");
}

VOID CMainScene::BrowseInto(const BrowseEntry& e)
{
	switch (e.Type)
	{
	case Entry_Drive:
	case Entry_Dir:
	case Entry_Game360:   // folders always open; converting is done from the executable entry inside
	case Entry_GameXbox:
	case Entry_AppDir:
		CurPath = e.Path;
		ShowBrowser("");
		break;
	case Entry_Exe:
		{
			vector<string> one; one.push_back(e.Exe);
			StartPrepare(one);
		}
		break;
	}
}

VOID CMainScene::BrowseUp()
{
	if (CurPath.empty()) { ShowMain(); return; }
	size_t pos = CurPath.find_last_of('\\');
	string from;
	if (pos == string::npos) { from = CurPath; CurPath = ""; }     // "Hdd:" -> drive list
	else { from = CurPath.substr(pos + 1); CurPath = CurPath.substr(0, pos); }
	ShowBrowser(from);
}

VOID CMainScene::ToggleQueue(int Index)
{
	if (Index < 0 || Index >= (int)Entries.size()) return;
	const BrowseEntry& e = Entries[Index];
	if (e.Exe.empty())
	{
		if (e.Type == Entry_AppDir) XNotify(L"This folder has several .xex files - open it and press X on the one to use");
		else XNotify(L"No executable here - open the folder that has the .xex / default.xbe");
		return;
	}

	BOOL removed = FALSE;
	for (size_t i = 0; i < Queue.size(); i++)
		if (_stricmp(Queue[i].c_str(), e.Exe.c_str()) == 0) { Queue.erase(Queue.begin() + i); removed = TRUE; break; }
	if (!removed) Queue.push_back(e.Exe);

	FileList.SetText(Index, EntryText(e).c_str());
	WCHAR hdr[512];
	swprintf_s(hdr, 512, L"%s    (queue: %d)", Widen(CurPath).c_str(), (int)Queue.size());
	textPath.SetText(hdr);
	SetBtn(btnY, Queue.empty() ? NULL : L"Convert Queue");
}

//--------------------------------------------------------------------------------------------------------------------------
// worker thread
//--------------------------------------------------------------------------------------------------------------------------
VOID CMainScene::FreePlans()
{
	for (size_t i = 0; i < Plans.size(); i++) delete Plans[i];
	Plans.clear();
}

DWORD WINAPI CMainScene::WorkerProc(LPVOID Param)
{
	CMainScene* self = (CMainScene*)Param;
	if (self->WorkerIsConvert) self->WorkerConvert();
	else self->WorkerPrepare();
	self->WorkerDone = TRUE;
	return 0;
}

VOID CMainScene::WorkerPrepare()
{
	god_u64 ft = NowFileTime();
	for (size_t i = 0; i < PendingExes.size(); i++)
	{
		CurrentJob = (int)i;
		GodPlan* p = new GodPlan;
		string err;
		if (GodPreparePlan(PendingExes[i].c_str(), CONTENT_ROOT, TRUE, ft, *p, err))
			Plans.push_back(p);
		else
		{
			string folder = PendingExes[i].substr(0, PendingExes[i].find_last_of('\\'));
			JobResult r; r.Title = Widen(LastPart(folder)); r.Exe = PendingExes[i]; r.Result = GOD_FAILED; r.Error = err;
			Results.push_back(r);
			delete p;
		}
	}
}

VOID CMainScene::WorkerConvert()
{
	for (size_t i = 0; i < Plans.size(); i++)
	{
		bool cancel = g_Progress.Cancel;
		memset((void*)&g_Progress, 0, sizeof(g_Progress));
		g_Progress.Cancel = cancel;
		if (cancel) break;

		CurrentJob = (int)i;
		JobStartTick = GetTickCount();

		string err;
		GodResult res = GodConvert(*Plans[i], &g_Progress, err);

		JobResult r; r.Title = FromGod(Plans[i]->Title); r.Exe = Plans[i]->ExePath; r.Result = res; r.Error = err;
		Results.push_back(r);
		if (res == GOD_CANCELLED) break;
	}

}

VOID CMainScene::StartPrepare(const vector<string>& Exes)
{
	FreePlans();
	Results.clear();
	PendingExes = Exes;
	WorkerIsConvert = FALSE;
	WorkerDone = FALSE;
	CurrentJob = 0;

	State = State_Preparing;
	HideList();
	ShowButtons(NULL, NULL, NULL, NULL);
	ShowPrompt(YellowTextCol, L"Reading the executable and scanning files...");

	hWorker = CreateThread(NULL, 0x40000, WorkerProc, this, CREATE_SUSPENDED, NULL);
	if (!hWorker) { ShowResult(FALSE, L"Could not start worker thread!"); return; }
	XSetThreadProcessor(hWorker, 4);
	ResumeThread(hWorker);
	SetTimer(TIMER_POLL, 250);
}

VOID CMainScene::StartConvert()
{
	memset((void*)&g_Progress, 0, sizeof(g_Progress));
	WorkerIsConvert = TRUE;
	WorkerDone = FALSE;
	CurrentJob = 0;
	JobStartTick = GetTickCount();

	State = State_Working;
	HideList();
	textPath.SetShow(false);
	ShowButtons(NULL, L"Cancel", NULL, NULL);
	btnB.SetFocus();
	ShowPrompt(YellowTextCol, L"Starting...");

	XEnableScreenSaver(FALSE);
	hWorker = CreateThread(NULL, 0x40000, WorkerProc, this, CREATE_SUSPENDED, NULL);
	if (!hWorker) { XEnableScreenSaver(TRUE); ShowResult(FALSE, L"Could not start worker thread!"); return; }
	XSetThreadProcessor(hWorker, 4);
	ResumeThread(hWorker);
	SetTimer(TIMER_POLL, 250);
}

VOID CMainScene::UpdateProgressText()
{
	int job = CurrentJob;
	if (job < 0 || job >= (int)Plans.size()) return;
	GodPlan* p = Plans[job];

	god_u64 done = g_Progress.BytesDone, total = g_Progress.BytesTotal;
	int pct = total ? (int)((done * 100) / total) : 0;
	DWORD elapsed = GetTickCount() - JobStartTick;
	double mbps = elapsed > 500 ? ((double)done / (1024.0 * 1024.0)) / (elapsed / 1000.0) : 0.0;
	DWORD etaSec = (mbps > 0.1 && total > done) ? (DWORD)(((double)(total - done) / (1024.0 * 1024.0)) / mbps) : 0;

	WCHAR line[256];
	wstring t;
	swprintf_s(line, 256, L"Converting %d of %d\n", job + 1, (int)Plans.size()); t += line;
	t += FromGod(p->Title) + L"\n\n";

	if (g_Progress.Cancel)
		t += L"Cancelling - cleaning up...";
	else if (g_Progress.Phase == GOD_PHASE_FINALIZING)
		t += L"Finalizing hash tables and header...";
	else
	{
		swprintf_s(line, 256, L"Part %d of %d    %d%%\n", g_Progress.CurrentPart, g_Progress.PartCount, pct); t += line;
		t += FormatSize(done) + L" / " + FormatSize(total) + L"\n";
		if (mbps > 0.1)
		{
			swprintf_s(line, 256, L"%.1f MB/s    about %um %02us left", mbps, etaSec / 60, etaSec % 60);
			t += line;
		}
	}
	t += L"\n\nB: Cancel";
	ShowPrompt(YellowTextCol, t);
}

VOID CMainScene::OnWorkerFinished()
{
	KillTimer(TIMER_POLL);
	if (hWorker) { WaitForSingleObject(hWorker, INFINITE); CloseHandle(hWorker); hWorker = NULL; }

	if (!WorkerIsConvert)
	{
		if (Plans.empty())
		{
			wstring msg = L"Can't convert:\n\n";
			for (size_t i = 0; i < Results.size(); i++) msg += Results[i].Title + L": " + Widen(Results[i].Error) + L"\n";
			ShowResult(FALSE, msg);
			return;
		}
		if (Plans.size() == 1) { CatIdx = 0; ShowCategory(); }
		else ShowQueueReview(0);
		return;
	}

	XEnableScreenSaver(TRUE);

	int ok = 0, failed = 0, cancelled = 0;
	wstring details;
	for (size_t i = 0; i < Results.size(); i++)
	{
		if (Results[i].Result == GOD_OK) ok++;
		else if (Results[i].Result == GOD_CANCELLED) { cancelled++; details += Results[i].Title + L": cancelled\n"; }
		else { failed++; details += Results[i].Title + L": " + Widen(Results[i].Error) + L"\n"; }
	}

	// converted games leave the queue
	for (size_t r = 0; r < Results.size(); r++)
		if (Results[r].Result == GOD_OK)
			for (size_t q = 0; q < Queue.size(); q++)
				if (_stricmp(Queue[q].c_str(), Results[r].Exe.c_str()) == 0) { Queue.erase(Queue.begin() + q); break; }

	WCHAR head[256];
	swprintf_s(head, 256, L"%d converted, %d failed, %d cancelled\n\n", ok, failed, cancelled);
	wstring msg = head;
	if (ok)
	{
		int cache = ResetDashboardContentCache();
		msg += L"Converted items should now show up on the Xbox dashboard.\n\n";
		if (cache < 0) msg += L"(If they don't: Settings > System > Storage once.)\n\n";
	}
	msg += details;

	FreePlans();
	ShowResult(failed == 0 && cancelled == 0, msg);
}

//--------------------------------------------------------------------------------------------------------------------------
// confirm + rename
//--------------------------------------------------------------------------------------------------------------------------
// Space needed for the current choices vs. free space on the HDD.
VOID CMainScene::SpaceInfo(god_u64& Need, god_u64& Free, BOOL& HaveFree)
{
	Need = 0;
	for (size_t i = 0; i < Plans.size(); i++) Need += Plans[i]->OutputBytes;
	Free = 0;
	HaveFree = GodGetFreeSpace(CONTENT_ROOT, &Free) ? TRUE : FALSE;
}

// Applies Category to the single plan and starts converting.
VOID CMainScene::ChooseCategory(int Category)
{
	if (Plans.size() != 1) return;
	GodPlan* p = Plans[0];
	string err;
	if (Category >= 0 && !GodSetCategory(*p, Category, err)) { XNotify(Widen(err).c_str()); return; }

	god_u64 need, freeBytes; BOOL haveFree;
	SpaceInfo(need, freeBytes, haveFree);
	if (!DriveExists("Hdd:")) { XNotify(L"No internal HDD found - packages must go on Hdd:"); return; }
	if (haveFree && freeBytes < need) { XNotify(L"Not enough free space on the HDD for this"); return; }
	StartConvert();
}

// Single item: what to make of it, plus the space it needs.
VOID CMainScene::ShowCategory()
{
	State = State_Category;
	HideList();
	textPath.SetShow(false);
	GodPlan* p = Plans[0];

	BOOL fixed = (p->Kind == GOD_KIND_XBOX1 || p->ShortcutOnly);   // nothing to choose

	// sizes / earlier conversions for both choices
	string err;
	int keep = p->Category;
	god_u64 gameSize = 0, appSize = 0;
	BOOL asGame = FALSE, asApp = FALSE, canShortcut = FALSE;
	if (!p->ShortcutOnly) { GodSetCategory(*p, GOD_CAT_GAME, err); gameSize = p->OutputBytes; asGame = p->AlreadyExists; }
	if (p->Kind != GOD_KIND_XBOX1 && GodSetCategory(*p, GOD_CAT_APP, err)) { canShortcut = TRUE; appSize = p->OutputBytes; asApp = p->AlreadyExists; }
	GodSetCategory(*p, keep, err);

	god_u64 need, freeBytes; BOOL haveFree;
	SpaceInfo(need, freeBytes, haveFree);

	wstring t = FromGod(p->Title) + L"\n\n";
	if (p->Kind == GOD_KIND_XBOX1)
		t += L"A: Convert  -  original Xbox game\n";
	else if (p->ShortcutOnly)
		t += L"A: Convert  -  shortcut\n";
	else
	{
		t += L"A: Game  -  full conversion\n";
		if (canShortcut) t += L"X: App or Demo  -  shortcut\n";
	}
	t += L"Y: Rename  -  renames this title\n\n\n";

	if (!p->ShortcutOnly) t += L"Space needed: " + FormatSize(gameSize) + (canShortcut && !fixed ? L"   (shortcut: " + FormatSize(appSize) + L")" : L"") + L"\n";
	else                  t += L"Space needed: " + FormatSize(appSize) + L"\n";
	t += haveFree ? L"Free on Hdd: " + FormatSize(freeBytes) : wstring(L"Free on Hdd: unknown");
	if (!DriveExists("Hdd:")) t += L"\n\n[!] No internal HDD found";
	else if (asGame || asApp) t += L"\n\nAlready converted - converting again replaces it";

	ShowPrompt(YellowTextCol, t);
	ShowButtons(fixed ? L"Convert" : L"Game", L"Back", (!fixed && canShortcut) ? L"App or Demo" : NULL, L"Rename");
	btnA.SetFocus();
}

// Queue: one row per item, A switches it between Game and App or Demo.
wstring CMainScene::QueueItemText(int i)
{
	GodPlan* p = Plans[i];
	wstring kind;
	if (p->Kind == GOD_KIND_XBOX1)           kind = L"Original Xbox";
	else if (p->ShortcutOnly)                kind = L"App or Demo";
	else if (p->Category == GOD_CAT_APP)     kind = L"App or Demo";
	else                                     kind = L"Game";
	return kind + L"   -   " + FromGod(p->Title);
}

VOID CMainScene::ShowQueueReview(int Select)
{
	State = State_QueueReview;
	textPrompt.SetShow(false);

	int oldCount = FileList.GetItemCount(), newCount = (int)Plans.size();
	if (oldCount > 0) FileList.SetCurSel(0);
	if (newCount > oldCount) FileList.InsertItems(oldCount, newCount - oldCount);
	else if (newCount < oldCount) FileList.DeleteItems(newCount, oldCount - newCount);
	for (int i = 0; i < newCount; i++) FileList.SetText(i, QueueItemText(i).c_str());

	god_u64 need, freeBytes; BOOL haveFree;
	SpaceInfo(need, freeBytes, haveFree);
	WCHAR hdr[256];
	swprintf_s(hdr, 256, L"Queue: %d items   Needs %s   Free %s%s", newCount, FormatSize(need).c_str(),
		haveFree ? FormatSize(freeBytes).c_str() : L"?", Results.empty() ? L"" : L"   (some skipped)");
	textPath.SetText(hdr);
	textPath.SetShow(true);

	if (Select < 0 || Select >= newCount) Select = 0;
	FileList.SetEnable(true);
	FileList.SetShow(true);
	FileList.SetCurSel(Select);
	FileList.SetTopItem(Select > 3 ? Select - 3 : 0);
	FileList.SetFocus();
	ShowButtons(L"Game / App", L"Back", L"Convert All", L"Rename");
}

VOID CMainScene::StartRename(int PlanIdx, int Return)
{
	if (PlanIdx < 0 || PlanIdx >= (int)Plans.size()) return;
	KbPlan = PlanIdx;
	KbReturn = Return;
	wstring cur = FromGod(Plans[PlanIdx]->Title);
	memset(&KbOverlapped, 0, sizeof(KbOverlapped));
	memset(KbResult, 0, sizeof(KbResult));
	DWORD r = XShowKeyboardUI(0, VKBD_DEFAULT, cur.c_str(), L"Game name", L"Name shown on the dashboard (max 63 characters)",
		KbResult, 64, &KbOverlapped);
	if (r != ERROR_IO_PENDING) { XNotify(L"Could not open the keyboard"); return; }
	State = State_Keyboard;
	SetTimer(TIMER_POLL, 100);
}

VOID CMainScene::PollKeyboard()
{
	if (!XHasOverlappedIoCompleted(&KbOverlapped)) return;
	KillTimer(TIMER_POLL);
	if (XGetOverlappedResult(&KbOverlapped, NULL, FALSE) == ERROR_SUCCESS && KbResult[0] && KbPlan < (int)Plans.size())
		Plans[KbPlan]->Title = ToGod(KbResult);
	if (KbReturn == 1) ShowQueueReview(KbPlan); else ShowCategory();
}

//--------------------------------------------------------------------------------------------------------------------------
// input + timers
//--------------------------------------------------------------------------------------------------------------------------
HRESULT CMainScene::OnNotifyPress(HXUIOBJ hObjPressed, BOOL& bHandled)
{
	bHandled = TRUE;

	if (hObjPressed == btnExitApp)
	{
		if (State != State_Working) bShouldExit = TRUE;
		return S_OK;
	}

	switch (State)
	{
	case State_Main:
		if (hObjPressed == btnA) { CurPath = ""; ShowBrowser(""); }
		break;

	case State_Browse:
		if (hObjPressed == FileList)
		{
			int sel = FileList.GetCurSel();
			if (sel >= 0 && sel < (int)Entries.size()) BrowseInto(Entries[sel]);
		}
		else if (hObjPressed == btnB)
		{
			if (PendingSel != -2) { PendingSel = -2; SetTimer(TIMER_BACK, 20); }   // -2 = "back" already queued
		}
		else if (hObjPressed == btnX) ToggleQueue(FileList.GetCurSel());
		else if (hObjPressed == btnY && !Queue.empty()) StartPrepare(Queue);
		break;

	case State_Category:
		if (hObjPressed == btnA) ChooseCategory(Plans[0]->ShortcutOnly ? GOD_CAT_APP : GOD_CAT_GAME);   // .xbe ignores it
		else if (hObjPressed == btnX) ChooseCategory(GOD_CAT_APP);
		else if (hObjPressed == btnY) StartRename(0, 0);
		else if (hObjPressed == btnB) { FreePlans(); Results.clear(); ShowBrowser(""); }
		break;

	case State_QueueReview:
		if (hObjPressed == FileList)
		{
			int i = FileList.GetCurSel();
			if (i >= 0 && i < (int)Plans.size())
			{
				GodPlan* p = Plans[i];
				if (p->Kind == GOD_KIND_XBOX1 || p->ShortcutOnly) XNotify(L"This one can only be converted one way");
				else
				{
					string err;
					if (!GodSetCategory(*p, p->Category == GOD_CAT_APP ? GOD_CAT_GAME : GOD_CAT_APP, err)) XNotify(Widen(err).c_str());
					ShowQueueReview(i);
				}
			}
		}
		else if (hObjPressed == btnY) StartRename(FileList.GetCurSel(), 1);
		else if (hObjPressed == btnX)
		{
			god_u64 need, freeBytes; BOOL haveFree;
			SpaceInfo(need, freeBytes, haveFree);
			if (!DriveExists("Hdd:")) XNotify(L"No internal HDD found - packages must go on Hdd:");
			else if (haveFree && freeBytes < need) XNotify(L"Not enough free space on the HDD - switch some to App or Demo or remove some");
			else StartConvert();
		}
		else if (hObjPressed == btnB) { FreePlans(); Results.clear(); ShowBrowser(""); }
		break;

	case State_Working:
		if (hObjPressed == btnB) { g_Progress.Cancel = true; UpdateProgressText(); }
		break;

	case State_Result:
		if (hObjPressed == btnA) { if (CurPath.empty()) ShowMain(); else ShowBrowser(""); }
		break;

	default:
		break;
	}
	return S_OK;
}

HRESULT CMainScene::OnTimer(XUIMessageTimer* pTimer, BOOL& bHandled)
{
	bHandled = TRUE;
	if (pTimer->nId == TIMER_BACK)
	{
		// B was pressed in the browser: go up now that the button press has fully finished, so the
		// rebuilt list keeps its highlight (rebuilding inside the B handler lost it).
		KillTimer(TIMER_BACK);
		PendingSel = -1;
		if (State == State_Browse) BrowseUp();
		return S_OK;
	}
	if (pTimer->nId != TIMER_POLL) return S_OK;

	switch (State)
	{
	case State_Preparing:
		if (WorkerDone) OnWorkerFinished();
		else
		{
			WCHAR t[128];
			swprintf_s(t, 128, L"Reading the executable and scanning files...\n\n%d of %d", CurrentJob + 1, (int)PendingExes.size());
			ShowPrompt(YellowTextCol, t);
		}
		break;
	case State_Working:
		if (WorkerDone) OnWorkerFinished();
		else UpdateProgressText();
		break;
	case State_Keyboard:
		PollKeyboard();
		break;
	default:
		KillTimer(TIMER_POLL);
		break;
	}
	return S_OK;
}
