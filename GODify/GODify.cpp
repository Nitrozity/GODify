//==========================================================================================================================
//
//											- GODify -
//		Convert games already on your HDD/USB (extracted folders with default.xex) into Games on Demand packages,
//		directly on an RGH/JTAG Xbox 360, so they show up on the dashboard.
//
// UI base: SonusGUI by Byrom (https://github.com/Byrom90/SonusGUI)
// GOD format: iso2god-rs by Ilia Pozdnyakov (MIT), XDVDFS layout: extract-xiso
//==========================================================================================================================

#include "stdafx.h"

HRESULT RenderGame(IDirect3DDevice9* pDevice)
{
	// Render game graphics.
	pDevice->Clear(
		0,
		NULL,
		D3DCLEAR_TARGET | D3DCLEAR_STENCIL | D3DCLEAR_ZBUFFER,
		D3DCOLOR_ARGB(255, 32, 32, 64),
		1.0,
		0);

	return S_OK;
}

HRESULT InitD3D(IDirect3DDevice9** ppDevice,
	D3DPRESENT_PARAMETERS* pd3dPP)
{
	IDirect3D9* pD3D;

	pD3D = Direct3DCreate9(D3D_SDK_VERSION);

	// Set up the presentation parameters.
	ZeroMemory(pd3dPP, sizeof(D3DPRESENT_PARAMETERS));
	pd3dPP->BackBufferWidth = 1280;
	pd3dPP->BackBufferHeight = 720;
	pd3dPP->BackBufferFormat = D3DFMT_A8R8G8B8;
	pd3dPP->BackBufferCount = 1;
	pd3dPP->MultiSampleType = D3DMULTISAMPLE_NONE;
	pd3dPP->SwapEffect = D3DSWAPEFFECT_DISCARD;
	pd3dPP->EnableAutoDepthStencil = TRUE;
	pd3dPP->AutoDepthStencilFormat = D3DFMT_D24S8;
	pd3dPP->PresentationInterval = D3DPRESENT_INTERVAL_ONE;

	// Create the device.
	return pD3D->CreateDevice(
		0,
		D3DDEVTYPE_HAL,
		NULL,
		D3DCREATE_HARDWARE_VERTEXPROCESSING,
		pd3dPP,
		ppDevice);
}


BOOL bShouldExit = FALSE;
// Entry point of the title.
int __cdecl main()
{
	DbgPrint("%s - Starting...", BUILD_VER);

	IsDevkit = *(DWORD*)0x8E038610 & 0x8000 ? FALSE : TRUE;
	DbgPrint("IsDevkit: %s", IsDevkit ? "TRUE" : "FALSE");

	if (CreateMount(MOUNT_FLASH, DEVICE_FLASH, FALSE)) // Set the flash mount so we can access the font
	{
		DbgPrint("Failed to set flash mount!");
		return 1;
	}

	// Storage devices for the browser / output. A missing USB device just fails to mount, which is fine.
	CreateMount("Hdd:",  DEVICE_HDD,  FALSE);
	CreateMount("Usb0:", DEVICE_USB0, FALSE);
	CreateMount("Usb1:", DEVICE_USB1, FALSE);
	CreateMount("Usb2:", DEVICE_USB2, FALSE);

	hAPP = GetModuleHandle(NULL);

	// The shortcut launcher is embedded as the "LNCH" section (see AppConf.xml)
	{
		PVOID lnch = NULL; ULONG lnchSize = 0;
		if (XGetModuleSection(hAPP, "LNCH", &lnch, &lnchSize) && lnch && lnchSize)
			GodSetShortcutLauncher(lnch, (god_u32)lnchSize);
		else
			DbgPrint("Shortcut launcher section missing!");

	}

	IDirect3DDevice9* pDevice;
	D3DPRESENT_PARAMETERS d3dpp;
	HRESULT hr;

	// Initialize D3D
	hr = InitD3D(&pDevice, &d3dpp);
	if (FAILED(hr)) {
		DbgPrint("Failed initializing D3D! Exiting...");
		return 1;
	}

	// Declare an instance of the XUI framework.
	CMyApp app;

	// Initialize the application.
	hr = app.InitShared(pDevice, &d3dpp,
		XuiD3DXTextureLoader);

	if (FAILED(hr))
	{
		DbgPrint("Failed initializing application! Exiting...\n");
		return 1;
	}


	// Register a default typeface
	HRESULT fontresult = app.RegisterDefaultTypeface(L"Tahoma", L"file://flash:/XenonJKLatin.xtt"); // Use the system font that already exists on the flash

	DbgPrint("Font register result: %08X", fontresult); 

	if (app.LoadSkin(LinkWChar(L"section://%08X,XDR#0_skin.xur", hAPP)) == 0)
	{
		if (app.LoadFirstScene(L"", LinkWChar(L"section://%08X,XDR#1_scene.xur", hAPP), NULL))
		{
			DbgPrint("Failed to load scene! Exiting...");
			XNotify(L"Failed to load scene!");
			bShouldExit = TRUE;
		}
	}
	else
	{
		DbgPrint("Failed to load skin! Exiting...");
		XNotify(L"Failed to load skin!");
		bShouldExit = TRUE;
	}


	while (!bShouldExit) {
		// Render game graphics.
		RenderGame(pDevice);

		// Update XUI
		app.RunFrame();

		// Render XUI
		hr = app.Render();

		// Update XUI Timers
		hr = XuiTimersRun();

		// Present the frame.
		pDevice->Present(NULL, NULL, NULL, NULL);

	}
	// Free resources, unregister custom classes, and exit.
	DbgPrint("Exiting...");
	app.Uninit();
	pDevice->Release();
}


