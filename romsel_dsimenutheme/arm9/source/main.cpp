#include <nds.h>
#include <nds/arm9/dldi.h>
#include "io_m3_common.h"
#include "io_g6_common.h"
#include "io_sc_common.h"
#include "exptools.h"

#include <fat.h>
#include "fat_ext.h"
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>

#include <gl2d.h>
#include <maxmod9.h>
#include <string.h>
#include <unistd.h>

#include "date.h"
#include "fileCopy.h"

#include "graphics/graphics.h"

#include "common/dsimenusettings.h"
#include "common/flashcard.h"
#include "common/nitrofs.h"
#include "common/systemdetails.h"
#include "myDSiMode.h"
#include "graphics/ThemeConfig.h"
#include "graphics/ThemeTextures.h"
#include "graphics/themefilenames.h"

#include "errorScreen.h"
#include "fileBrowse.h"
#include "nds_loader_arm9.h"
#include "gbaswitch.h"
#include "ndsheaderbanner.h"
#include "perGameSettings.h"
//#include "tool/logging.h"

#include "graphics/fontHandler.h"
#include "graphics/iconHandler.h"

#include "common/inifile.h"
#include "tool/stringtool.h"
#include "common/tonccpy.h"

#include "sound.h"
#include "language.h"

#include "cheat.h"
#include "crc.h"

#include "dmaExcludeMap.h"
#include "asyncReadExcludeMap.h"
#include "donorMap.h"
#include "saveMap.h"
#include "ROMList.h"

#include "sr_data_srllastran.h"		 // For rebooting into the game

bool useTwlCfg = false;

bool whiteScreen = true;
bool fadeType = false; // false = out, true = in
bool fadeSpeed = true; // false = slow (for DSi launch effect), true = fast
bool fadeColor = true; // false = black, true = white
bool controlTopBright = true;
bool controlBottomBright = true;
//bool widescreenEffects = false;

extern void ClearBrightness();
extern bool displayGameIcons;
extern bool showProgressIcon;
extern bool showProgressBar;
extern int progressBarLength;

const char *settingsinipath = "sd:/_nds/TWiLightMenu/settings.ini";
const char *bootstrapinipath = "sd:/_nds/nds-bootstrap.ini";

const char *unlaunchAutoLoadID = "AutoLoadInfo";
static char16_t hiyaNdsPath[] = u"sdmc:/hiya.dsi";
char launcherPath[256];

bool extention(const std::string& filename, const char* ext) {
	if(strcasecmp(filename.c_str() + filename.size() - strlen(ext), ext)) {
		return false;
	} else {
		return true;
	}
}

/**
 * Remove trailing slashes from a pathname, if present.
 * @param path Pathname to modify.
 */
void RemoveTrailingSlashes(std::string &path) {
	if (path.size() == 0) return;
	while (!path.empty() && path[path.size() - 1] == '/') {
		path.resize(path.size() - 1);
	}
}

/**
 * Remove trailing spaces from a cheat code line, if present.
 * @param path Code line to modify.
 */
/*static void RemoveTrailingSpaces(std::string& code)
{
	while (!code.empty() && code[code.size()-1] == ' ') {
		code.resize(code.size()-1);
	}
}*/

// These are used by flashcard functions and must retain their trailing slash.
static const std::string slashchar = "/";
static const std::string woodfat = "fat0:/";
static const std::string dstwofat = "fat1:/";

typedef TWLSettings::TLaunchType Launch;

int mpuregion = 0;
int mpusize = 0;

bool applaunch = false;
bool dsModeForced = false;

bool useBackend = false;

bool dropDown = false;
int currentBg = 0;
bool showSTARTborder = false;
bool buttonArrowTouched[2] = {false};
bool scrollWindowTouched = false;

bool titleboxXmoveleft = false;
bool titleboxXmoveright = false;

bool applaunchprep = false;

int spawnedtitleboxes = 0;

s16 usernameRendered[11] = {0};
bool usernameRenderedDone = false;

bool showColon = true;

struct statvfs st[2];

touchPosition touch;

//---------------------------------------------------------------------------------
void stop(void) {
	//---------------------------------------------------------------------------------
	while (1) {
		swiWaitForVBlank();
	}
}

/**
 * Set donor SDK version for a specific game.
 */
int SetDonorSDK() {
	char gameTid3[5];
	for (int i = 0; i < 3; i++) {
		gameTid3[i] = gameTid[CURPOS][i];
	}

	for (auto i : donorMap) {
		if (i.first == 5 && gameTid3[0] == 'V')
			return 5;

		if (i.second.find(gameTid3) != i.second.cend())
			return i.first;
	}

	return 0;
}


/**
 * Disable card read DMA for a specific game.
 */
bool setCardReadDMA() {
	if (perGameSettings_cardReadDMA == -1) {
		// TODO: If the list gets large enough, switch to bsearch().
		for (unsigned int i = 0; i < sizeof(cardReadDMAExcludeList)/sizeof(cardReadDMAExcludeList[0]); i++) {
			if (memcmp(gameTid[CURPOS], cardReadDMAExcludeList[i], 3) == 0) {
				// Found match
				return false;
			}
		}
	}

	return perGameSettings_cardReadDMA == -1 ? ms().cardReadDMA : perGameSettings_cardReadDMA;
}

/**
 * Disable asynch card read for a specific game.
 */
bool setAsyncReadDMA() {
	if (perGameSettings_asyncCardRead == -1) {
		// TODO: If the list gets large enough, switch to bsearch().
		for (unsigned int i = 0; i < sizeof(asyncReadExcludeList)/sizeof(asyncReadExcludeList[0]); i++) {
			if (memcmp(gameTid[CURPOS], asyncReadExcludeList[i], 3) == 0) {
				// Found match
				return false;
			}
		}
	}

	return perGameSettings_asyncCardRead == -1 ? ms().asyncCardRead : perGameSettings_asyncCardRead;
}

/**
 * Set MPU settings for a specific game.
 */
void SetMPUSettings() {
	scanKeys();
	int pressed = keysHeld();

	if (pressed & KEY_B) {
		mpuregion = 1;
	} else if (pressed & KEY_X) {
		mpuregion = 2;
	} else if (pressed & KEY_Y) {
		mpuregion = 3;
	} else {
		mpuregion = 0;
	}
	if (pressed & KEY_RIGHT) {
		mpusize = 3145728;
	} else if (pressed & KEY_LEFT) {
		mpusize = 1;
	} else {
		mpusize = 0;
	}
}

/**
 * Fix AP for some games.
 */
std::string setApFix(const char *filename) {
	if (flashcardFound()) {
		remove("fat:/_nds/nds-bootstrap/apFix.ips");
		remove("fat:/_nds/nds-bootstrap/apFixCheat.bin");
	}

	bool ipsFound = false;
	bool cheatVer = true;
	char ipsPath[256];
	if (!ipsFound) {
		snprintf(ipsPath, sizeof(ipsPath), "%s:/_nds/TWiLightMenu/extras/apfix/cht/%s.bin", sdFound() ? "sd" : "fat", filename);
		ipsFound = (access(ipsPath, F_OK) == 0);
	}

	if (!ipsFound) {
		snprintf(ipsPath, sizeof(ipsPath), "%s:/_nds/TWiLightMenu/extras/apfix/cht/%s-%X.bin", sdFound() ? "sd" : "fat", gameTid[CURPOS], headerCRC[CURPOS]);
		ipsFound = (access(ipsPath, F_OK) == 0);
	}

	if (!ipsFound) {
		snprintf(ipsPath, sizeof(ipsPath), "%s:/_nds/TWiLightMenu/extras/apfix/%s.ips", sdFound() ? "sd" : "fat", filename);
		ipsFound = (access(ipsPath, F_OK) == 0);
		if (ipsFound) {
			cheatVer = false;
		}
	}

	if (!ipsFound) {
		snprintf(ipsPath, sizeof(ipsPath), "%s:/_nds/TWiLightMenu/extras/apfix/%s-%X.ips", sdFound() ? "sd" : "fat", gameTid[CURPOS], headerCRC[CURPOS]);
		ipsFound = (access(ipsPath, F_OK) == 0);
		if (ipsFound) {
			cheatVer = false;
		}
	}

	if (ipsFound) {
		if (ms().secondaryDevice && sdFound()) {
			mkdir("fat:/_nds", 0777);
			mkdir("fat:/_nds/nds-bootstrap", 0777);
			fcopy(ipsPath, cheatVer ? "fat:/_nds/nds-bootstrap/apFixCheat.bin" : "fat:/_nds/nds-bootstrap/apFix.ips");
			return cheatVer ? "fat:/_nds/nds-bootstrap/apFixCheat.bin" : "fat:/_nds/nds-bootstrap/apFix.ips";
		}
		return ipsPath;
	} else {
		FILE *file = fopen(sdFound() ? "sd:/_nds/TWiLightMenu/extras/apfix.pck" : "fat:/_nds/TWiLightMenu/extras/apfix.pck", "rb");
		if (file) {
			char buf[5] = {0};
			fread(buf, 1, 4, file);
			if (strcmp(buf, ".PCK") != 0) // Invalid file
				return "";

			u32 fileCount;
			fread(&fileCount, 1, sizeof(fileCount), file);

			u32 offset = 0, size = 0;

			// Try binary search for the game
			int left = 0;
			int right = fileCount;

			while (left <= right) {
				int mid = left + ((right - left) / 2);
				fseek(file, 16 + mid * 16, SEEK_SET);
				fread(buf, 1, 4, file);
				int cmp = strcmp(buf, gameTid[CURPOS]);
				if (cmp == 0) { // TID matches, check CRC
					u16 crc;
					fread(&crc, 1, sizeof(crc), file);

					if (crc == headerCRC[CURPOS]) { // CRC matches
						fread(&offset, 1, sizeof(offset), file);
						fread(&size, 1, sizeof(size), file);
						cheatVer = fgetc(file) & 1;
						break;
					} else if (crc < headerCRC[CURPOS]) {
						left = mid + 1;
					} else {
						right = mid - 1;
					}
				} else if (cmp < 0) {
					left = mid + 1;
				} else {
					right = mid - 1;
				}
			}

			if (offset > 0 && size > 0) {
				fseek(file, offset, SEEK_SET);
				u8 *buffer = new u8[size];
				fread(buffer, 1, size, file);

				if (flashcardFound()) {
					mkdir("fat:/_nds", 0777);
					mkdir("fat:/_nds/nds-bootstrap", 0777);
				}
				snprintf(ipsPath, sizeof(ipsPath), "%s:/_nds/nds-bootstrap/apFix%s", ms().secondaryDevice ? "fat" : "sd", cheatVer ? "Cheat.bin" : ".ips");
				FILE *out = fopen(ipsPath, "wb");
				if(out) {
					fwrite(buffer, 1, size, out);
					fclose(out);
				}
				delete[] buffer;
				fclose(file);
				return ipsPath;
			}

			fclose(file);
		}
	}

	return "";
}

sNDSHeader ndsCart;

/**
 * Enable widescreen for some games.
 */
void SetWidescreen(const char *filename) {
	const char* wideCheatDataPath = ms().secondaryDevice && (!isDSiWare[CURPOS] || (isDSiWare[CURPOS] && !ms().dsiWareToSD)) ? "fat:/_nds/nds-bootstrap/wideCheatData.bin" : "sd:/_nds/nds-bootstrap/wideCheatData.bin";
	remove(wideCheatDataPath);

	bool useWidescreen = (perGameSettings_wideScreen == -1 ? ms().wideScreen : perGameSettings_wideScreen);

	if ((isDSiMode() && sys().arm7SCFGLocked()) || ms().consoleModel < 2
	|| !useWidescreen || ms().macroMode) {
		return;
	}
	
	if (isHomebrew[CURPOS] && ms().homebrewHasWide && (access("sd:/_nds/TWiLightMenu/TwlBg/Widescreen.cxi", F_OK) == 0)) {
		if (access("sd:/luma/sysmodules/TwlBg.cxi", F_OK) == 0) {
			rename("sd:/luma/sysmodules/TwlBg.cxi", "sd:/_nds/TWiLightMenu/TwlBg/TwlBg.cxi.bak");
		}
		if (rename("sd:/_nds/TWiLightMenu/TwlBg/Widescreen.cxi", "sd:/luma/sysmodules/TwlBg.cxi") == 0) {
			tonccpy((u32 *)0x02000300, sr_data_srllastran, 0x020);
			DC_FlushAll();
			fifoSendValue32(FIFO_USER_02, 1);
			stop();
		}
		return;
	}

	bool wideCheatFound = false;
	char wideBinPath[256];
	if (ms().launchType[ms().secondaryDevice] == Launch::ESDFlashcardLaunch) {
		snprintf(wideBinPath, sizeof(wideBinPath), "sd:/_nds/TWiLightMenu/extras/widescreen/%s.bin", filename);
		wideCheatFound = (access(wideBinPath, F_OK) == 0);
	}

	char s1GameTid[5];

	if (ms().slot1Launched) {
		// Reset Slot-1 to allow reading card header
		sysSetCardOwner (BUS_OWNER_ARM9);
		disableSlot1();
		for(int i = 0; i < 25; i++) { swiWaitForVBlank(); }
		enableSlot1();
		for(int i = 0; i < 15; i++) { swiWaitForVBlank(); }

		cardReadHeader((uint8*)&ndsCart);

		tonccpy(s1GameTid, ndsCart.gameCode, 4);
		s1GameTid[4] = 0;

		snprintf(wideBinPath, sizeof(wideBinPath), "sd:/_nds/TWiLightMenu/extras/widescreen/%s-%X.bin", s1GameTid, ndsCart.headerCRC16);
		wideCheatFound = (access(wideBinPath, F_OK) == 0);
	} else if (!wideCheatFound) {
		snprintf(wideBinPath, sizeof(wideBinPath), "sd:/_nds/TWiLightMenu/extras/widescreen/%s-%X.bin", gameTid[CURPOS], headerCRC[CURPOS]);
		wideCheatFound = (access(wideBinPath, F_OK) == 0);
	}

	if (isHomebrew[CURPOS]) {
		return;
	}

	mkdir(ms().secondaryDevice && (!isDSiWare[CURPOS] || (isDSiWare[CURPOS] && !ms().dsiWareToSD)) ? "fat:/_nds" : "sd:/_nds", 0777);
	mkdir(ms().secondaryDevice && (!isDSiWare[CURPOS] || (isDSiWare[CURPOS] && !ms().dsiWareToSD)) ? "fat:/_nds/nds-bootstrap" : "sd:/_nds/nds-bootstrap", 0777);

	if (wideCheatFound) {
		if (fcopy(wideBinPath, wideCheatDataPath) != 0) {
			std::string resultText = STR_FAILED_TO_COPY_WIDESCREEN;
			remove(wideCheatDataPath);
			clearText();
			printLarge(false, 0, ms().theme == 4 ? 24 : 72, resultText, Alignment::center);
			if (ms().theme != 4) {
				fadeType = true; // Fade in from white
			}
			for (int i = 0; i < 60 * 3; i++) {
				swiWaitForVBlank(); // Wait 3 seconds
			}
			if (ms().theme != 4) {
				fadeType = false;	   // Fade to white
				for (int i = 0; i < 25; i++) {
					swiWaitForVBlank();
				}
			}
			return;
		}
	} else {
		char *tid = ms().slot1Launched ? s1GameTid : gameTid[CURPOS];
		u16 crc16 = ms().slot1Launched ? ndsCart.headerCRC16 : headerCRC[CURPOS];

		FILE *file = fopen(sdFound() ? "sd:/_nds/TWiLightMenu/extras/widescreen.pck" : "fat:/_nds/TWiLightMenu/extras/widescreen.pck", "rb");
		if (file) {
			char buf[5] = {0};
			fread(buf, 1, 4, file);
			if (strcmp(buf, ".PCK") != 0) // Invalid file
				return;

			u32 fileCount;
			fread(&fileCount, 1, sizeof(fileCount), file);

			u32 offset = 0, size = 0;

			// Try binary search for the game
			int left = 0;
			int right = fileCount;

			while (left <= right) {
				int mid = left + ((right - left) / 2);
				fseek(file, 16 + mid * 16, SEEK_SET);
				fread(buf, 1, 4, file);
				int cmp = strcmp(buf, tid);
				if (cmp == 0) { // TID matches, check CRC
					u16 crc;
					fread(&crc, 1, sizeof(crc), file);

					if (crc == crc16) { // CRC matches
						fread(&offset, 1, sizeof(offset), file);
						fread(&size, 1, sizeof(size), file);
						wideCheatFound = true;
						break;
					} else if (crc < crc16) {
						left = mid + 1;
					} else {
						right = mid - 1;
					}
				} else if (cmp < 0) {
					left = mid + 1;
				} else {
					right = mid - 1;
				}
			}

			if (offset > 0) {
				fseek(file, offset, SEEK_SET);
				u8 *buffer = new u8[size];
				fread(buffer, 1, size, file);

				snprintf(wideBinPath, sizeof(wideBinPath), "%s:/_nds/nds-bootstrap/wideCheatData.bin", ms().secondaryDevice && (!isDSiWare[CURPOS] || (isDSiWare[CURPOS] && !ms().dsiWareToSD)) ? "fat" : "sd");
				FILE *out = fopen(wideBinPath, "wb");
				if(out) {
					fwrite(buffer, 1, size, out);
					fclose(out);
				}
				delete[] buffer;
			}

			fclose(file);
		}
	}
	if (wideCheatFound && (access("sd:/_nds/TWiLightMenu/TwlBg/Widescreen.cxi", F_OK) == 0)) {
		if (access("sd:/luma/sysmodules/TwlBg.cxi", F_OK) == 0) {
			rename("sd:/luma/sysmodules/TwlBg.cxi", "sd:/_nds/TWiLightMenu/TwlBg/TwlBg.cxi.bak");
		}
		if (rename("sd:/_nds/TWiLightMenu/TwlBg/Widescreen.cxi", "sd:/luma/sysmodules/TwlBg.cxi") == 0) {
			tonccpy((u32 *)0x02000300, sr_data_srllastran, 0x020);
			DC_FlushAll();
			fifoSendValue32(FIFO_USER_02, 1);
			stop();
		}
	}
}

char filePath[PATH_MAX];

void doPause() {
	while (1) {
		scanKeys();
		if (keysDown() & KEY_START)
			break;
		snd().updateStream();
		swiWaitForVBlank();
	}
	scanKeys();
}

void loadGameOnFlashcard (const char *ndsPath, bool dsGame) {
	bool runNds_boostCpu = false;
	bool runNds_boostVram = false;
	std::string filename = ndsPath;
	const size_t last_slash_idx = filename.find_last_of("/");
	if (std::string::npos != last_slash_idx) {
		filename.erase(0, last_slash_idx + 1);
	}

	loadPerGameSettings(filename);

	if (dsiFeatures() && dsGame) {
		runNds_boostCpu = perGameSettings_boostCpu == -1 ? ms().boostCpu : perGameSettings_boostCpu;
		runNds_boostVram = perGameSettings_boostVram == -1 ? ms().boostVram : perGameSettings_boostVram;
	}
	if (dsGame) {
		// Move .sav outside of "saves" folder for flashcard kernel usage
		const char *typeToReplace = ".nds";
		if (extention(filename, ".dsi")) {
			typeToReplace = ".dsi";
		} else if (extention(filename, ".ids")) {
			typeToReplace = ".ids";
		} else if (extention(filename, ".srl")) {
			typeToReplace = ".srl";
		} else if (extention(filename, ".app")) {
			typeToReplace = ".app";
		}

		std::string savename = replaceAll(filename, typeToReplace, getSavExtension());
		std::string savenameFc = replaceAll(filename, typeToReplace, ".sav");
		std::string romFolderNoSlash = ms().romfolder[true];
		RemoveTrailingSlashes(romFolderNoSlash);
		mkdir("saves", 0777);
		std::string savepath = romFolderNoSlash + "/saves/" + savename;
		std::string savepathFc = romFolderNoSlash + "/" + savenameFc;
		rename(savepath.c_str(), savepathFc.c_str());
	}

	std::string fcPath;
	int err = 0;
	snd().stopStream();

	if ((memcmp(io_dldi_data->friendlyName, "R4(DS) - Revolution for DS", 26) == 0)
	 || (memcmp(io_dldi_data->friendlyName, "R4TF", 4) == 0)
	 || (memcmp(io_dldi_data->friendlyName, "R4iDSN", 6) == 0)) {
		CIniFile fcrompathini("fat:/_wfwd/lastsave.ini");
		fcPath = replaceAll(ndsPath, "fat:/", woodfat);
		fcrompathini.SetString("Save Info", "lastLoaded", fcPath);
		fcrompathini.SaveIniFile("fat:/_wfwd/lastsave.ini");
		err = runNdsFile("fat:/Wfwd.dat", 0, NULL, true, true, true, runNds_boostCpu, runNds_boostVram, -1);
	} else if (memcmp(io_dldi_data->friendlyName, "Acekard AK2", 0xB) == 0) {
		CIniFile fcrompathini("fat:/_afwd/lastsave.ini");
		fcPath = replaceAll(ndsPath, "fat:/", woodfat);
		fcrompathini.SetString("Save Info", "lastLoaded", fcPath);
		fcrompathini.SaveIniFile("fat:/_afwd/lastsave.ini");
		err = runNdsFile("fat:/Afwd.dat", 0, NULL, true, true, true, runNds_boostCpu, runNds_boostVram, -1);
	} else if (memcmp(io_dldi_data->friendlyName, "DSTWO(Slot-1)", 0xD) == 0) {
		CIniFile fcrompathini("fat:/_dstwo/autoboot.ini");
		fcPath = replaceAll(ndsPath, "fat:/", dstwofat);
		fcrompathini.SetString("Dir Info", "fullName", fcPath);
		fcrompathini.SaveIniFile("fat:/_dstwo/autoboot.ini");
		err = runNdsFile("fat:/_dstwo/autoboot.nds", 0, NULL, true, true, true, runNds_boostCpu, runNds_boostVram, -1);
	} else if ((memcmp(io_dldi_data->friendlyName, "TTCARD", 6) == 0)
			 || (memcmp(io_dldi_data->friendlyName, "DSTT", 4) == 0)
			 || (memcmp(io_dldi_data->friendlyName, "DEMON", 5) == 0)
			 || (memcmp(io_dldi_data->friendlyName, "DSONE", 5) == 0)) {
		CIniFile fcrompathini("fat:/TTMenu/YSMenu.ini");
		fcPath = replaceAll(ndsPath, "fat:/", slashchar);
		fcrompathini.SetString("YSMENU", "AUTO_BOOT", fcPath);
		fcrompathini.SaveIniFile("fat:/TTMenu/YSMenu.ini");
		err = runNdsFile("fat:/YSMenu.nds", 0, NULL, true, true, true, runNds_boostCpu, runNds_boostVram, -1);
	}

	char text[64];
	snprintf(text, sizeof(text), STR_START_FAILED_ERROR.c_str(), err);
	fadeType = true;	// Fade from white
	if (err == 0) {
		printLarge(false, 4, 4, STR_ERROR_FLASHCARD_UNSUPPORTED);
		printLarge(false, 4, 68, io_dldi_data->friendlyName);
	} else {
		printLarge(false, 4, 4, text);
	}
	printSmall(false, 4, 90, STR_PRESS_B_RETURN);
	int pressed = 0;
	do {
		scanKeys();
		pressed = keysDownRepeat();
		checkSdEject();
		swiWaitForVBlank();
	} while (!(pressed & KEY_B));
	fadeType = false;	// Fade to white
	for (int i = 0; i < 25; i++) {
		swiWaitForVBlank();
	}
	if (!isDSiMode()) {
		chdir("fat:/");
	} else if (sdFound()) {
		chdir("sd:/");
	}
	runNdsFile("/_nds/TWiLightMenu/dsimenu.srldr", 0, NULL, true, false, false, true, true, -1);
	stop();
}

void unlaunchRomBoot(std::string_view rom) {
	snd().stopStream();
	std::u16string path(FontGraphic::utf8to16(rom));
	if (path.substr(0, 3) == u"sd:") {
		path = u"sdmc:" + path.substr(3);
	}

	tonccpy((u8*)0x02000800, unlaunchAutoLoadID, 12);
	*(u16*)(0x0200080C) = 0x3F0;		// Unlaunch Length for CRC16 (fixed, must be 3F0h)
	*(u16*)(0x0200080E) = 0;			// Unlaunch CRC16 (empty)
	*(u32*)(0x02000810) = 0;			// Unlaunch Flags
	*(u32*)(0x02000810) |= BIT(0);		// Load the title at 2000838h
	*(u32*)(0x02000810) |= BIT(1);		// Use colors 2000814h
	*(u16*)(0x02000814) = 0x7FFF;		// Unlaunch Upper screen BG color (0..7FFFh)
	*(u16*)(0x02000816) = 0x7FFF;		// Unlaunch Lower screen BG color (0..7FFFh)
	toncset((u8*)0x02000818, 0, 0x20+0x208+0x1C0);		// Unlaunch Reserved (zero)
	for (uint i = 0; i < std::min(path.length(), 0x103u); i++) {
		((char16_t*)0x02000838)[i] = path[i];		// Unlaunch Device:/Path/Filename.ext (16bit Unicode,end by 0000h)
	}
	while (*(u16*)(0x0200080E) == 0) {	// Keep running, so that CRC16 isn't 0
		*(u16*)(0x0200080E) = swiCRC16(0xFFFF, (void*)0x02000810, 0x3F0);		// Unlaunch CRC16
	}

	DC_FlushAll();						// Make reboot not fail
	fifoSendValue32(FIFO_USER_02, 1);	// Reboot into DSiWare title, booted via Unlaunch
	stop();
}

void unlaunchSetHiyaBoot(void) {
	snd().stopStream();
	tonccpy((u8 *)0x02000800, unlaunchAutoLoadID, 12);
	*(u16 *)(0x0200080C) = 0x3F0;			   // Unlaunch Length for CRC16 (fixed, must be 3F0h)
	*(u16 *)(0x0200080E) = 0;			   // Unlaunch CRC16 (empty)
	*(u32 *)(0x02000810) = (BIT(0) | BIT(1));	  // Load the title at 2000838h
							   // Use colors 2000814h
	*(u16 *)(0x02000814) = 0x7FFF;			   // Unlaunch Upper screen BG color (0..7FFFh)
	*(u16 *)(0x02000816) = 0x7FFF;			   // Unlaunch Lower screen BG color (0..7FFFh)
	toncset((u8 *)0x02000818, 0, 0x20 + 0x208 + 0x1C0); // Unlaunch Reserved (zero)
	for (uint i = 0; i < sizeof(hiyaNdsPath)/sizeof(hiyaNdsPath[0]); i++) {
		((char16_t*)0x02000838)[i] = hiyaNdsPath[i];		// Unlaunch Device:/Path/Filename.ext (16bit Unicode,end by 0000h)
	}
	while (*(vu16 *)(0x0200080E) == 0) { // Keep running, so that CRC16 isn't 0
		*(u16 *)(0x0200080E) = swiCRC16(0xFFFF, (void *)0x02000810, 0x3F0); // Unlaunch CRC16
	}
}

/**
 * Reboot into an SD game when in DS mode.
 */
void ntrStartSdGame(void) {
	if (ms().consoleModel == 0) {
		unlaunchRomBoot("sd:/_nds/TWiLightMenu/resetgame.srldr");
	} else {
		tonccpy((u32 *)0x02000300, sr_data_srllastran, 0x020);
		DC_FlushAll();						// Make reboot not fail
		fifoSendValue32(FIFO_USER_02, 1);
		stop();
	}
}

void dsCardLaunch() {
	snd().stopStream();
	*(u32 *)(0x02000300) = 0x434E4C54; // Set "CNLT" warmboot flag
	*(u16 *)(0x02000304) = 0x1801;
	*(u32 *)(0x02000308) = 0x43415254; // "CART"
	*(u32 *)(0x0200030C) = 0x00000000;
	*(u32 *)(0x02000310) = 0x43415254; // "CART"
	*(u32 *)(0x02000314) = 0x00000000;
	*(u32 *)(0x02000318) = 0x00000013;
	*(u32 *)(0x0200031C) = 0x00000000;
	while (*(u16 *)(0x02000306) == 0) { // Keep running, so that CRC16 isn't 0
		*(u16 *)(0x02000306) = swiCRC16(0xFFFF, (void *)0x02000308, 0x18);
	}

	unlaunchSetHiyaBoot();

	DC_FlushAll();						// Make reboot not fail
	fifoSendValue32(FIFO_USER_02, 1); // Reboot into DSiWare title, booted via Launcher
	stop();
}

void s2RamAccess(bool open) {
	if (io_dldi_data->ioInterface.features & FEATURE_SLOT_NDS) return;

	if (open) {
		if (*(u16*)(0x020000C0) == 0x334D) {
			_M3_changeMode(M3_MODE_RAM);
		} else if (*(u16*)(0x020000C0) == 0x3647) {
			_G6_SelectOperation(G6_MODE_RAM);
		} else if (*(u16*)(0x020000C0) == 0x4353) {
			_SC_changeMode(SC_MODE_RAM);
		}
	} else {
		if (*(u16*)(0x020000C0) == 0x334D) {
			_M3_changeMode(M3_MODE_MEDIA);
		} else if (*(u16*)(0x020000C0) == 0x3647) {
			_G6_SelectOperation(G6_MODE_MEDIA);
		} else if (*(u16*)(0x020000C0) == 0x4353) {
			_SC_changeMode(SC_MODE_MEDIA);
		}
	}
}

void gbaSramAccess(bool open) {
	if (open) {
		if (*(u16*)(0x020000C0) == 0x334D) {
			_M3_changeMode(M3_MODE_RAM);
		} else if (*(u16*)(0x020000C0) == 0x3647) {
			_G6_SelectOperation(G6_MODE_RAM);
		} else if (*(u16*)(0x020000C0) == 0x4353) {
			_SC_changeMode(SC_MODE_RAM_RO);
		}
	} else {
		if (*(u16*)(0x020000C0) == 0x334D) {
			_M3_changeMode((io_dldi_data->ioInterface.features & FEATURE_SLOT_GBA) ? M3_MODE_MEDIA : M3_MODE_RAM);
		} else if (*(u16*)(0x020000C0) == 0x3647) {
			_G6_SelectOperation((io_dldi_data->ioInterface.features & FEATURE_SLOT_GBA) ? G6_MODE_MEDIA : G6_MODE_RAM);
		} else if (*(u16*)(0x020000C0) == 0x4353) {
			_SC_changeMode((io_dldi_data->ioInterface.features & FEATURE_SLOT_GBA) ? SC_MODE_MEDIA : SC_MODE_RAM);
		}
	}
}

int main(int argc, char **argv) {
	defaultExceptionHandler();
	sys().initFilesystem();
	sys().initArm7RegStatuses();

	if (!sys().fatInitOk()) {
		SetBrightness(0, 0);
		SetBrightness(1, 0);
		consoleDemoInit();
		iprintf("FAT init failed!");
		stop();
	}

	if (access(settingsinipath, F_OK) != 0 && flashcardFound()) {
		settingsinipath =
		    "fat:/_nds/TWiLightMenu/settings.ini"; // Fallback to .ini path on flashcard, if not found on
							   // SD card, or if SD access is disabled
	}

	useTwlCfg = (dsiFeatures() && (*(u8*)0x02000400 & 0x0F) && (*(u8*)0x02000401 == 0) && (*(u8*)0x02000402 == 0) && (*(u8*)0x02000404 == 0) && (*(u8*)0x02000448 != 0));

	sysSetCartOwner(BUS_OWNER_ARM9); // Allow arm9 to access GBA ROM

	//logInit();
	ms().loadSettings();
	//widescreenEffects = (ms().consoleModel >= 2 && ms().wideScreen && access("sd:/luma/sysmodules/TwlBg.cxi", F_OK) == 0);
	tfn(); //
	tc().loadConfig();
	tex().videoSetup(); // allocate texture pointers

	fontInit();

	if (ms().theme == 5) {
		tex().loadHBTheme();
	} else if (ms().theme == 4) {
		tex().loadSaturnTheme();
	} else if (ms().theme == 1) {
		tex().load3DSTheme();
	} else {
		tex().loadDSiTheme();
	}

	//printf("Username copied\n");
	if(useTwlCfg) {
		tonccpy(usernameRendered, (s16*)0x02000448, sizeof(s16) * 10);
	} else {
		tonccpy(usernameRendered, PersonalData->name, sizeof(s16) * PersonalData->nameLen);
	}

	if (sdFound()) statvfs("sd:/", &st[0]);
	if (flashcardFound()) statvfs("fat:/", &st[1]);

	if (ms().theme == 4 || ms().theme == 5) {
		whiteScreen = false;
		fadeColor = false;
	}

	langInit();

	std::string filename;

	if (sdFound() && ms().consoleModel < 2 && ms().launcherApp != -1) {
		u8 setRegion = 0;

		if (ms().sysRegion == -1) {
			// Determine SysNAND region by searching region of System Settings on SDNAND
			char tmdpath[256] = {0};
			for (u8 i = 0x41; i <= 0x5A; i++) {
				snprintf(tmdpath, sizeof(tmdpath), "sd:/title/00030015/484e42%x/content/title.tmd", i);
				if (access(tmdpath, F_OK) == 0) {
					setRegion = i;
					break;
				}
			}
		} else {
			switch (ms().sysRegion) {
			case 0:
			default:
				setRegion = 0x4A; // JAP
				break;
			case 1:
				setRegion = 0x45; // USA
				break;
			case 2:
				setRegion = 0x50; // EUR
				break;
			case 3:
				setRegion = 0x55; // AUS
				break;
			case 4:
				setRegion = 0x43; // CHN
				break;
			case 5:
				setRegion = 0x4B; // KOR
				break;
			}
		}

		if (ms().sysRegion == 9) {
			sprintf(launcherPath, "nand:/launcher.dsi");
		} else {
			sprintf(launcherPath,
				 "nand:/title/00030017/484E41%X/content/0000000%i.app", setRegion, ms().launcherApp);
		}
	}

	graphicsInit();
	iconManagerInit();

	keysSetRepeat(10, 2);

	std::vector<std::string> extensionList;
	if (ms().showNds) {
		extensionList.emplace_back(".nds");
		extensionList.emplace_back(".dsi");
		extensionList.emplace_back(".ids");
		extensionList.emplace_back(".srl");
		extensionList.emplace_back(".app");
		extensionList.emplace_back(".argv");
	}
	if (memcmp(io_dldi_data->friendlyName, "DSTWO(Slot-1)", 0xD) == 0) {
		extensionList.emplace_back(".plg");
	}
	if (ms().showRvid) {
		extensionList.emplace_back(".rvid");
		extensionList.emplace_back(".fv");
	}
	if (ms().showGba) {
		extensionList.emplace_back(".agb");
		extensionList.emplace_back(".gba");
		extensionList.emplace_back(".mb");
	}
	if (ms().showXex) {
		extensionList.emplace_back(".xex");
		extensionList.emplace_back(".atr");
	}
	if (ms().showA26) {
		extensionList.emplace_back(".a26");
	}
	if (ms().showA52) {
		extensionList.emplace_back(".a52");
	}
	if (ms().showA78) {
		extensionList.emplace_back(".a78");
	}
	if (ms().showGb) {
		extensionList.emplace_back(".gb");
		extensionList.emplace_back(".sgb");
		extensionList.emplace_back(".gbc");
	}
	if (ms().showNes) {
		extensionList.emplace_back(".nes");
		extensionList.emplace_back(".fds");
	}
	if (ms().showSmsGg) {
		extensionList.emplace_back(".sms");
		extensionList.emplace_back(".gg");
	}
	if (ms().showMd) {
		extensionList.emplace_back(".gen");
	}
	if (ms().showSnes) {
		extensionList.emplace_back(".smc");
		extensionList.emplace_back(".sfc");
	}
	if (ms().showPce) {
		extensionList.emplace_back(".pce");
	}
	srand(time(NULL));

	char path[256] = {0};

	//logPrint("snd()\n");
	snd();

	if (ms().theme == 4) {
		//logPrint("snd().playStartup()\n");
		snd().playStartup();
	} else if (ms().dsiMusic != 0) {
		if ((ms().theme == 1 && ms().dsiMusic == 1) || (ms().dsiMusic == 3 && tc().playStartupJingle())) {
			//logPrint("snd().playStartup()\n");
			snd().playStartup();
			//logPrint("snd().setStreamDelay(snd().getStartupSoundLength() - tc().startupJingleDelayAdjust())\n");
			snd().setStreamDelay(snd().getStartupSoundLength() - tc().startupJingleDelayAdjust());
		}
		//logPrint("snd().beginStream()\n");
		snd().beginStream();
	}

	if (ms().previousUsedDevice && bothSDandFlashcard() && ms().launchType[ms().previousUsedDevice] == Launch::EDSiWareLaunch
	&& ((access(ms().dsiWarePubPath.c_str(), F_OK) == 0 && access("sd:/_nds/TWiLightMenu/tempDSiWare.pub", F_OK) == 0)
	 || (access(ms().dsiWarePrvPath.c_str(), F_OK) == 0 && access("sd:/_nds/TWiLightMenu/tempDSiWare.prv", F_OK) == 0))) {
		fadeType = true; // Fade in from white
		printSmall(false, 0, 20, STR_TAKEWHILE_CLOSELID, Alignment::center);
		printLarge(false, 0, (ms().theme == 4 ? 80 : 88), STR_NOW_COPYING_DATA, Alignment::center);
		printSmall(false, 0, (ms().theme == 4 ? 96 : 104), STR_DONOT_TURNOFF_POWER, Alignment::center);
		updateText(false);
		for (int i = 0; i < 30; i++) {
			snd().updateStream();
			swiWaitForVBlank();
		}
		showProgressIcon = true;
		controlTopBright = false;
		if (access(ms().dsiWarePubPath.c_str(), F_OK) == 0) {
			fcopy("sd:/_nds/TWiLightMenu/tempDSiWare.pub", ms().dsiWarePubPath.c_str());
			rename("sd:/_nds/TWiLightMenu/tempDSiWare.pub", "sd:/_nds/TWiLightMenu/tempDSiWare.pub.bak");
		}
		if (access(ms().dsiWarePrvPath.c_str(), F_OK) == 0) {
			fcopy("sd:/_nds/TWiLightMenu/tempDSiWare.prv", ms().dsiWarePrvPath.c_str());
			rename("sd:/_nds/TWiLightMenu/tempDSiWare.prv", "sd:/_nds/TWiLightMenu/tempDSiWare.prv.bak");
		}
		showProgressIcon = false;
		if (ms().theme != 4) {
			fadeType = false; // Fade to white
			for (int i = 0; i < 25; i++) {
				snd().updateStream();
				swiWaitForVBlank();
			}
		}
		clearText();
		updateText(false);
	}

	while (1) {

		snprintf(path, sizeof(path), "%s", ms().romfolder[ms().secondaryDevice].c_str());
		// Set directory
		chdir(path);

		// Navigates to the file to launch
		filename = browseForFile(extensionList);

		////////////////////////////////////
		// Launch the item

		if (applaunch) {
			// Delete previously launched DSiWare copied from flashcard to SD
			if (!ms().gotosettings) {
				if (access("sd:/_nds/TWiLightMenu/tempDSiWare.dsi", F_OK) == 0) {
					remove("sd:/_nds/TWiLightMenu/tempDSiWare.dsi");
				}
				if (access("sd:/_nds/TWiLightMenu/tempDSiWare.pub.bak", F_OK) == 0) {
					remove("sd:/_nds/TWiLightMenu/tempDSiWare.pub.bak");
				}
				if (access("sd:/_nds/TWiLightMenu/tempDSiWare.prv.bak", F_OK) == 0) {
					remove("sd:/_nds/TWiLightMenu/tempDSiWare.prv.bak");
				}
				if (access("sd:/_nds/nds-bootstrap/patchOffsetCache/tempDSiWare.bin", F_OK) == 0) {
					remove("sd:/_nds/nds-bootstrap/patchOffsetCache/tempDSiWare.bin");
				}
			}

			// Construct a command line
			getcwd(filePath, PATH_MAX);
			int pathLen = strlen(filePath);
			vector<char *> argarray;

			bool isArgv = false;
			if (strcasecmp(filename.c_str() + filename.size() - 5, ".argv") == 0) {
				ms().romPath[ms().secondaryDevice] = std::string(filePath) + std::string(filename);

				FILE *argfile = fopen(filename.c_str(), "rb");
				char str[PATH_MAX], *pstr;
				const char seps[] = "\n\r\t ";

				while (fgets(str, PATH_MAX, argfile)) {
					// Find comment and end string there
					if ((pstr = strchr(str, '#')))
						*pstr = '\0';

					// Tokenize arguments
					pstr = strtok(str, seps);

					while (pstr != NULL) {
						argarray.push_back(strdup(pstr));
						pstr = strtok(NULL, seps);
					}
				}
				fclose(argfile);
				filename = argarray.at(0);
				isArgv = true;
			} else {
				argarray.push_back(strdup(filename.c_str()));
			}

			ms().slot1Launched = false;

			// Launch DSiWare .nds via Unlaunch
			if ((isDSiMode() || sdFound()) && isDSiWare[CURPOS]) {
				const char *typeToReplace = ".nds";
				if (extention(filename, ".dsi")) {
					typeToReplace = ".dsi";
				} else if (extention(filename, ".ids")) {
					typeToReplace = ".ids";
				} else if (extention(filename, ".srl")) {
					typeToReplace = ".srl";
				} else if (extention(filename, ".app")) {
					typeToReplace = ".app";
				}

				char *name = argarray.at(0);
				strcpy(filePath + pathLen, name);
				free(argarray.at(0));
				argarray.at(0) = filePath;

				std::string romFolderNoSlash = ms().romfolder[ms().secondaryDevice];
				RemoveTrailingSlashes(romFolderNoSlash);
				mkdir("saves", 0777);

				ms().dsiWareSrlPath = std::string(argarray[0]);
				ms().dsiWarePubPath = romFolderNoSlash + "/saves/" + filename;
				ms().dsiWarePubPath = replaceAll(ms().dsiWarePubPath, typeToReplace, getPubExtension());
				ms().dsiWarePrvPath = romFolderNoSlash + "/saves/" + filename;
				ms().dsiWarePrvPath = replaceAll(ms().dsiWarePrvPath, typeToReplace, getPrvExtension());
				if (!isArgv) {
					ms().romPath[ms().secondaryDevice] = std::string(argarray[0]);
				}
				ms().homebrewBootstrap = isHomebrew[CURPOS];
				ms().launchType[ms().secondaryDevice] = Launch::EDSiWareLaunch;
				ms().previousUsedDevice = ms().secondaryDevice;
				ms().saveSettings();

				sNDSHeaderExt NDSHeader;

				FILE *f_nds_file = fopen(filename.c_str(), "rb");

				fread(&NDSHeader, 1, sizeof(NDSHeader), f_nds_file);
				fclose(f_nds_file);

				fadeSpeed = true; // Fast fading

				if ((getFileSize(ms().dsiWarePubPath.c_str()) == 0) && (NDSHeader.pubSavSize > 0)) {
					if (ms().theme == 5) displayGameIcons = false;
					clearText();
					if (memcmp(io_dldi_data->friendlyName, "CycloDS iEvolution", 18) == 0) {
						// Display nothing
					} else if (ms().consoleModel >= 2) {
						printSmall(false, 0, 20, STR_TAKEWHILE_PRESSHOME, Alignment::center);
					} else {
						printSmall(false, 0, 20, STR_TAKEWHILE_CLOSELID, Alignment::center);
					}
					printLarge(false, 0, (ms().theme == 4 ? 80 : 88), STR_CREATING_PUBLIC_SAVE, Alignment::center);
					updateText(false);
					if (ms().theme != 4 && !fadeType) {
						fadeType = true; // Fade in from white
						for (int i = 0; i < 35; i++) {
							swiWaitForVBlank();
						}
					}
					showProgressIcon = true;

					static const int BUFFER_SIZE = 4096;
					char buffer[BUFFER_SIZE];
					toncset(buffer, 0, sizeof(buffer));
					char savHdrPath[64];
					snprintf(savHdrPath, sizeof(savHdrPath), "nitro:/DSiWareSaveHeaders/%X.savhdr",
						 (unsigned int)NDSHeader.pubSavSize);
					FILE *hdrFile = fopen(savHdrPath, "rb");
					if (hdrFile)
						fread(buffer, 1, 0x200, hdrFile);
					fclose(hdrFile);

					FILE *pFile = fopen(ms().dsiWarePubPath.c_str(), "wb");
					if (pFile) {
						fwrite(buffer, 1, sizeof(buffer), pFile);
						fseek(pFile, NDSHeader.pubSavSize - 1, SEEK_SET);
						fputc('\0', pFile);
						fclose(pFile);
					}
					showProgressIcon = false;
					clearText();
					printLarge(false, 0, (ms().theme == 4 ? 32 : 88), STR_PUBLIC_SAVE_CREATED, Alignment::center);
					updateText(false);
					for (int i = 0; i < 60; i++) {
						swiWaitForVBlank();
					}
					if (ms().theme == 5) displayGameIcons = true;
				}

				if ((getFileSize(ms().dsiWarePrvPath.c_str()) == 0) && (NDSHeader.prvSavSize > 0)) {
					if (ms().theme == 5) displayGameIcons = false;
					clearText();
					if (memcmp(io_dldi_data->friendlyName, "CycloDS iEvolution", 18) == 0) {
						// Display nothing
					} else if (ms().consoleModel >= 2) {
						printSmall(false, 0, 20, STR_TAKEWHILE_PRESSHOME, Alignment::center);
					} else {
						printSmall(false, 0, 20, STR_TAKEWHILE_CLOSELID, Alignment::center);
					}
					printLarge(false, 0, (ms().theme == 4 ? 80 : 88), STR_CREATING_PRIVATE_SAVE, Alignment::center);
					updateText(false);
					if (ms().theme != 4 && !fadeType) {
						fadeType = true; // Fade in from white
						for (int i = 0; i < 35; i++) {
							swiWaitForVBlank();
						}
					}
					showProgressIcon = true;

					static const int BUFFER_SIZE = 4096;
					char buffer[BUFFER_SIZE];
					toncset(buffer, 0, sizeof(buffer));
					char savHdrPath[64];
					snprintf(savHdrPath, sizeof(savHdrPath), "nitro:/DSiWareSaveHeaders/%X.savhdr",
						 (unsigned int)NDSHeader.prvSavSize);
					FILE *hdrFile = fopen(savHdrPath, "rb");
					if (hdrFile)
						fread(buffer, 1, 0x200, hdrFile);
					fclose(hdrFile);

					FILE *pFile = fopen(ms().dsiWarePrvPath.c_str(), "wb");
					if (pFile) {
						fwrite(buffer, 1, sizeof(buffer), pFile);
						fseek(pFile, NDSHeader.prvSavSize - 1, SEEK_SET);
						fputc('\0', pFile);
						fclose(pFile);
					}
					showProgressIcon = false;
					clearText();
					printLarge(false, 0, (ms().theme == 4 ? 32 : 88), STR_PRIVATE_SAVE_CREATED, Alignment::center);
					updateText(false);
					for (int i = 0; i < 60; i++) {
						swiWaitForVBlank();
					}
					if (ms().theme == 5) displayGameIcons = true;
				}

				if (ms().theme != 4 && ms().theme != 5 && fadeType) {
					fadeType = false; // Fade to white
					for (int i = 0; i < 25; i++) {
						swiWaitForVBlank();
					}
				}

				if (ms().secondaryDevice && (ms().dsiWareToSD || (!ms().dsiWareBooter && ms().consoleModel < 2)) && sdFound()) {
					clearText();
					printSmall(false, 0, 20, ms().consoleModel >= 2 ? STR_BARSTOPPED_PRESSHOME : STR_BARSTOPPED_CLOSELID, Alignment::center);
					printLarge(false, 0, (ms().theme == 4 ? 80 : 88), STR_NOW_COPYING_DATA, Alignment::center);
					printSmall(false, 0, (ms().theme == 4 ? 96 : 104), STR_DONOT_TURNOFF_POWER, Alignment::center);
					updateText(false);
					if (ms().theme != 4) {
						fadeType = true; // Fade in from white
						for (int i = 0; i < 35; i++) {
							swiWaitForVBlank();
						}
					}
					showProgressIcon = true;
					fcopy(ms().dsiWareSrlPath.c_str(), "sd:/_nds/TWiLightMenu/tempDSiWare.dsi");
					if ((access(ms().dsiWarePubPath.c_str(), F_OK) == 0) && (NDSHeader.pubSavSize > 0)) {
						fcopy(ms().dsiWarePubPath.c_str(),
						      "sd:/_nds/TWiLightMenu/tempDSiWare.pub");
					}
					if ((access(ms().dsiWarePrvPath.c_str(), F_OK) == 0) && (NDSHeader.prvSavSize > 0)) {
						fcopy(ms().dsiWarePrvPath.c_str(),
						      "sd:/_nds/TWiLightMenu/tempDSiWare.prv");
					}
					showProgressIcon = false;
					if (ms().theme != 4 && ms().theme != 5) {
						fadeType = false; // Fade to white
						for (int i = 0; i < 25; i++) {
							swiWaitForVBlank();
						}
					}

					if ((access(ms().dsiWarePubPath.c_str(), F_OK) == 0 && (NDSHeader.pubSavSize > 0))
					 || (access(ms().dsiWarePrvPath.c_str(), F_OK) == 0 && (NDSHeader.prvSavSize > 0))) {
						clearText();
						printLarge(false, 0, ms().theme == 4 ? 16 : 72, STR_RESTART_AFTER_SAVING, Alignment::center);
						updateText(false);
						if (ms().theme != 4) {
							fadeType = true; // Fade in from white
						}
						for (int i = 0; i < 60 * 3; i++) {
							swiWaitForVBlank(); // Wait 3 seconds
						}
						if (ms().theme != 4 && ms().theme != 5) {
							fadeType = false;	   // Fade to white
							for (int i = 0; i < 25; i++) {
								swiWaitForVBlank();
							}
						}
					}
				}

				if (ms().theme == 5) {
					fadeType = false;		  // Fade to black
					for (int i = 0; i < 60; i++) {
						swiWaitForVBlank();
					}
				}

				if (ms().dsiWareBooter || ms().consoleModel >= 2) {
					CheatCodelist codelist;
					u32 gameCode, crc32;

					bool cheatsEnabled = true;
					const char* cheatDataBin = (ms().secondaryDevice && ms().dsiWareToSD) ? "sd:/_nds/nds-bootstrap/cheatData.bin" : "/_nds/nds-bootstrap/cheatData.bin";
					mkdir((ms().secondaryDevice && ms().dsiWareToSD) ? "sd:/_nds" : "/_nds", 0777);
					mkdir((ms().secondaryDevice && ms().dsiWareToSD) ? "sd:/_nds/nds-bootstrap" : "/_nds/nds-bootstrap", 0777);
					if(codelist.romData(ms().dsiWareSrlPath,gameCode,crc32)) {
						long cheatOffset; size_t cheatSize;
						FILE* dat=fopen(sdFound() ? "sd:/_nds/TWiLightMenu/extras/usrcheat.dat" : "fat:/_nds/TWiLightMenu/extras/usrcheat.dat","rb");
						if (dat) {
							if (codelist.searchCheatData(dat, gameCode, crc32, cheatOffset, cheatSize)) {
								codelist.parse(ms().dsiWareSrlPath);
								codelist.writeCheatsToFile(cheatDataBin);
								FILE* cheatData=fopen(cheatDataBin,"rb");
								if (cheatData) {
									u32 check[2];
									fread(check, 1, 8, cheatData);
									fclose(cheatData);
									if (check[1] == 0xCF000000 || getFileSize(cheatDataBin) > 0x1C00) {
										cheatsEnabled = false;
									}
								}
							} else {
								cheatsEnabled = false;
							}
							fclose(dat);
						} else {
							cheatsEnabled = false;
						}
					} else {
						cheatsEnabled = false;
					}
					if (!cheatsEnabled) {
						remove(cheatDataBin);
					}
				}

				if ((ms().dsiWareBooter || (memcmp(io_dldi_data->friendlyName, "CycloDS iEvolution", 18) == 0) || ms().consoleModel >= 2) && !ms().homebrewBootstrap) {
					// Use nds-bootstrap
					loadPerGameSettings(filename);

					char sfnSrl[62];
					char sfnPub[62];
					char sfnPrv[62];
					if (ms().secondaryDevice && ms().dsiWareToSD) {
						fatGetAliasPath("sd:/", "sd:/_nds/TWiLightMenu/tempDSiWare.dsi", sfnSrl);
						fatGetAliasPath("sd:/", "sd:/_nds/TWiLightMenu/tempDSiWare.pub", sfnPub);
						fatGetAliasPath("sd:/", "sd:/_nds/TWiLightMenu/tempDSiWare.prv", sfnPrv);
					} else {
						fatGetAliasPath(ms().secondaryDevice ? "fat:/" : "sd:/", ms().dsiWareSrlPath.c_str(), sfnSrl);
						fatGetAliasPath(ms().secondaryDevice ? "fat:/" : "sd:/", ms().dsiWarePubPath.c_str(), sfnPub);
						fatGetAliasPath(ms().secondaryDevice ? "fat:/" : "sd:/", ms().dsiWarePrvPath.c_str(), sfnPrv);
					}

					bootstrapinipath = (memcmp(io_dldi_data->friendlyName, "CycloDS iEvolution", 18) == 0) ? "fat:/_nds/nds-bootstrap.ini" : "sd:/_nds/nds-bootstrap.ini";
					CIniFile bootstrapini(bootstrapinipath);
					bootstrapini.SetString("NDS-BOOTSTRAP", "NDS_PATH", ms().secondaryDevice && ms().dsiWareToSD ? "sd:/_nds/TWiLightMenu/tempDSiWare.dsi" : ms().dsiWareSrlPath);
					bootstrapini.SetString("NDS-BOOTSTRAP", "APP_PATH", sfnSrl);
					bootstrapini.SetString("NDS-BOOTSTRAP", "SAV_PATH", sfnPub);
					bootstrapini.SetString("NDS-BOOTSTRAP", "PRV_PATH", sfnPrv);
					bootstrapini.SetString("NDS-BOOTSTRAP", "AP_FIX_PATH", "");
					bootstrapini.SetString("NDS-BOOTSTRAP", "GUI_LANGUAGE", ms().getGuiLanguageString());
					bootstrapini.SetInt("NDS-BOOTSTRAP", "LANGUAGE", perGameSettings_language == -2 ? ms().gameLanguage : perGameSettings_language);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "REGION", perGameSettings_region == -3 ? ms().gameRegion : perGameSettings_region);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "DSI_MODE", true);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_CPU", true);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_VRAM", true);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "DONOR_SDK_VER", 5);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "GAME_SOFT_RESET", 1);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "PATCH_MPU_REGION", 0);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "PATCH_MPU_SIZE", 0);
					bootstrapini.SetInt("NDS-BOOTSTRAP", "FORCE_SLEEP_PATCH", 
						(ms().forceSleepPatch
					|| (memcmp(io_dldi_data->friendlyName, "TTCARD", 6) == 0 && !sys().isRegularDS())
					|| (memcmp(io_dldi_data->friendlyName, "DSTT", 4) == 0 && !sys().isRegularDS())
					|| (memcmp(io_dldi_data->friendlyName, "DEMON", 5) == 0 && !sys().isRegularDS())
					|| (memcmp(io_dldi_data->friendlyName, "R4iDSN", 6) == 0 && !sys().isRegularDS()))
					);

					bootstrapini.SaveIniFile(bootstrapinipath);

					if (ms().theme == 5) {
						fadeType = false;		  // Fade to black
						for (int i = 0; i < 60; i++) {
							swiWaitForVBlank();
						}
					}

					bool useNightly = (perGameSettings_bootstrapFile == -1 ? ms().bootstrapFile : perGameSettings_bootstrapFile);
					bool useWidescreen = (perGameSettings_wideScreen == -1 ? ms().wideScreen : perGameSettings_wideScreen);

					if (!isDSiMode() && (!ms().secondaryDevice || (ms().secondaryDevice && ms().dsiWareToSD))) {
						*(u32*)(0x02000000) |= BIT(3);
						*(u32*)(0x02000004) = 0;
						*(bool*)(0x02000010) = useNightly;
						*(bool*)(0x02000014) = useWidescreen;
					}
					if (dsiFeatures() || !ms().secondaryDevice) {
						SetWidescreen(filename.c_str());
					}
					if (!isDSiMode() && (!ms().secondaryDevice || (ms().secondaryDevice && ms().dsiWareToSD))) {
						ntrStartSdGame();
					}

					char ndsToBoot[256];
					sprintf(ndsToBoot, "sd:/_nds/nds-bootstrap-%s.nds", useNightly ? "nightly" : "release");
					if(access(ndsToBoot, F_OK) != 0) {
						sprintf(ndsToBoot, "fat:/_nds/nds-bootstrap-%s.nds", useNightly ? "nightly" : "release");
					}

					argarray.at(0) = (char *)ndsToBoot;
					snd().stopStream();
					int err = runNdsFile(argarray[0], argarray.size(), (const char **)&argarray[0], true, true, false, true, true, -1);
					char text[64];
					snprintf(text, sizeof(text), STR_START_FAILED_ERROR.c_str(), err);
					clearText();
					fadeType = true;
					printLarge(false, 4, 4, text);
					if (err == 1) {
						printLarge(false, 4, 20, useNightly ? STR_BOOTSTRAP_NIGHTLY_NOT_FOUND : STR_BOOTSTRAP_RELEASE_NOT_FOUND);
					}
					printSmall(false, 4, 20 + calcLargeFontHeight(useNightly ? STR_BOOTSTRAP_NIGHTLY_NOT_FOUND : STR_BOOTSTRAP_RELEASE_NOT_FOUND), STR_PRESS_B_RETURN);
					updateText(false);
					int pressed = 0;
					do {
						scanKeys();
						pressed = keysDownRepeat();
						checkSdEject();
						swiWaitForVBlank();
					} while (!(pressed & KEY_B));
					fadeType = false;	// Fade to white
					for (int i = 0; i < 25; i++) {
						swiWaitForVBlank();
					}
					if (!isDSiMode()) {
						chdir("fat:/");
					} else if (sdFound()) {
						chdir("sd:/");
					}
					runNdsFile("/_nds/TWiLightMenu/dsimenu.srldr", 0, NULL, true, false, false, true, true, -1);
					stop();
				}

				// Move .pub and/or .prv out of "saves" folder
				std::string pubnameUl = replaceAll(filename, typeToReplace, ".pub");
				std::string prvnameUl = replaceAll(filename, typeToReplace, ".prv");
				std::string pubpathUl = romFolderNoSlash + "/" + pubnameUl;
				std::string prvpathUl = romFolderNoSlash + "/" + prvnameUl;
				if (access(ms().dsiWarePubPath.c_str(), F_OK) == 0)
				{
					rename(ms().dsiWarePubPath.c_str(), pubpathUl.c_str());
				}
				if (access(ms().dsiWarePrvPath.c_str(), F_OK) == 0)
				{
					rename(ms().dsiWarePrvPath.c_str(), prvpathUl.c_str());
				}

				unlaunchRomBoot(ms().secondaryDevice ? "sdmc:/_nds/TWiLightMenu/tempDSiWare.dsi" : ms().dsiWareSrlPath);
			}

			// Launch .nds directly or via nds-bootstrap
			if (extention(filename, ".nds") || extention(filename, ".dsi")
			 || extention(filename, ".ids") || extention(filename, ".srl")
			 || extention(filename, ".app")) {
				const char *typeToReplace = ".nds";
				if (extention(filename, ".dsi")) {
					typeToReplace = ".dsi";
				} else if (extention(filename, ".ids")) {
					typeToReplace = ".ids";
				} else if (extention(filename, ".srl")) {
					typeToReplace = ".srl";
				} else if (extention(filename, ".app")) {
					typeToReplace = ".app";
				}

				bool dsModeSwitch = false;
				bool dsModeDSiWare = false;

				if (memcmp(gameTid[CURPOS], "HND", 3) == 0 || memcmp(gameTid[CURPOS], "HNE", 3) == 0) {
					dsModeSwitch = true;
					dsModeDSiWare = true;
					useBackend = false; // Bypass nds-bootstrap
					ms().homebrewBootstrap = true;
				} else if (isHomebrew[CURPOS]) {
					loadPerGameSettings(filename);
					if (perGameSettings_directBoot || (ms().useBootstrap && ms().secondaryDevice)) {
						useBackend = false; // Bypass nds-bootstrap
					} else {
						useBackend = true;
					}
					if (isDSiMode() && !perGameSettings_dsiMode) {
						dsModeSwitch = true;
					}
					ms().homebrewBootstrap = true;
				} else {
					loadPerGameSettings(filename);
					useBackend = true;
					ms().homebrewBootstrap = false;
				}

				char *name = argarray.at(0);
				strcpy(filePath + pathLen, name);
				free(argarray.at(0));
				argarray.at(0) = filePath;
				if (useBackend) {
					if ((ms().useBootstrap || !ms().secondaryDevice) || (dsiFeatures() && unitCode[CURPOS] > 0 && (perGameSettings_dsiMode == -1 ? ms().bstrap_dsiMode : perGameSettings_dsiMode))) {
						std::string path = argarray[0];
						std::string savename = replaceAll(filename, typeToReplace, getSavExtension());
						std::string ramdiskname = replaceAll(filename, typeToReplace, getImgExtension(perGameSettings_ramDiskNo));
						std::string romFolderNoSlash = ms().romfolder[ms().secondaryDevice];
						RemoveTrailingSlashes(romFolderNoSlash);
						mkdir(isHomebrew[CURPOS] ? "ramdisks" : "saves", 0777);
						std::string savepath = romFolderNoSlash + "/saves/" + savename;
						if (sdFound() && ms().secondaryDevice && ms().fcSaveOnSd) {
							savepath = replaceAll(savepath, "fat:/", "sd:/");
						}
						std::string ramdiskpath = romFolderNoSlash + "/ramdisks/" + ramdiskname;

						if (!isHomebrew[CURPOS] && (strncmp(gameTid[CURPOS], "NTR", 3) != 0))
						{ // Create or expand save if game isn't homebrew
							u32 orgsavesize = getFileSize(savepath.c_str());
							u32 savesize = 524288; // 512KB (default size for most games)

							u32 gameTidHex = 0;
							tonccpy(&gameTidHex, &gameTid[CURPOS], 4);

							for (int i = 0; i < (int)sizeof(ROMList)/12; i++) {
								ROMListEntry* curentry = &ROMList[i];
								if (gameTidHex == curentry->GameCode) {
									if (curentry->SaveMemType != 0xFFFFFFFF) savesize = sramlen[curentry->SaveMemType];
									break;
								}
							}

							bool saveSizeFixNeeded = false;

							// TODO: If the list gets large enough, switch to bsearch().
							for (unsigned int i = 0; i < sizeof(saveSizeFixList) / sizeof(saveSizeFixList[0]); i++) {
								if (memcmp(gameTid[CURPOS], saveSizeFixList[i], 3) == 0) {
									// Found a match.
									saveSizeFixNeeded = true;
									break;
								}
							}

							if ((orgsavesize == 0 && savesize > 0) || (orgsavesize < savesize && saveSizeFixNeeded)) {
								if (ms().theme == 5) displayGameIcons = false;
								if (isDSiMode() && memcmp(io_dldi_data->friendlyName, "CycloDS iEvolution", 18) == 0) {
									// Display nothing
								} else if (dsiFeatures() && ms().consoleModel >= 2) {
									printSmall(false, 0, 20, STR_TAKEWHILE_PRESSHOME, Alignment::center);
								} else {
									printSmall(false, 0, 20, STR_TAKEWHILE_CLOSELID, Alignment::center);
								}
								printLarge(false, 0, (ms().theme == 4 ? 80 : 88), (orgsavesize == 0) ? STR_CREATING_SAVE : STR_EXPANDING_SAVE, Alignment::center);
								updateText(false);

								if (ms().theme != 4 && ms().theme != 5) {
									fadeSpeed = true; // Fast fading
									fadeType = true; // Fade in from white
								}
								showProgressIcon = true;

								if (orgsavesize > 0) {
									fsizeincrease(savepath.c_str(), sdFound() ? "sd:/_nds/TWiLightMenu/temp.sav" : "fat:/_nds/TWiLightMenu/temp.sav", savesize);
								} else {
									FILE *pFile = fopen(savepath.c_str(), "wb");
									if (pFile) {
										fseek(pFile, savesize - 1, SEEK_SET);
										fputc('\0', pFile);
										fclose(pFile);
									}
								}
								showProgressIcon = false;
								clearText();
								printLarge(false, 0, (ms().theme == 4 ? 32 : 88), (orgsavesize == 0) ? STR_SAVE_CREATED : STR_SAVE_EXPANDED, Alignment::center);
								updateText(false);
								for (int i = 0; i < 30; i++) {
									swiWaitForVBlank();
								}
								if (ms().theme != 4 && ms().theme != 5) {
									fadeType = false;	   // Fade to white
									for (int i = 0; i < 25; i++) {
										swiWaitForVBlank();
									}
								}
								clearText();
								updateText(false);
								if (ms().theme == 5) displayGameIcons = true;
							}
						}

						SetMPUSettings();

						bool useWidescreen = (perGameSettings_wideScreen == -1 ? ms().wideScreen : perGameSettings_wideScreen);

						bootstrapinipath = ((!ms().secondaryDevice || sdFound()) ? "sd:/_nds/nds-bootstrap.ini" : "fat:/_nds/nds-bootstrap.ini");
						CIniFile bootstrapini(bootstrapinipath);
						bootstrapini.SetString("NDS-BOOTSTRAP", "NDS_PATH", path);
						bootstrapini.SetString("NDS-BOOTSTRAP", "SAV_PATH", savepath);
						bootstrapini.SetString("NDS-BOOTSTRAP", "HOMEBREW_ARG", (useWidescreen && (gameTid[CURPOS][0] == 'W' || romVersion[CURPOS] == 0x57)) ? "wide" : "");
						if (!isHomebrew[CURPOS]) {
							bootstrapini.SetString("NDS-BOOTSTRAP", "AP_FIX_PATH", setApFix(argarray[0]));
						}
						bootstrapini.SetString("NDS-BOOTSTRAP", "RAM_DRIVE_PATH", (perGameSettings_ramDiskNo >= 0 && !ms().secondaryDevice) ? ramdiskpath : "sd:/null.img");
						bootstrapini.SetString("NDS-BOOTSTRAP", "GUI_LANGUAGE", ms().getGuiLanguageString());
						bootstrapini.SetInt("NDS-BOOTSTRAP", "LANGUAGE", perGameSettings_language == -2 ? ms().gameLanguage : perGameSettings_language);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "REGION", perGameSettings_region == -3 ? ms().gameRegion : perGameSettings_region);
						if (dsModeForced || (memcmp(io_dldi_data->friendlyName, "CycloDS iEvolution", 18)==0 ? unitCode[CURPOS]==3 : (unitCode[CURPOS] > 0 && unitCode[CURPOS] < 3) && sys().arm7SCFGLocked())) {
							bootstrapini.SetInt("NDS-BOOTSTRAP", "DSI_MODE", 0);
						} else if (dsiFeatures() || !ms().secondaryDevice) {
							bootstrapini.SetInt("NDS-BOOTSTRAP", "DSI_MODE", perGameSettings_dsiMode == -1 ? ms().bstrap_dsiMode : perGameSettings_dsiMode);
						}
						if (dsiFeatures() || !ms().secondaryDevice) {
							bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_CPU", perGameSettings_boostCpu == -1 ? ms().boostCpu : perGameSettings_boostCpu);
							bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_VRAM", perGameSettings_boostVram == -1 ? ms().boostVram : perGameSettings_boostVram);
							bootstrapini.SetInt("NDS-BOOTSTRAP", "CARD_READ_DMA", setCardReadDMA());
							bootstrapini.SetInt("NDS-BOOTSTRAP", "ASYNC_CARD_READ", setAsyncReadDMA());
						}
						bootstrapini.SetInt("NDS-BOOTSTRAP", "EXTENDED_MEMORY", perGameSettings_expandRomSpace == -1 ? ms().extendedMemory : perGameSettings_expandRomSpace);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "DONOR_SDK_VER", SetDonorSDK());
						bootstrapini.SetInt("NDS-BOOTSTRAP", "PATCH_MPU_REGION", mpuregion);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "PATCH_MPU_SIZE", mpusize);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "FORCE_SLEEP_PATCH", 
							(ms().forceSleepPatch
						|| (memcmp(io_dldi_data->friendlyName, "TTCARD", 6) == 0 && !sys().isRegularDS())
						|| (memcmp(io_dldi_data->friendlyName, "DSTT", 4) == 0 && !sys().isRegularDS())
						|| (memcmp(io_dldi_data->friendlyName, "DEMON", 5) == 0 && !sys().isRegularDS())
						|| (memcmp(io_dldi_data->friendlyName, "R4iDSN", 6) == 0 && !sys().isRegularDS()))
						);
						if (!isDSiMode() && ms().secondaryDevice && sdFound()) {
							CIniFile bootstrapiniSD("sd:/_nds/nds-bootstrap.ini");
							bootstrapini.SetInt("NDS-BOOTSTRAP", "DEBUG", bootstrapiniSD.GetInt("NDS-BOOTSTRAP", "DEBUG", 0));
							bootstrapini.SetInt("NDS-BOOTSTRAP", "LOGGING", bootstrapiniSD.GetInt("NDS-BOOTSTRAP", "LOGGING", 0)); 
						}
						bootstrapini.SaveIniFile(bootstrapinipath);

						CheatCodelist codelist;
						u32 gameCode, crc32;

						if (!isHomebrew[CURPOS]) {
							bool cheatsEnabled = true;
							const char* cheatDataBin = "/_nds/nds-bootstrap/cheatData.bin";
							mkdir("/_nds", 0777);
							mkdir("/_nds/nds-bootstrap", 0777);
							if(codelist.romData(path,gameCode,crc32)) {
								long cheatOffset; size_t cheatSize;
								FILE* dat=fopen(sdFound() ? "sd:/_nds/TWiLightMenu/extras/usrcheat.dat" : "fat:/_nds/TWiLightMenu/extras/usrcheat.dat","rb");
								if (dat) {
									if (codelist.searchCheatData(dat, gameCode, crc32, cheatOffset, cheatSize)) {
										codelist.parse(path);
										codelist.writeCheatsToFile(cheatDataBin);
										FILE* cheatData=fopen(cheatDataBin,"rb");
										if (cheatData) {
											u32 check[2];
											fread(check, 1, 8, cheatData);
											fclose(cheatData);
											if (check[1] == 0xCF000000 || getFileSize(cheatDataBin) > 0x8000) {
												cheatsEnabled = false;
											}
										}
									} else {
										cheatsEnabled = false;
									}
									fclose(dat);
								} else {
									cheatsEnabled = false;
								}
							} else {
								cheatsEnabled = false;
							}
							if (!cheatsEnabled) {
								remove(cheatDataBin);
							}
						}

						if (!isArgv) {
							ms().romPath[ms().secondaryDevice] = std::string(argarray[0]);
						}
						ms().homebrewHasWide = (isHomebrew[CURPOS] && gameTid[CURPOS][0] == 'W');
						ms().launchType[ms().secondaryDevice] = Launch::ESDFlashcardLaunch; // 1
						ms().previousUsedDevice = ms().secondaryDevice;
						ms().saveSettings();

						if (ms().theme == 5) {
							fadeType = false;		  // Fade to black
							for (int i = 0; i < 60; i++) {
								swiWaitForVBlank();
							}
						}

						if (dsiFeatures() || !ms().secondaryDevice) {
							SetWidescreen(filename.c_str());
						}
						if (!isDSiMode() && !ms().secondaryDevice) {
							ntrStartSdGame();
						}

						bool useNightly = (perGameSettings_bootstrapFile == -1 ? ms().bootstrapFile : perGameSettings_bootstrapFile);

						char ndsToBoot[256];
						sprintf(ndsToBoot, "sd:/_nds/nds-bootstrap-%s%s.nds", ms().homebrewBootstrap ? "hb-" : "", useNightly ? "nightly" : "release");
						if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
							sprintf(ndsToBoot, "fat:/_nds/nds-bootstrap-%s%s.nds", ms().homebrewBootstrap ? "hb-" : "", useNightly ? "nightly" : "release");
						}

						argarray.at(0) = (char *)ndsToBoot;
						snd().stopStream();
						int err = runNdsFile(argarray[0], argarray.size(), (const char **)&argarray[0], (ms().homebrewBootstrap ? false : true), true, false, true, true, -1);
						char text[64];
						snprintf(text, sizeof(text), STR_START_FAILED_ERROR.c_str(), err);
						clearText();
						printLarge(false, 4, 4, text);
						if (err == 1) {
							if(ms().homebrewBootstrap == true) {
								printLarge(false, 4, 20, useNightly ? STR_BOOTSTRAP_HB_NIGHTLY_NOT_FOUND : STR_BOOTSTRAP_HB_RELEASE_NOT_FOUND);
							} else {
								printLarge(false, 4, 20, useNightly ? STR_BOOTSTRAP_NIGHTLY_NOT_FOUND : STR_BOOTSTRAP_RELEASE_NOT_FOUND);
							}
						}
						if(ms().homebrewBootstrap == true) {
							printSmall(false, 4, 20 + calcLargeFontHeight(useNightly ? STR_BOOTSTRAP_HB_NIGHTLY_NOT_FOUND : STR_BOOTSTRAP_HB_RELEASE_NOT_FOUND), STR_PRESS_B_RETURN);
						} else {
							printSmall(false, 4, 20 + calcLargeFontHeight(useNightly ? STR_BOOTSTRAP_NIGHTLY_NOT_FOUND : STR_BOOTSTRAP_RELEASE_NOT_FOUND), STR_PRESS_B_RETURN);
						}
						updateText(false);
						fadeSpeed = true;
						fadeType = true;
						int pressed = 0;
						do {
							scanKeys();
							pressed = keysDownRepeat();
							checkSdEject();
							swiWaitForVBlank();
						} while (!(pressed & KEY_B));
						fadeType = false;	// Fade to white
						for (int i = 0; i < 25; i++) {
							swiWaitForVBlank();
						}
						if (!isDSiMode()) {
							chdir("fat:/");
						} else if (sdFound()) {
							chdir("sd:/");
						}
						runNdsFile("/_nds/TWiLightMenu/dsimenu.srldr", 0, NULL, true, false, false, true, true, -1);
						stop();
					} else {
						ms().romPath[ms().secondaryDevice] = std::string(argarray[0]);
						ms().launchType[ms().secondaryDevice] = Launch::ESDFlashcardLaunch;
						ms().previousUsedDevice = ms().secondaryDevice;
						ms().saveSettings();

						if (ms().theme == 5) {
							fadeType = false;		  // Fade to black
							for (int i = 0; i < 60; i++) {
								swiWaitForVBlank();
							}
						}

						loadGameOnFlashcard(argarray[0], true);
					}
				} else {
					if (!isArgv) {
						ms().romPath[ms().secondaryDevice] = std::string(argarray[0]);
					}
					ms().homebrewHasWide = (isHomebrew[CURPOS] && (gameTid[CURPOS][0] == 'W' || romVersion[CURPOS] == 0x57));
					ms().launchType[ms().secondaryDevice] = Launch::ESDFlashcardDirectLaunch;
					ms().previousUsedDevice = ms().secondaryDevice;
					if (isDSiMode() || !ms().secondaryDevice) {
						SetWidescreen(filename.c_str());
					}
					ms().saveSettings();

					if (ms().theme == 5) {
						fadeType = false;		  // Fade to black
						for (int i = 0; i < 60; i++) {
							swiWaitForVBlank();
						}
					}
					snd().stopStream();

					if (!isDSiMode() && !ms().secondaryDevice) {
						ntrStartSdGame();
					}

					int language = perGameSettings_language == -2 ? ms().gameLanguage : perGameSettings_language;
					int gameRegion = perGameSettings_region == -3 ? ms().gameRegion : perGameSettings_region;

					// Set region flag
					if (gameRegion == -2 && gameTid[CURPOS][3] != 'A' && gameTid[CURPOS][3] != '#') {
						if (gameTid[CURPOS][3] == 'J') {
							*(u8*)(0x02FFFD70) = 0;
						} else if (gameTid[CURPOS][3] == 'E' || gameTid[CURPOS][3] == 'T') {
							*(u8*)(0x02FFFD70) = 1;
						} else if (gameTid[CURPOS][3] == 'P' || gameTid[CURPOS][3] == 'V') {
							*(u8*)(0x02FFFD70) = 2;
						} else if (gameTid[CURPOS][3] == 'U') {
							*(u8*)(0x02FFFD70) = 3;
						} else if (gameTid[CURPOS][3] == 'C') {
							*(u8*)(0x02FFFD70) = 4;
						} else if (gameTid[CURPOS][3] == 'K') {
							*(u8*)(0x02FFFD70) = 5;
						}
					} else if (gameRegion == -1 || (gameRegion == -2 && (gameTid[CURPOS][3] == 'A' || gameTid[CURPOS][3] == '#'))) {
					  if (useTwlCfg) {
						u8 country = *(u8*)0x02000405;
						if (country == 0x01) {
							*(u8*)(0x02FFFD70) = 0;	// Japan
						} else if (country == 0xA0) {
							*(u8*)(0x02FFFD70) = 4;	// China
						} else if (country == 0x88) {
							*(u8*)(0x02FFFD70) = 5;	// Korea
						} else if (country == 0x41 || country == 0x5F) {
							*(u8*)(0x02FFFD70) = 3;	// Australia
						} else if ((country >= 0x08 && country <= 0x34) || country == 0x99 || country == 0xA8) {
							*(u8*)(0x02FFFD70) = 1;	// USA
						} else if (country >= 0x40 && country <= 0x70) {
							*(u8*)(0x02FFFD70) = 2;	// Europe
						}
					  } else {
						u8 consoleType = 0;
						readFirmware(0x1D, &consoleType, 1);
						if (consoleType == 0x43 || consoleType == 0x63) {
							*(u8*)(0x02FFFD70) = 4;	// China
						} else if (PersonalData->language == 0) {
							*(u8*)(0x02FFFD70) = 0;	// Japan
						} else if (PersonalData->language == 1 /*|| PersonalData->language == 2 || PersonalData->language == 5*/) {
							*(u8*)(0x02FFFD70) = 1;	// USA
						} else /*if (PersonalData->language == 3 || PersonalData->language == 4)*/ {
							*(u8*)(0x02FFFD70) = 2;	// Europe
						} /*else {
							*(u8*)(0x02FFFD70) = 5;	// Korea
						}*/
					  }
					} else {
						*(u8*)(0x02FFFD70) = gameRegion;
					}

					if (useTwlCfg && language >= 0 && language <= 7 && *(u8*)0x02000406 != language) {
						tonccpy((char*)0x02000600, (char*)0x02000400, 0x200);
						*(u8*)0x02000606 = language;
						*(u32*)0x02FFFDFC = 0x02000600;
					}

					bool useWidescreen = (perGameSettings_wideScreen == -1 ? ms().wideScreen : perGameSettings_wideScreen);

					if (ms().consoleModel >= 2 && useWidescreen && ms().homebrewHasWide) {
						//argarray.push_back((char*)"wide");
						SetWidescreen(NULL);
					}

					bool runNds_boostCpu = false;
					bool runNds_boostVram = false;
					if (dsiFeatures() && !dsModeDSiWare) {
						runNds_boostCpu = perGameSettings_boostCpu == -1 ? ms().boostCpu : perGameSettings_boostCpu;
						runNds_boostVram = perGameSettings_boostVram == -1 ? ms().boostVram : perGameSettings_boostVram;
					}
					int err = runNdsFile(argarray[0], argarray.size(), (const char **)&argarray[0], true, true, dsModeSwitch, runNds_boostCpu, runNds_boostVram, language);
					char text[64];
					snprintf(text, sizeof(text), STR_START_FAILED_ERROR.c_str(), err);
					printLarge(false, 4, 4, text);
					printSmall(false, 4, 20, STR_PRESS_B_RETURN);
					updateText(false);
					fadeSpeed = true;
					fadeType = true;
					int pressed = 0;
					do {
						scanKeys();
						pressed = keysDownRepeat();
						checkSdEject();
						swiWaitForVBlank();
					} while (!(pressed & KEY_B));
					fadeType = false;	// Fade to white
					for (int i = 0; i < 25; i++) {
						swiWaitForVBlank();
					}
					if (!isDSiMode()) {
						chdir("fat:/");
					} else if (sdFound()) {
						chdir("sd:/");
					}
					runNdsFile("/_nds/TWiLightMenu/dsimenu.srldr", 0, NULL, true, false, false, true, true, -1);
					stop();
				}
			} else {
				bool useNDSB = false;
				bool dsModeSwitch = false;
				bool boostCpu = true;
				bool boostVram = false;

				std::string romfolderNoSlash = ms().romfolder[ms().secondaryDevice];
				RemoveTrailingSlashes(romfolderNoSlash);
				char ROMpath[256];
				snprintf (ROMpath, sizeof(ROMpath), "%s/%s", romfolderNoSlash.c_str(), filename.c_str());
				ms().romPath[ms().secondaryDevice] = std::string(ROMpath);
				ms().previousUsedDevice = ms().secondaryDevice;
				ms().homebrewBootstrap = true;

				const char *ndsToBoot = "sd:/_nds/nds-bootstrap-release.nds";
				if (extention(filename, ".plg")) {
					ndsToBoot = "fat:/_nds/TWiLightMenu/bootplg.srldr";
					dsModeSwitch = true;

					// Print .plg path without "fat:" at the beginning
					char ROMpathDS2[256];
					if (ms().secondaryDevice) {
						for (int i = 0; i < 252; i++) {
							ROMpathDS2[i] = ROMpath[4+i];
							if (ROMpath[4+i] == '\x00') break;
						}
					} else {
						sprintf(ROMpathDS2, "/_nds/TWiLightMenu/tempPlugin.plg");
						fcopy(ROMpath, "fat:/_nds/TWiLightMenu/tempPlugin.plg");
					}

					CIniFile dstwobootini( "fat:/_dstwo/twlm.ini" );
					dstwobootini.SetString("boot_settings", "file", ROMpathDS2);
					dstwobootini.SaveIniFile( "fat:/_dstwo/twlm.ini" );
				} else if (extention(filename, ".rvid")) {
					ms().launchType[ms().secondaryDevice] = Launch::ERVideoLaunch;

					ndsToBoot = "sd:/_nds/TWiLightMenu/apps/RocketVideoPlayer.nds";
					if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
						ndsToBoot = "fat:/_nds/TWiLightMenu/apps/RocketVideoPlayer.nds";
						boostVram = true;
					}
				} else if (extention(filename, ".fv")) {
					ms().launchType[ms().secondaryDevice] = Launch::EMPEG4Launch;

					ndsToBoot = "sd:/_nds/TWiLightMenu/apps/FastVideoDS.nds";
					if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
						ndsToBoot = "fat:/_nds/TWiLightMenu/apps/FastVideoDS.nds";
						boostVram = true;
					}
				} else if (extention(filename, ".agb")
						|| extention(filename, ".gba")
						|| extention(filename, ".mb")) {
					ms().launchType[ms().secondaryDevice] = (ms().showGba == 1) ? Launch::EGBANativeLaunch : Launch::ESDFlashcardLaunch;

					if (ms().showGba == 1) {
						fontReinit();	// Re-load font into main memory

						if (ms().theme == 5) displayGameIcons = false;
						if (*(u16*)(0x020000C0) == 0x5A45) {
							printLarge(false, 0, (ms().theme == 4 ? 80 : 88), STR_PLEASE_WAIT, Alignment::center);
							updateText(false);
						}

						if (ms().theme != 4 && ms().theme != 5) {
							fadeSpeed = true; // Fast fading
							fadeType = true; // Fade in from white
						}
						showProgressIcon = true;
						showProgressBar = true;
						progressBarLength = 0;

						u32 ptr = 0x08000000;
						u32 romSize = getFileSize(filename.c_str());
						char titleID[4];
						FILE* gbaFile = fopen(filename.c_str(), "rb");
						fseek(gbaFile, 0xAC, SEEK_SET);
						fread(&titleID, 1, 4, gbaFile);
						if (strncmp(titleID, "AGBJ", 4) == 0 && romSize <= 0x40000) {
							ptr += 0x400;
						}
						u32 curPtr = ptr;
						fseek(gbaFile, 0, SEEK_SET);

						extern char copyBuf[0x8000];
						if (romSize > 0x2000000) romSize = 0x2000000;

						bool nor = false;
						if (*(u16*)(0x020000C0) == 0x5A45 && strncmp(titleID, "AGBJ", 4) != 0) {
							cExpansion::SetRompage(0);
							expansion().SetRampage(cExpansion::ENorPage);
							cExpansion::OpenNorWrite();
							cExpansion::SetSerialMode();
							for(u32 address=0;address<romSize&&address<0x2000000;address+=0x40000)
							{
								expansion().Block_Erase(address);
								progressBarLength = (address+0x40000)/(romSize/192);
								if (progressBarLength > 192) progressBarLength = 192;
							}
							nor = true;
						} else if (*(u16*)(0x020000C0) == 0x4353 && romSize > 0x1FFFFFE) {
							romSize = 0x1FFFFFE;
						}

						clearText();
						printSmall(false, 0, 20, STR_BARSTOPPED_CLOSELID, Alignment::center);
						printLarge(false, 0, (ms().theme == 4 ? 80 : 88), STR_NOW_LOADING, Alignment::center);
						updateText(false);

						for (u32 len = romSize; len > 0; len -= 0x8000) {
							if (fread(&copyBuf, 1, (len>0x8000 ? 0x8000 : len), gbaFile) > 0) {
								s2RamAccess(true);
								if (nor) {
									expansion().WriteNorFlash(curPtr-ptr, (u8*)copyBuf, (len>0x8000 ? 0x8000 : len));
								} else {
									tonccpy((u16*)curPtr, &copyBuf, (len>0x8000 ? 0x8000 : len));
								}
								s2RamAccess(false);
								curPtr += 0x8000;
								progressBarLength = ((curPtr-ptr)+0x8000)/(romSize/192);
								if (progressBarLength > 192) progressBarLength = 192;
							} else {
								break;
							}
						}
						fclose(gbaFile);

						ptr = 0x0A000000;

						std::string savename = replaceAll(filename, ".gba", ".sav");
						u32 savesize = getFileSize(savename.c_str());
						if (savesize > 0x10000) savesize = 0x10000;

						if (savesize > 0) {
							FILE* savFile = fopen(savename.c_str(), "rb");
							for (u32 len = savesize; len > 0; len -= 0x8000) {
								if (fread(&copyBuf, 1, (len>0x8000 ? 0x8000 : len), savFile) > 0) {
									gbaSramAccess(true);	// Switch to GBA SRAM
									cExpansion::WriteSram(ptr,(u8*)copyBuf,0x8000);
									gbaSramAccess(false);	// Switch out of GBA SRAM
									ptr += 0x8000;
								} else {
									break;
								}
							}
							fclose(savFile);
						}

						showProgressIcon = false;
						if (ms().theme != 4 && ms().theme != 5) {
							fadeType = false;	   // Fade to white
							for (int i = 0; i < 25; i++) {
								swiWaitForVBlank();
							}
						}
						clearText();
						updateText(false);
						if (ms().theme == 5) displayGameIcons = true;

						ndsToBoot = "fat:/_nds/TWiLightMenu/gbapatcher.srldr";
					} else if (ms().secondaryDevice) {
						ndsToBoot = ms().gbar2DldiAccess ? "sd:/_nds/GBARunner2_arm7dldi_ds.nds" : "sd:/_nds/GBARunner2_arm9dldi_ds.nds";
						if (dsiFeatures()) {
							ndsToBoot = ms().consoleModel > 0 ? "sd:/_nds/GBARunner2_arm7dldi_3ds.nds" : "sd:/_nds/GBARunner2_arm7dldi_dsi.nds";
						}
						if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
							ndsToBoot = ms().gbar2DldiAccess ? "fat:/_nds/GBARunner2_arm7dldi_ds.nds" : "fat:/_nds/GBARunner2_arm9dldi_ds.nds";
							if (dsiFeatures()) {
								ndsToBoot = ms().consoleModel > 0 ? "fat:/_nds/GBARunner2_arm7dldi_3ds.nds" : "fat:/_nds/GBARunner2_arm7dldi_dsi.nds";
							}
						}
						boostVram = false;
					} else {
						useNDSB = true;

						const char* gbar2Path = ms().consoleModel>0 ? "sd:/_nds/GBARunner2_arm7dldi_3ds.nds" : "sd:/_nds/GBARunner2_arm7dldi_dsi.nds";
						if (isDSiMode() && sys().arm7SCFGLocked()) {
							gbar2Path = ms().consoleModel > 0 ? "sd:/_nds/GBARunner2_arm7dldi_nodsp_3ds.nds" : "sd:/_nds/GBARunner2_arm7dldi_nodsp_dsi.nds";
						}

						ndsToBoot = (ms().bootstrapFile ? "sd:/_nds/nds-bootstrap-hb-nightly.nds" : "sd:/_nds/nds-bootstrap-hb-release.nds");
						CIniFile bootstrapini("sd:/_nds/nds-bootstrap.ini");

						bootstrapini.SetString("NDS-BOOTSTRAP", "GUI_LANGUAGE", ms().getGuiLanguageString());
						bootstrapini.SetInt("NDS-BOOTSTRAP", "LANGUAGE", ms().gameLanguage);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "DSI_MODE", 0);
						bootstrapini.SetString("NDS-BOOTSTRAP", "NDS_PATH", gbar2Path);
						bootstrapini.SetString("NDS-BOOTSTRAP", "HOMEBREW_ARG", ROMpath);
						bootstrapini.SetString("NDS-BOOTSTRAP", "RAM_DRIVE_PATH", "");
						bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_CPU", 1);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_VRAM", 0);

						bootstrapini.SaveIniFile("sd:/_nds/nds-bootstrap.ini");
					}
				} else if (extention(filename, ".xex")
						 || extention(filename, ".atr")) {
					ms().launchType[ms().secondaryDevice] = Launch::EXEGSDSLaunch;

					ndsToBoot = "sd:/_nds/TWiLightMenu/emulators/XEGS-DS.nds";
					if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
						ndsToBoot = "fat:/_nds/TWiLightMenu/emulators/XEGS-DS.nds";
						boostVram = true;
					}
				} else if (extention(filename, ".a26")) {
					ms().launchType[ms().secondaryDevice] = Launch::EStellaDSLaunch;

					ndsToBoot = "sd:/_nds/TWiLightMenu/emulators/StellaDS.nds";
					if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
						ndsToBoot = "fat:/_nds/TWiLightMenu/emulators/StellaDS.nds";
						boostVram = true;
					}
				} else if (extention(filename, ".a52")) {
					ms().launchType[ms().secondaryDevice] = Launch::EA5200DSLaunch;

					ndsToBoot = "sd:/_nds/TWiLightMenu/emulators/A5200DS.nds";
					if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
						ndsToBoot = "fat:/_nds/TWiLightMenu/emulators/A5200DS.nds";
						boostVram = true;
					}
				} else if (extention(filename, ".a78")) {
					ms().launchType[ms().secondaryDevice] = Launch::EA7800DSLaunch;

					ndsToBoot = "sd:/_nds/TWiLightMenu/emulators/A7800DS.nds";
					if(!dsiFeatures()) {
						ndsToBoot = "fat:/_nds/TWiLightMenu/emulators/A7800DS-LITE.nds";
					}
					if((!isDSiMode() && dsiFeatures()) || access(ndsToBoot, F_OK) != 0) {
						ndsToBoot = "fat:/_nds/TWiLightMenu/emulators/A7800DS.nds";
						boostVram = true;
					}
				} else if (extention(filename, ".gb") || extention(filename, ".sgb") || extention(filename, ".gbc")) {
					ms().launchType[ms().secondaryDevice] = Launch::EGameYobLaunch;

					ndsToBoot = "sd:/_nds/TWiLightMenu/emulators/gameyob.nds";
					if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
						ndsToBoot = "fat:/_nds/TWiLightMenu/emulators/gameyob.nds";
						dsModeSwitch = !isDSiMode();
						boostVram = true;
					}
				} else if (extention(filename, ".nes") || extention(filename, ".fds")) {
					ms().launchType[ms().secondaryDevice] = Launch::ENESDSLaunch;

					ndsToBoot = (ms().secondaryDevice ? "sd:/_nds/TWiLightMenu/emulators/nesds.nds" : "sd:/_nds/TWiLightMenu/emulators/nestwl.nds");
					if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
						ndsToBoot = "fat:/_nds/TWiLightMenu/emulators/nesds.nds";
						boostVram = true;
					}
				} else if (extention(filename, ".sms") || extention(filename, ".gg")) {
					mkdir(ms().secondaryDevice ? "fat:/data" : "sd:/data", 0777);
					mkdir(ms().secondaryDevice ? "fat:/data/s8ds" : "sd:/data/s8ds", 0777);

					if (!ms().secondaryDevice && !sys().arm7SCFGLocked() && ms().smsGgInRam) {
						ms().launchType[ms().secondaryDevice] = Launch::ESDFlashcardLaunch;

						useNDSB = true;

						ndsToBoot = (ms().bootstrapFile ? "sd:/_nds/nds-bootstrap-hb-nightly.nds" : "sd:/_nds/nds-bootstrap-hb-release.nds");
						CIniFile bootstrapini("sd:/_nds/nds-bootstrap.ini");

						bootstrapini.SetString("NDS-BOOTSTRAP", "GUI_LANGUAGE", ms().getGuiLanguageString());
						bootstrapini.SetInt("NDS-BOOTSTRAP", "LANGUAGE", ms().gameLanguage);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "DSI_MODE", 0);
						bootstrapini.SetString("NDS-BOOTSTRAP", "NDS_PATH", "sd:/_nds/TWiLightMenu/emulators/S8DS07.nds");
						bootstrapini.SetString("NDS-BOOTSTRAP", "HOMEBREW_ARG", "");
						bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_CPU", 1);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_VRAM", 0);

						bootstrapini.SetString("NDS-BOOTSTRAP", "RAM_DRIVE_PATH", ROMpath);
						bootstrapini.SaveIniFile("sd:/_nds/nds-bootstrap.ini");
					} else {
						ms().launchType[ms().secondaryDevice] = Launch::ES8DSLaunch;

						ndsToBoot = "sd:/_nds/TWiLightMenu/emulators/S8DS.nds";
						if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
							ndsToBoot = "fat:/_nds/TWiLightMenu/emulators/S8DS.nds";
							boostVram = true;
						}
					}
				} else if (extention(filename, ".gen")) {
					bool usePicoDrive = ((isDSiMode() && sdFound() && sys().arm7SCFGLocked())
						|| ms().showMd==2 || (ms().showMd==3 && getFileSize(filename.c_str()) > 0x300000));
					ms().launchType[ms().secondaryDevice] = (usePicoDrive ? Launch::EPicoDriveTWLLaunch : Launch::ESDFlashcardLaunch);

					if (usePicoDrive || ms().secondaryDevice) {
						ndsToBoot = usePicoDrive ? "sd:/_nds/TWiLightMenu/emulators/PicoDriveTWL.nds" : "sd:/_nds/TWiLightMenu/emulators/jEnesisDS.nds";
						if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
							ndsToBoot = usePicoDrive ? "fat:/_nds/TWiLightMenu/emulators/PicoDriveTWL.nds" : "fat:/_nds/TWiLightMenu/emulators/jEnesisDS.nds";
							boostVram = true;
						}
						dsModeSwitch = !usePicoDrive;
					} else {
						useNDSB = true;

						ndsToBoot = (ms().bootstrapFile ? "sd:/_nds/nds-bootstrap-hb-nightly.nds" : "sd:/_nds/nds-bootstrap-hb-release.nds");
						CIniFile bootstrapini("sd:/_nds/nds-bootstrap.ini");

						bootstrapini.SetString("NDS-BOOTSTRAP", "GUI_LANGUAGE", ms().getGuiLanguageString());
						bootstrapini.SetInt("NDS-BOOTSTRAP", "LANGUAGE", ms().gameLanguage);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "DSI_MODE", 0);
						bootstrapini.SetString("NDS-BOOTSTRAP", "NDS_PATH", "sd:/_nds/TWiLightMenu/emulators/jEnesisDS.nds");
						bootstrapini.SetString("NDS-BOOTSTRAP", "HOMEBREW_ARG", "fat:/ROM.BIN");
						bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_CPU", 1);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_VRAM", 0);

						bootstrapini.SetString("NDS-BOOTSTRAP", "RAM_DRIVE_PATH", ROMpath);
						bootstrapini.SaveIniFile("sd:/_nds/nds-bootstrap.ini");
					}
				} else if (extention(filename, ".smc") || extention(filename, ".sfc")) {
					ms().launchType[ms().secondaryDevice] = Launch::ESDFlashcardLaunch;

					if (ms().secondaryDevice) {
						ndsToBoot = "sd:/_nds/TWiLightMenu/emulators/SNEmulDS.nds";
						if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
							ndsToBoot = "fat:/_nds/TWiLightMenu/emulators/SNEmulDS.nds";
							boostCpu = false;
							boostVram = true;
						}
						dsModeSwitch = true;
					} else {
						useNDSB = true;

						ndsToBoot = (ms().bootstrapFile ? "sd:/_nds/nds-bootstrap-hb-nightly.nds" : "sd:/_nds/nds-bootstrap-hb-release.nds");
						CIniFile bootstrapini("sd:/_nds/nds-bootstrap.ini");

						bootstrapini.SetString("NDS-BOOTSTRAP", "GUI_LANGUAGE", ms().getGuiLanguageString());
						bootstrapini.SetInt("NDS-BOOTSTRAP", "LANGUAGE", ms().gameLanguage);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "DSI_MODE", 0);
						bootstrapini.SetString("NDS-BOOTSTRAP", "NDS_PATH", "sd:/_nds/TWiLightMenu/emulators/SNEmulDS.nds");
						bootstrapini.SetString("NDS-BOOTSTRAP", "HOMEBREW_ARG", "fat:/snes/ROM.SMC");
						bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_CPU", 0);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_VRAM", 0);

						bootstrapini.SetString("NDS-BOOTSTRAP", "RAM_DRIVE_PATH", ROMpath);
						bootstrapini.SaveIniFile("sd:/_nds/nds-bootstrap.ini");
					}
				} else if (extention(filename, ".pce")) {
					mkdir(ms().secondaryDevice ? "fat:/data" : "sd:/data", 0777);
					mkdir(ms().secondaryDevice ? "fat:/data/NitroGrafx" : "sd:/data/NitroGrafx", 0777);

					if (!ms().secondaryDevice && !sys().arm7SCFGLocked() && ms().smsGgInRam) {
						ms().launchType[ms().secondaryDevice] = Launch::ESDFlashcardLaunch;

						useNDSB = true;

						ndsToBoot = (ms().bootstrapFile ? "sd:/_nds/nds-bootstrap-hb-nightly.nds" : "sd:/_nds/nds-bootstrap-hb-release.nds");
						CIniFile bootstrapini("sd:/_nds/nds-bootstrap.ini");

						bootstrapini.SetString("NDS-BOOTSTRAP", "GUI_LANGUAGE", ms().getGuiLanguageString());
						bootstrapini.SetInt("NDS-BOOTSTRAP", "LANGUAGE", ms().gameLanguage);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "DSI_MODE", 0);
						bootstrapini.SetString("NDS-BOOTSTRAP", "NDS_PATH", "sd:/_nds/TWiLightMenu/emulators/NitroGrafx.nds");
						bootstrapini.SetString("NDS-BOOTSTRAP", "HOMEBREW_ARG", ROMpath);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_CPU", 1);
						bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_VRAM", 0);

						bootstrapini.SetString("NDS-BOOTSTRAP", "RAM_DRIVE_PATH", "");
						bootstrapini.SaveIniFile("sd:/_nds/nds-bootstrap.ini");
					} else {
						ms().launchType[ms().secondaryDevice] = Launch::ENitroGrafxLaunch;

						ndsToBoot = "sd:/_nds/TWiLightMenu/emulators/NitroGrafx.nds";
						if(!isDSiMode() || access(ndsToBoot, F_OK) != 0) {
							ndsToBoot = "fat:/_nds/TWiLightMenu/emulators/NitroGrafx.nds";
							boostVram = true;
						}
					}
				}

				ms().homebrewArg = useNDSB ? "" : ms().romPath[ms().secondaryDevice];
				ms().saveSettings();
				if (!isDSiMode() && !ms().secondaryDevice && !extention(filename, ".plg")) {
					ntrStartSdGame();
				}

				argarray.push_back(ROMpath);
				argarray.at(0) = (char *)ndsToBoot;
				snd().stopStream();

				int err = runNdsFile (argarray[0], argarray.size(), (const char **)&argarray[0], !useNDSB, true, dsModeSwitch, boostCpu, boostVram, -1);	// Pass ROM to emulator as argument

				char text[64];
				snprintf(text, sizeof(text), STR_START_FAILED_ERROR.c_str(), err);
				printLarge(false, 4, 4, text);
				if (err == 1 && useNDSB) {
					printLarge(false, 4, 20, ms().bootstrapFile ? STR_BOOTSTRAP_HB_NIGHTLY_NOT_FOUND : STR_BOOTSTRAP_HB_RELEASE_NOT_FOUND);
				}
				printSmall(false, 4, 20 + calcLargeFontHeight(ms().bootstrapFile ? STR_BOOTSTRAP_HB_NIGHTLY_NOT_FOUND : STR_BOOTSTRAP_HB_RELEASE_NOT_FOUND), STR_PRESS_B_RETURN);
				updateText(false);
				fadeSpeed = true;
				fadeType = true;
				int pressed = 0;
				do {
					scanKeys();
					pressed = keysDownRepeat();
					checkSdEject();
					swiWaitForVBlank();
				} while (!(pressed & KEY_B));
				fadeType = false;	// Fade to white
				for (int i = 0; i < 25; i++) {
					swiWaitForVBlank();
				}
				if (!isDSiMode()) {
					chdir("fat:/");
				} else if (sdFound()) {
					chdir("sd:/");
				}
				runNdsFile("/_nds/TWiLightMenu/dsimenu.srldr", 0, NULL, true, false, false, true, true, -1);
				stop();
			}

			while (argarray.size() != 0) {
				free(argarray.at(0));
				argarray.erase(argarray.begin());
			}
		}
	}

	return 0;
}
