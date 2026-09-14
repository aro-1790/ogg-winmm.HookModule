#include <windows.h>
#include "player.h"
#include "stub.h"

/* Populated at module load by hookshot_entry.cpp. */
MCIERROR (WINAPI *orig_mciSendCommandA)(MCIDEVICEID, UINT, DWORD, DWORD) = NULL;
MCIERROR (WINAPI *orig_mciSendStringA)(LPCSTR, LPSTR, UINT, HWND) = NULL;
MMRESULT (WINAPI *orig_waveOutOpen)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD, DWORD, DWORD) = NULL;
MMRESULT (WINAPI *orig_waveOutWrite)(HWAVEOUT, LPWAVEHDR, UINT) = NULL;

static float midiVol = 1.0;
static float waveVol = 1.0;
static int waveBits = -1;

void stub_midivol(int vol) { midiVol = vol < 0 || vol > 99  ? 1.0 : vol / 100.0; }
void stub_wavevol(int vol) { waveVol = vol < 0 || vol > 99  ? 1.0 : vol / 100.0; }
void unloadRealDLL() { }

MCIERROR WINAPI relay_mciSendCommandA(MCIDEVICEID a0, UINT a1, DWORD a2, DWORD a3)
{
	return orig_mciSendCommandA(a0, a1, a2, a3);
}

MCIERROR WINAPI relay_mciSendStringA(LPCSTR a0, LPSTR a1, UINT a2, HWND a3)
{
	return orig_mciSendStringA(a0, a1, a2, a3);
}

MMRESULT WINAPI fake_waveOutOpen(LPHWAVEOUT a0, UINT a1, LPCWAVEFORMATEX a2, DWORD a3, DWORD a4, DWORD a5)
{
	if (a2) waveBits = a2->wBitsPerSample;
	return orig_waveOutOpen(a0, a1, a2, a3, a4, a5);
}

MMRESULT WINAPI fake_waveOutWrite(HWAVEOUT a0, LPWAVEHDR a1, UINT a2)
{
	if ((waveVol != 1.0 || midiVol != 1.0 ) && a1 && a1->lpData && a1->dwUser != 0xCDDA7777) {
		void *addr = __builtin_return_address(0);
		char caller[MAX_PATH];
		MEMORY_BASIC_INFORMATION mbi;
		VirtualQuery(addr, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
		GetModuleFileName(mbi.AllocationBase, caller, MAX_PATH);

		float vol = 1.0;
		char *pos = strrchr(caller, '\\');
		if (strstr(pos, "wdmaud.drv")) vol = midiVol;
		else if (!strstr(pos, ".drv")) vol = waveVol;

		if (vol != 1.0) {
			switch (waveBits) {
				case 16: {
					short *w = (short *)a1->lpData;
					for (int i = 0, j = a1->dwBufferLength / 2; i < j; i++) w[i] *= vol;
					break;
				}
				case 8: {
					char *w = (char *)a1->lpData;
					for (int i = 0, j = a1->dwBufferLength; i < j; i++) w[i] *= vol;
					break;
				}
			}
		}
	}
	return orig_waveOutWrite(a0, a1, a2);
}