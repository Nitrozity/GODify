#ifndef __XuiClasses_H__
#define __XuiClasses_H__
#pragma once

class CMyApp : public CXuiModule
{
protected:
	virtual HRESULT RegisterXuiClasses();
	virtual HRESULT UnregisterXuiClasses();
};

//==============================================================================================================================================================================
//
//			Main scene: browse -> pick game(s) -> confirm -> convert to GOD
//
//==============================================================================================================================================================================
class CMainScene : public CXuiSceneImpl
{
public:
	XUI_IMPLEMENT_CLASS(CMainScene, L"MainScene", XUI_CLASS_SCENE)

	enum GUIState
	{
		State_Main,       // welcome screen
		State_Browse,     // file browser
		State_Preparing,  // worker thread is reading default.xex + scanning folders
		State_Category,   // single item: Game / App or Demo / Rename, plus space needed/free
		State_QueueReview,// queue: list of items, each switchable between Game and App or Demo
		State_Keyboard,   // rename keyboard is open
		State_Working,    // worker thread is converting
		State_Result,     // result shown on screen
	};

	enum EntryType
	{
		Entry_Drive,      // Hdd:, Usb0: ...
		Entry_Dir,        // plain folder
		Entry_Game360,    // folder with default.xex
		Entry_GameXbox,   // folder with default.xbe (original Xbox)
		Entry_AppDir,     // folder with some other .xex (homebrew / apps)
		Entry_Exe,        // an executable inside the current folder -> A converts it
	};

	struct BrowseEntry
	{
		EntryType Type;
		string    Name;     // display name
		string    Path;     // full path of the folder (or of the executable for Entry_Exe)
		string    Exe;      // executable that X queues ("" = ambiguous, open the folder to pick)
	};

	struct JobResult
	{
		wstring Title;
		string  Exe;        // source executable
		int     Result;     // GodResult
		string  Error;
	};

protected:
	CXuiTextElement  textPrompt, textBuildVer, textPath;
	CXuiControl      btnExitApp, btnA, btnB, btnX, btnY, btnLS, btnRS;
	CXuiImageElement ImgBackground;
	CXuiList         FileList;

	GUIState            State;
	string              CurPath;          // "" = drive list, "Hdd:" = device root, "Hdd:\Games" ...
	vector<BrowseEntry> Entries;
	vector<string>      Queue;            // executables marked with X
	int                 PendingSel;       // list row to re-select one frame after (re)filling the list
	int                 CatIdx;           // State_Category: plan being asked about

	// worker thread shared state
	vector<string>      PendingExes;      // executables to prepare
	vector<GodPlan*>    Plans;            // prepared plans (owned)
	vector<JobResult>   Results;
	string              PrepareError;
	HANDLE              hWorker;
	volatile BOOL       WorkerDone;
	volatile int        CurrentJob;
	DWORD               JobStartTick;
	BOOL                WorkerIsConvert;

	// keyboard
	XOVERLAPPED         KbOverlapped;
	WCHAR               KbResult[128];
	int                 KbPlan;           // which plan is being renamed
	int                 KbReturn;         // screen to return to after the keyboard: 0 = category, 1 = queue review

	HRESULT OnInit(XUIMessageInit* pInitData, BOOL& bHandled);
	HRESULT OnNotifyPress(HXUIOBJ hObjPressed, BOOL& bHandled);
	HRESULT OnTimer(XUIMessageTimer* pTimer, BOOL& bHandled);
	HRESULT SetTextColour(HXUIOBJ TextObject, DWORD dwTextColour);

	// screens
	VOID ShowMain();
	VOID ShowBrowser(const string& SelectName);
	VOID ShowCategory();
	VOID ChooseCategory(int Category);
	VOID ShowQueueReview(int Select);
	wstring QueueItemText(int Index);
	VOID SpaceInfo(god_u64& Need, god_u64& Free, BOOL& HaveFree);
	VOID ShowResult(BOOL Success, const wstring& ResultText);
	VOID ShowButtons(LPCWSTR a, LPCWSTR b, LPCWSTR x, LPCWSTR y);
	VOID ShowPrompt(DWORD Colour, const wstring& Text);
	VOID CenterPromptText();
	VOID HideList();

	// browser
	BOOL FillEntries();
	VOID BrowseInto(const BrowseEntry& e);
	VOID BrowseUp();
	VOID ToggleQueue(int Index);
	BOOL IsQueued(const string& ExePath);
	wstring EntryText(const BrowseEntry& e);

	// work
	VOID StartPrepare(const vector<string>& Exes);
	VOID StartConvert();
	VOID OnWorkerFinished();
	VOID UpdateProgressText();
	VOID FreePlans();
	VOID StartRename(int PlanIdx, int Return);
	VOID PollKeyboard();

	static DWORD WINAPI WorkerProc(LPVOID Param);
	VOID WorkerPrepare();
	VOID WorkerConvert();

	XUI_BEGIN_MSG_MAP()
		XUI_ON_XM_INIT(OnInit)
		XUI_ON_XM_NOTIFY_PRESS(OnNotifyPress)
		XUI_ON_XM_TIMER(OnTimer)
	XUI_END_MSG_MAP()
};

#endif
