#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <windows.h>
#include <vorbis/vorbisfile.h>

#define WAV_BUF_CNT	(2)		// Dual buffer
#define WAV_BUF_LEN	(44100*2)	// 44100Hz, 16-bit, 2-channel, 1/2 second buffer

bool		plr_run			= false;
bool		plr_bsy			= false;
unsigned int	plr_len			= 0;
float		plr_vol			= -1.0;

HWAVEOUT	plr_hw	 		= NULL;
HANDLE		plr_ev  		= NULL;
OggVorbis_File	plr_vf			= {0};
WAVEFORMATEX	plr_fmt			= {0};
int		plr_que			= 0;
int		plr_sta[WAV_BUF_CNT]	= {0};
WAVEHDR		plr_hdr[WAV_BUF_CNT]	= {0};
char		plr_buf[WAV_BUF_CNT][WAV_BUF_LEN] __attribute__ ((aligned(4)));

void plr_volume(int vol)
{
	if (vol < 0 || vol > 99) plr_vol = -1.0;
	else plr_vol = vol / 100.0;
}

unsigned int plr_length(const char *path) // in millisecond
{
	OggVorbis_File  vf;
	if (ov_fopen(path, &vf) != 0) return 0;
	unsigned int ret = (unsigned int)(ov_time_total(&vf, -1) * 1000);
	ov_clear(&vf);
	return ret;
}

void plr_reset()
{
	if (plr_vf.datasource) {
		ov_clear(&plr_vf);
	}

	if (plr_hw) {
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
	plr_reset();

	if (ov_fopen(path, &plr_vf) != 0) return 0;

	vorbis_info *vi = ov_info(&plr_vf, -1);
	if (!vi) {
		ov_clear(&plr_vf);
		return 0;
	}

	plr_fmt.wFormatTag      = WAVE_FORMAT_PCM;
	plr_fmt.nChannels       = vi->channels;
	plr_fmt.nSamplesPerSec  = vi->rate;
	plr_fmt.wBitsPerSample  = 16;
	plr_fmt.nBlockAlign     = plr_fmt.nChannels * (plr_fmt.wBitsPerSample / 8);
	plr_fmt.nAvgBytesPerSec = plr_fmt.nBlockAlign * plr_fmt.nSamplesPerSec;

	plr_ev = CreateEvent(NULL, 0, 1, NULL);

	if (waveOutOpen(&plr_hw, WAVE_MAPPER, &plr_fmt, (DWORD_PTR)plr_ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
		ov_clear(&plr_vf);
		CloseHandle(plr_ev);
		plr_ev = NULL;
		plr_hw = NULL;
		return 0;
	}

	plr_que = 0;
	for (int i = 0; i < WAV_BUF_CNT; i++) {
		plr_sta[i] = 0;
		plr_hdr[i].dwFlags = WHDR_DONE;
	}

	if (from) ov_time_seek(&plr_vf, (double)from / 1000);
	plr_len = to > from ? (unsigned int)ceil((to - from) / 1000.0 * vi->rate) * 2 * vi->channels : -1; // heed alignment 

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
	if (!plr_run || !plr_vf.datasource) return -1;

	plr_bsy = true;

	if (WaitForSingleObject(plr_ev, INFINITE) != 0 || !plr_run) {
		plr_bsy = false;
		return -1;
	}

	for (int n = 0, i = plr_que; n < WAV_BUF_CNT; n++, i = (i+1) % WAV_BUF_CNT) {
		if (plr_sta[i] != 0) {
			continue;
		}

		WAVEHDR *hdr = &plr_hdr[i];
		if (!(hdr->dwFlags & WHDR_DONE)) {
			break;
		}

		char *buf = plr_buf[i];
		unsigned int pos = 0, size = plr_len > WAV_BUF_LEN ? WAV_BUF_LEN : plr_len;
		while (pos < size) {
			long bytes = ov_read(&plr_vf, buf + pos, size - pos, 0, 2, 1, NULL);

			if (bytes == OV_HOLE) {
				continue;
			} else if (bytes == OV_EBADLINK || bytes == OV_EINVAL) {
				pos = 0;
				break;
			} else if (bytes == 0) {
				break;
			}

			pos += bytes;
		}

		if (pos == 0) {
			plr_run = false;
			plr_bsy = false;
			return 0;
		}
		plr_len -= pos;

		/* volume control, kinda nasty */
		if (plr_vol != -1) {
			short *sbuf = (short *)buf;
			for (int j = 0, end = pos / 2; j < end; j++)
				sbuf[j] *= plr_vol;
		}

		waveOutUnprepareHeader(plr_hw, hdr, sizeof(WAVEHDR));
		hdr->lpData		= buf;
		hdr->dwBufferLength	= pos;
		hdr->dwUser		= 0xCDDA7777; /* our lucky identifier */
		hdr->dwFlags		= 0;
		hdr->dwLoops		= 0;

		plr_sta[i] = 1;
	}

	for (int n = 0; n < WAV_BUF_CNT; n++, plr_que = (plr_que+1) % WAV_BUF_CNT) {
		if (plr_sta[plr_que] != 1) {
			break;
		}
		WAVEHDR *hdr = &plr_hdr[plr_que];
		if (waveOutPrepareHeader(plr_hw, hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR || waveOutWrite(plr_hw, hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
			SetEvent(plr_ev);
			Sleep(1);
			break;
		}
		plr_sta[plr_que] = 0;
	}

	plr_bsy = false;
	return 1;
}

/* TODO: */
/*
   int plr_seek(int sec)
   {
   int len = (int)ov_time_total(&plr_vf, -1);
   if(sec<0) sec=0;
   if(sec > len) sec = len;
   return ov_time_seek(&plr_vf, (double)sec);
   }

   int plr_tell()
   {
   int tpos = (int)ov_time_tell(&plr_vf);
   return tpos;
   }
   */
