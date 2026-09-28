#include <windows.h>
#include "Hookshot.h"

extern "C" {
	MCIERROR WINAPI fake_mciSendCommandA(MCIDEVICEID, UINT, DWORD, DWORD);
	MCIERROR WINAPI fake_mciSendStringA(LPCSTR, LPSTR, UINT, HWND);
	UINT     WINAPI fake_auxGetNumDevs(void);
	MMRESULT WINAPI fake_auxGetDevCapsA(UINT_PTR, LPAUXCAPS, UINT);
	MMRESULT WINAPI fake_auxGetVolume(UINT, LPDWORD);
	MMRESULT WINAPI fake_auxSetVolume(UINT, DWORD);
	MMRESULT WINAPI fake_waveOutOpen(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD, DWORD, DWORD);
	MMRESULT WINAPI fake_waveOutWrite(HWAVEOUT, LPWAVEHDR, UINT);

	extern MCIERROR (WINAPI *orig_mciSendCommandA)(MCIDEVICEID, UINT, DWORD, DWORD);
	extern MCIERROR (WINAPI *orig_mciSendStringA)(LPCSTR, LPSTR, UINT, HWND);
	extern MMRESULT (WINAPI *orig_waveOutOpen)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD, DWORD, DWORD);
	extern MMRESULT (WINAPI *orig_waveOutWrite)(HWAVEOUT, LPWAVEHDR, UINT);
}

using namespace Hookshot;

#define INSTALL(proc, fake, orig)                                    \
	do {                                                             \
		void *p = (void *)GetProcAddress(winmm, proc);               \
		if (p) {                                                     \
			hookshot->CreateHook(p, (const void *)&fake);            \
			orig = (decltype(orig))hookshot->GetOriginalFunction(p); \
		}                                                            \
	} while (0)

#define INSTALL_NOORIG(proc, fake)                                   \
	do {                                                             \
		void *p = (void *)GetProcAddress(winmm, proc);               \
		if (p) hookshot->CreateHook(p, (const void *)&fake);         \
	} while (0)

// The macros above take winmm and hookshot by name, so these parameter names
// are load-bearing. Both ways in below end up here.
static void InstallHooks(IHookshot *hookshot, HMODULE winmm)
{
	INSTALL_NOORIG("auxGetNumDevs",  fake_auxGetNumDevs);
	INSTALL_NOORIG("auxGetDevCapsA", fake_auxGetDevCapsA);
	INSTALL_NOORIG("auxGetVolume",   fake_auxGetVolume);
	INSTALL_NOORIG("auxSetVolume",   fake_auxSetVolume);

	INSTALL("mciSendCommandA", fake_mciSendCommandA, orig_mciSendCommandA);
	INSTALL("mciSendStringA",  fake_mciSendStringA,  orig_mciSendStringA);
	INSTALL("waveOutOpen",     fake_waveOutOpen,     orig_waveOutOpen);
	INSTALL("waveOutWrite",    fake_waveOutWrite,    orig_waveOutWrite);
}

static void OnWinmmLoaded(IHookshot *hookshot, const wchar_t *modulePath)
{
	HMODULE winmm = GetModuleHandleW(modulePath);
	if (winmm) InstallHooks(hookshot, winmm);
}

// Same stance as Xidi: a hook module that cannot install its hooks says so and
// ends the process, rather than leaving the game running with the overlay
// silently inert.
static void Fail(const wchar_t *reason)
{
	MessageBoxW(NULL, reason, L"ogg-winmm", MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
	TerminateProcess(GetCurrentProcess(), (UINT)-1);
}

HOOKSHOT_HOOK_MODULE_ENTRY(hookshot)
{
	HMODULE winmm = GetModuleHandleW(L"winmm.dll");
	if (winmm) {
		InstallHooks(hookshot, winmm);
		return;
	}

	// winmm is not loaded, so only a later LoadLibrary can bring it in, and
	// hearing about that needs interface version 2.
	IHookshot2 *hookshot2 = RequestNewerHookshotInterface<IHookshot2>(hookshot);
	if (!hookshot2) {
		Fail(L"The loaded Hookshot library does not support interface version 2, so "
		     L"ogg-winmm cannot wait for winmm.dll to be loaded.");
		return;
	}

	if (!SuccessfulResult(hookshot2->NotifyOnLibraryLoad(L"winmm.dll", &OnWinmmLoaded))) {
		Fail(L"Hookshot would not notify ogg-winmm when winmm.dll is loaded.");
	}
}