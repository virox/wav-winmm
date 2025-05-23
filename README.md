# wav-winmm CD Audio Emulator

**wav-winmm** is a replacement wrapper for `winmm.dll` used by many classic games and programs to play CD audio tracks.

Instead of accessing audio on physical CDs, wav-winmm emulates CD-DA playback using standard uncompressed **WAV** files (`Track02.wav`, `Track03.wav`, etc.). It also provides separate volume controls for CDDA, MIDI, and WAVE audio — something modern Windows versions no longer support natively.


# Usage:

1. **Rip your CD tracks** using a tool like Exact Audio Copy (EAC) and save them as **WAV** files using the naming convention:
> **Track02.wav, Track03.wav, Track04.wav, ...**

- Numbering usually starts from `Track02.wav` because `Track01` on mixed-mode CDs is often a data track.
- Do not skip track numbers or use names with spaces.

2. **Place the WAV files** in a folder called `Music` inside the same directory as your game's executable.

3. **Copy the following files** to the game folder:
- `winmm.dll` (this DLL from the wav-winmm build)
- `winmm.ini` (optional configuration file for volume control)

4. **(Optional)** Edit `winmm.ini` to customize volume levels for:
- `CDDA`
- `MIDI`
- `WAVE`

5. Run the game — and enjoy the music from your WAV files instead of a CD!

---

# Requirements

- Windows XP or newer
- Works with 32-bit games expecting CD audio via `winmm.dll`
- Also compatible with Windows 95/98 using this workaround:
1. Rename `winmm.dll` → `wincd.dll`, and `winmm.ini` → `wincd.ini`
2. Use a hex editor to replace `"winmm.dll"` with `"wincd.dll"` inside the game executable

---

# Building from Source

To build `wav-winmm.dll`:

- Use **MinGW** (e.g. `i686-w64-mingw32-gcc`)
- Run:
```bash
make                # or make -f Makefile.linuxMinGW
```

# Revisions:

v.2025.05.23
- Remove OGG/Vorbis support.
- Fully rewritten to use standard WAV files ripped from CD audio instead.
- Simplified build process; removed all external dependencies.

v.2025.01.16
- Treat PLAY as RESUME when in PAUSE.

v.2025.01.09
- Link to "msvcrt40.dll" for backward compatibility to Win9x.

v.2024.11.12
- Compatibility fix for thread scheduling on Windows XP or later.

v.2024.10.24
- Add an option to customize the music folder name.

v.2024.08.08
- Small compiler optimizations.

v.2024.03.21
- Fix the truncation at the end of each song ranging from 1ms to 500ms.

v.2023.07.24
- Correctly report stereo CD separate channel volume control capability.

v.2023.06.01
- Fix MCI_PAUSE & MCI_RESUME when not playing.
- Fix MCI_STATUS_LENGTH when time format is set to TMSF.
- Fix MCI_STATUS_POSITION when time format is set to milliseconds or MSF.

v.2023.04.14
- Implement MCI_SEEK & MCI_GETDEVCAPS.
- Allow aux volume control on left/right channel separately.  

v.2023.03.11
- Allow more than 1 data track before audio track.  
- Fix total track number.

v.2023.02.28
- Improve time range precision from second to millisecond.
- Add compatibility support for Win95/98.

v.2023.02.26:
- Implement time range playing down to seconds instead of always track-wise playing.
- Implement pause and resume.

v.2023.01.18:
- Implement play/seek/length/position for mciSendString.

v.2022.06.26:
- Fix MIDI file open bypass.

v.2022.05.26:
- Remove unnecessary monitor thread.
- Thread calling convention fix.
- Position calculation and status fix.
- Various compatibility fix.

v.2022.02.28:
- Implement MCI_INFO
- Fix MCI_SYSINFO
- Bugfixes for MCISendString

v.2022.02.27:
- Treat MCI_PAUSE as MCI_STOP and MCI_RESUME as MCI_PLAY, since MCICDA does not support resume.

v.2021.11.15:
- Ignore case when opening device.

v.2021.11.11:
- Post notify message to the correct window.
- Fix a race condition that sometimes causes music to stutter.

v.2021.11.10:
- Completely rewrite player control logic.
- Use persistent thread to improve performance.
- Use persistent buffer to improve performance.
- Fix various crash/freeze scenarios related to thread.
- Fix various memory leak scenarios related to buffer.

v.2021.08.08:
- Fix MCI_STATUS_LENGTH and MCI_STATUS_POSITION.
- Optimize player thread control.

v.2021.07.26:
- Relay MCI commands if they are not targeting cdaudio device.
- Add more stub functions to support more games.
- Filter MIDI source by the full driver name (wdmaud.drv).

v.2021.07.11:
- Implement an alternative method to control MIDI volume, which should make scaling coefficients identical to CDDA/WAVE volume control.

v.2021.07.10:
- Implement separate volume control for CDDA/MIDI/WAVE.
  This is necessary because Windows does not provide any method to separate the volume control of CDDA/MIDI/WAVE ever since Vista.

v.2021.07.09:
- Fix the start tick of each track.
  Since MCICDA does not support pause, unnecessary time accumulation is removed.

v.2021.07.08:
- Implement realtime TMSF report for MCI_STATUS_POSITION.
  Many games use this realtime status to sync up video and audio frame by frame.

# Licensing

This project, **wav-winmm**, is a fork of [ogg-winmm](https://github.com/ayuanx/ogg-winmm) by AyuanX and is licensed under the **GNU General Public License version 2 (GPL-2.0)**.

You are free to use, modify, and distribute this software under the terms of that license.

Please note:
- You **must include the full source code** if you distribute a compiled binary (`winmm.dll`)
- You **must retain this license** and include a copy of the GPL v2 license text in your distribution
- You **must credit the original author** (AyuanX) where appropriate

For full legal terms, see the included `LICENSE.txt` or visit:  
https://www.gnu.org/licenses/old-licenses/gpl-2.0.html
