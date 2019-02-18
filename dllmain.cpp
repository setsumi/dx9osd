// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

#include "detours.h"

#pragma comment(lib, "d3d9")
#pragma comment(lib, "d3dx9")
#pragma comment(lib, "version")

//---------------------------------------------------------------------------
#define SAFE_RELEASE(p) { if ( (p) ) { (p)->Release(); (p) = 0; } }
//---------------------------------------------------------------------------
#define LOGP "OSD: "
#define OSD_MSG_WNDCLASS L"DX9OSDMessageWindow"
#define TEXT_BORDER 20
//---------------------------------------------------------------------------
HINSTANCE hInstance = 0;			// handle of this DLL
HWND hMsgWnd = 0;					// hidden window used to init D3D and receive text messages
static wchar_t gMsgText[MAX_PATH];	// message's text
int gMaxFrames = 0;					// how many frames to keep message on screen
int gFrameCounter = 0;
FILE *gpLogFile = 0;

//---------------------------------------------------------------------------
void Log(const char* format, ...)
{
	if (!gpLogFile) gpLogFile = fopen("logfileosd.txt", "w");

	va_list argptr;
	va_start(argptr, format);
	vfprintf(stderr, format, argptr);
	if (gpLogFile) {
		vfprintf(gpLogFile, format, argptr);
		fflush(gpLogFile);
	}
	va_end(argptr);
}
void Fatal(char *message)
{
	Log(message);
	Sleep(5000); // wait for game to display its window
	MessageBoxA(NULL, message, "dx9osd", MB_OK|MB_ICONERROR|MB_SETFOREGROUND);
	HANDLE hnd = OpenProcess(PROCESS_TERMINATE, TRUE, GetCurrentProcessId());
	TerminateProcess(hnd, 0);
}

//---------------------------------------------------------------------------
// Hooking D3D stuff
//---------------------------------------------------------------------------
typedef HRESULT(WINAPI* pEndScene)(LPDIRECT3DDEVICE9);
pEndScene oEndScene = 0;

D3DRECT m_rec;
ID3DXFont *m_font = 0;
RECT fontRect;
D3DCOLOR bkgColor = D3DCOLOR_XRGB(0, 0, 255);
D3DCOLOR fontColor = D3DCOLOR_XRGB(255, 255, 255);
D3DVIEWPORT9 viewP;

//---------------------------------------------------------------------------
HRESULT WINAPI hkEndScene(LPDIRECT3DDEVICE9 pDev)
{	
	static bool init = false;

	if (gFrameCounter < gMaxFrames)
	{
		gFrameCounter++;

		// Init one time stuff
		if (!init)
		{
			init = true;
			Log(LOGP"hkEndScene(): One time init hit\n");

			// create font
			HRESULT hr = D3DXCreateFont(pDev, // D3D device
				32,                    // Height
				0,                     // Width
				FW_BOLD,               // Weight
				1,                     // MipLevels, 0 = autogen mipmaps
				FALSE,                 // Italic
				DEFAULT_CHARSET,       // CharSet
				OUT_DEFAULT_PRECIS,    // OutputPrecision
				DEFAULT_QUALITY,       // Quality
				DEFAULT_PITCH|FF_DONTCARE, // PitchAndFamily
				L"Arial",              // pFaceName
				&m_font);              // ppFont
			if (FAILED(hr)) {
				Log(LOGP"hkEndScene(): D3DXCreateFont() fail\n");
				goto endscene_ret;
			}

			// measure screen size
			hr = IDirect3DDevice9_GetViewport(pDev, &viewP);
			if (FAILED(hr)) {
				Log(LOGP"hkEndScene(): IDirect3DDevice9_GetViewport() fail\n");
				goto endscene_ret;
			}
		}

		if (gMaxFrames == 1) // take screenshot
		{
			Log(LOGP"hkEndScene(): Take screenshot hit\n");

			SYSTEMTIME lt;
			GetLocalTime(&lt);
			wchar_t tName[MAX_PATH];
			wsprintf(tName, L"%sdx9osd[%04d%02d%02d-%02d%02d%02d_%04d].png", gMsgText,
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds);

			IDirect3DSurface9* pDestTarget;
			HRESULT hr = pDev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_FORCE_DWORD, &pDestTarget);
			if (hr == D3D_OK) {
				D3DXSaveSurfaceToFileW(tName, D3DXIFF_PNG, pDestTarget, NULL, NULL);
				pDestTarget->Release();
			} else {
				Log(LOGP"hkEndScene(): GetBackBuffer() fail: 0x%X\n", hr);
			}
		}
		else // display OSD message
		{
			// Init stuff on each message
			if (gFrameCounter == 1) // first frame of a message
			{
				Log(LOGP"hkEndScene(): Message init hit\n");

				// measure text rectangle
				SetRect((LPRECT)&m_rec, 0, 0, 0, 0);
				m_font->DrawText( NULL, gMsgText, -1, (LPRECT)&m_rec, DT_CALCRECT, D3DXCOLOR(1.0f, 0.0f, 0.0f, 1.0f));
				// add borders
				m_rec.x2 += TEXT_BORDER * 2;
				m_rec.y2 += TEXT_BORDER * 2;
				if (m_rec.x2 > (LONG)viewP.Width) m_rec.x2 = viewP.Width - 1;
				if (m_rec.y2 > (LONG)viewP.Height) m_rec.x2 = viewP.Height - 1;

				LONG shX = (viewP.Width - m_rec.x2) / 2;
				LONG shY = (viewP.Height - m_rec.y2) / 2;
				SetRect((LPRECT)&m_rec, m_rec.x1 + shX, m_rec.y1 + shY, m_rec.x2 + shX, m_rec.y2 + shY);
				SetRect(&fontRect, m_rec.x1 + TEXT_BORDER, m_rec.y1 + TEXT_BORDER, m_rec.x2, m_rec.y2);
			}

			// Draw rectangle and text
			IDirect3DDevice9_Clear(pDev, 1, &m_rec, D3DCLEAR_TARGET, bkgColor, 1.0f, 0);	
			m_font->DrawText(NULL, gMsgText, -1, &fontRect, 0, fontColor);
		}
	}
endscene_ret:
	return oEndScene(pDev);
}

//---------------------------------------------------------------------------
bool InitD3D()
{
	LPDIRECT3D9       pD3D = NULL;
	LPDIRECT3DDEVICE9 pd3dDevice = NULL;

	if (NULL == (pD3D = Direct3DCreate9(D3D_SDK_VERSION))) {
		Log(LOGP"InitD3D(): Direct3DCreate9() fail\n");
		return false;
	}
	Log(LOGP"InitD3D(): Direct3DCreate9() - OK\n");

	D3DPRESENT_PARAMETERS d3dpp;
	ZeroMemory(&d3dpp, sizeof(d3dpp));
	d3dpp.Windowed = TRUE;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
	d3dpp.BackBufferWidth = 0;
	d3dpp.BackBufferHeight = 0;
	d3dpp.hDeviceWindow = hMsgWnd;

	HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hMsgWnd,
		D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &pd3dDevice);
	if (FAILED(hr))
	{
		Log(LOGP"InitD3D(): CreateDevice() fail: 0x%X\n", hr);
		pD3D->Release();
		return false;
	}
	Log(LOGP"InitD3D(): CreateDevice() - OK\n");

#ifdef _WIN64
#define DWORD__ DWORD64
#define PDWORD__ PDWORD64
#else
#define DWORD__ DWORD
#define PDWORD__ PDWORD
#endif

	DWORD__* vtablePtr;
	vtablePtr = (PDWORD__)(*((PDWORD__)pd3dDevice));
	oEndScene = (pEndScene)vtablePtr[42]; // index of EndScene()

	pd3dDevice->Release();
	pD3D->Release();

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&(PVOID&)oEndScene, hkEndScene);
	if (DetourTransactionCommit() != 0) {
		oEndScene = 0;
		Log(LOGP"InitD3D(): Set EndScene() hook fail\n");
		return false;
	}
	Log(LOGP"InitD3D(): Set EndScene() hook - OK\n");

	return true;
}

//---------------------------------------------------------------------------
LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_APP+1:
		{
			Log(LOGP"Message get (WM_APP+1) ");

			LRESULT textlen = SendMessage(hwnd, WM_GETTEXTLENGTH, 0, 0);
			if (textlen) {
				// get text of message
				SendMessage(hwnd, WM_GETTEXT, (WPARAM)(textlen + 1), (LPARAM)gMsgText);
				gMaxFrames = (int)wParam; // get interval
				gFrameCounter = 0;        // reset frame based time counter

				Log("\"%ls\", %d frames\n", gMsgText, gMaxFrames);
			} else
				Log(", but no text, you're doing it wrong\n");

			return 0;
		}
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

//---------------------------------------------------------------------------
HWND CreateMessageWindow()
{
	// Register the window class
	WNDCLASSEX wc =
	{
		sizeof(WNDCLASSEX), CS_DBLCLKS, MessageWindowProc, 0L, 0L,
		hInstance, NULL, NULL, NULL, NULL,
		OSD_MSG_WNDCLASS, NULL
	};
	if (0 == RegisterClassEx(&wc)) {
		Log(LOGP"CreateMessageWindow(): RegisterClassEx() fail\n");
		return NULL;
	}

	// Create the application's window
	HWND hWnd = CreateWindow(OSD_MSG_WNDCLASS, L"D3D9OSD window",
		WS_OVERLAPPEDWINDOW, 0, 0, 640, 480,
		NULL, NULL, hInstance, NULL);
	if (hWnd == NULL) {
		Log(LOGP"CreateMessageWindow(): CreateWindow() fail\n");
	}

	return hWnd;
}

//---------------------------------------------------------------------------
// our notification window thread
//---------------------------------------------------------------------------
void TW()
{
	// create window
	if (NULL == (hMsgWnd = CreateMessageWindow())) {
		Fatal(LOGP"TW: Create message window fail\n");
		return;
	}
	Log(LOGP"TW: Create message window - OK\n");

	// init D3D
	if (!InitD3D()) {
		Fatal(LOGP"TW: Init D3D fail\n");
		return;
	}
	Log(LOGP"TW: Init D3D - OK, entering window message loop\n");

	// run message loop
	MSG messages;
	while (GetMessage(&messages, NULL, 0, 0))
	{
		TranslateMessage(&messages);
		DispatchMessage(&messages);
	}
}

//---------------------------------------------------------------------------
void GetVersionString(wchar_t *version/*_OUT_*/)
{
	static wchar_t module_file[MAX_PATH];
	GetModuleFileName(hInstance, module_file, MAX_PATH - 1);
	//Log(LOGP"%ls\n", module_file);

	DWORD hnd = 0, vis = 0;
	if(0 != (vis = GetFileVersionInfoSize(module_file, &hnd))) {
		BYTE *data = new BYTE[vis];
		if(GetFileVersionInfo(module_file, hnd, vis, data)) {
			UINT ilen = 0;
			struct LANGANDCODEPAGE {
				WORD wLanguage;
				WORD wCodePage;
			} *lpTranslate;
			// Read first language and code page
			if(VerQueryValue(data, L"\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &ilen)) {
				wchar_t str[MAX_PATH];
				wsprintf(str, L"\\StringFileInfo\\%04x%04x\\FileVersion", lpTranslate->wLanguage, lpTranslate->wCodePage);
				// Retrieve file version for language and code page
				wchar_t *pver = NULL;
				if(VerQueryValue(data, str, (LPVOID*)&pver, &ilen)) {
					wcscpy(version, pver);
				}
			}
		}
		delete []data;
	}
}

//---------------------------------------------------------------------------
#ifdef _WIN64
#define APPNAME__ "DX9OSD(x64)"
#else
#define APPNAME__ "DX9OSD"
#endif
//---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	static wchar_t version[MAX_PATH];

	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		{
			hInstance = (HINSTANCE)hModule;
			DisableThreadLibraryCalls(hModule);
			GetVersionString(version);
			Log(LOGP"%s Version %ls\n", APPNAME__, version);

			// do our work
			HANDLE h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)TW, 0, 0, 0);
			CloseHandle(h);
		}
		break;
	case DLL_PROCESS_DETACH:
		{
			Log(LOGP"Process detach hit, cleanup\n");

			SAFE_RELEASE(m_font);
			if (oEndScene) {
				DetourTransactionBegin();
				DetourUpdateThread(GetCurrentThread());
				DetourDetach(&(PVOID&)oEndScene, hkEndScene);
				DetourTransactionCommit();
			}
			if (hMsgWnd) SendMessage(hMsgWnd, WM_CLOSE, 0, 0);
			UnregisterClass(OSD_MSG_WNDCLASS, hInstance);
			if (gpLogFile) fclose(gpLogFile);
		}
		break;
	}

	return TRUE;
}

//---------------------------------------------------------------------------
