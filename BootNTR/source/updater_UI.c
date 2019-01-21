#include "main.h"
#include "clock.h"
#include "sound.h"

sprite_t *changelogTextsprite = NULL;
sprite_t *updaterControlsText = NULL;
sprite_t *progBarSprite = NULL;
sprite_t *progBarFillSprite = NULL;
progressbar_t *updateProgBar = NULL;

extern char* versionList[50];
extern char g_modversion[15];
extern char* changelogList[50][30];

extern appInfoObject_t         *appTop;
extern sprite_t         *topInfoSprite;
extern sprite_t		 *topInfoSpriteUpd;

extern bool restartneeded;
extern bool forceUpdate;

extern cwav_t                  *sfx_sound;
extern cwav_t                  *lag_sound;

void    InitUpdatesUI(void)
{
	removeAppTop(false);
	
	if (!changelogTextsprite)
		newSpriteFromPNG(&changelogTextsprite, "romfs:/sprites/textSprites/changelogText.png");

	setSpritePos(changelogTextsprite, 156.0f, 0.0f);

	if (!updaterControlsText)
		newSpriteFromPNG(&updaterControlsText, "romfs:/sprites/textSprites/updateControlsText.png");

	newSpriteFromPNG(&progBarSprite, "romfs:/sprites/barBorder.png");
	setSpritePos(progBarSprite, 89.0f, 188.0f);
	newSpriteFromPNG(&progBarFillSprite, "romfs:/sprites/barFill.png");

	setSpritePos(updaterControlsText, 0.0f, 220.0f);

	updateProgBar = newProgressBar(progBarSprite, progBarFillSprite, 95.0f, 194.0f, 210.0f, 36.0f);
	addTopObject(updateProgBar);
	updateProgBar->isHidden = true;	
}

void setControlsMode(int mode) {
	switch (mode) {
	case 0:
	default:
		updaterControlsText->posX = 0.0f;
		updaterControlsText->amount = 1.f;
		break;
	case 1:
		updaterControlsText->posX = 55.f;
		updaterControlsText->amount = 0.7225f;
		break;
	case 2:
		updaterControlsText->posX = 161.f;
		updaterControlsText->amount = 0.1925f;
		break;
	}
}

void ExitUpdatesUI(void) {
	changeTopFooter(NULL);
	changeTopSprite(0);
	clearTop(false);
	removeAppTop(false);
}

void clearUpdateSprites() {
	if (changelogTextsprite) deleteSprite(changelogTextsprite);
	if (updaterControlsText) deleteSprite(updaterControlsText);
	if (topInfoSpriteUpd) deleteSprite(topInfoSpriteUpd);
	if (updateProgBar) deleteProgressBar(updateProgBar);
	changelogTextsprite = NULL;
	updaterControlsText = NULL;
	topInfoSpriteUpd = NULL;
	updateProgBar = NULL;
}

void drawUpdateText(int version, int page, int totpage) {
	clearTop(false);
	newAppTop(DEFAULT_COLOR, MEDIUM | BOLD | CENTER, "Ver. %s - Page %d/%d", versionList[version], page + 1, totpage);
	for (int i = 0; i < 10; i++) {
		if (changelogList[version][i + 10 * page] != NULL) {
			newAppTop(DEFAULT_COLOR, MEDIUM, "%s", changelogList[version][i + 10 * page]);
		}
	}
}

int getChgPageCount(int ver) {
	int count = 0;
	while (changelogList[ver][count]) count++;
	return (count / 10) + ((count % 10 != 0) ? 1 : 0);
}

void UpdaterMenuLoop() {
	bool updaterLoop = true;

	u32 keys = 0;
	u32 exitkey = 0;
	setControlsMode(1);
	if (versionList[0] == NULL) {
		newAppTop(DEFAULT_COLOR, MEDIUM | BOLD | CENTER, "Getting Update Info...");
		newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "If the launcher freezes, restart the 3ds");//if it fails, remove it EDIT: dosen't appear
		newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "Sorry for the inconvenience");//if it fails, remove it EDIT: dosen't appear
		updateUI();
		removeAppTop(false);
		if (!downloadChangelog()) {
			newAppTop(COLOR_RED, MEDIUM | BOLD | CENTER, "Couldn't get update info.");
			newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "Error info:");
			newAppTopMultiline(DEFAULT_COLOR, SMALL | CENTER, CURL_lastErrorCode);
			setControlsMode(2);
			while (updaterLoop && aptMainLoop()) {
				keys = hidKeysDown();
				if (keys & KEY_B) {
					PLAYBOOP();
					updaterLoop = false;
				}
				updateUI();
			}
			setControlsMode(0);
			return;
		}
		for (int i = 0; versionList[i]; i++) if (strcmp(g_modversion, versionList[i]) == 0 && versionList[i + 1]) forceUpdate = true;
	}
	int totver = 0;
	int page = 0;
	int totPage = 0;
	bool allowcontrol = false;
	u64 timer = Timer_Restart();
	while (versionList[totver]) totver++;
	int ver = totver - 1;
	totPage = getChgPageCount(ver);
	if (forceUpdate) {
		setControlsMode(0);
	}
	while (updaterLoop && aptMainLoop()) {
		allowcontrol = false;
		drawUpdateText(ver, page, totPage);
		if (Timer_HasTimePassed(250, timer)) {
			allowcontrol = true;
		}
		if ((keys & KEY_UP) && allowcontrol) {
			timer = Timer_Restart();
			if (ver < totver - 1) { ver++; PLAYCLICK(); }
			totPage = getChgPageCount(ver);
			page = 0;
		}
		if ((keys & KEY_DOWN) && allowcontrol) {
			timer = Timer_Restart();
			if (ver > 0) { ver--; PLAYCLICK(); }
			totPage = getChgPageCount(ver);
			page = 0;
		}
		if ((keys & KEY_RIGHT) && allowcontrol) {
			timer = Timer_Restart();
			if (page < totPage - 1) { page++; PLAYCLICK(); }
		}
		if ((keys & KEY_LEFT) && allowcontrol) {
			timer = Timer_Restart();
			if (page > 0) { page--; PLAYCLICK(); }
		}
		if (exitkey & KEY_B) {
			PLAYBOOP();
			updaterLoop = false;
		}
		updateUI();
		envIsHomebrew();
		if (exitkey & KEY_START) {
			if (forceUpdate) {
				updaterControlsText->isHidden = true;
				appTop->sprite = topInfoSpriteUpd;
				PLAYBEEP();
				int ret = performUpdate(updateProgBar, &restartneeded);
				STOPLAG();
				updateProgBar->isHidden = true;
				appTop->sprite = topInfoSprite;
				updaterControlsText->isHidden = false;
				bool errorLoop = true;
				int keysErr = 0;
				setControlsMode(2);
				while (errorLoop && aptMainLoop()) {
					keysErr = hidKeysDown();
					if (keysErr & KEY_B) {
						errorLoop = false;
						PLAYBOOP();
					}
					clearTop(false);
					if ((ret & 0xFF) == 0x3) {
						newAppTop(COLOR_RED, MEDIUM | BOLD | CENTER, "Update Failed");
						newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "Couldn't get version info.");
						newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "0x%08X", ret);
						newAppTopMultiline(DEFAULT_COLOR, SMALL | CENTER, CURL_lastErrorCode);
						newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "\nYou can ask for help in the");
						newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "MKW3DS discord server.");
					} else if (ret) {
						newAppTop(COLOR_RED, MEDIUM | BOLD | CENTER, "Update Failed");
						newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "\nThe update failed with");
						newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "the following error code:");
						newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "\n0x%08X\n", ret);
						newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "You can ask for help in the");
						newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "MKW3DS discord server.");
					}
					else {
						newAppTop(COLOR_GREEN, MEDIUM | BOLD | CENTER, "Update Succeded");
						newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "\nUpdated to %s", versionList[totver - 1]);
						if (restartneeded) {
							newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "\nThe app will restart.");
						}
						forceUpdate = false;
					}
					updateUI();
				}
				getModVersion();
				updaterLoop = false;
			}
		}
		keys = hidKeysDown() | hidKeysHeld();
		exitkey = hidKeysDown();
	}
	setControlsMode(0);
}

void UpdatesMenu() {

	changeTopFooter(updaterControlsText);
	changeTopSprite(1);
	UpdaterMenuLoop();
	ExitUpdatesUI();
}