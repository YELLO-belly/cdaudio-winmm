/*
	THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF 
	ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO 
	THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A 
	PARTICULAR PURPOSE.

	(c) 2020 DD

	cdaudio player using MCI Strings with the
	objective of fixing regression issue in cdaudio
	playback starting with Windows Vista. Mainly the
	lack of a working mode update after playing has
	finished (missing MCI_NOTIFY_SUCCESSFUL msg).
	
	Rewritten in 2022.
*/

#include <windows.h>
#include <stdio.h>
#include <direct.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include "resource.h"

#pragma comment(lib, "winmm.lib")
#define GetCurrentDir _getcwd

// debug logging
#ifdef _DEBUG
	#define dprintf(...) if (fh) { fprintf(fh, __VA_ARGS__); fflush(NULL); }
	FILE * fh = NULL;
#else
	#define dprintf(...)
#endif

//#define IDT_TIMER1 1

// Win32 GUI stuff:
#define IDC_MAIN_EDIT	101
#define ID_FILE_EXIT 9001
#define ID_VOLUME_0 9002
#define ID_VOLUME_10 9003
#define ID_VOLUME_20 9004
#define ID_VOLUME_30 9005
#define ID_VOLUME_40 9006
#define ID_VOLUME_50 9007
#define ID_VOLUME_60 9008
#define ID_VOLUME_70 9009
#define ID_VOLUME_80 9010
#define ID_VOLUME_90 9011
#define ID_VOLUME_100 9012
#define ID_VIEW_WDIR 9013
#define ID_HELP_INST 9014
#define ID_HELP_ABOUT 9015

// check marks:
int chkvol_0 = 0;
int chkvol_10 = 0;
int chkvol_20 = 0;
int chkvol_30 = 0;
int chkvol_40 = 0;
int chkvol_50 = 0;
int chkvol_60 = 0;
int chkvol_70 = 0;
int chkvol_80 = 0;
int chkvol_90 = 0;
int chkvol_100 = 0;

// Mailslot header stuff:
DWORD NumberOfBytesRead;
DWORD BytesWritten;
CHAR ServerName[] = "\\\\.\\Mailslot\\winmm_Mailslot";
char buffer[512];
char name[512];
int value;

const char g_szClassName[] = "myWindowClass";
char pos1[64] = "-";
char pos2[64] = "-";
char mode[64] = "";
char media[64] = "";
char tracks_s[64] = "";
char length_s[64] = "";
char pos_s[64] = "";
char cur_t_s[64] = "";
char play_cmd[512] = "";
char seek_cmd[512] = "";
char track_cmd[512] = "";
char ini_dir[256] = "";

int seek = 0;
int pos_int = 0;
int pos_t_int = 0;
int length_int = 0;
int length_t_int = 0;

int mci_from = 0;
int mci_from_m = 0;
int mci_from_s = 0;
int mci_from_f = 0;

int mci_to = 0;
int mci_to_m = 0;
int mci_to_s = 0;
int mci_to_f = 0;

int mci_tracks = 0;
int cur_track = 0;
int skip_cmd = 0;
int mci_play = 0;
int mci_play_pump = 0;
int mci_time = 0;
int mci_time_set = 0;
int mci_pause = 0;
int tracks = 0;
int notify_msg = 0;
int quit = 0;
int volume = 0xAFC8AFC8; // Default vol 70%

FILE * fp;
HANDLE reader = NULL;
HANDLE player = NULL;
HWND hEdit;
HWND g_hMainWindow = NULL;

// Mailslot reader thread:
int reader_main( void )
{
	HANDLE Mailslot;
	// Create mailslot:
	if ((Mailslot = CreateMailslot("\\\\.\\Mailslot\\cdaudioplr_Mailslot", 0, MAILSLOT_WAIT_FOREVER, NULL)) == INVALID_HANDLE_VALUE)
	{
		dprintf("mailslot error %d\n", GetLastError());
		return 0;
	}

	// Loop to read mailslot:
	while(ReadFile(Mailslot, buffer, 512, &NumberOfBytesRead, NULL) != 0)
	{

		if (NumberOfBytesRead > 0){
			dprintf("Mailslot buffer: %s\n", buffer);

				// Read mci_tracks
				if(strstr(buffer,"mci_tracks")){
					mci_tracks = 1;
					SetWindowText(hEdit, TEXT("Command: MCI_STATUS_NUMBER_OF_TRACKS"));
				}

				// Read notify msg request:
				if(strstr(buffer,"mci_notify")){
					notify_msg = 1;
					SetWindowText(hEdit, TEXT("Command: MCI_NOTIFY"));
				}

				// Read time format:
				if(strstr(buffer,"mci_time")){
					sscanf(buffer,"%d %s", &value, name);
					mci_time = value;
					mci_time_set = 1;
					SetWindowText(hEdit, TEXT("Command: MCI_SET_TIME_FORMAT"));
				}

				// Read MCI_FROM:
				if(strstr(buffer,"mci_from")){
					sscanf(buffer,"%d %d %d %d %s", &mci_from_f, &mci_from_s, &mci_from_m, &mci_from, name);
					//SetWindowText(hEdit, TEXT("Command: MCI_FROM"));
				}

				// Read MCI_TO:
				if(strstr(buffer,"mci_to")){
					sscanf(buffer,"%d %d %d %d %s", &mci_to_f, &mci_to_s, &mci_to_m, &mci_to, name);
					//SetWindowText(hEdit, TEXT("Command: MCI_TO"));
				}

				// Read MCI_PLAY:
				if(strstr(buffer,"mci_play")){
					mci_pause = 0;
					if(!mci_from && !mci_from_m && !mci_from_s && !mci_from_f && 
					!mci_to && !mci_to_m && !mci_to_s && !mci_to_f){
						snprintf(play_cmd, 512, "play cdaudio");
						skip_cmd = 1;
					}
					if(mci_play){
						mci_play_pump = 1;
					}
					else{
						mci_play = 1;
					}
					
					SetWindowText(hEdit, TEXT("Command: MCI_PLAY"));
				}

				// Read MCI_SEEK from a string:
				if(strstr(buffer,"seek cdaudio")){
					mci_pause = 0;
					mci_play = 0;
					notify_msg = 0;
					fgets(buffer,512,stdin);
					strcpy(seek_cmd,buffer);
					
					// Look for notify word and remove it from string
					if(strstr(seek_cmd,"notify")){
						
						char word[20] = "notify";
						int i, j, len_str, len_word, temp, chk=0;
						
						len_str = strlen(seek_cmd);
						len_word = strlen(word);
						for(i=0; i<len_str; i++){
							temp = i;
							for(j=0; j<len_word; j++){
								if(seek_cmd[i]==word[j])
								i++;
							}
							chk = i-temp;
							if(chk==len_word){
								i = temp;
								for(j=i; j<(len_str-len_word); j++)
								seek_cmd[j] = seek_cmd[j+len_word];
								len_str = len_str-len_word;
								seek_cmd[j]='\0';
							}
						}
					}
					seek = 1;
					SetWindowText(hEdit, TEXT("Command: MCI_SEEK(string)"));
				}

				// Read MCI_PLAY from a string:
				if(strstr(buffer,"play cdaudio")){
					mci_pause = 0;
					fgets(buffer,512,stdin);
					strcpy(play_cmd,buffer);
					
					// Look for notify word and remove it from string
					if(strstr(play_cmd,"notify")){
						notify_msg = 1;
						
						char word[20] = "notify";
						int i, j, len_str, len_word, temp, chk=0;
						
						len_str = strlen(play_cmd);
						len_word = strlen(word);
						for(i=0; i<len_str; i++){
							temp = i;
							for(j=0; j<len_word; j++){
								if(play_cmd[i]==word[j])
								i++;
							}
							chk = i-temp;
							if(chk==len_word){
								i = temp;
								for(j=i; j<(len_str-len_word); j++)
								play_cmd[j] = play_cmd[j+len_word];
								len_str = len_str-len_word;
								play_cmd[j]='\0';
							}
						}
					}
					
					skip_cmd = 1;
					if(mci_play){
						mci_play_pump = 1;
					}
					else{
						mci_play = 1;
					}
					
					SetWindowText(hEdit, TEXT("Command: MCI_PLAY(string)"));
				}

				// Read MCI_STOP:
				if(strstr(buffer,"mci_stop")){
					mci_pause = 1;
					mci_play = 0;
					notify_msg = 0;
					SetWindowText(hEdit, TEXT("Command: MCI_STOP/PAUSE"));
				}

				// Read track length:
				if(strstr(buffer,"track_length")){
					sscanf(buffer,"%d %s", &value, name);
					int mci_track;
					mci_track = value;
					SetWindowText(hEdit, TEXT("Command: MCI_STATUS_LENGTH|MCI_TRACK"));
					sprintf(track_cmd, "status cdaudio length track %d wait", mci_track);
					length_t_int = 1;
				}

				// Read full length:
				if(strstr(buffer,"full_length")){
					SetWindowText(hEdit, TEXT("Command: MCI_STATUS_LENGTH"));
					length_int = 1;
				}

				// Read track position:
				if(strstr(buffer,"track_pos")){
					sscanf(buffer,"%d %s", &value, name);
					int mci_track;
					mci_track = value;
					SetWindowText(hEdit, TEXT("Command: MCI_STATUS_POSITION|MCI_TRACK"));
					sprintf(track_cmd, "status cdaudio position track %d wait", mci_track);
					pos_t_int = 1;
				}

				// Read current position:
				if(strstr(buffer,"cur_pos")){
					SetWindowText(hEdit, TEXT("Command: MCI_STATUS_POSITION"));
					pos_int = 1;
				}

				// Read current track:
				if(strstr(buffer,"current_track")){
					SetWindowText(hEdit, TEXT("Command: Current Track"));
					cur_track = 1;
				}

				// Read aux volume:
				if(strstr(buffer,"aux_vol")){
					sscanf(buffer,"%d %s", &value, name);
					waveOutSetVolume(NULL, value);
					volume = value;
					SetWindowText(hEdit, TEXT("Command: auxSetVolume"));
				}

				// Read exit message
				if(strstr(buffer,"exit")){
					PostMessage(g_hMainWindow,WM_SHOWWINDOW,SW_RESTORE,0);
					quit = 1;
				}
		}
	}
	return 0;
}

// Player thread:
int player_main( void )
{
	// Get the working directory:
	char wdir[FILENAME_MAX];
	GetCurrentDir(wdir, FILENAME_MAX);
	sprintf(ini_dir, "%s\\cdaudio_vol.ini", wdir);


	//Set volume:
	//(Note: Since we have a separate player app 
	//the volume can also be adjusted from the 
	//Windows mixer with better accuracy...)

	int cdaudio_vol = 100; // 100 uses auxVolume

	fp = fopen (ini_dir, "r");
			// If not null read values
			if (fp!=NULL){
			fscanf(fp, "%d", &cdaudio_vol);
			fclose(fp);
		}
		// Else write new ini file
		else{
		fp = fopen (ini_dir, "w+");
		fprintf(fp, "%d\n"
			"#\n"
			"# cdaudioplr CD music volume override control.\n"
			"# Change the number to the desired volume level (0-100).\n\n"
			"[options]\n"
			"# Enable debug log\n"
			"Log = 0", cdaudio_vol);
		fclose(fp);
	}

	if (cdaudio_vol < 0) cdaudio_vol = 0;
	if (cdaudio_vol > 100) cdaudio_vol = 100;

	if (cdaudio_vol <= 0){
		waveOutSetVolume(NULL, 0x0);
		chkvol_0 = 1;
	}
	if (cdaudio_vol <= 10 && cdaudio_vol > 1){
		waveOutSetVolume(NULL, 0x1F401F40); // 8000
		chkvol_10 = 1;
	}
	if (cdaudio_vol <= 20 && cdaudio_vol > 10){
		waveOutSetVolume(NULL, 0x32C832C8); // 13000
		chkvol_20 = 1;
	}
	if (cdaudio_vol <= 30 && cdaudio_vol > 20){
		waveOutSetVolume(NULL, 0x4A384A38); // 19000
		chkvol_30 = 1;
	}
	if (cdaudio_vol <= 40 && cdaudio_vol > 30){
		waveOutSetVolume(NULL, 0x652C652C); // 25900
		chkvol_40 = 1;
	}
	if (cdaudio_vol <= 50 && cdaudio_vol > 40){
		waveOutSetVolume(NULL, 0x7D007D00); // 32000
		chkvol_50 = 1;
	}
	if (cdaudio_vol <= 60 && cdaudio_vol > 50){
		waveOutSetVolume(NULL, 0x9A4C9A4C); // 39500
		chkvol_60 = 1;
	}
	if (cdaudio_vol <= 70 && cdaudio_vol > 60){
		waveOutSetVolume(NULL, 0xAFC8AFC8); // 45000
		chkvol_70 = 1;
	}
	if (cdaudio_vol <= 80 && cdaudio_vol > 70){
		waveOutSetVolume(NULL, 0xCB20CB20); // 52000
		chkvol_80 = 1;
	}
	if (cdaudio_vol <= 90 && cdaudio_vol > 80){
		waveOutSetVolume(NULL, 0xE290E290); // 58000
		chkvol_90 = 1;
	}
	if (cdaudio_vol >= 100 && cdaudio_vol > 90){
		waveOutSetVolume(NULL, volume); // Use AuxVol
		chkvol_100 = 1;
	}

	mciSendStringA("status cdaudio media present wait", media, 64, NULL);
	// Loop to check if CD is inserted: 
	while(strcmp(media,"true")!=0)
	{
		SetWindowText(hEdit, TEXT("Looking for CD ..."));
		mciSendStringA("status cdaudio media present wait", media, 64, NULL);
		Sleep(100);
	}
	SetWindowText(hEdit, TEXT("CD Found... READY."));
	
	mciSendStringA("status cdaudio number of tracks wait", tracks_s, 64, NULL);
	sscanf(tracks_s, "%d", &tracks);

	// Close/Open cdaudio: 
	mciSendStringA("close cdaudio wait", NULL, 0, NULL); // Important! 
	mciSendStringA("open cdaudio wait", NULL, 0, NULL);

	//Instead of opening a handle then writing to the mailslot and closing the handle again one could simply open the handle once in a shared write mode as such:
	//HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
	
	// Waiting loop:
	while(1)
	{
		if(mci_tracks){
			mciSendStringA("status cdaudio number of tracks wait", tracks_s, 64, NULL);
			sscanf(tracks_s, "%d", &tracks);
			mci_tracks = 0;
			// Write no. of tracks for winmm wrapper:
			HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(Mailslot, strcat(tracks_s, " tracks"), 64, &BytesWritten, NULL);
			CloseHandle(Mailslot);
		}
		if(mci_time==0 && mci_time_set){
			mciSendStringA("set cdaudio time format msf wait", NULL, 0, NULL);
			mci_time_set = 0;
		}
		if(mci_time==1 && mci_time_set){
			mciSendStringA("set cdaudio time format tmsf wait", NULL, 0, NULL);
			mci_time_set = 0;
		}
		if(mci_time==2 && mci_time_set){
			mciSendStringA("set cdaudio time format ms wait", NULL, 0, NULL);
			mci_time_set = 0;
		}
		if(length_int){
			mciSendStringA("status cdaudio length wait", length_s, 64, NULL);
			length_int = 0;
			// Write result to winmm wrapper:
			HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(Mailslot, strcat(length_s, " length"), 64, &BytesWritten, NULL);
			CloseHandle(Mailslot);
			sprintf(length_s,"");
		}
		if(length_t_int){
			mciSendStringA(track_cmd, length_s, 64, NULL);
			length_t_int = 0;
			// Write result to winmm wrapper:
			HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(Mailslot, strcat(length_s, " length_t"), 64, &BytesWritten, NULL);
			CloseHandle(Mailslot);
			sprintf(track_cmd,"");
			sprintf(length_s,"");
		}
		if(pos_int){
			mciSendStringA("status cdaudio position wait", pos_s, 64, NULL);
			pos_int = 0;
			// Write result to winmm wrapper:
			HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(Mailslot, strcat(pos_s, " pos"), 64, &BytesWritten, NULL);
			CloseHandle(Mailslot);
			sprintf(pos_s,"");
		}
		if(pos_t_int){
			mciSendStringA(track_cmd, pos_s, 64, NULL);
			pos_t_int = 0;
			// Write result to winmm wrapper:
			HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(Mailslot, strcat(pos_s, " pos_t"), 64, &BytesWritten, NULL);
			CloseHandle(Mailslot);
			sprintf(track_cmd,"");
			sprintf(pos_s,"");
		}
		if(seek){
			mciSendStringA(seek_cmd, NULL, 0, NULL);
			dprintf("seek cmd is: %s\n",seek_cmd);
			seek = 0;
		}
		if(cur_track){
			mciSendStringA("status cdaudio current track", cur_t_s, 64, NULL);
			cur_track = 0;
			// Write result to winmm wrapper:
			HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(Mailslot, strcat(cur_t_s, " cur_t"), 64, &BytesWritten, NULL);
			CloseHandle(Mailslot);
			sprintf(cur_t_s,"");
		}

		// Play loop:
		while(mci_play)
		{
			dprintf("while mci_play loop\n");
						
			// Set time format:
			if(mci_time==0 && mci_time_set){
				mciSendStringA("set cdaudio time format msf  wait", NULL, 0, NULL);
				mci_time_set = 0;
			}
			if(mci_time==1 && mci_time_set){
				mciSendStringA("set cdaudio time format tmsf wait", NULL, 0, NULL);
				mci_time_set = 0;
			}
			if(mci_time==2 && mci_time_set){
				mciSendStringA("set cdaudio time format ms wait", NULL, 0, NULL);
				mci_time_set = 0;
			}

			// MSF
			if(mci_time==0 && !skip_cmd){
				if(!mci_to_m && !mci_to_s && !mci_to_f){
					sprintf(play_cmd, "play cdaudio from %d:%d:%d", 
					mci_from_m, mci_from_s, mci_from_f);
				}
				else if(!mci_from_m && !mci_from_s && !mci_from_f){
					sprintf(play_cmd, "play cdaudio to %d:%d:%d", 
					mci_to_m, mci_to_s, mci_to_f);
				}
				else{
					sprintf(play_cmd, "play cdaudio from %d:%d:%d to %d:%d:%d", 
					mci_from_m, mci_from_s, mci_from_f, 
					mci_to_m, mci_to_s, mci_to_f);
				}
			}
			// TMSF
			if(mci_time==1 && !skip_cmd){
				if(mci_to==0 && mci_to_m==0 && mci_to_s==0 && mci_to_f==0){
					sprintf(play_cmd, "play cdaudio from %d:%d:%d:%d", 
					mci_from, mci_from_m, mci_from_s, mci_from_f);
				}
				else if(!mci_from && !mci_from_m && !mci_from_s && !mci_from_f){
					sprintf(play_cmd, "play cdaudio to %d:%d:%d:%d", 
					mci_to, mci_to_m, mci_to_s, mci_to_f);
				}
				else{
					sprintf(play_cmd, "play cdaudio from %d:%d:%d:%d to %d:%d:%d:%d", 
					mci_from, mci_from_m, mci_from_s, mci_from_f, 
					mci_to, mci_to_m, mci_to_s, mci_to_f);
				}
			}
			// MILLISECONDS
			if(mci_time==2 && !skip_cmd){
				if(!mci_to){
					sprintf(play_cmd, "play cdaudio from %d", mci_from);
				}
				else if(!mci_from){
					sprintf(play_cmd, "play cdaudio to %d", mci_to);
				}
				else{
					sprintf(play_cmd, "play cdaudio from %d to %d", mci_from, mci_to);
				}
			}

			// Issue play command: 
			mciSendStringA(play_cmd, NULL, 0, NULL);
			dprintf("%s\n", play_cmd);
			mciSendStringA("status cdaudio mode wait", mode, 64, NULL); // Get the initial mode. 
			dprintf("mode: %s\n", mode);

			// Empty from and to values:
			mci_from = mci_from_m = mci_from_s = mci_from_f = 0;
			mci_to = mci_to_m = mci_to_s = mci_to_f = 0;
			
			// Empty play command:
			sprintf(play_cmd,"");
			skip_cmd = 0;

			// Write mode playing for winmm wrapper:
			HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(Mailslot, "2 mode", 64, &BytesWritten, NULL);
			CloseHandle(Mailslot);

			// Handle CD spin-up with a loop:
			// (While mode is playing and position does not change.)
			while(strcmp(pos1,pos2)==0 && strcmp(mode,"playing")==0 && mci_play)
			{
				dprintf("while CD spin-up loop\n");
				mciSendStringA("status cdaudio position wait", pos1, 64, NULL);
				
				int counter = 0;
				while(counter < 10) // a 100ms sleep seems enough here
				{
					if (mci_pause || mci_play_pump){
						break;
					}
					if(length_int){
						mciSendStringA("status cdaudio length wait", length_s, 64, NULL);
						length_int = 0;
						// Write result to winmm wrapper:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, strcat(length_s, " length"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						sprintf(length_s,"");
					}
					if(length_t_int){
						mciSendStringA(track_cmd, length_s, 64, NULL);
						length_t_int = 0;
						// Write result to winmm wrapper:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, strcat(length_s, " length_t"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						sprintf(track_cmd,"");
						sprintf(length_s,"");
					}
					if(pos_int){
						mciSendStringA("status cdaudio position wait", pos_s, 64, NULL);
						pos_int = 0;
						// Write result to winmm wrapper:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, strcat(pos_s, " pos"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						sprintf(pos_s,"");
					}
					if(pos_t_int){
						mciSendStringA(track_cmd, pos_s, 64, NULL);
						pos_t_int = 0;
						// Write result to winmm wrapper:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, strcat(pos_s, " pos_t"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						sprintf(track_cmd,"");
						sprintf(pos_s,"");
					}
					if(cur_track){
						mciSendStringA("status cdaudio current track", cur_t_s, 64, NULL);
						cur_track = 0;
						// Write result to winmm wrapper:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, strcat(cur_t_s, " cur_t"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						sprintf(cur_t_s,"");
					}
					if(mci_tracks){
						mciSendStringA("status cdaudio number of tracks wait", tracks_s, 64, NULL);
						sscanf(tracks_s, "%d", &tracks);
						mci_tracks = 0;
						// Write no. of tracks for winmm wrapper:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, strcat(tracks_s, " tracks"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
					}
					Sleep(10);
					counter ++;
				}
				
				mciSendStringA("status cdaudio position wait", pos2, 64, NULL);
				
				if (mci_pause || mci_play_pump){
					break;
				}
			}

			// cdaudio play loop:
			// (While positions differ track must be playing.)
			while(strcmp(pos1,pos2)!=0 && strcmp(mode,"playing")==0 && mci_play)
			{
				dprintf("actual play loop\n");
				mciSendStringA("status cdaudio position wait", pos1, 64, NULL);
				// dprintf("    POS: %s\r", pos1);
				
				int counter = 0;
				while(counter < 50) // Needs a minimum of 400ms sleep to get reliable position data. (Could vary depending on hardware.)
				{
					if (mci_play_pump || mci_pause){
						break;
					}
					if(length_int){
						mciSendStringA("status cdaudio length wait", length_s, 64, NULL);
						length_int = 0;
						// Write result to winmm wrapper:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, strcat(length_s, " length"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						sprintf(length_s,"");
					}
					if(length_t_int){
						mciSendStringA(track_cmd, length_s, 64, NULL);
						length_t_int = 0;
						// Write result to winmm wrapper:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, strcat(length_s, " length_t"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						sprintf(track_cmd,"");
						sprintf(length_s,"");
					}
					if(pos_int){
						mciSendStringA("status cdaudio position wait", pos_s, 64, NULL);
						pos_int = 0;
						// Write result to winmm wrapper:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, strcat(pos_s, " pos"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						sprintf(pos_s,"");
					}
					if(pos_t_int){
						mciSendStringA(track_cmd, pos_s, 64, NULL);
						pos_t_int = 0;
						// Write result to winmm wrapper:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, strcat(pos_s, " pos_t"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						sprintf(track_cmd,"");
						sprintf(pos_s,"");
					}
					if(cur_track){
						mciSendStringA("status cdaudio current track", cur_t_s, 64, NULL);
						cur_track = 0;
						// Write result to winmm wrapper:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, strcat(cur_t_s, " cur_t"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
						sprintf(cur_t_s,"");
					}
					if(mci_tracks){
						mciSendStringA("status cdaudio number of tracks wait", tracks_s, 64, NULL);
						sscanf(tracks_s, "%d", &tracks);
						mci_tracks = 0;
						// Write no. of tracks for winmm wrapper:
						HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(Mailslot, strcat(tracks_s, " tracks"), 64, &BytesWritten, NULL);
						CloseHandle(Mailslot);
					}
					Sleep(10);
					counter ++;
				}
				
				mciSendStringA("status cdaudio position wait", pos2, 64, NULL);
				// dprintf("    POS: %s\r", pos2);
				// Check for mode change: 
				mciSendStringA("status cdaudio mode wait", mode, 64, NULL);
				
				if (mci_play_pump){
					break;
				}
				if (mci_pause){
					dprintf("  Playback paused\n");
					break;
				}
			}

			// Handle finished playback: 
			if (!mci_play_pump)
			{
				// Close the device and open it again using stored info.
				// Fixes issues with mcicda end of playback and last track bug:
				char temp_time[64] = "";
				char temp_pos[64] = "";
				char temp_track_cmd[512] = "";

				mciSendStringA("status cdaudio time format wait", temp_time, 64, NULL);
				mciSendStringA("status cdaudio position wait", temp_pos, 64, NULL);
				mciSendStringA("close cdaudio wait", NULL, 0, NULL);
				mciSendStringA("open cdaudio wait", NULL, 0, NULL);
				// Restore time format:
				sprintf(temp_track_cmd, "set cdaudio time format %s", temp_time);
				mciSendStringA(temp_track_cmd, NULL, 0, NULL);
				// Restore last position:
				sprintf(temp_track_cmd, "seek cdaudio to %s", temp_pos);
				mciSendStringA(temp_track_cmd, NULL, 0, NULL);

				// Write MODE stopped for winmm wrapper:
				HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				WriteFile(Mailslot, "1 mode", 64, &BytesWritten, NULL);
				CloseHandle(Mailslot);
			}

			// Get last mode: 
			if(!mci_play_pump){
				mciSendStringA("status cdaudio mode wait", mode, 64, NULL);
				dprintf("mode: %s\n", mode);
			}

			// Write notify success message: 
			if (notify_msg && !mci_play_pump && !mci_pause){
				HANDLE Mailslot = CreateFile(ServerName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				WriteFile(Mailslot, "1 notify_s", 64, &BytesWritten, NULL);
				CloseHandle(Mailslot);
				notify_msg = 0;
				dprintf("Notify SUCCESS!\n");
			}

			// Reset pos strings for next run: 
			strcpy (pos1, "-");
			strcpy (pos2, "-");
			if(!mci_play_pump)mci_play = 0;
			mci_play_pump = 0;
			//SetWindowText(hEdit, TEXT("Playback ended..."));
		}

		Sleep(10);
	}

	return 0;
}

// Message handling: 
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
		/*
		case MM_MCINOTIFY:
		{
			if (msg==MM_MCINOTIFY && wParam==MCI_NOTIFY_SUCCESSFUL)
		}
		break;
		*/
		case WM_CREATE:
		{
			HFONT hfDefault;

			// Static text display: 
			hEdit = CreateWindow("STATIC", "", WS_VISIBLE | WS_CHILD | SS_LEFT, 0,0,100,100, hwnd, (HMENU)IDC_MAIN_EDIT, GetModuleHandle(NULL), NULL);

			HMENU hMenu, hSubMenu;
			HICON hIcon, hIconSm;

			hMenu = CreateMenu();

			hSubMenu = CreatePopupMenu();
			AppendMenu(hSubMenu, MF_STRING, ID_FILE_EXIT, "E&xit");
			AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT64)hSubMenu, "&File");

			hSubMenu = CreatePopupMenu();
			AppendMenu(hSubMenu, MF_STRING, ID_VOLUME_0, "&Mute");
			AppendMenu(hSubMenu, MF_STRING, ID_VOLUME_10, "&10%");
			AppendMenu(hSubMenu, MF_STRING, ID_VOLUME_20, "&20%");
			AppendMenu(hSubMenu, MF_STRING, ID_VOLUME_30, "&30%");
			AppendMenu(hSubMenu, MF_STRING, ID_VOLUME_40, "&40%");
			AppendMenu(hSubMenu, MF_STRING, ID_VOLUME_50, "&50%");
			AppendMenu(hSubMenu, MF_STRING, ID_VOLUME_60, "&60%");
			AppendMenu(hSubMenu, MF_STRING, ID_VOLUME_70, "&70%");
			AppendMenu(hSubMenu, MF_STRING, ID_VOLUME_80, "&80%");
			AppendMenu(hSubMenu, MF_STRING, ID_VOLUME_90, "&90%");
			AppendMenu(hSubMenu, MF_STRING, ID_VOLUME_100, "&Disable override");

			AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT64)hSubMenu, "&Volume override");

			hSubMenu = CreatePopupMenu();
			AppendMenu(hSubMenu, MF_STRING, ID_VIEW_WDIR, "&Running from...");
			AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT64)hSubMenu, "&View");

			hSubMenu = CreatePopupMenu();
			AppendMenu(hSubMenu, MF_STRING, ID_HELP_INST, "&Instructions");
			AppendMenu(hSubMenu, MF_STRING, ID_HELP_ABOUT, "&About");
			AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT64)hSubMenu, "&Help");

			SetMenu(hwnd, hMenu);
		}
		break;
		case WM_SIZE:
		{
			// Resize text area: 
			HWND hEdit;
			RECT rcClient;

			GetClientRect(hwnd, &rcClient);

			hEdit = GetDlgItem(hwnd, IDC_MAIN_EDIT);
			SetWindowPos(hEdit, NULL, 0, 0, rcClient.right, rcClient.bottom, SWP_NOZORDER);
		}
		break;
		
		case WM_INITMENUPOPUP:
			// check marks 
			CheckMenuItem((HMENU)wParam, ID_VOLUME_0, MF_BYCOMMAND | ( chkvol_0 ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem((HMENU)wParam, ID_VOLUME_10, MF_BYCOMMAND | ( chkvol_10 ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem((HMENU)wParam, ID_VOLUME_20, MF_BYCOMMAND | ( chkvol_20 ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem((HMENU)wParam, ID_VOLUME_30, MF_BYCOMMAND | ( chkvol_30 ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem((HMENU)wParam, ID_VOLUME_40, MF_BYCOMMAND | ( chkvol_40 ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem((HMENU)wParam, ID_VOLUME_50, MF_BYCOMMAND | ( chkvol_50 ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem((HMENU)wParam, ID_VOLUME_60, MF_BYCOMMAND | ( chkvol_60 ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem((HMENU)wParam, ID_VOLUME_70, MF_BYCOMMAND | ( chkvol_70 ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem((HMENU)wParam, ID_VOLUME_80, MF_BYCOMMAND | ( chkvol_80 ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem((HMENU)wParam, ID_VOLUME_90, MF_BYCOMMAND | ( chkvol_90 ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem((HMENU)wParam, ID_VOLUME_100, MF_BYCOMMAND | ( chkvol_100 ? MF_CHECKED : MF_UNCHECKED));
		break;
		
		
		case WM_COMMAND:
			switch(LOWORD(wParam))
			{
				case ID_FILE_EXIT:
					PostMessage(hwnd, WM_CLOSE, 0, 0);
				break;
				// GUI adjust volume and write to .ini 
				case ID_VOLUME_0:
					waveOutSetVolume(NULL, 0x0);
					fp = fopen (ini_dir, "w+");
					fprintf(fp, "0");
					fclose(fp);
					
					// check marks 
					chkvol_0 = 1;
					chkvol_10 = 0;
					chkvol_20 = 0;
					chkvol_30 = 0;
					chkvol_40 = 0;
					chkvol_50 = 0;
					chkvol_60 = 0;
					chkvol_70 = 0;
					chkvol_80 = 0;
					chkvol_90 = 0;
					chkvol_100 = 0;
					
				break;
				case ID_VOLUME_10:
					waveOutSetVolume(NULL, 0x1F401F40);
					fp = fopen (ini_dir, "w+");
					fprintf(fp, "10");
					fclose(fp);
					
					// check marks 
					chkvol_0 = 0;
					chkvol_10 = 1;
					chkvol_20 = 0;
					chkvol_30 = 0;
					chkvol_40 = 0;
					chkvol_50 = 0;
					chkvol_60 = 0;
					chkvol_70 = 0;
					chkvol_80 = 0;
					chkvol_90 = 0;
					chkvol_100 = 0;
					
				break;
				case ID_VOLUME_20:
					waveOutSetVolume(NULL, 0x32C832C8);
					fp = fopen (ini_dir, "w+");
					fprintf(fp, "20");
					fclose(fp);
					
					// check marks 
					chkvol_0 = 0;
					chkvol_10 = 0;
					chkvol_20 = 1;
					chkvol_30 = 0;
					chkvol_40 = 0;
					chkvol_50 = 0;
					chkvol_60 = 0;
					chkvol_70 = 0;
					chkvol_80 = 0;
					chkvol_90 = 0;
					chkvol_100 = 0;
					
				break;
				case ID_VOLUME_30:
					waveOutSetVolume(NULL, 0x4A384A38);
					fp = fopen (ini_dir, "w+");
					fprintf(fp, "30");
					fclose(fp);
					
					// check marks 
					chkvol_0 = 0;
					chkvol_10 = 0;
					chkvol_20 = 0;
					chkvol_30 = 1;
					chkvol_40 = 0;
					chkvol_50 = 0;
					chkvol_60 = 0;
					chkvol_70 = 0;
					chkvol_80 = 0;
					chkvol_90 = 0;
					chkvol_100 = 0;
					
				break;
				case ID_VOLUME_40:
					waveOutSetVolume(NULL, 0x652C652C);
					fp = fopen (ini_dir, "w+");
					fprintf(fp, "40");
					fclose(fp);
					
					// check marks 
					chkvol_0 = 0;
					chkvol_10 = 0;
					chkvol_20 = 0;
					chkvol_30 = 0;
					chkvol_40 = 1;
					chkvol_50 = 0;
					chkvol_60 = 0;
					chkvol_70 = 0;
					chkvol_80 = 0;
					chkvol_90 = 0;
					chkvol_100 = 0;
					
				break;
				case ID_VOLUME_50:
					waveOutSetVolume(NULL, 0x7D007D00);
					fp = fopen (ini_dir, "w+");
					fprintf(fp, "50");
					fclose(fp);
					
					// check marks 
					chkvol_0 = 0;
					chkvol_10 = 0;
					chkvol_20 = 0;
					chkvol_30 = 0;
					chkvol_40 = 0;
					chkvol_50 = 1;
					chkvol_60 = 0;
					chkvol_70 = 0;
					chkvol_80 = 0;
					chkvol_90 = 0;
					chkvol_100 = 0;
					
				break;
				case ID_VOLUME_60:
					waveOutSetVolume(NULL, 0x9A4C9A4C);
					fp = fopen (ini_dir, "w+");
					fprintf(fp, "60");
					fclose(fp);
					
					// check marks 
					chkvol_0 = 0;
					chkvol_10 = 0;
					chkvol_20 = 0;
					chkvol_30 = 0;
					chkvol_40 = 0;
					chkvol_50 = 0;
					chkvol_60 = 1;
					chkvol_70 = 0;
					chkvol_80 = 0;
					chkvol_90 = 0;
					chkvol_100 = 0;
					
				break;
				case ID_VOLUME_70:
					waveOutSetVolume(NULL, 0xAFC8AFC8);
					fp = fopen (ini_dir, "w+");
					fprintf(fp, "70");
					fclose(fp);
					
					// check marks 
					chkvol_0 = 0;
					chkvol_10 = 0;
					chkvol_20 = 0;
					chkvol_30 = 0;
					chkvol_40 = 0;
					chkvol_50 = 0;
					chkvol_60 = 0;
					chkvol_70 = 1;
					chkvol_80 = 0;
					chkvol_90 = 0;
					chkvol_100 = 0;
					
				break;
				case ID_VOLUME_80:
					waveOutSetVolume(NULL, 0xCB20CB20);
					fp = fopen (ini_dir, "w+");
					fprintf(fp, "80");
					fclose(fp);
					
					// check marks 
					chkvol_0 = 0;
					chkvol_10 = 0;
					chkvol_20 = 0;
					chkvol_30 = 0;
					chkvol_40 = 0;
					chkvol_50 = 0;
					chkvol_60 = 0;
					chkvol_70 = 0;
					chkvol_80 = 1;
					chkvol_90 = 0;
					chkvol_100 = 0;
					
				break;
				case ID_VOLUME_90:
					waveOutSetVolume(NULL, 0xE290E290);
					fp = fopen (ini_dir, "w+");
					fprintf(fp, "90");
					fclose(fp);
					
					// check marks 
					chkvol_0 = 0;
					chkvol_10 = 0;
					chkvol_20 = 0;
					chkvol_30 = 0;
					chkvol_40 = 0;
					chkvol_50 = 0;
					chkvol_60 = 0;
					chkvol_70 = 0;
					chkvol_80 = 0;
					chkvol_90 = 1;
					chkvol_100 = 0;
					
				break;
				case ID_VOLUME_100:
					waveOutSetVolume(NULL, volume);
					fp = fopen (ini_dir, "w+");
					fprintf(fp, "100");
					fclose(fp);
					
					// check marks 
					chkvol_0 = 0;
					chkvol_10 = 0;
					chkvol_20 = 0;
					chkvol_30 = 0;
					chkvol_40 = 0;
					chkvol_50 = 0;
					chkvol_60 = 0;
					chkvol_70 = 0;
					chkvol_80 = 0;
					chkvol_90 = 0;
					chkvol_100 = 1;
					
				break;
				case ID_VIEW_WDIR:
				{
					char buff[FILENAME_MAX];
					GetCurrentDir( buff, FILENAME_MAX );
					MessageBox(hwnd, buff, "cdaudio-winmm player is running from:", MB_OK | MB_ICONINFORMATION);

				}
				break;
				case ID_HELP_INST:
					MessageBox(hwnd, TEXT("1. Place winmm.dll wrapper\n"
					"    into the game folder.\n2. Place cdaudioplr.exe\n"
					"    in 'mcicda' -subfolder.\n3. Run the game normally.\n\n"
					"Additional tips:\n\n- You can also start cdaudioplr.exe\n"
					"manually before running the game.\n\n- Do not place cdaudioplr.exe and\n"
					"winmm.dll in the same folder."), TEXT("Instructions"), MB_OK);
				break;
				case ID_HELP_ABOUT:
					MessageBox(hwnd, TEXT("cdaudio-winmm player\nversion 1.6 (c) 2022\n\nRestores track repeat\nand volume control\nin Vista and later."), TEXT("About"), MB_OK);
				break;
			}
		break;
		/*
		case WM_TIMER: // Handle timer message.
			if (msg==WM_TIMER && wParam==IDT_TIMER1)
			{
				return 0;
			}
		break;
		*/
		case WM_CLOSE:
			DestroyWindow(hwnd);
		break;
		case WM_DESTROY:
			PostQuitMessage(0);
		break;
		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

// Main Window process: 
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
	LPSTR lpCmdLine, int nCmdShow)
{
	// Checks that program is not already running: 
	CreateMutexA(NULL, TRUE, "cdaudioplrMutex");
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		return 0;
	}

	// When cdaudioplr.exe is auto-started by the wrapper it may inherit the CPU 
	// affinity of the game program. A particularly problematic case is the original 
	// Midtown Madness game executable which runs in a high priority class and sets
	// the player to run on the first CPU core (single core affinity). This results 
	// in the player hanging unless the mouse cursor is moved around. 

	// Set cdaudioplr.exe to run in high priority 
	// SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS); 

	// Set affinity to last CPU core
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	int lastcore = sysinfo.dwNumberOfProcessors;
	SetProcessAffinityMask(GetCurrentProcess(), lastcore);

	WNDCLASSEX wc;
	HWND hwnd;
	MSG Msg;

	wc.cbSize        = sizeof(WNDCLASSEX);
	wc.style         = 0;
	wc.lpfnWndProc   = WndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hInstance;
	wc.hIcon         = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MYICON));
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wc.lpszMenuName  = NULL;
	wc.lpszClassName = g_szClassName;
	wc.hIconSm       = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MYICON), IMAGE_ICON, 16, 16, 0);

	if(!RegisterClassEx(&wc))
	{
		MessageBox(NULL, "Window Registration Failed!", "Error!",
			MB_ICONEXCLAMATION | MB_OK);
		return 0;
	}

	// Check that winmm.dll & cdaudioplr.exe are not in the same dir: 
	struct stat sb;
	char file[] = ".\\cdaudioplr.exe";
	char file2[] = ".\\winmm.dll";

	if (stat(file, &sb) == 0 && S_ISREG(sb.st_mode) && stat(file2, &sb) == 0 && S_ISREG(sb.st_mode)) 
	{
		MessageBox(NULL, "cdaudioplr.exe and winmm.dll can not be in the same dir.", "Error!",
			MB_ICONEXCLAMATION | MB_OK);
		return 0;
	}

	// Check for Win version. If less than Vista quit.
	/*
	DWORD dwVersion = 0; 
	dwVersion = GetVersion();
	if (dwVersion < 590000000)
	{
		MessageBox(NULL, "Needs WinVista or newer.", "Error!",
			MB_ICONEXCLAMATION | MB_OK);
		return 0;
	}
	*/
	

	hwnd = CreateWindowEx(
		WS_EX_CLIENTEDGE,
		g_szClassName,
		"cdaudio-winmm player",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 300, 100,
		NULL, NULL, hInstance, NULL);

	if(hwnd == NULL)
	{
		MessageBox(NULL, "Window Creation Failed!", "Error!",
			MB_ICONEXCLAMATION | MB_OK);
		return 0;
	}

	g_hMainWindow = hwnd;

	ShowWindow(hwnd, SW_SHOWNOACTIVATE); // Start with inactive window. 
	UpdateWindow(hwnd);

	// Set working directory to module directory: 
	char szFileName[MAX_PATH];
	GetModuleFileName(hInstance, szFileName, MAX_PATH);
	int len = strlen(szFileName);
	szFileName[len-14] = '\0'; // delete cdaudioplr.exe from string. 
	SetCurrentDirectory(szFileName);

	// debug logging 
	#ifdef _DEBUG
		int bLog = GetPrivateProfileInt("options", "Log", 0, ".\\cdaudio_vol.ini");
		if(bLog)fh = fopen("cdaudioplr.log", "w");
	#endif
	dprintf("Beginning of debug log:\n");

	// Set timer
	// SetTimer(hwnd, 1, 10, NULL);
	// Or set a named timer
	// SetTimer(hwnd, IDT_TIMER1, 10, NULL); // Use a named timer so we can handle its message.
	
	// Start threads: 
	reader = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)reader_main, NULL, 0, NULL);
	player = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)player_main, NULL, 0, NULL);
	
	// THREAD_PRIORITY_BELOW_NORMAL = -1
	// THREAD_PRIORITY_NORMAL = 0
	// THREAD_PRIORITY_ABOVE_NORMAL = 1
	// THREAD_PRIORITY_HIGHEST = 2
	// SetThreadPriority(player,2);

	// Message Loop: 
	while(GetMessage(&Msg, NULL, 0, 0) > 0 && !quit)
	{
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}

	// debug logging 
	dprintf("End of debug log.\n");
	#ifdef _DEBUG
	if (fh)
	{
		fclose(fh);
		fh = NULL;
	}
	#endif

	return Msg.wParam;
}
