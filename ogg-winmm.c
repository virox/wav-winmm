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

/* Code revised by DD (2020) */

#include <windows.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include "player.h"

#define MAGIC_DEVICEID 0xBEEF
#define MAX_TRACKS 99

struct track_info
{
    char path[MAX_PATH];    /* full path to ogg */
    unsigned int length;    /* seconds */
    unsigned int position;  /* seconds */
};

static struct track_info tracks[MAX_TRACKS];

struct play_info
{
    int first;
    int last;
};

#ifdef _DEBUG
    #define dprintf(...) if (fh) { fprintf(fh, __VA_ARGS__); fflush(NULL); }
    FILE *fh = NULL;
#else
    #define dprintf(...)
#endif

int paused = 0;
int notify = 0;
int playing = 0;
HANDLE player = NULL;
int firstTrack = -1;
int lastTrack = 0;
int numTracks = 1; /* +1 for data track on mixed mode cd's */
char music_path[2048];
int time_format = MCI_FORMAT_TMSF;
CRITICAL_SECTION cs;
char alias_s[100] = "cdaudio";

int player_main(struct play_info *info)
{
    int first = info->first;
    int last = info->last;
    int current = first;

    playing = 1;

    while (current <= last && playing)
    {
        dprintf("Next track: %s\r\n", tracks[current].path);
        playing = plr_play(tracks[current].path);

        while (1)
        {
            if (paused)
            {
                Sleep(100);
                continue;
            }
            if (plr_pump() == 0)
                break;

            if (!playing)
            {
                return 0;
            }
        }

        /* Commented out to repeat track after done playing. */
        /* current++; */

        /* Sending notify successful message:*/
        if(notify == 1)
        {
            dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
            SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, 0xBEEF);
            notify = 0;
            /* NOTE: Notify message after successful playback is not working in Vista+ */
            /* Bug or broken design in mcicda.dll (also noted by the Wine team) */
        }
    }

    playing = 0;
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
#ifdef _DEBUG
        fh = fopen("winmm.log", "w"); /* Renamed to .log*/
#endif
        GetModuleFileName(hinstDLL, music_path, sizeof music_path);

        memset(tracks, 0, sizeof tracks);

        InitializeCriticalSection(&cs);

        char *last = strrchr(music_path, '\\');
        if (last)
        {
            *last = '\0';
        }
        strncat(music_path, "\\MUSIC", sizeof music_path - 1);

        dprintf("ogg-winmm music directory is %s\r\n", music_path);
        dprintf("ogg-winmm searching tracks...\r\n");

        unsigned int position = 0;

        for (int i = 1; i < MAX_TRACKS; i++) /* "Changed: int i = 0" to "1" we can skip track00.ogg" */
        {
            snprintf(tracks[i].path, sizeof tracks[i].path, "%s\\Track%02d.ogg", music_path, i);
            tracks[i].length = plr_length(tracks[i].path);
            tracks[i].position = position;

            if (tracks[i].length < 4)
            {
                tracks[i].path[0] = '\0';
                position += 4; /* missing tracks are 4 second data tracks for us */
            }
            else
            {
                if (firstTrack == -1)
                {
                    firstTrack = i;
                }
                if(i == numTracks) numTracks -= 1; /* Take into account pure music cd's starting with track01.ogg */

                dprintf("Track %02d: %02d:%02d @ %d seconds\r\n", i, tracks[i].length / 60, tracks[i].length % 60, tracks[i].position);
                numTracks++;
                lastTrack = i;
                position += tracks[i].length;
            }
        }

        dprintf("Emulating total of %d CD tracks.\r\n\r\n", numTracks);
    }

#ifdef _DEBUG
    if (fdwReason == DLL_PROCESS_DETACH)
    {
        if (fh)
        {
            fclose(fh);
            fh = NULL;
        }
    }
#endif

    return TRUE;
}

/* MCI commands */
/* https://docs.microsoft.com/windows/win32/multimedia/multimedia-commands */
MCIERROR WINAPI fake_mciSendCommandA(MCIDEVICEID IDDevice, UINT uMsg, DWORD_PTR fdwCommand, DWORD_PTR dwParam)
{
    char cmdbuf[1024];

    dprintf("mciSendCommandA(IDDevice=%p, uMsg=%p, fdwCommand=%p, dwParam=%p)\r\n", IDDevice, uMsg, fdwCommand, dwParam);

    if (fdwCommand & MCI_NOTIFY)
    {
        dprintf("  MCI_NOTIFY\r\n");
        notify = 1; /* storing the notify request */
    }

    if (fdwCommand & MCI_WAIT)
    {
        dprintf("  MCI_WAIT\r\n");
    }

    if (uMsg == MCI_OPEN)
    {
        LPMCI_OPEN_PARMS parms = (LPVOID)dwParam;

        dprintf("  MCI_OPEN\r\n");

        if (fdwCommand & MCI_OPEN_ALIAS)
        {
            dprintf("    MCI_OPEN_ALIAS\r\n");
        }

        if (fdwCommand & MCI_OPEN_SHAREABLE)
        {
            dprintf("    MCI_OPEN_SHAREABLE\r\n");
        }

        if (fdwCommand & MCI_OPEN_TYPE_ID)
        {
            dprintf("    MCI_OPEN_TYPE_ID\r\n");

            if (LOWORD(parms->lpstrDeviceType) == MCI_DEVTYPE_CD_AUDIO)
            {
                dprintf("  Returning magic device id for MCI_DEVTYPE_CD_AUDIO\r\n");
                parms->wDeviceID = MAGIC_DEVICEID;
                return 0;
            }
        }

        if (fdwCommand & MCI_OPEN_TYPE && !(fdwCommand & MCI_OPEN_TYPE_ID))
        {
            dprintf("    MCI_OPEN_TYPE\r\n");
            dprintf("        -> %s\r\n", parms->lpstrDeviceType);

            if (strcmp(parms->lpstrDeviceType, "cdaudio") == 0)
            {
                dprintf("  Returning magic device id for MCI_DEVTYPE_CD_AUDIO\r\n");
                parms->wDeviceID = MAGIC_DEVICEID;
                return 0;
            }
        }

    }

    if (IDDevice == MAGIC_DEVICEID || IDDevice == 0 || IDDevice == 0xFFFFFFFF)
    {
        if (uMsg == MCI_SET)
        {
            LPMCI_SET_PARMS parms = (LPVOID)dwParam;

            dprintf("  MCI_SET\r\n");

            if (fdwCommand & MCI_SET_TIME_FORMAT)
            {
                dprintf("    MCI_SET_TIME_FORMAT\r\n");

                time_format = parms->dwTimeFormat;

                if (parms->dwTimeFormat == MCI_FORMAT_BYTES)
                {
                    dprintf("      MCI_FORMAT_BYTES\r\n");
                }

                if (parms->dwTimeFormat == MCI_FORMAT_FRAMES)
                {
                    dprintf("      MCI_FORMAT_FRAMES\r\n");
                }

                if (parms->dwTimeFormat == MCI_FORMAT_HMS)
                {
                    dprintf("      MCI_FORMAT_HMS\r\n");
                }

                if (parms->dwTimeFormat == MCI_FORMAT_MILLISECONDS)
                {
                    dprintf("      MCI_FORMAT_MILLISECONDS\r\n");
                }

                if (parms->dwTimeFormat == MCI_FORMAT_MSF)
                {
                    dprintf("      MCI_FORMAT_MSF\r\n");
                }

                if (parms->dwTimeFormat == MCI_FORMAT_SAMPLES)
                {
                    dprintf("      MCI_FORMAT_SAMPLES\r\n");
                }

                if (parms->dwTimeFormat == MCI_FORMAT_TMSF)
                {
                    dprintf("      MCI_FORMAT_TMSF\r\n");
                }
            }
        }

        if (uMsg == MCI_CLOSE)
        {
            dprintf("  MCI_CLOSE\r\n");
            
            /*Let's not kill the player thread.*/
            /*
            if (player)
            {
                TerminateThread(player, 0);
            }

            playing = 0;
            player = NULL;
            */
            /* NOTE: MCI_CLOSE does stop the music in Vista+ but the original behaviour did not */
            /* it only closed the handle to the opened device. You could still send MCI commands */
            /* to a default cdaudio device but if you had used an alias you needed to re-open it. */
        }

        if (uMsg == MCI_PLAY)
        {
            LPMCI_PLAY_PARMS parms = (LPVOID)dwParam;
            static struct play_info info = { -1, -1 };

            dprintf("  MCI_PLAY\r\n");
            paused = 0;

            if (fdwCommand & MCI_FROM)
            {
                dprintf("    dwFrom: %d\r\n", parms->dwFrom);

                /* FIXME: rounding to nearest track */
                if (time_format == MCI_FORMAT_TMSF)
                {
                    info.first = MCI_TMSF_TRACK(parms->dwFrom);

                    dprintf("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwFrom));
                    dprintf("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwFrom));
                    dprintf("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwFrom));
                    dprintf("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwFrom));
                }
                else if (time_format == MCI_FORMAT_MILLISECONDS)
                {
                    info.first = 0;

                    for (int i = 0; i < MAX_TRACKS; i++)
                    {
                        /* FIXME: take closest instead of absolute */
                        if (tracks[i].position == parms->dwFrom / 1000)
                        {
                            info.first = i;
                        }
                    }

                    dprintf("      mapped milliseconds to %d\n", info.first);
                }
                else
                {
                    /* FIXME: not really */
                    info.first = parms->dwFrom;
                }

                if (info.first < firstTrack)
                    info.first = firstTrack;

                if (info.first > lastTrack)
                    info.first = lastTrack;

                info.last = info.first;
            }

            if (fdwCommand & MCI_TO)
            {
                dprintf("    dwTo:   %d\r\n", parms->dwTo);

                if (time_format == MCI_FORMAT_TMSF)
                {
                    info.last = MCI_TMSF_TRACK(parms->dwTo);

                    dprintf("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwTo));
                    dprintf("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwTo));
                    dprintf("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwTo));
                    dprintf("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwTo));
                }
                else if (time_format == MCI_FORMAT_MILLISECONDS)
                {
                    info.last = info.first;

                    for (int i = info.first; i < MAX_TRACKS; i ++)
                    {
                        /* FIXME: use better matching */
                        if (tracks[i].position + tracks[i].length > parms->dwFrom / 1000)
                        {
                            info.last = i;
                            break;
                        }
                    }

                    dprintf("      mapped milliseconds to %d\n", info.last);
                }
                else
                    info.last = parms->dwTo;

                if (info.last < info.first)
                    info.last = info.first;

                if (info.last > lastTrack)
                    info.last = lastTrack;
            }

            if (info.first && (fdwCommand & MCI_FROM))
            {
                if (player)
                {
                    TerminateThread(player, 0);
                }

                playing = 0;
                player = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)player_main, (void *)&info, 0, NULL);
            }
            Sleep(10); /* A temporary fix. 10ms delay fixes "The Fifth Element Demo" endless play loop... */
        }

        if (uMsg == MCI_STOP)
        {
            dprintf("  MCI_STOP\r\n");
            plr_stop(); /* Make STOP command instant. */
            
            playing = 0;
        }

        if (uMsg == MCI_PAUSE)
        {
            dprintf("  MCI_PAUSE\r\n");
            paused = 1;
        }

        if (uMsg == MCI_STATUS)
        {
            LPMCI_STATUS_PARMS parms = (LPVOID)dwParam;

            dprintf("  MCI_STATUS\r\n");

            parms->dwReturn = 0;

            if (fdwCommand & MCI_TRACK)
            {
                dprintf("    MCI_TRACK\r\n");
                dprintf("      dwTrack = %d\r\n", parms->dwTrack);
            }

            if (fdwCommand & MCI_STATUS_ITEM)
            {
                dprintf("    MCI_STATUS_ITEM\r\n");

                if (parms->dwItem == MCI_STATUS_CURRENT_TRACK)
                {
                    dprintf("      MCI_STATUS_CURRENT_TRACK\r\n");
                }

                if (parms->dwItem == MCI_STATUS_LENGTH)
                {
                    dprintf("      MCI_STATUS_LENGTH\r\n");

                    int seconds = tracks[parms->dwTrack].length;

                    if (seconds)
                    {
                        if (time_format == MCI_FORMAT_MILLISECONDS)
                        {
                            parms->dwReturn = seconds * 1000;
                        }
                        else
                        {
                            parms->dwReturn = MCI_MAKE_MSF(seconds / 60, seconds % 60, 0);
                        }
                    }
                }

                if (parms->dwItem == MCI_CDA_STATUS_TYPE_TRACK)
                {
                    dprintf("      MCI_CDA_STATUS_TYPE_TRACK\r\n");
                    /*Fix from the Dxwnd project*/
                    /* ref. by WinQuake */
                    if((parms->dwTrack > 0) &&  (parms->dwTrack , MAX_TRACKS)){
                        if(tracks[parms->dwTrack].length > 0)
                            parms->dwReturn = MCI_CDA_TRACK_AUDIO; 
                    }
                }

                if (parms->dwItem == MCI_STATUS_MEDIA_PRESENT)
                {
                    dprintf("      MCI_STATUS_MEDIA_PRESENT\r\n");
                    parms->dwReturn = lastTrack > 0;
                }

                if (parms->dwItem == MCI_STATUS_NUMBER_OF_TRACKS)
                {
                    dprintf("      MCI_STATUS_NUMBER_OF_TRACKS\r\n");
                    parms->dwReturn = numTracks;
                }

                if (parms->dwItem == MCI_STATUS_POSITION)
                {
                    dprintf("      MCI_STATUS_POSITION\r\n");

                    if (fdwCommand & MCI_TRACK)
                    {
                        /* FIXME: implying milliseconds */
                        parms->dwReturn = tracks[parms->dwTrack].position * 1000;
                    }
                }

                if (parms->dwItem == MCI_STATUS_MODE)
                {
                    dprintf("      MCI_STATUS_MODE\r\n");
                    dprintf("        we are %s\r\n", playing ? "playing" : "NOT playing");

                    parms->dwReturn = playing ? MCI_MODE_PLAY : MCI_MODE_STOP;
                }

                if (parms->dwItem == MCI_STATUS_READY)
                {
                    dprintf("      MCI_STATUS_READY\r\n");
                    /*Fix from the Dxwnd project*/
                    /* referenced by Quake/cd_win.c */
                    parms->dwReturn = TRUE; /* TRUE=ready, FALSE=not ready */
                }

                if (parms->dwItem == MCI_STATUS_TIME_FORMAT)
                {
                    dprintf("      MCI_STATUS_TIME_FORMAT\r\n");
                }

                if (parms->dwItem == MCI_STATUS_START)
                {
                    dprintf("      MCI_STATUS_START\r\n");
                }
            }

            dprintf("  dwReturn %d\n", parms->dwReturn);

        }

        return 0;
    }

    /* fallback */
    return MCIERR_UNRECOGNIZED_COMMAND;
}

/* MCI command strings */
/* https://docs.microsoft.com/windows/win32/multimedia/multimedia-command-strings */
MCIERROR WINAPI fake_mciSendStringA(LPCTSTR cmd, LPTSTR ret, UINT cchReturn, HANDLE hwndCallback)
{
    char cmdbuf[1024];
    char cmp_str[1024];

    dprintf("[MCI String = %s]\n", cmd);

    /* copy cmd into cmdbuf */
    strcpy (cmdbuf,cmd);
    /* change cmdbuf into lower case */
    for (int i = 0; cmdbuf[i]; i++)
    {
        cmdbuf[i] = tolower(cmdbuf[i]);
    }

    if (strstr(cmdbuf, "sysinfo cdaudio quantity"))
    {
        dprintf("  Returning quantity: 1\r\n");
        strcpy(ret, "1");
        return 0;
    }

    /* Example: "sysinfo cdaudio name 1 open" returns "cdaudio" or the alias.*/
    if (strstr(cmdbuf, "sysinfo cdaudio name"))
    {
        dprintf("  Returning name: cdaudio\r\n");
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

    /* Look for the use of an alias */
    /* Example: "open d: type cdaudio alias cd1" */
    if (strstr(cmdbuf, "type cdaudio alias"))
    {
        char *tmp_s = strrchr(cmdbuf, ' ');
        if (tmp_s && *(tmp_s +1))
        {
            sprintf(alias_s, "%s", tmp_s +1);
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
        sprintf(alias_s, "cdaudio");
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
        if (strstr(cmdbuf, "msf"))
        {
            static MCI_SET_PARMS parms;
            parms.dwTimeFormat = MCI_FORMAT_MSF;
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
    }

    /* Handle "status cdaudio/alias" */
    sprintf(cmp_str, "status %s", alias_s);
    if (strstr(cmdbuf, cmp_str)){
        if (strstr(cmdbuf, "number of tracks"))
        {
            dprintf("  Returning number of tracks (%d)\r\n", numTracks);
            sprintf(ret, "%d", numTracks);
            return 0;
        }
        int track = 0;
        if (sscanf(cmdbuf, "status %*s length track %d", &track) == 1)
        {
            static MCI_STATUS_PARMS parms;
            parms.dwItem = MCI_STATUS_LENGTH;
            parms.dwTrack = track;
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
    }

    /* Handle "play cdaudio/alias" */
    int from = -1, to = -1;
    sprintf(cmp_str, "play %s from", alias_s);
    if (strstr(cmdbuf, cmp_str)){
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
        /* Missing: "play cdaudio to" */
    }

    return 0;
}

UINT WINAPI fake_auxGetNumDevs()
{
    dprintf("fake_auxGetNumDevs()\r\n");
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
    dprintf("fake_auxGetVolume(uDeviceId=%08X, lpdwVolume=%p)\r\n", uDeviceID, lpdwVolume);
    *lpdwVolume = 0x00000000;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI fake_auxSetVolume(UINT uDeviceID, DWORD dwVolume)
{
    static DWORD oldVolume = -1;
    char cmdbuf[256];

    dprintf("fake_auxSetVolume(uDeviceId=%08X, dwVolume=%08X)\r\n", uDeviceID, dwVolume);

    if (dwVolume == oldVolume)
    {
        return MMSYSERR_NOERROR;
    }

    oldVolume = dwVolume;

    unsigned short left = LOWORD(dwVolume);
    unsigned short right = HIWORD(dwVolume);

    dprintf("    left : %ud (%04X)\n", left, left);
    dprintf("    right: %ud (%04X)\n", right, right);

    plr_volume((left / 65535.0f) * 100);

    return MMSYSERR_NOERROR;
}
