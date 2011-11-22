#include <stdlib.h>
#include <wiiuse/wpad.h>
#include <dirent.h>
#include "wiiload/wiiload.h"
#include "globals.h"
#include "bin2o.h"
#include "triiforce/nand.h"
#include "network.h"
#include "sys/errno.h"
#include "http.h"
#include "ios.h"
#include "microsneek.h"
#include "identify.h"
#include "gui.h"
#include "neek.h"

#define CHNMAX 1024

#define TITLE_UPPER(x)		((u32)((x) >> 32))
#define TITLE_LOWER(x)		((u32)(x))
#define TITLE_ID(x,y)		(((u64)(x) << 32) | (y))

/*

This allow to browse for applications in games folder of mounted drive

*/
extern s_grlibSettings grlibSettings;
static s_gameConfig gameConf;

static int browse = 0;
static int scanned = 0;

static s_game *games;
static int gamesCnt;
static int games2Disp;
static int gamesPage; // Page to be displayed
static int gamesPageMax; // 
static int gamesSelected = -1;	// Current selected app with wimote

static int browserRet = 0;
static int showHidden = 0;

static s_grlib_iconSetting is;

static void Redraw (void);
static int GameBrowse (void);

char* neek_GetGames (void);
void neek_KillDIConfig (void);

#define ICONW 80
#define ICONH 150

#define XMIDLEINFO 320
#define INIT_X (30 + ICONW / 2)
#define INIT_Y (60 + ICONH / 2)
#define INC_X ICONW+22
#define INC_Y ICONH+25

static void InitializeGui (void)
	{
	// Prepare black box
	int i, il;
	int x = INIT_X;
	int y = INIT_Y;

	gui_Init();

	gui.spotsIdx = 0;
	gui.spotsXpage = 12;
	gui.spotsXline = 6;

	grlib_IconSettingInit (&is);

	is.themed = theme.ok;
	is.border = 5.0;
	is.magX = 1.0;
	is.magY = 1.0;
	is.magXSel = 1.2;
	is.magYSel = 1.2;
	is.iconFake = vars.tex[TEX_NONE];
	is.bkgTex = theme.frameBack;
	is.fgrSelTex = theme.frameSel;
	is.fgrTex = theme.frame;
	is.bkgColor = RGBA (0,0,0,255);
	is.borderSelColor = RGBA (255,255,255,255); 	// Border color
	is.borderColor = RGBA (128,128,128,255); 		// Border color
	is.fontReverse = 0; 		// Border color

	il = 0;
	for (i = 0; i < gui.spotsXpage; i++)
		{
		gui.spots[i].ico.x = x;
		gui.spots[i].ico.y = y;
		gui.spots[i].ico.w = ICONW;
		gui.spots[i].ico.h = ICONH;

		il++;
		if (il == gui.spotsXline)
			{
			x = INIT_X;
			y += INC_Y;
			il = 0;
			}
		else
			{
			x+=INC_X;
			}
		}
	}

static bool DownloadCovers_Get (char *path, char *buff)
	{
	u8* outbuf=NULL;
	u32 outlen=0;
	
	outbuf = downloadfile (buff, &outlen);
		
	if (IsPngBuff (outbuf, outlen))
		{
		//suficientes bytes
		FILE *f;
		
		f = fopen (path, "wb");
		if (f)
			{
			fwrite (outbuf, outlen, 1, f);
			fclose (f);
			}
		
		free(outbuf);
		return (TRUE);
		}
		
	return (FALSE);
	}

static void DownloadCovers (void)
	{
	int ia, stop;
	char buff[300];

	Redraw ();
	grlib_PushScreen ();
	
	Video_WaitPanel (TEX_HGL, "Stopping wiiload thread");
	WiiLoad_Pause ();
	
	stop = 0;
	
	for (ia = 0; ia < gamesCnt; ia++)
		{
		char path[PATHMAX];

		Video_WaitPanel (TEX_HGL, "Downloading %s.png (%d of %d)|(B) Stop", games[ia].asciiId, ia, gamesCnt);
		sprintf (path, "%s://ploader/covers/%s.png", vars.defMount, games[ia].asciiId);
		
		if (!fsop_FileExist(path))
			{
			bool ret = FALSE;
			
			if (!ret)
				{
				sprintf (buff, "http://art.gametdb.com/wii/cover/US/%s.png", games[ia].asciiId);
				ret = DownloadCovers_Get (path, buff);
				}
			if (!ret)
				{
				sprintf (buff, "http://art.gametdb.com/wii/cover/EN/%s.png", games[ia].asciiId);
				ret = DownloadCovers_Get (path, buff);
				}
				
			if (grlib_GetUserInput () == WPAD_BUTTON_B)
				{
				stop = 1;
				break;
				}
			}
					
		if (stop) break;
		}
		
	WiiLoad_Resume ();
	
	GameBrowse ();
	}
	
static void ReadGameConfig (int ia)
	{
	ManageGameConfig (games[ia].asciiId, 0, &gameConf);

	//strcpy (games[ia].asciiId, gameConf.asciiId);
	games[ia].hidden = gameConf.hidden;
	games[ia].priority = gameConf.priority;
	}

static void WriteGameConfig (int ia)
	{
	if (ia < 0) return;
	
	strcpy (gameConf.asciiId, games[ia].asciiId);
	gameConf.hidden = games[ia].hidden;
	gameConf.priority = games[ia].priority;
	
	ManageGameConfig (games[ia].asciiId, 1, &gameConf);
	}

static void StructFree (void)
	{
	int i;
	
	for (i = 0; i < gamesCnt; i++)
		{
		games[i].png = NULL;
		
		if (games[i].name != NULL) 
			{
			free (games[i].name);
			games[i].name = NULL;
			}
		}
		
	gamesCnt = 0;
	}
	
static void SaveTitlesCache (void)
	{
	FILE* f = NULL;
	char cfg[256];
	char buff[256];
	int i;
	
	sprintf (cfg, "%s://ploader/channels.txt", vars.defMount);
	f = fopen(cfg, "wb");
	if (!f) return;
	
	for (i = 0; i < gamesCnt; i++)
		{
		sprintf (buff, "%s:%s\n", games[i].asciiId, games[i].name);
		fwrite (buff, 1, strlen(buff), f );
		}
	fclose(f);
	}

static bool CheckFilter (int ai)
	{
	return TRUE;
	}

#define SKIP 10
static void AppsSort (void)
	{
	int i;
	int mooved;
	s_game app;
	
	// Apply filters
	games2Disp = 0;
	for (i = 0; i < gamesCnt; i++)
		{
		games[i].filterd = CheckFilter(i);
		if (games[i].filterd && (!games[i].hidden || showHidden)) games2Disp++;
		}
	
	// Sort by hidden, use stupid algorithm...
	do
		{
		mooved = 0;
		
		for (i = 0; i < gamesCnt - 1; i++)
			{
			if (games[i].hidden > games[i+1].hidden)
				{
				// swap
				memcpy (&app, &games[i+1], sizeof(s_game));
				memcpy (&games[i+1], &games[i], sizeof(s_game));
				memcpy (&games[i], &app, sizeof(s_game));
				mooved = 1;
				}
			}
		}
	while (mooved);

	// Sort by name, use stupid algorithm...
	do
		{
		mooved = 0;
		
		for (i = 0; i < games2Disp - 1; i++)
			{
			if (games[i].name && games[i+1].name && strcmp (games[i+1].name, games[i].name) < 0)
				{
				// swap
				memcpy (&app, &games[i+1], sizeof(s_game));
				memcpy (&games[i+1], &games[i], sizeof(s_game));
				memcpy (&games[i], &app, sizeof(s_game));
				mooved = 1;
				}
			}
		}
	while (mooved);

	// Sort by priority
	do
		{
		mooved = 0;
		
		for (i = 0; i < games2Disp - 1; i++)
			{
			if (games[i+1].priority > games[i].priority)
				{
				// swap
				memcpy (&app, &games[i+1], sizeof(s_game));
				memcpy (&games[i+1], &games[i], sizeof(s_game));
				memcpy (&games[i], &app, sizeof(s_game));
				mooved = 1;
				}
			}
		}
	while (mooved);

	gamesPageMax = games2Disp / gui.spotsXpage;
	}
	

static int GameBrowse (void)
	{
	Debug ("begin GameBrowse");
	
	gui.spotsIdx = 0;
	gui_Clean ();
	StructFree ();

	//if (vars.neek != NEEK_NONE) // use neek interface to build up game listing
		{
		int i;
		char *titles;
		char *p;
		
		//UNEEK_GetGameCountb ( &cnt );
		
		titles = neek_GetGames ();
		if (!titles) return 0;
		
		p = titles;
		i = 0;
		
		Debug ("GameBrowse [begin]");
				
		do
			{
			if (*p != '\0' && strlen(p))
				{
				Debug ("GameBrowse [add %d:%s]", i, p);
				// Add name
				games[i].name = malloc (strlen(p));
				strcpy (games[i].name, p);
				p += (strlen(p) + 1);
				
				Debug ("GameBrowse [add %d:%s]", i, p);
				// Add id
				strcpy (games[i].asciiId, p);
				p += (strlen(p) + 1);
				
				games[i].slot = i;
				
				ReadGameConfig (i);
				
				i ++;
				}
			else
				break;
			}
		while (TRUE);
		
		gamesCnt = i;
		
		free (titles);
		}

	AppsSort ();
	
	scanned = 1;

	Debug ("end GameBrowse");
	return gamesCnt;
	}

static GRRLIB_texImg * GetTitleTexture (int ai)
	{
	char path[PATHMAX];
	
	sprintf (path, "%s://ploader/covers/%s.png", vars.defMount, games[ai].asciiId);
	return GRRLIB_LoadTextureFromFile (path);
	return NULL;
	}

static int FindSpot (void)
	{
	int i,j;
	static time_t t = 0;
	char info[300];
	
	gamesSelected = -1;
	
	for (i = 0; i < gui.spotsIdx; i++)
		{
		if (grlib_irPos.x > gui.spots[i].ico.rx1 && grlib_irPos.x < gui.spots[i].ico.rx2 && grlib_irPos.y > gui.spots[i].ico.ry1 && grlib_irPos.y < gui.spots[i].ico.ry2)
			{
			// Ok, we have the point
			gamesSelected = gui.spots[i].id;

			gui.spots[i].ico.sel = true;
			grlib_IconDraw (&is, &gui.spots[i].ico);

			grlib_SetFontBMF (fonts[FNTNORM]);
			grlib_printf (XMIDLEINFO, theme.ch_line1Y, GRLIB_ALIGNCENTER, 0, games[gamesSelected].name);
			
			grlib_SetFontBMF (fonts[FNTSMALL]);
			
			*info = '\0';
			for (j = 0; j < CHANNELS_MAXFILTERS; j++)
				if (*games[gamesSelected].asciiId == *CHANNELS_NAMES[j])
					{
					sprintf (info, "%s ", &CHANNELS_NAMES[j][1]);
					break;
					}

			strcat (info, "(");
			strcat (info, games[gamesSelected].asciiId);
			strcat (info, ")");
			
			grlib_printf (XMIDLEINFO, theme.ch_line2Y, GRLIB_ALIGNCENTER, 0, info);
			
			t = time(NULL);
			break;
			}
		}
	
	grlib_SetFontBMF (fonts[FNTNORM]);
	if (!grlib_irPos.valid)
		{
		if (gamesSelected == -1) grlib_printf (XMIDLEINFO, theme.ch_line2Y, GRLIB_ALIGNCENTER, 0, "Point an icon with the wiimote or use a GC controller!");
		}
	else
		if (time(NULL) - t > 0 && gamesSelected == -1)
			{
			grlib_printf (XMIDLEINFO, theme.ch_line2Y, GRLIB_ALIGNCENTER, 0, "(A) Run title (B) Title menu (1) to HB mode (2) Filters");
			}
	
	return gamesSelected;
	}
	
#define CHOPT_IOS 5
#define CHOPT_VID 8
#define CHOPT_VIDP 4
#define CHOPT_LANG 11
#define CHOPT_HOOK 8
#define CHOPT_OCA 4

static void ShowAppMenu (int ai)
	{
	char buff[1024];
	char b[64];
	int item;
	
	int opt[6] = {CHOPT_IOS, CHOPT_VID, CHOPT_VIDP, CHOPT_LANG, CHOPT_HOOK, CHOPT_OCA};

	char *ios[CHOPT_IOS] = { "Default", "USA" , "EURO", "JAP", "Korean"};
	/*
	char *videooptions[CHOPT_VID] = { "Default Video Mode", "Force NTSC480i", "Force NTSC480p", "Force PAL480i", "Force PAL480p", "Force PAL576i", "Force MPAL480i", "Force MPAL480p" };
	char *videopatchoptions[CHOPT_VIDP] = { "No Video patches", "Smart Video patching", "More Video patching", "Full Video patching" };
	char *languageoptions[CHOPT_LANG] = { "Default Language", "Japanese", "English", "German", "French", "Spanish", "Italian", "Dutch", "S. Chinese", "T. Chinese", "Korean" };
	char *hooktypeoptions[CHOPT_HOOK] = { "No Ocarina&debugger", "Hooktype: VBI", "Hooktype: KPAD", "Hooktype: Joypad", "Hooktype: GXDraw", "Hooktype: GXFlush", "Hooktype: OSSleepThread", "Hooktype: AXNextFrame" };
	char *ocarinaoptions[CHOPT_OCA] = { "No Ocarina", "Ocarina from NAND", "Ocarina from SD", "Ocarina from USB" };
	*/
	grlib_SetFontBMF(fonts[FNTNORM]);

	ReadGameConfig (gamesSelected);
	gameConf.language ++; // umph... language in triiforce start at -1... not index friendly
	do
		{
		
		buff[0] = '\0';
		//strcat (buff, "Set as autoboot##1|");
		
		if (games[gamesSelected].hidden)
			strcat (buff, "Remove hide flag##2|");
		else
			strcat (buff, "Hide this title ##3|");

		sprintf (b, "Vote this title (%d/10)##4", games[gamesSelected].priority);
		strcat (buff, b);

		strcat (buff, "||");
		strcat (buff, "NAND: "); strcat (buff, ios[gameConf.ios]); strcat (buff, "##100|");
		/*
		if (config.chnBrowser.nand != NAND_REAL)
			{
			
			if (gameConf.ios != MICROSNEEK_IOS)
				{
				strcat (buff, "Video: "); strcat (buff, videooptions[gameConf.vmode]); strcat (buff, "##101|");
				strcat (buff, "Video Patch: "); strcat (buff, videopatchoptions[gameConf.vpatch]); strcat (buff, "##102|");
				strcat (buff, "Language: "); strcat (buff, languageoptions[gameConf.language]); strcat (buff, "##103|");
				strcat (buff, "Hook type: "); strcat (buff, hooktypeoptions[gameConf.hook]); strcat (buff, "##104|");
				strcat (buff, "Ocarina: "); strcat (buff, ocarinaoptions[gameConf.ocarina]); strcat (buff, "##105|");
				}
			else
				{
				strcat (buff, "Video: n/a##200|");
				strcat (buff, "Video Patch: n/a##200|");
				strcat (buff, "Language: n/a##200|");
				strcat (buff, "Hook type: n/a##200|");
				strcat (buff, "Ocarina: n/a##200|");
				}
			}
		*/
		strcat (buff, "|");
		strcat (buff, "Close##-1");
		
		item = grlib_menu (games[ai].name, buff);
		if (item < 100) break;
		if (item >= 100)
			{
			int i = item - 100;
			
			if (i == 0) { gameConf.ios ++; if (gameConf.ios >= opt[i]) gameConf.ios = 0; }
			if (i == 1) { gameConf.vmode ++; if (gameConf.vmode >= opt[i]) gameConf.vmode = 0; }
			if (i == 2) { gameConf.vpatch ++; if (gameConf.vpatch >= opt[i]) gameConf.vpatch = 0; }
			if (i == 3) { gameConf.language ++; if (gameConf.language >= opt[i]) gameConf.language = 0; }
			if (i == 4) { gameConf.hook ++; if (gameConf.hook >= opt[i]) gameConf.hook = 0; }
			if (i == 5) { gameConf.ocarina ++; if (gameConf.ocarina >= opt[i]) gameConf.ocarina = 0; }
			}
		}
	while (TRUE);
	gameConf.language --;
	
	if (item == 1)
		{
		memcpy (&config.autoboot.channel, &gameConf, sizeof (s_gameConfig));
		config.autoboot.appMode = APPMODE_CHAN;
		config.autoboot.enabled = TRUE;

		config.autoboot.nand = config.chnBrowser.nand;
		strcpy (config.autoboot.nandPath, config.chnBrowser.nandPath);

		strcpy (config.autoboot.asciiId, games[gamesSelected].asciiId);
		strcpy (config.autoboot.path, games[gamesSelected].name);
		ConfigWrite();
		}

	if (item == 2)
		{
		games[gamesSelected].hidden = 0;
		WriteGameConfig (gamesSelected);
		AppsSort ();
		}

	if (item == 3)
		{
		games[gamesSelected].hidden = 1;
		WriteGameConfig (gamesSelected);
		AppsSort ();
		}

	if (item == 4)
		{
		int item;
		item = grlib_menu ("Vote Title", "10 (Best)|9|8|7|6|5 (Average)|4|3|2|1 (Bad)");
		games[gamesSelected].priority = 10-item;
		
		WriteGameConfig (gamesSelected);
		AppsSort ();
		}

	WriteGameConfig (gamesSelected);
	
	GameBrowse ();
	}

static void ShowFilterMenu (void)
	{
	u8 *f;
	char buff[512];
	int item;
	
	f = config.chnBrowser.filter;
	
	do
		{
		buff[0] = '\0';
		for (item = 0; item < CHANNELS_MAXFILTERS; item++)
			grlib_menuAddCheckItem (buff, 100 + item, f[item], &CHANNELS_NAMES[item][1]);
		
		item = grlib_menu ("Filter menu':\nPress (B) to close, (+) Select all, (-) Deselect all", buff);

		if (item == MNUBTN_PLUS)
			{
			int i; 	for (i = 0; i < CHANNELS_MAXFILTERS; i++) f[i] = 1;
			}

		if (item == MNUBTN_MINUS)
			{
			int i; 	for (i = 0; i < CHANNELS_MAXFILTERS; i++) f[i] = 0;
			}
		
		if (item >= 100)
			{
			int i = item - 100;
			f[i] = !f[i];
			}
		}
	while (item != -1);
	GameBrowse ();
	AppsSort ();
	}

// Nand folder can be only root child...
#define MAXNANDFOLDERS 16
static int ScanForNandFolders (char **nf, int idx, char *device)
	{
	DIR *pdir;
	struct dirent *pent;
	char path[300];
	char nand[300];

	sprintf (path, "%s://", device);
	
	pdir=opendir(path);
	if (!pdir) return idx;
	
	while ((pent=readdir(pdir)) != NULL) 
		{
		if (idx == MAXNANDFOLDERS) break;
		
		sprintf (nand, "%s://%s/title/00000001", device, pent->d_name);

		if (fsop_DirExist(nand))
			{
			//grlib_dosm (nand);
			
			sprintf (nand, "%s://%s", device, pent->d_name);
			nf[idx] = malloc (strlen(nand)+1);
			strcpy (nf[idx], nand); // Store to the list
			idx++;
			}
		}
	closedir(pdir);
	
	return idx;
	}

static void ShowNandMenu (void)
	{
	char buff[300];
	char *nandFolders[MAXNANDFOLDERS];
	int nandFodersCnt = 0;

	
	//MountDevices (true);
	
	buff[0] = '\0';
	
	strcat (buff, "Use Real NAND##100|");
	if (vars.neek == NEEK_NONE)
		{
		if (NandExits(DEV_SD))
			grlib_menuAddItem (buff, 101, "sd://");

		if (NandExits(DEV_USB))
			grlib_menuAddItem (buff, 102, "usb://");

		nandFodersCnt = ScanForNandFolders (nandFolders, nandFodersCnt, "sd");
		nandFodersCnt = ScanForNandFolders (nandFolders, nandFodersCnt, "usb");

		int i, id = 200;
		for (i = 0;  i < nandFodersCnt; i++)
			grlib_menuAddItem (buff, id++, nandFolders[i]);
		}
		
	strcat (buff, "|");
	strcat (buff, "Cancel##-1");
		
	Redraw();
	grlib_PushScreen();
	
	int item = grlib_menu ("Select NAND Source", buff);
		
	if (item == 100)
		{
		config.chnBrowser.nand = NAND_REAL;
		browse = 1;
		}

	if (item == 101)
		{
		config.chnBrowser.nand = NAND_EMUSD;
		browse = 1;
		}

	if (item == 102)
		{
		config.chnBrowser.nand = NAND_EMUUSB;
		browse = 1;
		}
	
	config.chnBrowser.nandPath[0] = '\0';
	if (item >= 200)
		{
		int i = item - 200;
		char dev[10];
		
		strcpy (dev, "sd://");
		if (strstr (nandFolders[i], dev))
			{
			config.chnBrowser.nand = NAND_EMUSD;
			strcpy (config.chnBrowser.nandPath, &nandFolders[i][strlen(dev)-1]);
			}
			
		strcpy (dev, "usb://");
		if (strstr (nandFolders[i], dev))
			{
			config.chnBrowser.nand = NAND_EMUUSB;
			strcpy (config.chnBrowser.nandPath, &nandFolders[i][strlen(dev)-1]);
			}
		
		//grlib_dosm ("%d, %s", config.chnBrowser.nand, config.chnBrowser.nandPath);	
		browse = 1;
		}
	
	int i; for (i = 0; i < nandFodersCnt; i++) free (nandFolders[i]);
	}

	
static void ShowNandOptions (void)
	{
	char buff[300];
	
	//MountDevices (true);
	
	buff[0] = '\0';
	
	strcat (buff, "Download covers...##10||");
	strcat (buff, "Rebuild game list: neek2o standard mode (reboot required)...##9|");
	strcat (buff, "Rebuild game list: <desc>[id].wbs mode (reboot required)...##12|");
	strcat (buff, "Reset configuration files...##11||");
	strcat (buff, "Cancel##-1");
		
	Redraw();
	grlib_PushScreen();
	
	int item = grlib_menu ("NAND Options", buff);
		
	if (item == 9)
		{
		neek_KillDIConfig ();
		Shutdown (0);
		SYS_ResetSystem(SYS_RETURNTOMENU,0,0);
		}

	if (item == 12)
		{
		neek_CreateCDIConfig ();
		Shutdown (0);
		SYS_ResetSystem(SYS_RETURNTOMENU,0,0);
		}
		
	if (item == 10)
		{
		DownloadCovers();
		}

	if (item == 11)
		{
		CleanTitleConfiguration();
		}
	}

	
static void ShowMainMenu (void)
	{
	char buff[300];
	
	buff[0] = '\0';
	
	strcat (buff, "Switch to Homebrew mode##100|");
	strcat (buff, "Switch to Channel mode##101|");
	strcat (buff, "|");
	strcat (buff, "Game options##8|");
	//strcat (buff, "|");
	//strcat (buff, "Select titles filter##3|");

	if (showHidden)
		strcat (buff, "Hide hidden titles##6|");
	else
		strcat (buff, "Show hidden titles##7|");
		
	strcat (buff, "|");
	strcat (buff, "Run WII system menu##4|");
	strcat (buff, "Run BOOTMII##20|");
	strcat (buff, "Run DISC##21|");
	
	strcat (buff, "|");	
	strcat (buff, "Options...##5|");
	strcat (buff, "Cancel##-1");
		
	Redraw();
	grlib_PushScreen();
	
	int item = grlib_menu ("Channel menu'", buff);
		
	if (item == 100)
		{
		browserRet = INTERACTIVE_RET_TOHOMEBREW;
		}
	if (item == 101)
		{
		browserRet = INTERACTIVE_RET_TOCHANNELS;
		}
		
	if (item == 3)
		{
		ShowFilterMenu ();
		}
		
	if (item == 4)
		{
		browserRet = INTERACTIVE_RET_HOME;
		}
		
	if (item == 5)
		{
		ShowAboutMenu ();
		}

	if (item == 6)
		{
		showHidden = 0;
		AppsSort ();
		}

	if (item == 7)
		{
		showHidden = 1;
		AppsSort ();
		}
		
	if (item == 20)
		{
		browserRet = INTERACTIVE_RET_BOOTMII;
		}

	if (item == 21)
		{
		browserRet = INTERACTIVE_RET_DISC;
		}

	if (item == 1)
		{
		ShowNandMenu();
		}
	
	if (item == 8)
		{
		ShowNandOptions();
		}
	}

static void Redraw (void)
	{
	int i;
	int ai;	// Application index (corrected by the offset)
	char sdev[64];
	
	Debug ("Redraw #1");

	if (!theme.ok)
		Video_DrawBackgroud (1);
	else
		GRRLIB_DrawImg( 0, 0, theme.bkg, 0, 1, 1, RGBA(255, 255, 255, 255) ); 
	
	if (config.chnBrowser.nand == NAND_REAL)
		strcpy (sdev, "Real NAND");
	else if (config.chnBrowser.nand == NAND_EMUSD)
		sprintf (sdev, "EmuNAND [SD] %s", config.chnBrowser.nandPath);
	else if (config.chnBrowser.nand == NAND_EMUUSB)
		sprintf (sdev, "EmuNAND [USB] %s", config.chnBrowser.nandPath);
	
	grlib_SetFontBMF(fonts[FNTNORM]);
	char ahpbrot[16];
	if (vars.ahbprot)
		strcpy (ahpbrot," AHPBROT");
	else
		strcpy (ahpbrot,"");
	
	if (vars.neek == NEEK_NONE)
		grlib_printf ( 25, 26, GRLIB_ALIGNLEFT, 0, "postLoader"VER" (IOS%d%s) - %s", vars.ios, ahpbrot, sdev);
	else
		{
		char neek[32];
		
		if (vars.neek == NEEK_2o)
			strcpy (neek, "neek2o");
		else
			strcpy (neek, "neek");

		if (strlen(vars.neekName))
			grlib_printf ( 25, 26, GRLIB_ALIGNLEFT, 0, "postLoader"VER" (%s %s) - Games", neek, vars.neekName);
		else
			grlib_printf ( 25, 26, GRLIB_ALIGNLEFT, 0, "postLoader"VER" (%s) - Games", neek);
		}
		
	grlib_printf ( 615, 26, GRLIB_ALIGNRIGHT, 0, "Page %d of %d", gamesPage+1, gamesPageMax+1);
	
	Debug ("chbrowser: Draw bk icons");
	
	Debug ("Redraw #2");

	// Prepare black box
	s_grlib_icon ico;
	for (i = 0; i < gui.spotsXpage; i++)
		{
		// Make sure that icon is not in sel state
		gui.spots[i].ico.sel = false;
		gui.spots[i].ico.title[0] = '\0';
		grlib_IconInit (&ico, &gui.spots[i].ico);

		ico.noIcon = true;
		grlib_IconDraw (&is, &ico);
		}
	
	Debug ("Redraw #3");

	// Draw Icons
	Debug ("chbrowser: Draw fg icons");
	gui.spotsIdx = 0;
	for (i = 0; i < gui.spotsXpage; i++)
		{
		ai = (gamesPage * gui.spotsXpage) + i;
		
		if (ai < gamesCnt && ai < games2Disp && gui.spotsIdx < SPOTSMAX)
			{
			// Draw application png
			if (gui.spots[gui.spotsIdx].id != ai)
				{
				if (gui.spots[gui.spotsIdx].ico.icon) GRRLIB_FreeTexture (gui.spots[gui.spotsIdx].ico.icon);
				gui.spots[gui.spotsIdx].ico.icon = GetTitleTexture (ai);
				}
				
			if (!gui.spots[gui.spotsIdx].ico.icon)
				strcpy (gui.spots[gui.spotsIdx].ico.title, games[ai].name);
			else
				gui.spots[gui.spotsIdx].ico.title[0] = '\0';

			grlib_IconDraw (&is, &gui.spots[gui.spotsIdx].ico);

			// Let's add the spot points, to reconize the icon pointed by wiimote
			gui.spots[gui.spotsIdx].id = ai;
			
			gui.spotsIdx++;
			}
		}

	grlib_SetFontBMF(fonts[FNTNORM]);
	
	if (gamesCnt == 0 && scanned)
		{
		grlib_DrawCenteredWindow ("No games found !", WAITPANWIDTH, 133, 0, NULL);
		Video_DrawIcon (TEX_EXCL, 320, 250);
		}
	}
	
static void Overlay (void)
	{
	Video_DrawWIFI ();
	return;
	}
		
int GameBrowser (void)
	{
	Debug ("GameBrowser");

	if (!vars.neek)
		{
		return INTERACTIVE_RET_TOHOMEBREW;
		}
	
	u32 btn;
	u8 redraw = 1;

	scanned = 0;
	browserRet = -1;

	grlibSettings.color_window = RGBA(192,192,192,255);
	grlibSettings.color_windowBg = RGBA(32,32,32,128);
	
	grlib_SetRedrawCallback (Redraw, Overlay);
	
	games = calloc (CHNMAX, sizeof(s_game));
	
	// Immediately draw the screen...
	StructFree ();
	InitializeGui ();
	
	Redraw ();
	grlib_PushScreen ();
	grlib_PopScreen ();
	grlib_Render();  // Render the theme.frame buffer to the TV
	
	GameBrowse ();
	
	ConfigWrite ();

	if (config.chnPage >= 0 && config.chnPage <= gamesPageMax)
		gamesPage = config.chnPage;
	else
		gamesPage = 0;
	
	// Loop forever
    while (browserRet == -1) 
		{
		btn = grlib_GetUserInput();
		
		// If [HOME] was pressed on the first Wiimote, break out of the loop
		if (btn)
			{
			if (btn & WPAD_BUTTON_1) 
				{
				browserRet = INTERACTIVE_RET_TOHOMEBREW;
				}
			
			if (btn & WPAD_BUTTON_A && gamesSelected != -1) 
				{
				ReadGameConfig (gamesSelected);
				
				memcpy (&config.run.game, &gameConf, sizeof(s_gameConfig));
				config.run.neekSlot = games[gamesSelected].slot;
				//config.run.nand = config.chnBrowser.nand;
				//strcpy (config.run.nandPath, config.chnBrowser.nandPath);
				
				browserRet = INTERACTIVE_RET_GAMESEL;
				break;
				}
				
			/////////////////////////////////////////////////////////////////////////////////////////////
			// Select application as default
			if (btn & WPAD_BUTTON_B && gamesSelected != -1)
				{
				ShowAppMenu (gamesSelected);
				redraw = 1;
				}

			if (btn & WPAD_BUTTON_2)
				{
				/*
				ShowFilterMenu ();
				ConfigWrite ();
				redraw = 1;
				*/
				}

			if (btn & WPAD_BUTTON_HOME)
				{
				ShowMainMenu ();
				ConfigWrite ();
				redraw = 1;
				}
			
			if (btn & WPAD_BUTTON_PLUS) {gamesPage++; redraw = 1;}
			if (btn & WPAD_BUTTON_MINUS)  {gamesPage--; redraw = 1;}
			}
		
		if (redraw)
			{
			if (gamesPage < 0)
				{
				gamesPage = 0;
				continue;
				}
			if (gamesPage > gamesPageMax)
				{
				gamesPage = gamesPageMax;
				continue;
				}
			
			Redraw ();
			grlib_PushScreen ();
			
			redraw = 0;
			}
		
		grlib_PopScreen ();
		FindSpot ();
		Overlay ();
		grlib_DrawIRCursor ();
        grlib_Render();  // Render the theme.frame buffer to the TV
		
		if (browse)
			{
			GameBrowse ();
			browse = 0;
			redraw = 1;
			}
		
		if (grlibSettings.wiiswitch_poweroff || grlibSettings.wiiswitch_reset)
			{
			browserRet = INTERACTIVE_RET_SHUTDOWN;
			break;
			}

		if (wiiload.status == WIILOAD_HBZREADY)
			{
			WiiloadZipMenu ();
			redraw = 1;
			}
			
		if (wiiload.status == WIILOAD_HBREADY && WiiloadPostloaderDolMenu())
			{
			browserRet = INTERACTIVE_RET_WIILOAD;
			redraw = 1;
			break;
			}
		
		usleep (5000);
		}

	// save current page
	config.chnPage = gamesPage;

	SaveTitlesCache ();
	
	// Clean up all data
	StructFree ();
	gui_Clean ();
	free (games);
	
	grlib_SetRedrawCallback (NULL, NULL);
	
	return browserRet;
	}