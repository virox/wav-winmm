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
#include <ctype.h>
#include <dirent.h>
#include <time.h>
#include "player.h"
#include "stub.h"

#define MAGIC_DEVICEID 0xCDDA
#define MAX_TRACKS 99

#ifdef _DEBUG
#define dprintf(...) if (fh) { fprintf(fh, __VA_ARGS__); fflush(NULL); }
FILE *fh = NULL;
#else
#define dprintf(...)
#endif

struct track_info
{
	char path[MAX_PATH];    /* full path to ogg */
	unsigned int length;    /* seconds */
	unsigned int position;  /* seconds */
	clock_t tick;           /* clock tick at play start */
};

struct play_info
{
	int first;
	int last;
};

static struct track_info tracks[MAX_TRACKS];
static struct play_info info = { -1, -1 };

HANDLE player = NULL;
HANDLE event = NULL;
HWND window = NULL;
const char alias_def[] = "cdaudio";
char alias_s[100] = "cdaudio";
char music_path[MAX_PATH];

int command = 0;
int playing = 0;
int notify = 0;
int current  = 1;
int firstTrack = -1;
int lastTrack = 0;
int numTracks = 0;
int time_format = MCI_FORMAT_MSF;

DWORD auxVol = -1;
int cddaVol = -1;
int midiVol = -1;
int waveVol = -1;

/* NOTE: The player is currently incapable of playing tracks from a specified
 * position. Instead it plays whole tracks only. Previous pause logic using
 * Sleep caused crackling sound and was not acceptable. Future plan is to add
 * proper seek commands to support playback from arbitrary positions on the track.
 */

int WINAPI player_main(void *unused)
{
	while (WaitForSingleObject(event, INFINITE) == 0) {
		int first = info.first < firstTrack ? firstTrack : info.first;
		int last = info.last - 1 < first ? first : info.last - 1; /* -1 for plr logic */
		current = first;
		dprintf("[Thread] From %d to %d\n", first, last);

		while (command == MCI_PLAY && current <= last) {
			dprintf("[Thread] Current track %s\n", tracks[current].path);
			tracks[current].tick = clock();
			playing = 1;
			plr_play(tracks[current].path);

			while (command == MCI_PLAY) {
				if (plr_pump() == 0) {
					current++;
					break;
				}

			}
		}

		/* Sending notify successful message:*/
		if (command == MCI_PLAY && notify) {
			notify = 0;
			SendNotifyMessageA(window, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
			/* NOTE: Notify message after successful playback is not working in Vista+.
			   MCI_STATUS_MODE does not update to show that the track is no longer playing.
			   Bug or broken design in mcicda.dll (also noted by the Wine team) */
			dprintf("[Thread] Send MCI_NOTIFY_SUCCESSFUL message\n");
		}

		playing = 0;
		plr_reset();
		if (command == MCI_DELETE) break;
	}
	
	CloseHandle(event);
	event = NULL;
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
#ifdef _DEBUG
		fh = fopen("winmm.log", "w");
#endif
		GetModuleFileName(hinstDLL, music_path, sizeof(music_path));

		char *last = strrchr(music_path, '.');
		if (last) {
			*last = '\0';
			strcat(last, ".ini");

			cddaVol = GetPrivateProfileInt("OGG-WinMM", "CDDAVolume", -1, music_path);
			midiVol = GetPrivateProfileInt("OGG-WinMM", "MIDIVolume", -1, music_path);
			waveVol = GetPrivateProfileInt("OGG-WinMM", "WAVEVolume", -1, music_path);

			/* 100% volume is equivalent to no override */
			if (cddaVol < 0 || cddaVol > 99 ) cddaVol = -1;
			if (midiVol < 0 || midiVol > 99 ) midiVol = -1;
			if (waveVol < 0 || waveVol > 99 ) waveVol = -1;

			plr_volume(cddaVol);
			stub_midivol(midiVol);
			stub_wavevol(waveVol);
		}

		last = strrchr(music_path, '\\');
		if (last) *last = '\0';
		strcat(music_path, "\\MUSIC");

		dprintf("ogg-winmm music directory is %s\n", music_path);
		dprintf("ogg-winmm searching tracks...\n");

		memset(tracks, 0, sizeof tracks);
		unsigned int position = 0;

		for (int i = 1; i < MAX_TRACKS; i++) /* "Changed: int i = 0" to "1" we can skip track00.ogg" */
		{
			snprintf(tracks[i].path, sizeof(tracks[i].path), "%s\\Track%02d.ogg", music_path, i);
			tracks[i].length = plr_length(tracks[i].path);
			tracks[i].position = position;

			if (firstTrack == -1)
			{
				firstTrack = i;
				if (i != 1) numTracks = i - 1; /* Take into account data tracks before music tracks */
			}

			dprintf("Track %02d: %02d:%02d @ %d seconds\n", i, tracks[i].length / 60, tracks[i].length % 60, tracks[i].position);
			numTracks++;
			lastTrack = i;
			position += tracks[i].length;
		}
		dprintf("Emulating total of %d CD tracks.\n\n", numTracks);

		if (firstTrack != -1) {
			event = CreateEvent(NULL, FALSE, FALSE, NULL);
			player = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)player_main, NULL, 0, NULL);
		}
	}

	if (fdwReason == DLL_PROCESS_DETACH)
	{
#ifdef _DEBUG
		if (fh)
		{
			fclose(fh);
			fh = NULL;
		}
#endif
		command = MCI_DELETE;
		if (event) SetEvent(event);
	}

	return TRUE;
}

/* MCI commands */
/* https://docs.microsoft.com/windows/win32/multimedia/multimedia-commands */
MCIERROR WINAPI fake_mciSendCommandA(MCIDEVICEID IDDevice, UINT uMsg, DWORD_PTR fdwCommand, DWORD_PTR dwParam)
{
	char cmdbuf[1024];
	dprintf("mciSendCommandA(IDDevice=%p, uMsg=%p, fdwCommand=%p, dwParam=%p)\n", IDDevice, uMsg, fdwCommand, dwParam);

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
	} else if (IDDevice == MAGIC_DEVICEID || IDDevice == 0 || IDDevice == 0xFFFFFFFF) {
		switch (uMsg) {
			case MCI_CLOSE:
				{
					dprintf("  MCI_CLOSE\n");
					/* NOTE: MCI_CLOSE does stop the music in Vista+ but the original behaviour did not
					   it only closed the handle to the opened device. You could still send MCI commands
					   to a default cdaudio device but if you had used an alias you needed to re-open it.
					   In addition WinXP had a bug where after MCI_CLOSE the device would be unresponsive. */
				}
				break;
			case MCI_PLAY:
				{
					dprintf("  MCI_PLAY\n");
					LPMCI_PLAY_PARMS parms = (LPVOID)dwParam;

					if (fdwCommand & MCI_FROM) {
						dprintf("    dwFrom: %d\n", parms->dwFrom);

						/* FIXME: rounding to nearest track */
						if (time_format == MCI_FORMAT_TMSF) {
							info.first = MCI_TMSF_TRACK(parms->dwFrom);

							dprintf("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwFrom));
							dprintf("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwFrom));
							dprintf("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwFrom));
							dprintf("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwFrom));
						} else { /* MSF or Milliseconds */
							if (time_format == MCI_FORMAT_MSF) {
								parms->dwFrom = MCI_MSF_MINUTE(parms->dwFrom) * 60 + MCI_MSF_SECOND(parms->dwFrom);  
							} else {
								parms->dwFrom /= 1000;
							}
							info.first = 0;
							for (int i = firstTrack; i <= lastTrack; i++) {
								if (tracks[i].position + tracks[i].length > parms->dwFrom) {
									info.first = i;
									break;
								}
							}
							/* If no match is found do not play */
							if (info.first == 0) {
								command = MCI_STOP;
								plr_stop();
								return 0;
							}
							dprintf("      mapped dwFrom to track %d\n", info.first);
						}
						if (info.first < firstTrack) info.first = firstTrack;
						else if (info.first > lastTrack) info.first = lastTrack;
						info.last = lastTrack; /* default MCI_TO */
					}

					if (fdwCommand & MCI_TO) {
						dprintf("    dwTo:   %d\n", parms->dwTo);

						if (time_format == MCI_FORMAT_TMSF) {
							info.last = MCI_TMSF_TRACK(parms->dwTo);

							dprintf("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwTo));
							dprintf("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwTo));
							dprintf("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwTo));
							dprintf("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwTo));
						} else { /* MSF or Milliseconds */
							if (time_format == MCI_FORMAT_MSF) {
								parms->dwTo = MCI_MSF_MINUTE(parms->dwTo) * 60 + MCI_MSF_SECOND(parms->dwTo);  
							} else {
								parms->dwTo /= 1000;
							}
							info.last = lastTrack;
							for (int i = info.first; i <= lastTrack; i++) {
								if (tracks[i].position + tracks[i].length >= parms->dwTo) {
									info.last = i;
									break;
								}
							}
							dprintf("      mapped dwTo to track %d\n", info.last);
						}
						if (info.last < info.first) info.last = info.first;
						else if (info.last > lastTrack) info.last = lastTrack;
					}

					if ((info.first && (fdwCommand & MCI_FROM)) || (info.last && (fdwCommand & MCI_TO))) {
						if (playing) {
							command = MCI_STOP;
							plr_stop();
							while (playing) {
								Sleep(0);
							}
						}
						if (event) {
							command = MCI_PLAY;
							SetEvent(event);
						}
					}
				}
				break;
			case MCI_STOP:
				{
					dprintf("  MCI_STOP\n");
					command = MCI_STOP;
					plr_stop(); /* Make STOP command instant. */
				}
				break;
			case MCI_PAUSE: /* FIXME: MCICDA does not support resume, so pause should be equivalent to stop */
				{
					dprintf("  MCI_PAUSE\n");
					command = MCI_STOP;
					plr_stop();
				}
				break;
			case MCI_INFO: /* Handling of MCI_INFO */
				{
					dprintf("  MCI_INFO\n");
					LPMCI_INFO_PARMS parms = (LPVOID)dwParam;

					if (fdwCommand & MCI_INFO_PRODUCT) {
						dprintf("    MCI_INFO_PRODUCT\n");
						strncpy((char*)parms->lpstrReturn, alias_s, parms->dwRetSize); /* name */
					}

					if (fdwCommand & MCI_INFO_MEDIA_IDENTITY) {
						dprintf("    MCI_INFO_MEDIA_IDENTITY\n");
						memcpy((LPVOID)(parms->lpstrReturn), "0099CDDA0099CDDA", parms->dwRetSize); /* 16 hexadecimal digits */
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
					}

					if (fdwCommand & MCI_SYSINFO_QUANTITY) {
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

						switch (parms->dwItem) {
							case MCI_STATUS_LENGTH:
								dprintf("      MCI_STATUS_LENGTH\n");
								if(fdwCommand & MCI_TRACK) { /* Get track length */
									unsigned int seconds = tracks[parms->dwTrack].length;
									if (time_format == MCI_FORMAT_MILLISECONDS) {
										parms->dwReturn = seconds * 1000;
									} else {
										parms->dwReturn = MCI_MAKE_MSF(seconds/60, seconds%60, 0);
									}
								} else { /* Get full length */
									unsigned int seconds = tracks[lastTrack].position + tracks[lastTrack].length;
									if (time_format == MCI_FORMAT_MILLISECONDS) {
										parms->dwReturn = seconds * 1000;
									} else {
										parms->dwReturn = MCI_MAKE_MSF(seconds/60, seconds%60, 0);
									}
								}
								break;
							case MCI_STATUS_POSITION:
								dprintf("      MCI_STATUS_POSITION\n");
								if (fdwCommand & MCI_TRACK) { /* Track position */
									unsigned int seconds = tracks[parms->dwTrack].position;
									if (time_format == MCI_FORMAT_MILLISECONDS) {
										parms->dwReturn = seconds * 1000;
									} else if (time_format == MCI_FORMAT_MSF) {
										parms->dwReturn = MCI_MAKE_MSF(seconds/60, seconds%60, 0);
									} else { /* TMSF */
										parms->dwReturn = MCI_MAKE_TMSF(parms->dwTrack, 0, 0, 0);
									}
								} else if (fdwCommand & MCI_STATUS_START) { /* Medium start position */
									if (time_format == MCI_FORMAT_TMSF) {
										parms->dwReturn = MCI_MAKE_TMSF(firstTrack, 0, 0, 0);
									} else {
										parms->dwReturn = 0;
									}
								} else { /* Playing position */
									unsigned int ms = playing ? (clock() - tracks[current].tick) * 1000 / CLOCKS_PER_SEC : 0;
									if (time_format == MCI_FORMAT_MILLISECONDS) {
										parms->dwReturn = ms;
									} else if (time_format == MCI_FORMAT_MSF) {
										parms->dwReturn = MCI_MAKE_MSF(ms/60000%100, ms%60000/1000, ms%1000/13.5);
									} else { /* TMSF */
										/* for CD-DA, frames(sectors) range from 0 to 74  */
										parms->dwReturn = MCI_MAKE_TMSF(current, ms/60000%100, ms%60000/1000, ms%1000/13.5);
									}
								}
								break;
							case MCI_STATUS_NUMBER_OF_TRACKS:
								dprintf("      MCI_STATUS_NUMBER_OF_TRACKS\n");
								parms->dwReturn = numTracks;
								break;
							case MCI_STATUS_MODE:
								dprintf("      MCI_STATUS_MODE\n");
								dprintf("        we are %s\n", playing ? "playing" : "NOT playing");
								parms->dwReturn = playing ? MCI_MODE_PLAY : MCI_MODE_STOP;
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
					dprintf("  dwReturn %d\n", parms->dwReturn);
				}
				break;
			case MCI_RESUME: /* FIXME: MCICDA does not support resume, so resume should be equivalent to play */
				{
					dprintf("  MCI_RESUME\n");
					if (event) {
						command = MCI_PLAY;
						SetEvent(event);
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
		fake_mciSendCommandA(MAGIC_DEVICEID, MCI_OPEN, 0, (DWORD_PTR)NULL);
		return 0;
	}

	if (strstr(cmdbuf, "open cdaudio"))
	{
		fake_mciSendCommandA(MAGIC_DEVICEID, MCI_OPEN, 0, (DWORD_PTR)NULL);
		return 0;
	}

	/* reset alias with "close alias" string */
	sprintf(cmp_str, "close %s", alias_s);
	if (strstr(cmdbuf, cmp_str))
	{
		sprintf(alias_s, alias_def);
		return 0;
	}

	/* Handle "set cdaudio/alias time format" */
	sprintf(cmp_str, "set %s time format", alias_s);
	if (strstr(cmdbuf, cmp_str)){
		if (strstr(cmdbuf, "milliseconds"))
		{
			static MCI_SET_PARMS parms;
			parms.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			return 0;
		}
		if (strstr(cmdbuf, "tmsf"))
		{
			static MCI_SET_PARMS parms;
			parms.dwTimeFormat = MCI_FORMAT_TMSF;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			return 0;
		}
		if (strstr(cmdbuf, "msf"))
		{
			static MCI_SET_PARMS parms;
			parms.dwTimeFormat = MCI_FORMAT_MSF;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			return 0;
		}
	}

	/* Handle "status cdaudio/alias" */
	sprintf(cmp_str, "status %s", alias_s);
	if (strstr(cmdbuf, cmp_str)){
		if (strstr(cmdbuf, "number of tracks"))
		{
			dprintf("  Returning number of tracks (%d)\n", numTracks);
			sprintf(ret, "%d", numTracks);
			return 0;
		}
		int track = 0;
		if (sscanf(cmdbuf, "status %*s length track %d", &track) == 1)
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_LENGTH;
			parms.dwTrack = track;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(ret, "%d", parms.dwReturn);
			return 0;
		}
		if (strstr(cmdbuf, "length"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_LENGTH;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			sprintf(ret, "%d", parms.dwReturn);
			return 0;
		}
		if (sscanf(cmdbuf, "status %*s position track %d", &track) == 1)
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_POSITION;
			parms.dwTrack = track;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			sprintf(ret, "%d", parms.dwReturn);
			return 0;
		}
		if (strstr(cmdbuf, "position"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_POSITION;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			sprintf(ret, "%d", parms.dwReturn);
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
			if (playing) {
				dprintf("   -> playing\n");
				strcpy(ret, "playing");
			}
			else{
				dprintf("   -> stopped\n");
				strcpy(ret, "stopped");
			}
			return 0;
		}
	}

	/* Handle "play cdaudio/alias" */
	int from = -1, to = -1;
	sprintf(cmp_str, "play %s", alias_s);
	if (strstr(cmdbuf, cmp_str)){
		if (strstr(cmdbuf, "notify")){
			notify = 1; /* storing the notify request */
			window = (HWND)hwndCallback;
		}
		if (sscanf(cmdbuf, "play %*s from %d to %d", &from, &to) == 2)
		{
			static MCI_PLAY_PARMS parms;
			parms.dwFrom = from;
			parms.dwTo = to;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO, (DWORD_PTR)&parms);
			return 0;
		}
		if (sscanf(cmdbuf, "play %*s from %d", &from) == 1)
		{
			static MCI_PLAY_PARMS parms;
			parms.dwFrom = from;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
			return 0;
		}
		if (sscanf(cmdbuf, "play %*s to %d", &to) == 1)
		{
			static MCI_PLAY_PARMS parms;
			parms.dwTo = to;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_TO, (DWORD_PTR)&parms);
			return 0;
		}
	}

	return relay_mciSendStringA(cmd, ret, cchReturn, hwndCallback);
	/* return 0; */
}

UINT WINAPI fake_auxGetNumDevs()
{
	dprintf("fake_auxGetNumDevs()\n");
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
	lpCaps->dwSupport = AUXCAPS_VOLUME;

	return MMSYSERR_NOERROR;
}

MMRESULT WINAPI fake_auxGetVolume(UINT uDeviceID, LPDWORD lpdwVolume)
{
	dprintf("fake_auxGetVolume(uDeviceId=%08X, lpdwVolume=%p)\n", uDeviceID, lpdwVolume);

	*lpdwVolume = auxVol;
	return MMSYSERR_NOERROR;
}

MMRESULT WINAPI fake_auxSetVolume(UINT uDeviceID, DWORD dwVolume)
{
	dprintf("fake_auxSetVolume(uDeviceId=%08X, dwVolume=%08X)\n", uDeviceID, dwVolume);

	auxVol = dwVolume;
	/* Use left channel vol to control both channels */
	if (cddaVol == -1) plr_volume((auxVol & 0xFFFF) * 100 / 65535);

	return MMSYSERR_NOERROR;
}

