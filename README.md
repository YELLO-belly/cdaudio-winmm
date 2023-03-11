## What is it?

![screenshot](screenshot-v05.png)

This is a winmm wrapper to a separate cdaudio player that handles the track repeat that is broken from Windows Vista onwards (among fixing other things MS broke in mcicda implementation). Unlike the ogg-winmm wrapper which plays ripped .ogg files cdaudio-winmm instead tries to directly play the cdtracks on a physical disc (or cdimage) using a separate player program. Communication between winmm.dll and the player is done using [mailslots.](https://learn.microsoft.com/en-us/windows/win32/ipc/mailslots) 

The trick is to handle the broken MCI mode change by monitoring POSTION and MODE. If MODE = "playing" and POSITION has not changed then we can determine that the track has finished playing and can update the MODE and send the MM_NOTIFY_SUCCESSFUL message (if requested). The cdaudio player code might also be useful to anyone wanting to write an MCI cdaudio player on newer Windows systems.

# Usage:

- Place winmm.dll wrapper into the game folder.
- Place cdaudioplr.exe in 'mcicda' -sub-folder.
- Run the game normally.

NOTE:
- You can start cdaudioplr.exe manually before running the game. Sometimes this may be necessary since the game may query the cd device before the wrapper has time to initialize the player. There is now also a batch file that can be used to make sure that the cdaudioplr.exe is launched before the game:  
https://raw.githubusercontent.com/YELLO-belly/cdaudio-winmm/master/batch/start_game.cmd  
<sub>(right click on link and choose save link as...)</sub>  
- Do not place cdaudioplr.exe and winmm.dll in the same folder!
- ~~v.0.3 now supports mp3 and wav playback if a music folder is found containing the tracks in the correct format. (track02.mp3/wav ...)~~ No longer available in 1.5.  

Extra note:
- Apparently on some machines the local winmm.dll wrapper is ignored and the real system dll is used instead. This may be because some other program has already loaded the winmm.dll library or some system setting forces the use of the real dll. The wrapper can be forced to load by renaming it to for example to winm2.dll and hex editing the program executable to point to this renamed winmm.dll instead.
- There is now also a PowerShell script available in the sources to help alleviate issues where the wrapper is ignored by Windows.  
See: https://github.com/YELLO-belly/ogg-winmm/tree/master/PS-Script  
or: https://github.com/YELLO-belly/ogg-winmm/raw/master/PS-Script/force-winmm-loading.ps1  
<sub>(right click on link and choose save link as...)</sub>

# cdaudio-winmm player v.1.5.2:
- Fix mciSendCommand DeviceType case sensitive. To keep in line with the ogg-winmm fork changes. Some games can use DeviceType "CDAudio" in which case the old logic was failing since it looked only for the lower case "cdaudio" string.

# cdaudio-winmm player v.1.5.1:
- Added a manual startup delay adjustment in winmm.ini. This can be changed from 1-9 seconds (whole numbers only). The default is 0 which will set a 1.5 second delay as will invalid values. The startup delay determines how long the cdaudioplr.exe has time to initialize itself.

# cdaudio-winmm player v.1.5 final rev:
- A very small but important revision to the wait time for mode change. Turns out 1 second is not enough in some cases. It is now increased to 3 seconds maximum wait time for the mode change. In theory though depending on the quality of the CD-ROM disc and the CD/DVD drive it could take more than 3 seconds. Another option could have been to set an infinite wait but if something goes wrong this could result in the game deadlocking.
- MCIDevID option now uses the real MCI to open waveaudio device and to lock that id for the emulation. Solves issues with games that are not happy with the fake 48879 id.
- SendMessageA for for the notify message is now in its own thread. It was causing a latency/ lock up when send from inside the mailslot reader thread.
- Longer initial sleep in the wrapper to wait for cdaudioplr.exe to initialize.  


# cdaudio-winmm player (Experimental v.1.5):

- Attempt to bring in improvements from ogg-winmm. Should now support MSF and Milliseconds time formats as well as other previously missing functions.
- Removed the previous capability of using .wav or .mp3 files. This is now purely for playback directly from the disc.
- The nature of the server to client system using Mailslots causes issues that have been difficult to resolve. Hence this release should be considered experimental.


# cdaudio-winmm player (beta v.0.4.0.3) -UPDATED-:  
(Marked as v.1.0 in the releases.)  

- MCI device ID now defaults to 0x1 (int 1). Old 0xBEEF (int 48879) can be restored from winmm.ini. (edited cdaudio-winmm.c, winmm.ini)
- Non cdaudio related MCI commands are now relayed to the real winmm for video playback, etc. (taken from AyuanX ogg-winmm fork) (edited cdaudio-winmm.c, cdaudio-winmm.def, stubs.c)
- Subsequently MCI_GETDEVCAPS needs to be handled now. (edited cdaudio-winmm.c)
- Needed to Add some extra complexity in the player loop where the fact that the mailslot can send a new notify msg request before the player loop is finished caused music not to repeat. (edited cdaudioplr_src\cdaudioplr.c -> new skip_notify_msg variable)


# Original changelog:

0.4.0.3 changes:
- Better no. of tracks logic. Should now work more reliably.
- Set player to always run on the last CPU core to fix issues with games set to run on single core affinity (e.g. Midtown Madness).
- Handle TMS time format (Driver).

Note: This may be the last maintenance release. Implementing support for games that switch between different time formats and use milliseconds to play a tracks from arbitrary position will require a full re-write of the logic. In addition a better wrapper is needed to only catch commands send to the cdaudio device and not swallow other commands that are meant for video playback for example. The latency with Mailslot communication is also proving difficult to manage as the logic becomes more complex.

0.4.0.2 changes:
- Using the waveaudio device for .wav file playback.
- Fix MCI_STATUS_POSITION for current track in TMSF (MidtownMadness Open1560 compatibility)

0.4.0.1 changes:
- Fixed a dgVoodoo2 ddraw.dll wrapper incompatibility issue with track repeats.

0.4 changes:
- Added "AutoClose" option in winmm.ini.
- MciSendCommand improvements from ogg-winmm project. Support for more games.
- MciSendString improvements copied from ogg-winmm.
- AuxVolume control enabled. (should use in-game volume sliders. Manual volume override available for problematic cases)
- SetCurrentDirectory fix for cdaudioplr.exe when started from winmm.dll.

0.3 changes:
- mp3/wav support
- cleared up some naming inconsistency


# Building:

Build with make.cmd if path variables are set (C:\MinGW\bin).
Or build from msys with command: mingw32-make

