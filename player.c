/*
 * This file is part of wav-winmm, a fork of ogg-winmm.
 * 
 * wav-winmm is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 *
 * wav-winmm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */


#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <windows.h>
#include <string.h>

#define WAV_BUF_CNT	(2)				// Dual buffer
#define WAV_BUF_TME	(1000)				// The expected playtime of the buffer in milliseconds: 1000ms
#define WAV_BUF_LEN	(44100*2*2*(WAV_BUF_TME/1000))	// 44100Hz, 16-bit, 2-channel, 1 second buffer

bool		plr_run			= false;
bool		plr_bsy			= false;
unsigned int	plr_len			= 0;
float		plr_vol[2]		= {1.0, 1.0}; // Left, Right

HWAVEOUT	plr_hw	 		= NULL;
HANDLE		plr_ev  		= NULL;
FILE*		plr_fp			= NULL;
WAVEFORMATEX	plr_fmt			= {0};
int		plr_que			= 0;
int		plr_sta[WAV_BUF_CNT]	= {0};
WAVEHDR		plr_hdr[WAV_BUF_CNT]	= {0};
char		plr_buf[WAV_BUF_CNT][WAV_BUF_LEN] __attribute__ ((aligned(4)));

void plr_volume(int vol_l, int vol_r)
{
	if (vol_l < 0 || vol_l > 99) plr_vol[0] = 1.0;
	else plr_vol[0] = vol_l / 100.0;

	if (vol_r < 0 || vol_r > 99) plr_vol[1] = 1.0;
	else plr_vol[1] = vol_r / 100.0;
}

unsigned int plr_length(const char *path) // in milliseconds
{
	FILE* f = fopen(path, "rb");
	if (!f) return 0;

	unsigned char header[44];
	if (fread(header, 1, 44, f) < 44) {
		fclose(f);
		return 0;
	}
	if (memcmp(header, "RIFF", 4) != 0 || memcmp(header+8, "WAVE", 4) != 0) {
		fclose(f);
		return 0;
	}
	int channels = header[22] | (header[23] << 8);
	int sampleRate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
	int bitsPerSample = header[34] | (header[35] << 8);
	unsigned int dataSize = header[40] | (header[41] << 8) | (header[42] << 16) | (header[43] << 24);

	int bytesPerSec = sampleRate * channels * (bitsPerSample / 8);
	int millis = (bytesPerSec > 0) ? (dataSize * 1000 / bytesPerSec) : 0;

	fclose(f);
	return millis;
}

void plr_reset(BOOL wait)
{
	if (plr_fp) {
		fclose(plr_fp);
		plr_fp = NULL;
	}

	if (plr_hw) {
		if (wait) {
			for (int n = 0; n < WAV_BUF_CNT; n++, plr_que = (plr_que+1) % WAV_BUF_CNT) {
				if (!(plr_hdr[plr_que].dwFlags & WHDR_DONE)) WaitForSingleObject(plr_ev, WAV_BUF_TME);
			}
		}
		waveOutReset(plr_hw);
		for (int i = 0; i < WAV_BUF_CNT; i++) {
			waveOutUnprepareHeader(plr_hw, &plr_hdr[i], sizeof(WAVEHDR));
		}
		waveOutClose(plr_hw);
		plr_hw = NULL;
	}

	if (plr_ev) {
		CloseHandle(plr_ev);
		plr_ev = NULL;
	}
}

int plr_play(const char *path, unsigned int from, unsigned int to)
{
	(void)from; (void)to; // WAV seeking not implemented

	plr_fp = fopen(path, "rb");
	if (!plr_fp) return 0;

	unsigned char header[44];
	if (fread(header, 1, 44, plr_fp) < 44) {
		fclose(plr_fp);
		plr_fp = NULL;
		return 0;
	}

	if (memcmp(header, "RIFF", 4) != 0 || memcmp(header+8, "WAVE", 4) != 0) {
		fclose(plr_fp);
		plr_fp = NULL;
		return 0;
	}

	int audioFormat    = header[20] | (header[21] << 8);
	int channels       = header[22] | (header[23] << 8);
	int sampleRate     = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
	int bitsPerSample  = header[34] | (header[35] << 8);

	if (audioFormat != 1 || bitsPerSample != 16) {
		fclose(plr_fp);
		plr_fp = NULL;
		return 0;
	}

	plr_fmt.wFormatTag      = WAVE_FORMAT_PCM;
	plr_fmt.nChannels       = channels;
	plr_fmt.nSamplesPerSec  = sampleRate;
	plr_fmt.wBitsPerSample  = bitsPerSample;
	plr_fmt.nBlockAlign     = channels * (bitsPerSample / 8);
	plr_fmt.nAvgBytesPerSec = plr_fmt.nBlockAlign * sampleRate;
	plr_fmt.cbSize          = 0;

	plr_ev = CreateEvent(NULL, 0, 1, NULL);

	if (waveOutOpen(&plr_hw, WAVE_MAPPER, &plr_fmt, (DWORD_PTR)plr_ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
		fclose(plr_fp); plr_fp = NULL;
		CloseHandle(plr_ev); plr_ev = NULL;
		return 0;
	}

	plr_que = 0;
	for (int i = 0; i < WAV_BUF_CNT; i++) {
		plr_sta[i] = 0;
		plr_hdr[i].dwFlags = WHDR_DONE;
	}

	plr_run = true;
	return 1;
}

void plr_stop()
{
	if (!plr_run) return;

	plr_run = false;

	if (plr_ev) {
		SetEvent(plr_ev);
		while (plr_bsy) {
			Sleep(1);
		}
	}
}

void plr_pause()
{
	if (plr_hw) waveOutPause(plr_hw);
}

void plr_resume()
{
	if (plr_hw) waveOutRestart(plr_hw);
}

int plr_pump()
{
	if (!plr_run || !plr_fp) return -1;

	plr_bsy = true;

	if (WaitForSingleObject(plr_ev, INFINITE) != 0 || !plr_run) {
		plr_bsy = false;
		return -1;
	}

	for (int n = 0, i = plr_que; n < WAV_BUF_CNT; n++, i = (i+1) % WAV_BUF_CNT) {
		if (plr_sta[i] != 0) continue;

		WAVEHDR *hdr = &plr_hdr[i];
		if (!(hdr->dwFlags & WHDR_DONE)) break;

		char *buf = plr_buf[i];
		unsigned int pos = 0;
		size_t bytes = fread(buf, 1, WAV_BUF_LEN, plr_fp);
		pos += (unsigned int)bytes;

		if (pos == 0) {
			plr_run = false;
			plr_bsy = false;
			return 0;
		}

		if (plr_vol[0] != 1.0 || plr_vol[1] != 1.0) {
			short *sbuf = (short *)buf;
			for (int j = 0, end = pos / 2; j < end; j+=2) {
				sbuf[j]   *= plr_vol[0];
				sbuf[j+1] *= plr_vol[1];
			}
		}

		waveOutUnprepareHeader(plr_hw, hdr, sizeof(WAVEHDR));
		hdr->lpData = buf;
		hdr->dwBufferLength = pos;
		hdr->dwUser = 0xCDDA7777;
		hdr->dwFlags = 0;
		hdr->dwLoops = 0;

		plr_sta[i] = 1;
	}

	for (int n = 0; n < WAV_BUF_CNT; n++, plr_que = (plr_que+1) % WAV_BUF_CNT) {
		if (plr_sta[plr_que] != 1) break;
		WAVEHDR *hdr = &plr_hdr[plr_que];
		if (waveOutPrepareHeader(plr_hw, hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
		    waveOutWrite(plr_hw, hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
			SetEvent(plr_ev);
			Sleep(1);
			break;
		}
		plr_sta[plr_que] = 0;
	}

	plr_bsy = false;
	return 1;
}
