/*
  Copyright (c) 2012 Toni Spets <toni.spets@iki.fi>
 
  Permission to use, copy, modify, and distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.
 
  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
  ________________________________________________________________________
  
  This code has been modified to work with the stand-alone cdaudioplr.exe
  to fix issues with cdaudio playback in old games starting with Win Vista.
  Edits by: DD (2020)
  ________________________________________________________________________
*/

#include <windows.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>

// Mailslot header stuff: 
DWORD NumberOfBytesRead;
DWORD BytesWritten;
CHAR ServerName[] = "\\\\.\\Mailslot\\cdaudioplr_Mailslot";
char buffer[512];
char name[512];
char dwVolume_str[32] = "";
char dwFrom_str[64] = "";
char dwTo_str[64] = "";
int value;
int mode = 1; // 1=stopped, 2=playing 
int m_s = 0;
int s_s = 0;
int f_s = 0;
int tt_s = 0;
int tm_s = 0;
int ts_s = 0;
int tf_s = 0;

// MCI Relay declarations: 
MCIERROR WINAPI relay_mciSendCommandA(MCIDEVICEID a0, UINT a1, DWORD a2, DWORD a3);
MCIERROR WINAPI relay_mciSendStringA(LPCSTR a0, LPSTR a1, UINT a2, HWND a3);

int MAGIC_DEVICEID = 48879;
MCI_OPEN_PARMS mciOpenParms;

#ifdef _DEBUG
	#define dprintf(...) if (fh) { fprintf(fh, __VA_ARGS__); fflush(NULL); }
	FILE *fh = NULL;
#else
	#define dprintf(...)
#endif

int notfy_flag = 0;
char alias_s[100] = "cdaudio";
int time_format = MCI_FORMAT_MSF;
int numTracks = 0; // default: MAX 99 tracks 
int mciStatusRet = 0;
int once = 0;
int StartDelayMs = 1500;
int AllMusicTracks = 0;

CRITICAL_SECTION cs;
HANDLE reader = NULL;
HANDLE notifier = NULL;

int sendnotify = 0;

int send_notify_msg_main( void )
{
	while(1){
		if(sendnotify){
			SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
			sendnotify = 0;
		}
		Sleep(10);
	}
}

// Mailslot reader thread: 
int reader_main( void )
{
	HANDLE Mailslot;

	// Create mailslot: 
	if ((Mailslot = CreateMailslot("\\\\.\\Mailslot\\winmm_Mailslot", 0, MAILSLOT_WAIT_FOREVER, NULL)) == INVALID_HANDLE_VALUE)
	{
		dprintf("mailslot error %d\n", GetLastError());
		return 0;
	}

	// Start cdaudio player: 
	ShellExecuteA(NULL, "open", ".\\mcicda\\cdaudioplr.exe", NULL, NULL, SW_SHOWNOACTIVATE);

	// Loop to read mailslot: 
	while(ReadFile(Mailslot, buffer, 512, &NumberOfBytesRead, NULL) != 0)
	{

		if (NumberOfBytesRead > 0){
			sscanf(buffer,"%d %s", &value, name);
			//dprintf("-[ Mailslot stored name = %s | value = %d ]-\n", name, value);
			dprintf("Mailslot Buffer: %s\n", buffer);

				// Read mode 
				if(strcmp(name,"mode")==0){
					mode = value;
					//if(mode==1)Beep(450,400);
					//if(mode==2)Beep(750,400);
				}

				// Handle notify message: 
				if(strstr(buffer,"notify_s")){
					//endMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
					sendnotify = 1;
					notfy_flag = 0;
				}

				// Read no. of tracks: 
				if(strcmp(name,"tracks")==0){
					numTracks = value;
				}

				// Read full length: 
				if(strstr(buffer,"length")){
					//char * cut;
					//cut = strtok (buffer," .-");
					sscanf(buffer,"%d:%d:%d", &m_s, &s_s, &f_s);
					mciStatusRet = value;
				}

				// Read track length: 
				if(strstr(buffer,"length_t")){
					sscanf(buffer,"%d:%d:%d", &m_s, &s_s, &f_s);
					mciStatusRet = value;
				}

				// Read current position: 
				if(strstr(buffer,"pos")){
					sscanf(buffer,"%d:%d:%d", &m_s, &s_s, &f_s);
					sscanf(buffer,"%d:%d:%d:%d", &tt_s, &tm_s, &ts_s, &tf_s);
					mciStatusRet = value;
				}

				// Read track position: 
				if(strstr(buffer,"pos_t")){
					sscanf(buffer,"%d:%d:%d", &m_s, &s_s, &f_s);
					sscanf(buffer,"%d:%d:%d:%d", &tt_s, &tm_s, &ts_s, &tf_s);
					mciStatusRet = value;
				}
				// Read current track: 
				if(strstr(buffer,"cur_t")){
					mciStatusRet = value;
				}
		}
	}
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
#ifdef _DEBUG
		int bLog = GetPrivateProfileInt("winmm", "Log", 0, ".\\winmm.ini");
		if(bLog)fh = fopen("winmm.log", "w");
#endif

		InitializeCriticalSection(&cs);

		// Start Mailslot reader thread: 
		reader = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)reader_main, NULL, 0, NULL);
		notifier = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)send_notify_msg_main, NULL, 0, NULL);

		// Read winmm.ini for StartDelay
		int StartDelay = GetPrivateProfileInt("winmm", "StartDelay", 0, ".\\winmm.ini");
		if ((StartDelay < 1) || (StartDelay > 9)){
			StartDelayMs = 10;
			once = 1;
		}
		else{
			StartDelayMs = StartDelay * 1000;
		}

		// Look for all music tracks option
		AllMusicTracks = GetPrivateProfileInt("winmm", "AllMusicTracks", 0, ".\\winmm.ini");

		int bMCIDevID = GetPrivateProfileInt("winmm", "MCIDevID", 0, ".\\winmm.ini");
		if(bMCIDevID){
			mciOpenParms.lpstrDeviceType = "waveaudio";
			int MCIERRret = 0;
			if (MCIERRret = mciSendCommand(0, MCI_OPEN, MCI_OPEN_TYPE, (DWORD)(LPVOID) &mciOpenParms)){
				// Failed to open wave device.
				MAGIC_DEVICEID = 48879;
				dprintf("Failed to open wave device! Using 0xBEEF as cdaudio id.\r\n");
			}
			else{
				MAGIC_DEVICEID = mciOpenParms.wDeviceID;
				dprintf("Wave device opened succesfully using cdaudio ID %d for emulation.\r\n",MAGIC_DEVICEID);
			}
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

		// Read winmm.ini 
		int bAutoClose = GetPrivateProfileInt("winmm", "AutoClose", 0, ".\\winmm.ini");
		
		if(bAutoClose)
		{
			// Write exit message for cdaudioplr.exe: 
			HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(Mailslot, "exit", 64, &BytesWritten, NULL);
			CloseHandle(Mailslot);
			
		}
	}

	return TRUE;
}

MCIERROR WINAPI fake_mciSendCommandA(MCIDEVICEID IDDevice, UINT uMsg, DWORD_PTR fdwCommand, DWORD_PTR dwParam)
{
	if (!once) {
		once = 1;
		Sleep(StartDelayMs); // Sleep a bit to ensure cdaudioplr.exe is initialized.
	}
	
	char cmdbuf[1024];

	dprintf("mciSendCommandA(IDDevice=%p, uMsg=%p, fdwCommand=%p, dwParam=%p)\r\n", IDDevice, uMsg, fdwCommand, dwParam);

	if (uMsg == MCI_OPEN)
	{
		LPMCI_OPEN_PARMS parms = (LPVOID)dwParam;

		dprintf("  MCI_OPEN\r\n");

		if (fdwCommand & MCI_OPEN_ALIAS)
		{
			dprintf("	 MCI_OPEN_ALIAS\r\n");
			dprintf("		 -> %s\r\n", parms->lpstrAlias);
		}

		if (fdwCommand & MCI_OPEN_SHAREABLE)
		{
			dprintf("	 MCI_OPEN_SHAREABLE\r\n");
		}

		if (fdwCommand & MCI_OPEN_TYPE_ID)
		{
			dprintf("	 MCI_OPEN_TYPE_ID\r\n");

			if (LOWORD(parms->lpstrDeviceType) == MCI_DEVTYPE_CD_AUDIO)
			{
				dprintf("  Returning magic device id for MCI_DEVTYPE_CD_AUDIO\r\n");
				parms->wDeviceID = MAGIC_DEVICEID;
				return 0;
			}
			else return relay_mciSendCommandA(IDDevice, uMsg, fdwCommand, dwParam); // Added MCI relay 
		}

		if (fdwCommand & MCI_OPEN_TYPE && !(fdwCommand & MCI_OPEN_TYPE_ID))
		{
			dprintf("	 MCI_OPEN_TYPE\r\n");
			dprintf("		 -> %s\r\n", parms->lpstrDeviceType);

			/* copy alias to buffer */
			char cmpaliasbuf[1024];
			strcpy (cmpaliasbuf,parms->lpstrDeviceType);
			/* change cmpaliasbuf into lower case */
			for (int i = 0; cmpaliasbuf[i]; i++)
			{
			    cmpaliasbuf[i] = tolower(cmpaliasbuf[i]);
			}

			if (strcmp(cmpaliasbuf, "cdaudio") == 0)
			{
				dprintf("  Returning magic device id for MCI_DEVTYPE_CD_AUDIO\r\n");
				parms->wDeviceID = MAGIC_DEVICEID;
				return 0;
			}
			else return relay_mciSendCommandA(IDDevice, uMsg, fdwCommand, dwParam); // Added MCI relay 
		}
	}

	if (IDDevice == MAGIC_DEVICEID || IDDevice == 0 || IDDevice == 0xFFFFFFFF)
	{

		if (uMsg == MCI_GETDEVCAPS)
		{
			LPMCI_GETDEVCAPS_PARMS parms = (LPVOID)dwParam;

			dprintf("  MCI_GETDEVCAPS\r\n");

			parms->dwReturn = 0;

			if (fdwCommand & MCI_GETDEVCAPS_ITEM)
			{
				dprintf("  MCI_GETDEVCAPS_ITEM\r\n");
				
				if (parms->dwItem == MCI_GETDEVCAPS_CAN_PLAY || parms->dwItem == MCI_GETDEVCAPS_CAN_EJECT || parms->dwItem == MCI_GETDEVCAPS_HAS_AUDIO)
				{
					parms->dwReturn = TRUE;
				}
				else if (parms->dwItem == MCI_GETDEVCAPS_DEVICE_TYPE)
				{
					parms->dwReturn = MCI_DEVTYPE_CD_AUDIO;
				}
				else
				{
					parms->dwReturn = FALSE;
				}
			}
		}

		if (uMsg == MCI_SET)
		{
			LPMCI_SET_PARMS parms = (LPVOID)dwParam;

			dprintf("  MCI_SET\r\n");

			if (fdwCommand & MCI_SET_TIME_FORMAT)
			{
				dprintf("	 MCI_SET_TIME_FORMAT\r\n");

				time_format = parms->dwTimeFormat;

				if (parms->dwTimeFormat == MCI_FORMAT_BYTES)
				{
					dprintf("	   MCI_FORMAT_BYTES\r\n");
				}

				if (parms->dwTimeFormat == MCI_FORMAT_FRAMES)
				{
					dprintf("	   MCI_FORMAT_FRAMES\r\n");
				}

				if (parms->dwTimeFormat == MCI_FORMAT_HMS)
				{
					dprintf("	   MCI_FORMAT_HMS\r\n");
				}

				if (parms->dwTimeFormat == MCI_FORMAT_MILLISECONDS)
				{
					dprintf("	   MCI_FORMAT_MILLISECONDS\r\n");
					// Write time format:
					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					WriteFile(Mailslot, "2 mci_time", 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
				}

				if (parms->dwTimeFormat == MCI_FORMAT_MSF)
				{
					dprintf("	   MCI_FORMAT_MSF\r\n");
					// Write time format:
					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					WriteFile(Mailslot, "0 mci_time", 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
				}

				if (parms->dwTimeFormat == MCI_FORMAT_SAMPLES)
				{
					dprintf("	   MCI_FORMAT_SAMPLES\r\n");
				}

				if (parms->dwTimeFormat == MCI_FORMAT_TMSF)
				{
					dprintf("	   MCI_FORMAT_TMSF\r\n");
					// Write time format:
					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					WriteFile(Mailslot, "1 mci_time", 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
				}
			}
			if (fdwCommand & MCI_NOTIFY)
			{
				dprintf("  MCI_NOTIFY\r\n");
				dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
				SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
				Sleep(50);
			}
		}

		if (uMsg == MCI_SEEK)
		{
			LPMCI_SEEK_PARMS parms = (LPVOID)dwParam;

			dprintf("  MCI_SEEK\r\n");
			if(mode==2 && notfy_flag)SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_ABORTED, MAGIC_DEVICEID);
			notfy_flag = 0;

			// Write MCI_STOP: 
			HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(Mailslot, "mci_stop", 64, &BytesWritten, NULL);
			CloseHandle(Mailslot);

			// Wait for mode change. Max 3000msec sleep.
			int counter = 0;
			while(mode == 2 && counter < 300)
			{
				Sleep(10); // Wait for mode change. 
				counter ++;
			}

			mode = 1;

			if (fdwCommand & MCI_SEEK_TO_START)
			{
				dprintf("	 Seek to firstTrack \r\n");
				char mci_seek_string[] = "seek cdaudio to start";
				HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				WriteFile(Mailslot, mci_seek_string, 64, &BytesWritten, NULL);
				CloseHandle(Mailslot);
			}

			if (fdwCommand & MCI_SEEK_TO_END)
			{
				dprintf("	 Seek to end of disc\r\n");
				char mci_seek_string[] = "seek cdaudio to end";
				HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				WriteFile(Mailslot, mci_seek_string, 64, &BytesWritten, NULL);
				CloseHandle(Mailslot);
			}
			
			if (fdwCommand & MCI_TO)
			{
				dprintf("	 dwTo:	 %d\r\n", parms->dwTo);

				if (time_format == MCI_FORMAT_TMSF)
				{
					// Write MCI_FROM:
					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					snprintf(dwFrom_str, 64, "%d %d %d %d", MCI_TMSF_FRAME(parms->dwTo), 
					MCI_TMSF_SECOND(parms->dwTo), MCI_TMSF_MINUTE(parms->dwTo), MCI_TMSF_TRACK(parms->dwTo));
					WriteFile(Mailslot, strcat(dwFrom_str, " mci_from"), 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
					
					dprintf("	   TRACK  %d\n", MCI_TMSF_TRACK(parms->dwTo));
					dprintf("	   MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwTo));
					dprintf("	   SECOND %d\n", MCI_TMSF_SECOND(parms->dwTo));
					dprintf("	   FRAME  %d\n", MCI_TMSF_FRAME(parms->dwTo));
				}
				else if (time_format == MCI_FORMAT_MILLISECONDS)
				{
					// Write MCI_FROM:
					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					snprintf(dwFrom_str, 64, "0 0 0 %d", parms->dwTo);
					WriteFile(Mailslot, strcat(dwFrom_str, " mci_from"), 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
					
					dprintf("	   milliseconds %d\n", parms->dwTo);
				}
				else // MCI_FORMAT_MSF
				{
					// Write MCI_FROM:
					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					snprintf(dwFrom_str, 64, "%d %d %d 0", MCI_MSF_FRAME(parms->dwTo), 
					MCI_MSF_SECOND(parms->dwTo), MCI_MSF_MINUTE(parms->dwTo));
					WriteFile(Mailslot, strcat(dwFrom_str, " mci_from"), 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
					
					dprintf("	   MINUTE %d\n", MCI_MSF_MINUTE(parms->dwTo));
					dprintf("	   SECOND %d\n", MCI_MSF_SECOND(parms->dwTo));
					dprintf("	   FRAME  %d\n", MCI_MSF_FRAME(parms->dwTo));
				}
			}
			if (fdwCommand & MCI_NOTIFY)
			{
				dprintf("  MCI_NOTIFY\r\n");
				dprintf("  Sending MCI_NOTIFY_SUCCESSFUL message...\r\n");
				SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAGIC_DEVICEID);
				Sleep(50);
			}
		}

		if (fdwCommand & MCI_WAIT)
		{
			dprintf("  MCI_WAIT\r\n");
		}

		if (uMsg == MCI_CLOSE)
		{
			dprintf("  MCI_CLOSE\r\n");
			time_format = MCI_FORMAT_MSF;
		}

		if (uMsg == MCI_PLAY)
		{

			LPMCI_PLAY_PARMS parms = (LPVOID)dwParam;

			dprintf("  MCI_PLAY\r\n");

			if (fdwCommand & MCI_NOTIFY)
			{
				dprintf("  MCI_NOTIFY\r\n");
				
				notfy_flag = 1;

				// Write notify message reguest:
				HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				WriteFile(Mailslot, "mci_notify", 64, &BytesWritten, NULL);
				CloseHandle(Mailslot);
			}

			if (fdwCommand & MCI_FROM)
			{
				if (time_format == MCI_FORMAT_MSF)
				{
					// Write MCI_FROM:
					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					snprintf(dwFrom_str, 64, "%d %d %d 0", MCI_MSF_FRAME(parms->dwFrom), 
					MCI_MSF_SECOND(parms->dwFrom), MCI_MSF_MINUTE(parms->dwFrom));
					WriteFile(Mailslot, strcat(dwFrom_str, " mci_from"), 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
					
					dprintf("	 dwFrom:\r\n ");
					dprintf("	   MINUTE %d\n", MCI_MSF_MINUTE(parms->dwFrom));
					dprintf("	   SECOND %d\n", MCI_MSF_SECOND(parms->dwFrom));
					dprintf("	   FRAME  %d\n", MCI_MSF_FRAME(parms->dwFrom));
				}

				if (time_format == MCI_FORMAT_TMSF)
				{
					// Write MCI_FROM:
					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					snprintf(dwFrom_str, 64, "%d %d %d %d", MCI_TMSF_FRAME(parms->dwFrom), 
					MCI_TMSF_SECOND(parms->dwFrom), MCI_TMSF_MINUTE(parms->dwFrom), MCI_TMSF_TRACK(parms->dwFrom));
					WriteFile(Mailslot, strcat(dwFrom_str, " mci_from"), 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
					
					dprintf("	 dwFrom:\r\n ");
					dprintf("	   TRACK  %d\n", MCI_TMSF_TRACK(parms->dwFrom));
					dprintf("	   MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwFrom));
					dprintf("	   SECOND %d\n", MCI_TMSF_SECOND(parms->dwFrom));
					dprintf("	   FRAME  %d\n", MCI_TMSF_FRAME(parms->dwFrom));
				}

				else if (time_format == MCI_FORMAT_MILLISECONDS)
				{
					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					snprintf(dwFrom_str, 64, "0 0 0 %d", parms->dwFrom);
					WriteFile(Mailslot, strcat(dwFrom_str, " mci_from"), 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
					
					dprintf("	 dwFrom:   \r\n ");
					dprintf("	   milliseconds %d\n", parms->dwFrom);
				}
			}

			if (fdwCommand & MCI_TO)
			{
				if (time_format == MCI_FORMAT_MSF)
				{
					// Write MCI_TO:
					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					snprintf(dwTo_str, 64, "%d %d %d 0", MCI_MSF_FRAME(parms->dwTo), 
					MCI_MSF_SECOND(parms->dwTo), MCI_MSF_MINUTE(parms->dwTo));
					WriteFile(Mailslot, strcat(dwTo_str, " mci_to"), 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
					
					dprintf("	 dwTo:	 \r\n");
					dprintf("	   MINUTE %d\n", MCI_MSF_MINUTE(parms->dwTo));
					dprintf("	   SECOND %d\n", MCI_MSF_SECOND(parms->dwTo));
					dprintf("	   FRAME  %d\n", MCI_MSF_FRAME(parms->dwTo));
				}

				if (time_format == MCI_FORMAT_TMSF)
				{
					// Write MCI_TO:
					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					snprintf(dwTo_str, 64, "%d %d %d %d", MCI_TMSF_FRAME(parms->dwTo), 
					MCI_TMSF_SECOND(parms->dwTo), MCI_TMSF_MINUTE(parms->dwTo), MCI_TMSF_TRACK(parms->dwTo));
					WriteFile(Mailslot, strcat(dwTo_str, " mci_to"), 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
					
					dprintf("	 dwTo:	 \r\n");
					dprintf("	   TRACK  %d\n", MCI_TMSF_TRACK(parms->dwTo));
					dprintf("	   MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwTo));
					dprintf("	   SECOND %d\n", MCI_TMSF_SECOND(parms->dwTo));
					dprintf("	   FRAME  %d\n", MCI_TMSF_FRAME(parms->dwTo));
				}

				else if (time_format == MCI_FORMAT_MILLISECONDS)
				{
					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					snprintf(dwFrom_str, 64, "0 0 0 %d", parms->dwTo);
					WriteFile(Mailslot, strcat(dwFrom_str, " mci_to"), 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
					
					dprintf("	 dwTo:	 \r\n");
					dprintf("	   milliseconds %d\n", parms->dwTo);
				}
			}

			// Write MCI_PLAY: 
			HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(Mailslot, "mci_play", 64, &BytesWritten, NULL);
			CloseHandle(Mailslot);

			// Wait for mode change. Max 3000msec sleep.
			/*int counter = 0;
			while(mode == 1 && counter < 300)
			{
				Sleep(10); // Wait for mode change. 
				counter ++;
			}*/

			mode = 2;
		}

		if (uMsg == MCI_PAUSE || uMsg == MCI_STOP)
		{
			if(uMsg == MCI_STOP)dprintf("  MCI_STOP\r\n");
			if(uMsg == MCI_PAUSE)dprintf("  MCI_PAUSE\r\n");
			if(mode==2 && notfy_flag)SendMessageA((HWND)0xffff, MM_MCINOTIFY, MCI_NOTIFY_ABORTED, MAGIC_DEVICEID);
			notfy_flag = 0;

			// Write MCI_STOP: 
			HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(Mailslot, "mci_stop", 64, &BytesWritten, NULL);
			CloseHandle(Mailslot);
			
			// Wait for mode change. Max 3000msec sleep.
			int counter = 0;
			while(mode == 2 && counter < 300)
			{
				Sleep(10); // Wait for mode change. 
				counter ++;
			}

			mode = 1;
		}

		if (uMsg == MCI_INFO)
		{
			dprintf("  MCI_INFO\n");
			LPMCI_INFO_PARMS parms = (LPVOID)dwParam;

			if(fdwCommand & MCI_INFO_PRODUCT)
			{
				dprintf("	 MCI_INFO_PRODUCT\n");
				memcpy((LPVOID)(parms->lpstrReturn), (LPVOID)&"CD Audio", 9);
				dprintf("		 Return: %s\r\n", parms->lpstrReturn);
			}

			if(fdwCommand & MCI_INFO_MEDIA_IDENTITY)
			{
				dprintf("	 MCI_INFO_MEDIA_IDENTITY\n");
				memcpy((LPVOID)(parms->lpstrReturn), (LPVOID)&"12345678", 9);
				dprintf("		 Return: %s\r\n", parms->lpstrReturn);
			}
		}

		// Handling of MCI_SYSINFO (Heavy Gear, Battlezone2, Interstate 76) 
		if (uMsg == MCI_SYSINFO)
		{
			dprintf("  MCI_SYSINFO\r\n");
			LPMCI_SYSINFO_PARMSA parms = (LPVOID)dwParam;

			if(fdwCommand & MCI_SYSINFO_QUANTITY)
			{
				dprintf("	 MCI_SYSINFO_QUANTITY\r\n");
				memcpy((LPVOID)(parms->lpstrReturn), (LPVOID)&"1", 2); // quantity = 1 
				//parms->dwRetSize = sizeof(DWORD);
				//parms->dwNumber = MAGIC_DEVICEID;
				dprintf("		 Return: %s\r\n", parms->lpstrReturn);
			}

			if(fdwCommand & MCI_SYSINFO_NAME || fdwCommand & MCI_SYSINFO_INSTALLNAME)
			{
				dprintf("	 MCI_SYSINFO_NAME\r\n");
				memcpy((LPVOID)(parms->lpstrReturn), (LPVOID)&"cdaudio", 8); // name = cdaudio 
				//parms->dwRetSize = sizeof(DWORD);
				//parms->dwNumber = MAGIC_DEVICEID;
				dprintf("		 Return: %s\r\n", parms->lpstrReturn);
			}
		}

		if (uMsg == MCI_STATUS)
		{
			LPMCI_STATUS_PARMS parms = (LPVOID)dwParam;

			dprintf("  MCI_STATUS\r\n");

			parms->dwReturn = 0;

			if (fdwCommand & MCI_TRACK)
			{
				dprintf("	 MCI_TRACK\r\n");
				dprintf("	   dwTrack = %d\r\n", parms->dwTrack);
			}

			if (fdwCommand & MCI_STATUS_ITEM)
			{
				dprintf("	 MCI_STATUS_ITEM\r\n");

				if (parms->dwItem == MCI_STATUS_CURRENT_TRACK)
				{
					dprintf("	   MCI_STATUS_CURRENT_TRACK\r\n");

					HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					WriteFile(Mailslot, "current_track", 64, &BytesWritten, NULL);
					CloseHandle(Mailslot);
					
					// Wait for response:
					int counter = 0;
					while(mciStatusRet == 0 && counter < 30)
					{
						Sleep(10);
						counter ++;
					}
					parms->dwReturn = mciStatusRet;
					mciStatusRet = 0;
				}

				if (parms->dwItem == MCI_STATUS_LENGTH)
				{
					dprintf("	   MCI_STATUS_LENGTH\r\n");

					// Get track length 
					if(fdwCommand & MCI_TRACK)
					{
						mciStatusRet = 0;
						// Write track_length request: 
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						snprintf(dwFrom_str, 64, "%d", parms->dwTrack);
						WriteFile(Mailslot, strcat(dwFrom_str, " track_length"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						
						if (time_format == MCI_FORMAT_MILLISECONDS)
						{
							// Wait for response:
							int counter = 0;
							while(mciStatusRet == 0 && counter < 30)
							{
								Sleep(10);
								counter ++;
							}
							parms->dwReturn = mciStatusRet;
							mciStatusRet = 0;
						}
						else // MSF & TMSF
						{
							/*
							Sleep(50);
							HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
							WriteFile(Mailslot, strcat(dwFrom_str, " track_length"), 64, &BytesWritten, NULL);
							CloseHandle(Mailslot);
							*/
							
							// Wait for response:
							int counter = 0;
							while(mciStatusRet == 0 && counter < 30)
							{
								Sleep(10);
								counter ++;
							}
							
							parms->dwReturn = MCI_MAKE_MSF(m_s,s_s,f_s);
							mciStatusRet = 0;
							m_s=0;
							s_s=0;
							f_s=0;
						}
					}
					// Get full length 
					else
					{
						mciStatusRet = 0;
						// Write full_length request: 
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, "full_length", 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						
						if (time_format == MCI_FORMAT_MILLISECONDS)
						{
							// Wait for response:
							int counter = 0;
							while(mciStatusRet == 0 && counter < 30)
							{
								Sleep(10);
								counter ++;
							}
							parms->dwReturn = mciStatusRet;
							mciStatusRet = 0;
						}
						else // MSF & TMSF
						{
							// Wait for response:
							int counter = 0;
							while(mciStatusRet == 0 && counter < 30)
							{
								Sleep(10);
								counter ++;
							}
							parms->dwReturn = MCI_MAKE_MSF(m_s,s_s,f_s);
							mciStatusRet = 0;
							m_s=0;
							s_s=0;
							f_s=0;
						}
					}
				}

				if (parms->dwItem == MCI_CDA_STATUS_TYPE_TRACK) {
					// ref. by WinQuake 
					dprintf("> MCI_CDA_STATUS_TYPE_TRACK\n");
					if(AllMusicTracks){
						parms->dwReturn = MCI_CDA_TRACK_AUDIO;
					}
					else{
						if(parms->dwTrack == 1) parms->dwReturn = MCI_CDA_TRACK_OTHER;
						else parms->dwReturn = MCI_CDA_TRACK_AUDIO;
					}
				}

				if (parms->dwItem == MCI_STATUS_MEDIA_PRESENT)
				{
					dprintf("	   MCI_STATUS_MEDIA_PRESENT\r\n");
					parms->dwReturn = TRUE;
				}

				if (parms->dwItem == MCI_STATUS_NUMBER_OF_TRACKS)
				{
					dprintf("	   MCI_STATUS_NUMBER_OF_TRACKS\r\n");
					if(numTracks == 0){
						// Ask for no. of tracks:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, "mci_tracks", 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
					}
					// Wait for response:
					int counter = 0;
					while(numTracks == 0 && counter < 500)
					{
						Sleep(10);
						counter ++;
					}
					if(numTracks == 0)parms->dwReturn = 99;
					else parms->dwReturn = numTracks;
				}

				if (parms->dwItem == MCI_STATUS_POSITION)
				{
					// Track position 
					dprintf("	   MCI_STATUS_POSITION\r\n");

					if (fdwCommand & MCI_TRACK)
					{
						mciStatusRet = 0;
						// Write track_pos request: 
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						snprintf(dwFrom_str, 64, "%d", parms->dwTrack);
						WriteFile(Mailslot, strcat(dwFrom_str, " track_pos"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						
						if (time_format == MCI_FORMAT_MILLISECONDS)
						{
							// Wait for response:
							int counter = 0;
							while(mciStatusRet == 0 && counter < 30)
							{
								Sleep(10);
								counter ++;
							}
							parms->dwReturn = mciStatusRet;
							mciStatusRet = 0;
						}
						// TMSF
						else if (time_format == MCI_FORMAT_TMSF)
						{							
							// Wait for response:
							int counter = 0;
							while(mciStatusRet == 0 && counter < 30)
							{
								Sleep(10);
								counter ++;
							}
							parms->dwReturn = MCI_MAKE_TMSF(tt_s,tm_s,ts_s,tf_s);
							mciStatusRet = 0;
							tt_s=0;
							tm_s=0;
							ts_s=0;
							tf_s=0;
						}
						// MSF
						else
						{
							// Wait for response:
							int counter = 0;
							while(mciStatusRet == 0 && counter < 30)
							{
								Sleep(10);
								counter ++;
							}
							parms->dwReturn = MCI_MAKE_MSF(m_s,s_s,f_s);
							mciStatusRet = 0;
							m_s=0;
							s_s=0;
							f_s=0;
						}
					}

					// Current position
					else {
						mciStatusRet = 0;
						// Write cur_pos request: 
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, "cur_pos", 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						
						if (time_format == MCI_FORMAT_MILLISECONDS)
						{
							// Wait for response:
							int counter = 0;
							while(mciStatusRet == 0 && counter < 30)
							{
								Sleep(10);
								counter ++;
							}
							parms->dwReturn = mciStatusRet;
							mciStatusRet = 0;
						}
						// TMSF
						else if (time_format == MCI_FORMAT_TMSF)
						{
							// Wait for response:
							int counter = 0;
							while(mciStatusRet == 0 && counter < 30)
							{
								Sleep(10);
								counter ++;
							}
							parms->dwReturn = MCI_MAKE_TMSF(tt_s,tm_s,ts_s,tf_s);
							mciStatusRet = 0;
							tt_s=0;
							tm_s=0;
							ts_s=0;
							tf_s=0;
						}
						// MSF
						else
						{
							// Wait for response:
							int counter = 0;
							while(mciStatusRet == 0 && counter < 30)
							{
								Sleep(10);
								counter ++;
							}
							parms->dwReturn = MCI_MAKE_MSF(m_s,s_s,f_s);
							mciStatusRet = 0;
							m_s=0;
							s_s=0;
							f_s=0;
						}
					}
				}

				if (parms->dwItem == MCI_STATUS_MODE)
				{
					dprintf("	   MCI_STATUS_MODE\r\n");
					if(mode == 1){
						parms->dwReturn = MCI_MODE_STOP;
						dprintf("		 Stopped\r\n");
					}
					else{
						parms->dwReturn = MCI_MODE_PLAY;
						dprintf("		 Playing\r\n");
					}
				}

				if (parms->dwItem == MCI_STATUS_READY) {
					// referenced by Quake/cd_win.c
					dprintf("> MCI_STATUS_READY\n");
					parms->dwReturn = TRUE;
				}

				if (parms->dwItem == MCI_STATUS_TIME_FORMAT)
				{
					dprintf("	   MCI_STATUS_TIME_FORMAT\r\n");
					parms->dwReturn = time_format;
				}

				if (parms->dwItem == MCI_STATUS_START)
				{
					dprintf("	   MCI_STATUS_START\r\n");
					if (time_format == MCI_FORMAT_MILLISECONDS)
						parms->dwReturn = 2001;
					else if (time_format == MCI_FORMAT_MSF)
						parms->dwReturn = MCI_MAKE_MSF(0, 2, 0);
					else //TMSF
						parms->dwReturn = MCI_MAKE_TMSF(1, 0, 0, 0);
				}
			}

			dprintf("  dwReturn %d\n", parms->dwReturn);

		}

		return 0;
	}

	else return relay_mciSendCommandA(IDDevice, uMsg, fdwCommand, dwParam); // Added MCI relay 
}

MCIERROR WINAPI fake_mciSendStringA(LPCTSTR cmd, LPTSTR ret, UINT cchReturn, HANDLE hwndCallback)
{
	if (!once) {
		once = 1;
		Sleep(StartDelayMs); // Sleep a bit to ensure cdaudioplr.exe is initialized.
	}

	char cmdbuf[1024];
	char cmp_str[1024];

	dprintf("[MCI String = %s]\n", cmd);

	// copy cmd into cmdbuf 
	strcpy (cmdbuf,cmd);
	// change cmdbuf into lower case 
	for (int i = 0; cmdbuf[i]; i++)
	{
		cmdbuf[i] = tolower(cmdbuf[i]);
	}

	// handle info
	sprintf(cmp_str, "info %s", alias_s);
	if (strstr(cmdbuf, cmp_str))
	{
		if (strstr(cmdbuf, "identity"))
		{
			dprintf("  Returning identity: 12345678\r\n");
			strcpy(ret, "12345678");
			return 0;
		}
		
		if (strstr(cmdbuf, "product"))
		{
			dprintf("  Returning product: CD Audio\r\n");
			strcpy(ret, "CD Audio");
			return 0;
		}
	}

	// MCI_GETDEVCAPS SendString equivalent 
	sprintf(cmp_str, "capability %s", alias_s);
	if (strstr(cmdbuf, cmp_str))
	{
		if (strstr(cmdbuf, "device type")){
			strcpy(ret, "cdaudio");
		}
		else if (strstr(cmdbuf, "can eject")){
			strcpy(ret, "true");
		}
		else if (strstr(cmdbuf, "can play")){
			strcpy(ret, "true");
		}
		else if (strstr(cmdbuf, "has audio")){
			strcpy(ret, "true");
		}
		else{
			strcpy(ret, "false");
		}
		return 0;
	}

	// Handle sysinfo (does not use alias!)
	if (strstr(cmdbuf, "sysinfo cdaudio")){
		if (strstr(cmdbuf, "quantity"))
		{
			dprintf("  Returning quantity: 1\r\n");
			strcpy(ret, "1");
			return 0;
		}
		/* Example: "sysinfo cdaudio name 1 open" returns "cdaudio" or the alias.*/
		if (strstr(cmdbuf, "name"))
		{
			if (strstr(cmdbuf, "open")){
				dprintf("  Returning alias name: %s\r\n",alias_s);
				sprintf(ret, "%s", alias_s);
				return 0;
			}
		}
		if (strstr(cmdbuf, "name") || strstr(cmdbuf, "installname"))
		{
			dprintf("  Returning name: cdaudio\r\n");
			strcpy(ret, "cdaudio");
			return 0;
		}
	}

	// Handle "stop cdaudio/alias" 
	sprintf(cmp_str, "stop %s", alias_s);
	if (strstr(cmdbuf, cmp_str))
	{
		fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STOP, 0, (DWORD_PTR)NULL);
		return 0;
	}

	// Handle "pause cdaudio/alias" 
	sprintf(cmp_str, "pause %s", alias_s);
	if (strstr(cmdbuf, cmp_str))
	{
		fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PAUSE, 0, (DWORD_PTR)NULL);
		return 0;
	}

	// Handle "open"
	if (strstr(cmdbuf, "open")){
		/* Look for the use of an alias */
		/* Example: "open d: type cdaudio alias cd1" */
		if (strstr(cmdbuf, "type cdaudio alias"))
		{
	        //Remove wait from the string
			if (strstr(cmdbuf, "wait")){
				
				char word[20] = " wait";
				int i, j, len_str, len_word, temp, chk=0;
				
				len_str = strlen(cmdbuf);
				len_word = strlen(word);
				for(i=0; i<len_str; i++){
					temp = i;
					for(j=0; j<len_word; j++){
						if(cmdbuf[i]==word[j])
						i++;
					}
					chk = i-temp;
					if(chk==len_word){
						i = temp;
						for(j=i; j<(len_str-len_word); j++)
						cmdbuf[j] = cmdbuf[j+len_word];
						len_str = len_str-len_word;
						cmdbuf[j]='\0';
					}
				}
			}
			
			char *tmp_s = strrchr(cmdbuf, ' ');
			if (tmp_s && *(tmp_s +1))
			{
				sprintf(alias_s, "%s", tmp_s +1);
			}
			//char devid_str[100];
			//sprintf(devid_str, "%d", MAGIC_DEVICEID);
			//strcpy(ret, devid_str);
			//opened = 1;
			return 0;
		}
		/* Look for the use of an alias */
		/* Example: "open cdaudio alias cd1" */
		if (strstr(cmdbuf, "open cdaudio alias"))
		{
	        //Remove wait from the string
			if (strstr(cmdbuf, "wait")){
				
				char word[20] = " wait";
				int i, j, len_str, len_word, temp, chk=0;
				
				len_str = strlen(cmdbuf);
				len_word = strlen(word);
				for(i=0; i<len_str; i++){
					temp = i;
					for(j=0; j<len_word; j++){
						if(cmdbuf[i]==word[j])
						i++;
					}
					chk = i-temp;
					if(chk==len_word){
						i = temp;
						for(j=i; j<(len_str-len_word); j++)
						cmdbuf[j] = cmdbuf[j+len_word];
						len_str = len_str-len_word;
						cmdbuf[j]='\0';
					}
				}
			}
			
			char *tmp_s = strrchr(cmdbuf, ' ');
			if (tmp_s && *(tmp_s +1))
			{
				sprintf(alias_s, "%s", tmp_s +1);
				dprintf("alias is: %s\n",alias_s);
			}
			//char devid_str[100];
			//sprintf(devid_str, "%d", MAGIC_DEVICEID);
			//strcpy(ret, devid_str);
			//opened = 1;
			return 0;
		}
		// Normal open cdaudio
		if (strstr(cmdbuf, "open cdaudio"))
		{
			//char devid_str[100];
			//sprintf(devid_str, "%d", MAGIC_DEVICEID);
			//strcpy(ret, devid_str);
			//opened = 1;
			return 0;
		}
	}

	// reset alias with "close alias" string 
	sprintf(cmp_str, "close %s", alias_s);
	if (strstr(cmdbuf, cmp_str))
	{
		sprintf(alias_s, "cdaudio");
		time_format = MCI_FORMAT_MSF; // reset time format
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
		if (strstr(cmdbuf, "ms")) // Another accepted string for milliseconds
		{
			static MCI_SET_PARMS parms;
			parms.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
			return 0;
		}
	}

	// Handle "status cdaudio/alias" 
	sprintf(cmp_str, "status %s", alias_s);
	if (strstr(cmdbuf, cmp_str)){
		if (strstr(cmdbuf, "time format"))
		{
			if(time_format==MCI_FORMAT_MILLISECONDS){
				strcpy(ret, "milliseconds");
				return 0;
			}
			if(time_format==MCI_FORMAT_TMSF){
				strcpy(ret, "tmsf");
				return 0;
			}
			if(time_format==MCI_FORMAT_MSF){
				strcpy(ret, "msf");
				return 0;
			}
		}
		if (strstr(cmdbuf, "number of tracks"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			sprintf(ret, "%d", numTracks);
			dprintf("  Returning number of tracks (%d)\r\n", numTracks);
			return 0;
		}

		if (strstr(cmdbuf, "current track"))
		{
			//static MCI_STATUS_PARMS parms;
			//parms.dwItem = MCI_STATUS_CURRENT_TRACK;
			//fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			//sprintf(ret, "%d", parms.dwReturn);
			//dprintf("  Current track is (%d)\r\n", parms.dwReturn);
			
			HANDLE Mailslot8 = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(Mailslot8, "current_track", 64, &BytesWritten, NULL);
			CloseHandle(Mailslot8);
					
			// Wait for response:
			int counter = 0;
			while(mciStatusRet == 0 && counter < 30)
			{
				Sleep(10);
				counter ++;
			}
			sprintf(ret, "%d", mciStatusRet);
			dprintf("  Current track is (%d)\r\n", mciStatusRet);
			mciStatusRet = 0;
			return 0;
		}

		int track = 0;
		if (sscanf(cmdbuf, "status %*s type track %d", &track) == 1)
		{
			if(AllMusicTracks){
				strcpy(ret, "audio");
				return 0;
			}
			else{
				if(track == 1)strcpy(ret, "other");
				else strcpy(ret, "audio");
				return 0;
			}
		}
		if (sscanf(cmdbuf, "status %*s length track %d", &track) == 1)
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_LENGTH;
			parms.dwTrack = track;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			if(time_format == MCI_FORMAT_MILLISECONDS){
				sprintf(ret, "%d", parms.dwReturn);
			}
			if(time_format == MCI_FORMAT_MSF || time_format == MCI_FORMAT_TMSF){
				sprintf(ret, "%02d:%02d:%02d", MCI_MSF_MINUTE(parms.dwReturn), MCI_MSF_SECOND(parms.dwReturn), MCI_MSF_FRAME(parms.dwReturn));
			}
			return 0;
		}
		if (strstr(cmdbuf, "length"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_LENGTH;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			if(time_format == MCI_FORMAT_MILLISECONDS){
				sprintf(ret, "%d", parms.dwReturn);
			}
			if(time_format == MCI_FORMAT_MSF || time_format == MCI_FORMAT_TMSF){
				sprintf(ret, "%02d:%02d:%02d", MCI_MSF_MINUTE(parms.dwReturn), MCI_MSF_SECOND(parms.dwReturn), MCI_MSF_FRAME(parms.dwReturn));
			}
			return 0;
		}
		if (sscanf(cmdbuf, "status %*s position track %d", &track) == 1)
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_POSITION;
			parms.dwTrack = track;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
			if(time_format == MCI_FORMAT_MILLISECONDS){
				sprintf(ret, "%d", parms.dwReturn);
			}
			if(time_format == MCI_FORMAT_MSF){
				sprintf(ret, "%02d:%02d:%02d", MCI_MSF_MINUTE(parms.dwReturn), MCI_MSF_SECOND(parms.dwReturn), MCI_MSF_FRAME(parms.dwReturn));
			}
			if(time_format == MCI_FORMAT_TMSF){
				sprintf(ret, "%02d:%02d:%02d:%02d", MCI_TMSF_TRACK(parms.dwReturn), MCI_TMSF_MINUTE(parms.dwReturn), MCI_TMSF_SECOND(parms.dwReturn), MCI_TMSF_FRAME(parms.dwReturn));
			}
			return 0;
		}
		if (strstr(cmdbuf, "start position"))
		{
			if(time_format == MCI_FORMAT_MILLISECONDS){
				strcpy(ret, "2001");
			}
			if(time_format == MCI_FORMAT_MSF){
				strcpy(ret, "00:02:00");
			}
			if(time_format == MCI_FORMAT_TMSF){
				strcpy(ret, "01:00:00:00");
			}
			return 0;
		}
		if (strstr(cmdbuf, "position"))
		{
			static MCI_STATUS_PARMS parms;
			parms.dwItem = MCI_STATUS_POSITION;
			fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
			if(time_format == MCI_FORMAT_MILLISECONDS){
				sprintf(ret, "%d", parms.dwReturn);
			}
			if(time_format == MCI_FORMAT_MSF){
				sprintf(ret, "%02d:%02d:%02d", MCI_MSF_MINUTE(parms.dwReturn), MCI_MSF_SECOND(parms.dwReturn), MCI_MSF_FRAME(parms.dwReturn));
			}
			if(time_format == MCI_FORMAT_TMSF){
				sprintf(ret, "%02d:%02d:%02d:%02d", MCI_TMSF_TRACK(parms.dwReturn), MCI_TMSF_MINUTE(parms.dwReturn), MCI_TMSF_SECOND(parms.dwReturn), MCI_TMSF_FRAME(parms.dwReturn));
			}
			return 0;
		}
		if (strstr(cmdbuf, "media present"))
		{
			strcpy(ret, "TRUE");
			return 0;
		}
		// Add: Mode handling 
		if (strstr(cmdbuf, "mode"))
		{
			if(mode == 1){
				dprintf("	-> stopped\r\n");
				strcpy(ret, "stopped");
				}
			else{
				dprintf("	-> playing\r\n");
				strcpy(ret, "playing");
			}
			return 0;
		}
	}

	// Handle "seek cdaudio/alias"
	sprintf(cmp_str, "seek %s", alias_s);
	if (strstr(cmdbuf, cmp_str)){

		const char* s; //const char is needed for the replacement to work.
		s = cmdbuf;
		
		// Replace any "alias" name with cdaudio:
		char rewrite[] = "cdaudio";
		char* mci_seek_string;
		int i, cnt = 0;
		int Newlength = strlen(rewrite);
		int Oldlength = strlen(alias_s);

		for (i = 0; s[i] != '\0'; i++) {
			if (strstr(&s[i], alias_s) == &s[i]) {
				cnt++;
				i += Oldlength - 1;
			}
		}
		mci_seek_string = (char*)malloc(i + cnt * (Newlength - Oldlength) + 1);
		i = 0;
		while (*s) {
			if (strstr(s, alias_s) == s) {
				strcpy(&mci_seek_string[i], rewrite);
				i += Newlength;
				s += Oldlength;
			}
			else {
				mci_seek_string[i++] = *s++;
			}
		}
		mci_seek_string[i] = '\0';

		// Send rewritten string to the player: 
		HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(Mailslot, mci_seek_string, 64, &BytesWritten, NULL);
		CloseHandle(Mailslot);

		// Wait for mode change. Max 3000msec sleep.
		int counter = 0;
		while(mode == 2 && counter < 300)
		{
			Sleep(10); // Wait for mode change. 
			counter ++;
		}
		mode = 1; // Seek stops playback

		return 0;
	}

	// Handle "play cdaudio/alias" 
	sprintf(cmp_str, "play %s", alias_s);
	if (strstr(cmdbuf, cmp_str)){

		// Abort playback if "from 0" is called (TMSF Track playback only). Seen in Hunter Hunted.
		if(time_format == MCI_FORMAT_TMSF){
			int from = -1, to = -1;
			if (sscanf(cmdbuf, "play %*s from %d to %d", &from, &to) == 2){
				if(strstr(cmdbuf, "from 0"))return 0;
			}
			if (sscanf(cmdbuf, "play %*s from %d", &from) == 1){
				if(strstr(cmdbuf, "from 0"))return 0;
			}
		}

		const char* s; //const char is needed for the replacement to work.
		s = cmdbuf;

		// Replace any "alias" name with cdaudio:
		char rewrite[] = "cdaudio";
		char* mci_play_string;
		int i, cnt = 0;
		int Newlength = strlen(rewrite);
		int Oldlength = strlen(alias_s);

		for (i = 0; s[i] != '\0'; i++) {
			if (strstr(&s[i], alias_s) == &s[i]) {
				cnt++;
				i += Oldlength - 1;
			}
		}
		mci_play_string = (char*)malloc(i + cnt * (Newlength - Oldlength) + 1);
		i = 0;
		while (*s) {
			if (strstr(s, alias_s) == s) {
				strcpy(&mci_play_string[i], rewrite);
				i += Newlength;
				s += Oldlength;
			}
			else {
				mci_play_string[i++] = *s++;
			}
		}
		mci_play_string[i] = '\0';

		// Send rewritten string to the player: 
		HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(Mailslot, mci_play_string, 64, &BytesWritten, NULL);
		CloseHandle(Mailslot);

		// Wait for mode change. Max 3000msec sleep.
		/*int counter = 0;
		while(mode == 1 && counter < 300)
		{
			Sleep(10); // Wait for mode change. 
			counter ++;
		}*/
		mode=2;

		return 0;
	}

	return relay_mciSendStringA(cmd, ret, cchReturn, hwndCallback); // Added MCI relay
}

UINT WINAPI fake_auxGetNumDevs()
{
	dprintf("fake_auxGetNumDevs()\r\n");
	return 1;
}

MMRESULT WINAPI fake_auxGetDevCapsA(UINT_PTR uDeviceID, LPAUXCAPS lpCaps, UINT cbCaps)
{
	dprintf("fake_auxGetDevCapsA(uDeviceID=%08X, lpCaps=%p, cbCaps=%08X\n", uDeviceID, lpCaps, cbCaps);

	lpCaps->wMid = 2; // MM_CREATIVE
	lpCaps->wPid = 401; // MM_CREATIVE_AUX_CD
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
	if (!once) {
		once = 1;
		Sleep(StartDelayMs); // Sleep a bit to ensure cdaudioplr.exe is initialized.
	}
	
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

	dprintf("	 left : %ud (%04X)\n", left, left);
	dprintf("	 right: %ud (%04X)\n", right, right);
	
	// Write volume:
	HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	snprintf(dwVolume_str, 32, "%d", dwVolume);
	WriteFile(Mailslot, strcat(dwVolume_str, " aux_vol"), 64, &BytesWritten, NULL);
	CloseHandle(Mailslot);

	return MMSYSERR_NOERROR;
}
