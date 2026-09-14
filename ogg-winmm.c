/*
 * Copyright (c) 2012 Toni Spets <toni.spets@iki.fi>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <windows.h>
#include <stdio.h>
#include "player.h"
#include "stub.h"

#define MAGIC_DEVICEID 0xCDDA
#define MEDIA_IDENTITY "CDDA7777CDDA7777"
#define MAX_TRACKS 99

//#define _DEBUG

#ifdef _DEBUG
#define dprintf(...) if (fh) { fprintf(fh, __VA_ARGS__); fflush(NULL); }
FILE *fh = NULL;
#else
#define dprintf(...)
#endif

struct track_info
{
	char path[MAX_PATH];    /* full path to ogg */
	unsigned int position;  /* milliseconds */
	unsigned int length;    /* milliseconds */
	DWORD tick;           /* clock tick at play start */
};

struct play_info
{
	int first;
	unsigned int from; /* millseconds; 0: track beginning, -1: track end */
	int last;
	unsigned int to; /* milliseconds; 0 track beginning, -1: track end */
};

struct track_info tracks[MAX_TRACKS+1]; // Track 0 is reserved.
struct play_info info = {0};

DWORD thread = 0; // Needed for Win95/98 compatibility
HANDLE player = NULL;
HANDLE event = NULL;
HWND window = NULL;
const char alias_def[] = "cdaudio";
char alias_s[100] = "cdaudio";
char path[MAX_PATH];
char cddaPath[MAX_PATH];

int mode = MCI_MODE_STOP;
int command = 0;
int notify = 0;
int current  = 0;
int firstTrack = 0;
int lastTrack = 0;
int numTracks = 0;
int time_format = MCI_FORMAT_MSF;

DWORD auxVol = -1; // HWORD: Right, LWORD: Left
int cddaVol = 100;
int midiVol = 100;
int waveVol = 100;

DWORD WINAPI player_main(void *unused)
{
	while (WaitForSingleObject(event, INFINITE) == 0) {
		int first = info.first < firstTrack ? firstTrack : info.first;
		int last = info.last > lastTrack+1 ? lastTrack+1 : info.last;
		unsigned int from = info.from, to = info.to;
		if (from == -1) {first++; from = 0;}
		if (!to) {last--; to = -1;} // Convert [,) to [,]
		current = first;
		dprintf("[Thread] From %d (%d ms) to %d (%d ms)\n", first, from, last, to);

		while (command == MCI_PLAY && current <= last) {
			dprintf("[Thread] Current track %s\n", tracks[current].path);
			tracks[current].tick = GetTickCount();
			mode = MCI_MODE_PLAY;
			plr_play(tracks[current].path, current == first ? from : 0, current == last ? to : -1);

			while (command == MCI_PLAY) {
				int more = plr_pump();
				if (more == 0) {
					current++;
					break;
				} else if (more < 0) {
					break;
				}
			}

			plr_reset(command == MCI_PLAY);
		}

		/* Sending notify successful message:*/
		if (command == MCI_PLAY && notify) {
			notify = 0;
			SendNotifyMessageA(window, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
			dprintf("[Thread] Send MCI_NOTIFY_SUCCESSFUL message\n");
		}

		mode = MCI_MODE_STOP;
		if (command == MCI_DELETE) break;
	}
	
	CloseHandle(event);
	event = NULL;
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH) {
#ifdef _DEBUG
		fh = fopen("winmm.log", "w");
#endif
		GetModuleFileName(NULL, path, sizeof(path));

		char *last = strrchr(path, '\\');
		if (last) {
			strcpy(last + 1, "ogg-winmm.ini");

			GetPrivateProfileString("OGG-WinMM", "CDDAPath", "Music", cddaPath, MAX_PATH, path);
			cddaVol = GetPrivateProfileInt("OGG-WinMM", "CDDAVolume", 100, path);
			midiVol = GetPrivateProfileInt("OGG-WinMM", "MIDIVolume", 100, path);
			waveVol = GetPrivateProfileInt("OGG-WinMM", "WAVEVolume", 100, path);

			if (cddaVol < 0 || cddaVol > 100 ) cddaVol = 100;
			if (midiVol < 0 || midiVol > 100 ) midiVol = 100;
			if (waveVol < 0 || waveVol > 100 ) waveVol = 100;

			plr_volume(cddaVol, cddaVol);
			stub_midivol(midiVol);
			stub_wavevol(waveVol);
		}

		last = strrchr(path, '\\');
		if (last) *last = '\0';
		strcat(path, "\\");
		strcat(path, cddaPath);

		DWORD fa = GetFileAttributes(path);
		if (fa != INVALID_FILE_ATTRIBUTES && fa & FILE_ATTRIBUTE_DIRECTORY) {
			dprintf("ogg-winmm music directory is %s\n", path);

			memset(tracks, 0, sizeof(tracks));
			unsigned int position = 0;

			for (int i = 1; i <= MAX_TRACKS; i++) {
				snprintf(tracks[i].path, MAX_PATH, "%s\\Track%02d.ogg", path, i);
				tracks[i].position = position;
				tracks[i].length = plr_length(tracks[i].path);

				if (tracks[i].length) {
					dprintf("Track %02u: %02u:%02u:%03u @ %u ms\n", i, tracks[i].length / 60000, tracks[i].length / 1000 % 60, tracks[i].length % 1000, tracks[i].position);
					if (!firstTrack) firstTrack = i;
					lastTrack = i;
					numTracks++;
					position += tracks[i].length;
				} else {
					tracks[i].path[0] = '\0';
				}

				if (numTracks && !tracks[i].length) break;
			}
			dprintf("Emulating total of %d CD tracks.\n", numTracks);

			if (numTracks) {
				event = CreateEvent(NULL, FALSE, FALSE, NULL);
				player = CreateThread(NULL, 0, player_main, NULL, 0, &thread);
				dprintf("Creating thread 0x%X\n\n", player);
			}
		}
	} else if (fdwReason == DLL_PROCESS_DETACH) {
#ifdef _DEBUG
		if (fh)
		{
			fclose(fh);
			fh = NULL;
		}
#endif
		command = MCI_DELETE;
		plr_stop();
		if (event) SetEvent(event);
		if (player) WaitForSingleObject(player, INFINITE);

		unloadRealDLL();
	}

	return TRUE;
}

/* MCI commands */
/* https://docs.microsoft.com/windows/win32/multimedia/multimedia-commands */
MCIERROR WINAPI fake_mciSendCommandA(MCIDEVICEID IDDevice, UINT uMsg, DWORD_PTR fdwCommand, DWORD_PTR dwParam)
{
	char cmdbuf[1024];
	dprintf("mciSendCommandA(IDDevice=%p, uMsg=%p, fdwCommand=%p, dwParam=%p) @ %04X\n", IDDevice, uMsg, fdwCommand, dwParam, GetTickCount());

	if (fdwCommand & MCI_NOTIFY) {
		dprintf("  MCI_NOTIFY\n");
		notify = 1; /* storing the notify request */
		window = *(HWND*)dwParam;
	}
	if (fdwCommand & MCI_WAIT) {
		dprintf("  MCI_WAIT\n");
	}

	if (uMsg == MCI_OPEN) {
		dprintf("  MCI_OPEN\n");
		LPMCI_OPEN_PARMS parms = (LPVOID)dwParam;

		if (fdwCommand & MCI_OPEN_ALIAS) {
			dprintf("    MCI_OPEN_ALIAS\n");
			dprintf("        -> %s\n", parms->lpstrAlias);
		}

		if (fdwCommand & MCI_OPEN_SHAREABLE) {
			dprintf("    MCI_OPEN_SHAREABLE\n");
		}

		if (fdwCommand & MCI_OPEN_TYPE_ID) {
			dprintf("    MCI_OPEN_TYPE_ID\n");

			if (LOWORD(parms->lpstrDeviceType) == MCI_DEVTYPE_CD_AUDIO) {
				dprintf("  Returning magic device id for MCI_DEVTYPE_CD_AUDIO\n");
				parms->wDeviceID = MAGIC_DEVICEID;
				return 0;
			}
			else return relay_mciSendCommandA(IDDevice, uMsg, fdwCommand, dwParam);
		}

		if (fdwCommand & MCI_OPEN_TYPE && !(fdwCommand & MCI_OPEN_TYPE_ID)) {
			dprintf("    MCI_OPEN_TYPE\n");
			dprintf("        -> %s\n", parms->lpstrDeviceType);

			if (stricmp(parms->lpstrDeviceType, alias_def) == 0) {
				dprintf("  Returning magic device id for MCI_DEVTYPE_CD_AUDIO\n");
				parms->wDeviceID = MAGIC_DEVICEID;
				return 0;
			}
			else return relay_mciSendCommandA(IDDevice, uMsg, fdwCommand, dwParam);
		}
		return relay_mciSendCommandA(IDDevice, uMsg, fdwCommand, dwParam);
	} else if (IDDevice == MAGIC_DEVICEID || IDDevice == 0 || IDDevice == 0xFFFFFFFF) {
		switch (uMsg) {
			case MCI_CLOSE:
				{
					dprintf("  MCI_CLOSE\n");
					command = MCI_STOP;
					plr_stop();
					/* NOTE: MCI_CLOSE does stop the music in Vista+ but the original behaviour did not
					   it only closed the handle to the opened device. You could still send MCI commands
					   to a default cdaudio device but if you had used an alias you needed to re-open it.
					   In addition WinXP had a bug where after MCI_CLOSE the device would be unresponsive. */
				}
				break;
			case MCI_PLAY:
				{
					dprintf("  MCI_PLAY\n");

					// Treat PLAY as RESUME when in PAUSE.
					if ((mode == MCI_MODE_PAUSE) && !(fdwCommand & MCI_FROM)) {
						mode = MCI_MODE_PLAY;
						plr_resume();
						break;
					}

					LPMCI_PLAY_PARMS parms = (LPVOID)dwParam;

					if (fdwCommand & MCI_FROM) {
						dprintf("    dwFrom: 0x%08X\n", parms->dwFrom);

						if (time_format == MCI_FORMAT_TMSF) {
							info.first = MCI_TMSF_TRACK(parms->dwFrom);
							info.from = MCI_TMSF_MINUTE(parms->dwFrom) * 60000 + MCI_TMSF_SECOND(parms->dwFrom) * 1000 + MCI_TMSF_FRAME(parms->dwFrom) * 1000 / 75; // 1 second consists of 75 frames

							dprintf("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwFrom));
							dprintf("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwFrom));
							dprintf("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwFrom));
							dprintf("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwFrom));
						} else { /* MSF or millisecond */
							if (time_format == MCI_FORMAT_MSF) {
								parms->dwFrom = MCI_MSF_MINUTE(parms->dwFrom) * 60000 + MCI_MSF_SECOND(parms->dwFrom) * 1000 + MCI_MSF_FRAME(parms->dwFrom) * 1000 / 75;  
							}
							info.first = 0;
							for (int i = firstTrack; i <= lastTrack; i++) {
								if (tracks[i].position + tracks[i].length > parms->dwFrom) {
									info.first = i;
									info.from = parms->dwFrom - tracks[i].position;
									break;
								}
							}
							/* If no match is found do not play */
							if (info.first == 0) {
								command = MCI_STOP;
								plr_stop();
								return 0;
							}
							dprintf("      mapped dwFrom to track %d (%d ms)\n", info.first, info.from);
						}
						info.last = lastTrack; /* default MCI_TO */
						info.to = -1;
					}

					if (fdwCommand & MCI_TO) {
						dprintf("    dwTo:   0x%08X\n", parms->dwTo);

						if (time_format == MCI_FORMAT_TMSF) {
							info.last = MCI_TMSF_TRACK(parms->dwTo);
							info.to = MCI_TMSF_MINUTE(parms->dwTo) * 60000 + MCI_TMSF_SECOND(parms->dwTo) * 1000 + MCI_TMSF_FRAME(parms->dwTo) * 1000 / 75;

							dprintf("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwTo));
							dprintf("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwTo));
							dprintf("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwTo));
							dprintf("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwTo));
						} else { /* MSF or millisecond */
							if (time_format == MCI_FORMAT_MSF) {
								parms->dwTo = MCI_MSF_MINUTE(parms->dwTo) * 60000 + MCI_MSF_SECOND(parms->dwTo) * 1000 + MCI_MSF_FRAME(parms->dwTo) * 1000 / 75;  
							}
							info.last = lastTrack;
							info.to = -1;
							for (int i = info.first; i <= lastTrack; i++) {
								if (tracks[i].position + tracks[i].length >= parms->dwTo) {
									info.last = i;
									info.to = parms->dwTo - tracks[i].position;
									break;
								}
							}
							dprintf("      mapped dwTo to track %d (%d ms)\n", info.last, info.to);
						}
					}

					if (event) {
						if (mode != MCI_MODE_STOP) {
							command = MCI_STOP;
							plr_stop();
							while (mode != MCI_MODE_STOP) Sleep(1);
						}
						// If play time is less than 15ms (1 frame = 1000/75 ms), do not play.
						if ((fdwCommand & MCI_FROM) && (fdwCommand & MCI_TO) && (info.first == info.last) && (info.from + 15 >= info.to)) {
							if (notify) {
								notify = 0;
								SendNotifyMessageA(window, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
								dprintf("(FROM == TO) Send message but no play\n");
							}
						} else {
							command = MCI_PLAY;
							mode = MCI_MODE_PLAY;
							SetEvent(event);
						}
					}
				}
				break;
			case MCI_SEEK:
				{
					dprintf("  MCI_SEEK\n");
					command = MCI_STOP;
					plr_stop();

					if (fdwCommand & MCI_SEEK_TO_START) {
						dprintf("    MCI_SEEK_TO_START\n");
						info.first = firstTrack;
						info.from = 0;
					} else if (fdwCommand & MCI_SEEK_TO_END) {
						dprintf("    MCI_SEEK_TO_END\n");
						info.first = lastTrack;
						info.from = -1;
					} else if (fdwCommand & MCI_TO) {
						dprintf("    MCI_TO\n");
						LPMCI_SEEK_PARMS parms = (LPVOID)dwParam;
						if (time_format == MCI_FORMAT_TMSF) {
							info.first = MCI_TMSF_TRACK(parms->dwTo);
							info.from = MCI_TMSF_MINUTE(parms->dwTo) * 60000 + MCI_TMSF_SECOND(parms->dwTo) * 1000 + MCI_TMSF_FRAME(parms->dwTo) * 1000 / 75;

							dprintf("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwTo));
							dprintf("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwTo));
							dprintf("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwTo));
							dprintf("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwTo));
						} else { /* MSF or millisecond */
							if (time_format == MCI_FORMAT_MSF) {
								parms->dwTo = MCI_MSF_MINUTE(parms->dwTo) * 60000 + MCI_MSF_SECOND(parms->dwTo) * 1000 + MCI_MSF_FRAME(parms->dwTo) * 1000 / 75;  
							}
							info.first = lastTrack;
							info.from = 0;
							for (int i = info.first; i <= lastTrack; i++) {
								if (tracks[i].position + tracks[i].length >= parms->dwTo) {
									info.first = i;
									info.from = parms->dwTo - tracks[i].position;
									break;
								}
							}
							dprintf("      mapped dwTo to track %d (%d ms)\n", info.first, info.from);
						}
					}
					info.last = lastTrack;
					info.to = -1;
				}
				break;
			case MCI_STOP:
				{
					dprintf("  MCI_STOP\n");
					command = MCI_STOP;
					plr_stop(); /* Make STOP command instant. */
				}
				break;
			case MCI_PAUSE:
				{
					dprintf("  MCI_PAUSE\n");
					if (mode == MCI_MODE_PLAY) {
						plr_pause();
						mode = MCI_MODE_PAUSE;
					}
				}
				break;
			case MCI_INFO: /* Handling of MCI_INFO */
				{
					dprintf("  MCI_INFO\n");
					LPMCI_INFO_PARMS parms = (LPVOID)dwParam;

					if (fdwCommand & MCI_INFO_PRODUCT) {
						dprintf("    MCI_INFO_PRODUCT\n");
						strncpy((char*)parms->lpstrReturn, alias_s, parms->dwRetSize); /* name */
					} else if (fdwCommand & MCI_INFO_MEDIA_IDENTITY) {
						dprintf("    MCI_INFO_MEDIA_IDENTITY\n");
						memcpy((LPVOID)(parms->lpstrReturn), MEDIA_IDENTITY, parms->dwRetSize); /* 16 hexadecimal digits */
					}
				}
				break;
			case MCI_GETDEVCAPS:
				{
					dprintf("  MCI_GETDEVCAPS\n");
					LPMCI_GETDEVCAPS_PARMS parms = (LPVOID)dwParam;

					if (fdwCommand & MCI_GETDEVCAPS_ITEM) {
						switch (parms->dwItem) {
							case MCI_GETDEVCAPS_DEVICE_TYPE:
								parms->dwReturn = MCI_DEVTYPE_CD_AUDIO;
								break;
							case MCI_GETDEVCAPS_HAS_AUDIO:
							case MCI_GETDEVCAPS_CAN_EJECT:
							case MCI_GETDEVCAPS_CAN_PLAY:
								parms->dwReturn = TRUE;
								break;
							default:
								parms->dwReturn = 0;
						}
						dprintf("    MCI_GETDEVCAPS_ITEM, dwItem=0x%08X, dwReturn=0x%08X\n", parms->dwItem, parms->dwReturn);
					}
				}
				break;
			case MCI_SET:
				{
					dprintf("  MCI_SET\n");
					LPMCI_SET_PARMS parms = (LPVOID)dwParam;

					if (fdwCommand & MCI_SET_TIME_FORMAT) {
						dprintf("    MCI_SET_TIME_FORMAT\n");
						time_format = parms->dwTimeFormat;

						if (parms->dwTimeFormat == MCI_FORMAT_MILLISECONDS) {
							dprintf("      MCI_FORMAT_MILLISECONDS\n");
						} else if (parms->dwTimeFormat == MCI_FORMAT_MSF) {
							dprintf("      MCI_FORMAT_MSF\n");
						} else if (parms->dwTimeFormat == MCI_FORMAT_TMSF) {
							dprintf("      MCI_FORMAT_TMSF\n");
						} else if (parms->dwTimeFormat == MCI_FORMAT_SAMPLES) {
							dprintf("      MCI_FORMAT_SAMPLES\n");
						} else if (parms->dwTimeFormat == MCI_FORMAT_BYTES) {
							dprintf("      MCI_FORMAT_BYTES\n");
						} else if (parms->dwTimeFormat == MCI_FORMAT_HMS) {
							dprintf("      MCI_FORMAT_HMS\n");
						} else if (parms->dwTimeFormat == MCI_FORMAT_FRAMES) {
							dprintf("      MCI_FORMAT_FRAMES\n");
						}
					}
				}
				break;
			case MCI_SYSINFO: /* Handling of MCI_SYSINFO (Heavy Gear, Battlezone2, Interstate 76) */
				{
					dprintf("  MCI_SYSINFO\n");
					LPMCI_SYSINFO_PARMSA parms = (LPVOID)dwParam;

					if (fdwCommand & MCI_SYSINFO_NAME) {
						dprintf("    MCI_SYSINFO_NAME\n");
						strncpy((char*)parms->lpstrReturn, alias_s, parms->dwRetSize); /* name */
					} else if (fdwCommand & MCI_SYSINFO_QUANTITY) {
						dprintf("    MCI_SYSINFO_QUANTITY\n");
						*(DWORD*)parms->lpstrReturn = 1; /* quantity = 1 */
					}
				}
				break;
			case MCI_STATUS:
				{
					dprintf("  MCI_STATUS\n");
					LPMCI_STATUS_PARMS parms = (LPVOID)dwParam;
					parms->dwReturn = 0;

					if (fdwCommand & MCI_TRACK) {
						dprintf("    MCI_TRACK\n");
						dprintf("      dwTrack = %d\n", parms->dwTrack);
					}

					if (fdwCommand & MCI_STATUS_START) {
						dprintf("    MCI_STATUS_START\n");
					}

					if (fdwCommand & MCI_STATUS_ITEM) {
						dprintf("    MCI_STATUS_ITEM\n");

						unsigned int ms;
						switch (parms->dwItem) {
							case MCI_STATUS_LENGTH:
								dprintf("      MCI_STATUS_LENGTH\n");
								if(fdwCommand & MCI_TRACK) { /* Get track length */
									ms = tracks[parms->dwTrack].length;
								} else { /* Get full length */
									parms->dwTrack = lastTrack;
									ms = tracks[lastTrack].position + tracks[lastTrack].length;
								}
								if (time_format == MCI_FORMAT_MILLISECONDS) {
									parms->dwReturn = ms;
								} else { // WTF! MCI_FORMAT_MSF and MCI_FORMAT_TMSF both return in MSF
									parms->dwReturn = MCI_MAKE_MSF(ms/60000, ms/1000%60, ms%1000*75/1000);
								}
								break;
							case MCI_STATUS_POSITION:
								dprintf("      MCI_STATUS_POSITION\n");
								if (fdwCommand & MCI_TRACK) { /* Track position */
									parms->dwReturn = tracks[parms->dwTrack].position; // as milliseconds
									ms = 0;
								} else if (fdwCommand & MCI_STATUS_START) { /* Medium start position */
									parms->dwTrack = firstTrack;
									parms->dwReturn = 0;
									ms = 0;
								} else { /* Playing position */
									parms->dwTrack = current;
									parms->dwReturn = tracks[parms->dwTrack].position; // as milliseconds
									// FIXME: fix position for pause
									ms = mode == MCI_MODE_PLAY ? GetTickCount() - tracks[parms->dwTrack].tick : 0;
								}
								if (time_format == MCI_FORMAT_MILLISECONDS) {
									parms->dwReturn += ms;
								} else if (time_format == MCI_FORMAT_MSF) {
									ms += parms->dwReturn;
									parms->dwReturn = MCI_MAKE_MSF(ms/60000, ms/1000%60, ms%1000*75/1000);
								} else { /* MCI_FORMAT_TMSF */
									/* for CD-DA, frames(sectors) range from 0 to 74  */
									parms->dwReturn = MCI_MAKE_TMSF(parms->dwTrack, ms/60000, ms/1000%60, ms%1000*75/1000);
								}
								break;
							case MCI_STATUS_NUMBER_OF_TRACKS:
								dprintf("      MCI_STATUS_NUMBER_OF_TRACKS\n");
								parms->dwReturn = lastTrack; // including data tracks
								break;
							case MCI_STATUS_MODE:
								dprintf("      MCI_STATUS_MODE\n");
								parms->dwReturn = mode;
								break;
							case MCI_STATUS_MEDIA_PRESENT:
								dprintf("      MCI_STATUS_MEDIA_PRESENT\n");
								parms->dwReturn = TRUE;
								break;
							case MCI_STATUS_TIME_FORMAT:
								dprintf("      MCI_STATUS_TIME_FORMAT\n");
								parms->dwReturn = time_format;
								break;
							case MCI_STATUS_READY:
								dprintf("      MCI_STATUS_READY\n");
								parms->dwReturn = TRUE; /* TRUE=ready, FALSE=not ready */
								break;
							case MCI_STATUS_CURRENT_TRACK:
								dprintf("      MCI_STATUS_CURRENT_TRACK\n");
								parms->dwReturn = current;
								break;
							case MCI_CDA_STATUS_TYPE_TRACK:
								dprintf("      MCI_CDA_STATUS_TYPE_TRACK\n");
								parms->dwReturn = (parms->dwTrack >= firstTrack && parms->dwTrack <= lastTrack) ? MCI_CDA_TRACK_AUDIO : MCI_CDA_TRACK_OTHER;
								break;
						}
					}
					dprintf("  dwReturn 0x%08X\n", parms->dwReturn);
				}
				break;
			case MCI_RESUME: /* FIXME: MCICDA does not support resume? */
				{
					dprintf("  MCI_RESUME\n");
					if (mode == MCI_MODE_PAUSE) {
						mode = MCI_MODE_PLAY;
						plr_resume();
					}
				}
				break;
		}
		return 0;
	} else return relay_mciSendCommandA(IDDevice, uMsg, fdwCommand, dwParam);
}

/* MCI command strings */
/* https://docs.microsoft.com/windows/win32/multimedia/multimedia-command-strings */
MCIERROR WINAPI fake_mciSendStringA(LPCSTR cmd, LPSTR ret, UINT cchReturn, HANDLE hwndCallback)
{
	char cmdbuf[1000];
	char cmp_str[1000];

	dprintf("[MCI String = %s]\n", cmd);

	/* copy cmd into cmdbuf */
	strcpy (cmdbuf, cmd);
	/* change cmdbuf into lower case */
	for (int i = 0; cmdbuf[i]; i++)
	{
		cmdbuf[i] = tolower(cmdbuf[i]);
	}

	if (strstr(cmdbuf, "sysinfo cdaudio quantity"))
	{
		dprintf("  Returning quantity: 1\n");
		strcpy(ret, "1");
		return 0;
	}

	/* Example: "sysinfo cdaudio name 1 open" returns "cdaudio" or the alias.*/
	if (strstr(cmdbuf, "sysinfo cdaudio name"))
	{
		dprintf("  Returning name: cdaudio\n");
		sprintf(ret, "%s", alias_s);
		return 0;
	}

	/* Handle "stop cdaudio/alias" */
	sprintf(cmp_str, "stop %s", alias_s);
	if (strstr(cmdbuf, cmp_str))
	{
		fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STOP, 0, (DWORD_PTR)NULL);
		return 0;
	}

	/* Handle "pause cdaudio/alias" */
	sprintf(cmp_str, "pause %s", alias_s);
	if (strstr(cmdbuf, cmp_str))
	{
		fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PAUSE, 0, (DWORD_PTR)NULL);
		return 0;
	}

	/* Handle "resume cdaudio/alias" */
	sprintf(cmp_str, "resume %s", alias_s);
	if (strstr(cmdbuf, cmp_str))
	{
		fake_mciSendCommandA(MAGIC_DEVICEID, MCI_RESUME, 0, (DWORD_PTR)NULL);
		return 0;
	}

	/* Look for the use of an alias */
	/* Example: "open d: type cdaudio alias cd1" */
	if (strstr(cmdbuf, "type cdaudio alias"))
	{
		char *tmp_s = strrchr(cmdbuf, ' ');
		if (tmp_s && tmp_s[1])
		{
			sprintf(alias_s, "%s", tmp_s+1);
		}
		// fake_mciSendCommandA(MAGIC_DEVICEID, MCI_OPEN, 0, (DWORD_PTR)NULL);
		return 0;
	}

	if (strstr(cmdbuf, "open cdaudio"))
	{
		// fake_mciSendCommandA(MAGIC_DEVICEID, MCI_OPEN, 0, (DWORD_PTR)NULL);
		return 0;
	}

	/* reset alias with "close alias" string */
	sprintf(cmp_str, "close %s", alias_s);
	if (strstr(cmdbuf, cmp_str))
	{
		strcpy(alias_s, alias_def);
		return 0;
	}

	/* Handle "seek cdaudio/alias" */
	sprintf(cmp_str, "seek %s", alias_s);
	if (strstr(cmdbuf, cmp_str))
	{
		fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STOP, 0, (DWORD_PTR)NULL);

		int track;
		if (strstr(cmdbuf, "to start"))
		{
			info.first = 0;
		}
		else if (strstr(cmdbuf, "to end"))
		{
			info.first = lastTrack + 1;
		}
		else if (sscanf(cmdbuf, "seek %*s to %d", &track) == 1) // TMSF only
		{
			info.first = track;
		}
		info.from = 0;
		return 0;
	}

	/* Handle "set cdaudio/alias time format" */
	sprintf(cmp_str, "set %s time format", alias_s);
	if (strstr(cmdbuf, cmp_str)){
		MCI_SET_PARMS parms;
		if (strstr(cmdbuf, "milliseconds"))
		{
			parms.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			return 0;
		}
		if (strstr(cmdbuf, "tmsf"))
		{
			parms.dwTimeFormat = MCI_FORMAT_TMSF;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			return 0;
		}
		if (strstr(cmdbuf, "msf"))
		{
			parms.dwTimeFormat = MCI_FORMAT_MSF;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			return 0;
		}
	}

	/* Handle "status cdaudio/alias" */
	sprintf(cmp_str, "status %s", alias_s);
	if (strstr(cmdbuf, cmp_str)){
		MCI_STATUS_PARMS parms;
		if (strstr(cmdbuf, "number of tracks"))
		{
			dprintf("  Returning number of tracks (%d)\n", lastTrack);
			sprintf(ret, "%u", lastTrack); // including data tracks
			return 0;
		}
		int track = 0;
		if (sscanf(cmdbuf, "status %*s length track %d", &track) == 1)
		{
			parms.dwItem = MCI_STATUS_LENGTH;
			parms.dwTrack = track;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			if (time_format == MCI_FORMAT_MILLISECONDS) {
				sprintf(ret, "%d", parms.dwReturn);
			} else {
				sprintf(ret, "%02d:%02d:00", MCI_MSF_MINUTE(parms.dwReturn), MCI_MSF_SECOND(parms.dwReturn));
			}
			return 0;
		}
		if (strstr(cmdbuf, "length"))
		{
			parms.dwItem = MCI_STATUS_LENGTH;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			if (time_format == MCI_FORMAT_MILLISECONDS) {
				sprintf(ret, "%d", parms.dwReturn);
			} else {
				sprintf(ret, "%02d:%02d:00", MCI_MSF_MINUTE(parms.dwReturn), MCI_MSF_SECOND(parms.dwReturn));
			}
			return 0;
		}
		if (sscanf(cmdbuf, "status %*s position track %d", &track) == 1)
		{
			parms.dwItem = MCI_STATUS_POSITION;
			parms.dwTrack = track;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(ret, "%d", parms.dwReturn);
			return 0;
		}
		if (strstr(cmdbuf, "position"))
		{
			parms.dwItem = MCI_STATUS_POSITION;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			if (time_format == MCI_FORMAT_MILLISECONDS) {
				sprintf(ret, "%d", parms.dwReturn);
			} else if (time_format == MCI_FORMAT_MSF) {
				sprintf(ret, "%02d:%02d:%02d", MCI_MSF_MINUTE(parms.dwReturn), MCI_MSF_SECOND(parms.dwReturn), MCI_MSF_FRAME(parms.dwReturn));
			} else { /* TMSF */
				sprintf(ret, "%02d:%02d:%02d:%02d", MCI_TMSF_TRACK(parms.dwReturn), MCI_TMSF_MINUTE(parms.dwReturn), MCI_TMSF_SECOND(parms.dwReturn), MCI_TMSF_FRAME(parms.dwReturn));
			}
			return 0;
		}
		if (strstr(cmdbuf, "media present"))
		{
			strcpy(ret, "TRUE");
			return 0;
		}
		/* Add: Mode handling */
		if (strstr(cmdbuf, "mode"))
		{
			switch (mode) {
				case MCI_MODE_PLAY:
					dprintf("   -> playing\n");
					strcpy(ret, "playing");
					break;
				case MCI_MODE_PAUSE:
					dprintf("   -> paused\n");
					strcpy(ret, "paused");
					break;
				default:
					dprintf("   -> stopped\n");
					strcpy(ret, "stopped");
					break;
			}
			return 0;
		}
	}

	/* Handle "play cdaudio/alias" */
	int from = 0, to = 0;
	sprintf(cmp_str, "play %s", alias_s);
	if (strstr(cmdbuf, cmp_str)){
		MCI_PLAY_PARMS parms = {0};

		if (strstr(cmdbuf, "notify")){
			notify = 1; /* storing the notify request */
			window = (HWND)hwndCallback;
		}
		
		// Handle time format (MM:SS:FF) - check these FIRST before integer formats
		int from_min, from_sec, from_frame, to_min, to_sec, to_frame;
		if (sscanf(cmdbuf, "play %*s from %d:%d:%d to %d:%d:%d", &from_min, &from_sec, &from_frame, &to_min, &to_sec, &to_frame) == 6)
		{
			if (time_format == MCI_FORMAT_MSF) {
				parms.dwFrom = MCI_MAKE_MSF(from_min, from_sec, from_frame);
				parms.dwTo = MCI_MAKE_MSF(to_min, to_sec, to_frame);
			} else {
				return MCIERR_UNSUPPORTED_FUNCTION;
			}
			
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO, (DWORD_PTR)&parms);
			return 0;
		}
		
		// Handle time format (MM:SS:FF) for from only
		if (sscanf(cmdbuf, "play %*s from %d:%d:%d", &from_min, &from_sec, &from_frame) == 3)
		{
			if (time_format == MCI_FORMAT_MSF) {
				parms.dwFrom = MCI_MAKE_MSF(from_min, from_sec, from_frame);
			} else {
				return MCIERR_UNSUPPORTED_FUNCTION;
			}
			
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
			return 0;
		}
		
		// Handle time format (MM:SS:FF) for to only
		if (sscanf(cmdbuf, "play %*s to %d:%d:%d", &to_min, &to_sec, &to_frame) == 3)
		{
			if (time_format == MCI_FORMAT_MSF) {
				parms.dwTo = MCI_MAKE_MSF(to_min, to_sec, to_frame);
			} else {
				return MCIERR_UNSUPPORTED_FUNCTION;
			}
			
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
			return 0;
		}
		
		// Handle integer formats AFTER time formats
		if (sscanf(cmdbuf, "play %*s from %d to %d", &from, &to) == 2)
		{
			parms.dwFrom = from;
			parms.dwTo = to;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO, (DWORD_PTR)&parms);
			return 0;
		}
		if (sscanf(cmdbuf, "play %*s from %d", &from) == 1)
		{
			parms.dwFrom = from;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
			return 0;
		}
		if (sscanf(cmdbuf, "play %*s to %d", &to) == 1)
		{
			parms.dwTo = to;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
			return 0;
		}

		parms.dwFrom = info.first;
		fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
		return 0;
	}

	return relay_mciSendStringA(cmd, ret, cchReturn, hwndCallback);
	/* return 0; */
}

UINT WINAPI fake_auxGetNumDevs()
{
	dprintf("fake_auxGetNumDevs() = 1\n");
	return 1;
}

MMRESULT WINAPI fake_auxGetDevCapsA(UINT_PTR uDeviceID, LPAUXCAPS lpCaps, UINT cbCaps)
{
	dprintf("fake_auxGetDevCapsA(uDeviceID=%08X, lpCaps=%p, cbCaps=%08X\n", uDeviceID, lpCaps, cbCaps);

	lpCaps->wMid = 2 /*MM_CREATIVE*/;
	lpCaps->wPid = 401 /*MM_CREATIVE_AUX_CD*/;
	lpCaps->vDriverVersion = 1;
	strcpy(lpCaps->szPname, "ogg-winmm virtual CD");
	lpCaps->wTechnology = AUXCAPS_CDAUDIO;
	lpCaps->dwSupport = AUXCAPS_LRVOLUME | AUXCAPS_VOLUME;

	return MMSYSERR_NOERROR;
}

MMRESULT WINAPI fake_auxGetVolume(UINT uDeviceID, LPDWORD lpdwVolume)
{
	*lpdwVolume = auxVol;
	dprintf("fake_auxGetVolume(uDeviceId=%08X, dwVolume=%08X)\n", uDeviceID, *lpdwVolume);
	return MMSYSERR_NOERROR;
}

MMRESULT WINAPI fake_auxSetVolume(UINT uDeviceID, DWORD dwVolume)
{
	dprintf("fake_auxSetVolume(uDeviceId=%08X, dwVolume=%08X)\n", uDeviceID, dwVolume);

	auxVol = dwVolume;
	plr_volume((auxVol & 0xFFFF) * cddaVol / 65535, (auxVol >> 16) * cddaVol / 65535);

	return MMSYSERR_NOERROR;
}

