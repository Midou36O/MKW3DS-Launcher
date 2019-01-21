#include "main.h"
#include "errno.h"
#include "sys/stat.h"
#include "json/json.h"
#include "graphics.h"
#include "drawableObject.h"
#include "allocator\newlibHeap.h"
#include "button.h"
#include "clock.h"
#include <malloc.h>
#include "Unicode.h"
#include "sound.h"

typedef struct downFileInfo_s
{
	char* fileName;
	u32	  updateIndex;
	char  mode;
} downFileInfo_t;

static char *str_result_buf = NULL;
static size_t str_result_sz = 0;
static size_t str_result_written = 0;

char CURL_lastErrorCode[CURL_ERROR_SIZE];
static char *file_result_buf = NULL;
static size_t file_result_sz = 0;
static size_t file_result_written = 0;
static progressbar_t* file_progbar = NULL;
static int file_singlePixel = -1;
static int fileDownCnt = 0;
static int totFileDownCnt = 0;
char updatingVer[30] = { 0 };
FILE *downfile = NULL;
char progTextBuf[2][20];

char* versionList[50] = { NULL };
extern char g_modversion[15];
char* changelogList[50][30] = { NULL };

extern cwav_t                  *sfx_sound;
extern cwav_t				   *lag_sound;

bool isWifiAvailable() {
	u32 wifiStatus;
	ACU_GetWifiStatus(&wifiStatus);
	return (bool)wifiStatus;
}

void    DisableSleep(void)
{
	u8  reg;

	if (R_FAILED(mcuHwcInit()))
		return;

	if (R_FAILED(MCUHWC_ReadRegister(0x18, &reg, 1)))
		return;

	reg |= 0x6C; ///< Disable home btn & shell state events
	MCUHWC_WriteRegister(0x18, &reg, 1);
	mcuHwcExit();
}

void    EnableSleep(void)
{
	u8  reg;

	if (R_FAILED(mcuHwcInit()))
		return;

	if (R_FAILED(MCUHWC_ReadRegister(0x18, &reg, 1)))
		return;

	reg &= ~0x6C; ///< Enable home btn & shell state events
	MCUHWC_WriteRegister(0x18, &reg, 1);
	mcuHwcExit();
}

char* getProgText(float prog, int index) {
	if (prog < (1024 * 1024)) {
		sprintf(progTextBuf[index], "%.2f KB", prog / 1024);
		return progTextBuf[index];
	}
	else {
		sprintf(progTextBuf[index], "%.2f MB", prog / (1024 * 1024));
		return progTextBuf[index];
	}
}

void updateTop(curl_off_t dlnow, curl_off_t dltot, float speed) {
	clearTop(false);
	newAppTop(DEFAULT_COLOR, CENTER | BOLD | MEDIUM, updatingVer);
	newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "Downloading Files");
	newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "%d / %d", fileDownCnt, totFileDownCnt);
	newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "\n%s / %s", getProgText(dlnow, 0), getProgText(dltot, 1));
	newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "%.2f KB/s", speed);
}

static size_t handle_data(char *ptr, size_t size, size_t nmemb, void *userdata) {
	(void)userdata;
	const size_t bsz = size * nmemb;
	
	if (!str_result_buf) {
		str_result_sz = 1 << 9;
		str_result_buf = memalign(0x1000, 1 << 9);
	}
	bool needrealloc = false;
	while (bsz + str_result_written >= str_result_sz) {
		str_result_sz <<= 1;
		needrealloc = true;
	}
	if (needrealloc) {
		str_result_buf = realloc(str_result_buf, str_result_sz);
		if (!str_result_buf) {
			return 0;
		}
	}
	if (!str_result_buf) return 0;
	memcpy(str_result_buf + str_result_written, ptr, bsz);
	str_result_written += bsz;
	return bsz;
}

int file_progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
	if (!(dltotal && dlnow)) return 0;
	static float oldprogress = 0;
	static u64 timer = 0;
	static u64 timer2 = 0;
	if (file_singlePixel == -1) { //Not initialized
		oldprogress = 0;
		timer2 = 0;
		timer = Timer_Restart();
	}
	file_singlePixel = dltotal / (curl_off_t)(file_progbar->rectangle->width);
	if (dlnow - oldprogress >= file_singlePixel) {
		file_progbar->rectangle->amount = ((float)dlnow) / ((float)dltotal);
		timer2 = Timer_Restart();
		u64 progmsec = getTimeInMsec(timer2 - timer);
		timer = Timer_Restart();
		if (!progmsec) progmsec = 1;
		curl_off_t progbytes = dlnow - oldprogress;
		updateTop(dlnow, dltotal, ((float)progbytes) / ((float)progmsec));
		updateUI();
		oldprogress = dlnow;
		
	}
	return 0;
};

bool filecommit() {
	if (!downfile) return false;
	fseek(downfile, 0, SEEK_END);
	u32 byteswritten = fwrite(file_result_buf, 1, file_result_written, downfile);
	if (byteswritten != file_result_written) return false;
	file_result_written = 0;
	return true;
}

char *strdup(const char *s) {
	char *d = malloc(strlen(s) + 1);   // Space for length plus nul
	if (d == NULL) return NULL;          // No memory
	strcpy(d, s);                        // Copy the characters
	return d;                            // Return the new string
}

FILE* fopen_mkdir(const char* name, const char* mode)
{
	char*	_path = strdup(name);
	char    *p;
	FILE*	retfile = NULL;

	errno = 0;
	for (p = _path + 1; *p; p++)
	{
		if (*p == '/')
		{
			*p = '\0';
			if (mkdir(_path, 777) != 0)
				if (errno != EEXIST) goto error;
			*p = '/';
		}
	}
	retfile = fopen(name, mode);
error:
	free(_path);
	return retfile;
}

void    memcpy32(void *dest, const void *src, size_t count)
{
	u32         lastbytes = count & 3;
	u32         *dest_ = (u32 *)dest;
	const u32   *src_ = (u32*)src;

	count >>= 2;

	while (count--)
		*dest_++ = *src_++;

	u8  *dest8_ = (u8 *)dest_;
	u8  *src8_ = (u8 *)src_;

	while (lastbytes--)
		*dest8_++ = *src8_++;
}

static size_t file_handle_data(char *ptr, size_t size, size_t nmemb, void *userdata) {
	(void)userdata;
	const size_t bsz = size * nmemb;
	size_t tofill = 0;
	if (!file_result_buf) {
		file_result_sz = 0x60000;
		file_result_buf = memalign(0x1000, file_result_sz);
		if (!file_result_buf) return 0;
	}
	if (file_result_written + bsz >= file_result_sz) {
		tofill = file_result_sz - file_result_written;
		memcpy32(file_result_buf + file_result_written, ptr, tofill);
		file_result_written += tofill;
		if (!filecommit()) return 0;
	}
	memcpy32(file_result_buf + file_result_written, ptr + tofill, bsz - tofill);
	file_result_written += bsz - tofill;
	return bsz;
}

int downloadFile(char* URL, char* filepath, progressbar_t* progbar) {

	int retcode = 0;
	
	
	void *socubuf = memalign(0x1000, 0x100000);
	if (!socubuf) {
		sprintf(CURL_lastErrorCode, "Failed to allocate memory.");
		retcode = 1;
		goto exit;
	}
	
	int res = socInit(socubuf, 0x100000);
	if (R_FAILED(res)) {
		sprintf(CURL_lastErrorCode, "socInit returned: 0x%08X", res);
		goto exit;
	}
	
	downfile = fopen_mkdir(filepath, "wb");
	if (!downfile || !progbar) {
		sprintf(CURL_lastErrorCode, "Failed to create file.");
		retcode = 4;
		goto exit;
	}
	
	file_progbar = progbar;
	progbar->isHidden = false;
	progbar->rectangle->amount = 0;
	clearTop(false);
	newAppTop(DEFAULT_COLOR, CENTER | BOLD | MEDIUM, updatingVer);
	newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "Downloading Files");
	newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "%d / %d", fileDownCnt, totFileDownCnt);
	updateUI();
	
	CURL *hnd = curl_easy_init();
	curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, 102400L);
	curl_easy_setopt(hnd, CURLOPT_URL, URL);
	curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(hnd, CURLOPT_USERAGENT, "Mozilla/5.0 (Nintendo 3DS; U; ; en) AppleWebKit/536.30 (KHTML, like Gecko) CTGP-7/1.0 CTGP-7/1.0");
	curl_easy_setopt(hnd, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(hnd, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(hnd, CURLOPT_ACCEPT_ENCODING, "gzip");
	curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
	curl_easy_setopt(hnd, CURLOPT_XFERINFOFUNCTION, file_progress_callback);
	curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, file_handle_data);
	curl_easy_setopt(hnd, CURLOPT_ERRORBUFFER, CURL_lastErrorCode);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(hnd, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(hnd, CURLOPT_STDERR, stdout);
	
	CURL_lastErrorCode[0] = 0;
	CURLcode cres = curl_easy_perform(hnd);
	curl_easy_cleanup(hnd);
	
	if (cres != CURLE_OK) {
		retcode = cres;
		goto exit;
	}
	filecommit();
	fflush(downfile);
	
exit:
	socExit();
	
	if (socubuf) {
		free(socubuf);
	}
	if (downfile) {
		fclose(downfile);
		downfile = NULL;
	}
	file_progbar = NULL;
	file_singlePixel = -1;
	file_result_buf = NULL;
	file_result_written = 0;
	file_result_sz = 0;
	
	return retcode;
}


int downloadString(char* URL, char** out) {

	DisableSleep();

	*out = NULL;
	int retcode = 0;

	void *socubuf = memalign(0x1000, 0x100000);
	if (!socubuf) {
		sprintf(CURL_lastErrorCode, "Failed to allocate memory.");
		retcode = 1;
		goto exit;
	}
	int res = socInit(socubuf, 0x100000);
	if (R_FAILED(res)) {
		sprintf(CURL_lastErrorCode, "socInit returned: 0x%08X", res);
		retcode = 2;
		goto exit;
	}

	CURL *hnd = curl_easy_init();
	curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, 102400L);
	curl_easy_setopt(hnd, CURLOPT_URL, URL);
	curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(hnd, CURLOPT_USERAGENT, "Mozilla/5.0 (Nintendo 3DS; U; ; en) AppleWebKit/536.30 (KHTML, like Gecko) CTGP-7/1.0 CTGP-7/1.0");
	curl_easy_setopt(hnd, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(hnd, CURLOPT_ACCEPT_ENCODING, "gzip");
	curl_easy_setopt(hnd, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
	curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, handle_data);
	curl_easy_setopt(hnd, CURLOPT_ERRORBUFFER, CURL_lastErrorCode);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(hnd, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(hnd, CURLOPT_STDERR, stdout);

	CURL_lastErrorCode[0] = 0;
	CURLcode cres = curl_easy_perform(hnd);
	curl_easy_cleanup(hnd);

	if (cres != CURLE_OK) {
		retcode = cres;
		goto exit;
	}

	str_result_buf[str_result_written] = '\0';
	*out = str_result_buf;

exit:
	socExit();

	if (socubuf) {
		free(socubuf);
	}

	str_result_buf = NULL;
	str_result_written = 0;
	str_result_sz = 0;

	EnableSleep();

	return retcode;
}

bool downloadChangelog() {
	if (!isWifiAvailable()) return false;
	STARTLAG();
	char* downstr;
	char *token;
	char *end_str;
	int index1 = 0;
	int index2 = 0;

	int retcode = downloadString("https://raw.githubusercontent.com/Midou36O/MKW3DS-updates/master/updates/changeloglist", &downstr); //Works! Yay!
	if (retcode || downstr == NULL) {
		if (downstr) {
			free(downstr);
		}
		STOPLAG();
		return false;
	}
	token = strtok_r(downstr, ";", &end_str);
	while (token != NULL)
	{
		char *end_token;
		char *token2 = strtok_r(token, ":", &end_token);
		while (token2 != NULL)
		{
			if (index2 == 0) {
				versionList[index1] = malloc(strlen(token2) + 1);
				strcpy(versionList[index1], token2);
			}
			else {
				changelogList[index1][index2 - 1] = malloc(strlen(token2) + 1);
				strcpy(changelogList[index1][index2 - 1], token2);
			}
			index2++;
			token2 = strtok_r(NULL, ":", &end_token);
		}
		token = strtok_r(NULL, ";", &end_str);
		index1++;
		index2 = 0;
	}
	free(downstr);
	STOPLAG();
	return true;
}

bool updateAvailable() {
	if (!isWifiAvailable()) return false;
	char* downstr = NULL;
	int retcode = downloadString("http://bit.ly/2CnDdHH", &downstr);
	//NOTE: bit.ly is used to know how many people use the mod and from which country they are. This data helps CTGP-7 devs to know the most used languages, the amount of people playing, etc.. This data is not shared with anyone and is anonymus (IPs not registered) 
	if (retcode || downstr == NULL) {
		if (downstr) {
			free(downstr);
		}
		return false;
	}
	if(strcmp(g_modversion, downstr) == 0) {
		free(downstr);
		return false;
	} else {
		free(downstr);
		if (!downloadChangelog()) return false;
		int index = 0;
		while (versionList[index]) index++;
		for (int i = 0; i < index; i++) {
			if (strcmp(versionList[i], g_modversion) == 0) {
				if (i == index - 1) {
					return false;
					break;
				}
				else {
					return true;
					break;
				}
			}
		}
		return false;
	}
}

downFileInfo_t* createNewFileInfo(u32 index) {
	downFileInfo_t* ret;
	ret = (downFileInfo_t *)calloc(1, sizeof(downFileInfo_t));
	ret->updateIndex = index;
	return ret;
}

void destroyFileInfo(downFileInfo_t* ptr) {
	if (ptr->fileName) free(ptr->fileName);
	free(ptr);
}

char* concat(const char *s1, const char *s2)
{
	char *result = malloc(strlen(s1) + strlen(s2) + 1);//+1 for the null-terminator
													   //in real code you would check for errors in malloc here
	strcpy(result, s1);
	strcat(result, s2);
	return result;
}

static inline void freeFileInfo(downFileInfo_t** fileInfo) {
	if (!fileInfo) return;
	for (int i = 0; fileInfo[i]; i++) destroyFileInfo(fileInfo[i]);
	free(fileInfo);
}

static void writeFaultyURL(char* URL) {
	if (!URL) return;
	FILE* tmpfl = fopen("/MKW3DS/failedURL.txt", "w");
	fwrite(URL, 1, strlen(URL), tmpfl);
	fclose(tmpfl);
}

int updateLuma3DS(progressbar_t* progbar) {
	strcpy(updatingVer, "Luma plugin loader");
	totFileDownCnt = 1;
	fileDownCnt = 1;
	progbar->rectangle->amount = 0;
	progbar->isHidden = false;
	DisableSleep();
	int retcode = downloadFile(PLGLDR_URL, PLGLDR_TMP, progbar);
	if (retcode == 0) {
		remove("/boot.firm.bak");
		rename("/boot.firm", "/boot.firm.bak");
		rename(PLGLDR_TMP, "/boot.firm");
	}
	EnableSleep();
	progbar->isHidden = true;
	updatingVer[0] = '\0';
	totFileDownCnt = 0;
	fileDownCnt = 0;
	return retcode;
}

void fileNameTrim(char* filename) {
	int size = strlen(filename);
	for (int i = size - 1; i >= 0; i--) {
		char c = filename[i];
		if (c == '\n' || c == '\r' || c == ' ') filename[i] = '\0';
		else break;
	}
}


int performUpdate(progressbar_t* progbar, bool* restartNeeded) {
	DisableSleep();
	int index = -1;
	for (int i = 0; versionList[i]; i++) if (strcmp(g_modversion, versionList[i]) == 0) index = i;
	if (index == -1) return 1;
	index++;
	char*  URL = NULL;
	char *end_str; 
	char* token;
	FILE* ciaFile = NULL;
	FILE* brewFile = NULL;
	downFileInfo_t** downfileinfo = NULL;
	int downfileinfocnt = 0; 
	totFileDownCnt = 0;
	fileDownCnt = 0;
	remove(TOINSTALL_CIA_PATH);
	STARTLAG();
	clearTop(false);
	newAppTop(DEFAULT_COLOR, MEDIUM | BOLD | CENTER, "Fetching Update Info");
	newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "Please wait...");
	updateUI();
	while (versionList[index]) {
		if (!isWifiAvailable()) {
			freeFileInfo(downfileinfo);
			EnableSleep();
			return 2;
		}
		char* downstr = NULL;
		if (!URL) URL = malloc(200);
		int filecount = 0; 
		sprintf(URL, "https://github.com/Midou36O/MKW3DS-updates/releases/download/v%s/filelist.txt", versionList[index]);
		int retcode = downloadString(URL, &downstr); 
		
		if (retcode || downstr == NULL) {
			if (downstr) {
				free(downstr);
			}
			writeFaultyURL(URL);
			freeFileInfo(downfileinfo);
			EnableSleep();
			return 3 | (retcode << 8);
		}
		free(URL);
		URL = NULL;
		for (int i = 0; downstr[i]; i++) if (downstr[i] == '\n') filecount++;
		if (!downfileinfo) {
			downfileinfo = malloc((filecount) * 4); 
		}
		else {
			downfileinfo = realloc(downfileinfo, (downfileinfocnt + filecount) * 4); 
		}
		if (!downfileinfo) {
			EnableSleep();
			return 4;
		}
		int index1 = 0; 
		for (int i = 0; i < filecount; i++) downfileinfo[i + downfileinfocnt] = createNewFileInfo(index); 
		token = strtok_r(downstr, "\n", &end_str);
		while (token != NULL && index1 < filecount)
		{
			downfileinfo[index1 + downfileinfocnt]->mode = token[0];
			downfileinfo[index1 + downfileinfocnt]->fileName = malloc(strlen(&token[1]) + 1);
			strcpy(downfileinfo[index1 + downfileinfocnt]->fileName, &token[1]);
			index1++; 
			token = strtok_r(NULL, "\n", &end_str);
		}
		downfileinfocnt += filecount;
		index++;
		free(downstr);
	}
	sprintf(updatingVer, "Updating to %s", versionList[index - 1]);
	downfileinfo = realloc(downfileinfo, (downfileinfocnt + 1) * 4); 
	if (!downfileinfo) svcBreak((UserBreakType)0); 
	downfileinfo[downfileinfocnt] = NULL;
	STOPLAG();
	for (int i = 0; downfileinfo[i]; i++) { 
		downFileInfo_t* src = downfileinfo[i];
		fileNameTrim(src->fileName);
		if (!(src->mode == 'M')) continue;
		for (int j = 0; downfileinfo[j]; j++) { 
			downFileInfo_t* dst = downfileinfo[j];
			if (dst->mode == 'I' || dst->mode == 'F' || dst->mode == 'T') continue; 
			if (src->updateIndex < dst->updateIndex && strcmp(src->fileName, dst->fileName) == 0) { 
				src->mode = 'I'; 
				break;
			}
		}
		if (src->mode == 'M') totFileDownCnt++; 
	}
	clearTop(false);
	fileDownCnt = 1;
	for (int i = 0; downfileinfo[i]; i++) {
		char* tmpbuf1 = NULL;
		char* tmpbuf2 = NULL;
		if (downfileinfo[i]->mode == 'F') {
			if (downfileinfo[i + 1] && downfileinfo[i + 1]->mode == 'T') {
				tmpbuf1 = concat(DEFAULT_MOD_PATH, downfileinfo[i]->fileName);
				tmpbuf2 = concat(DEFAULT_MOD_PATH, downfileinfo[i+1]->fileName);
				remove(tmpbuf2);
				rename(tmpbuf1, tmpbuf2);
			}
		}
		else if (downfileinfo[i]->mode == 'D') {
			tmpbuf1 = concat(DEFAULT_MOD_PATH, downfileinfo[i]->fileName);
			remove(tmpbuf1);
		}
		else if (downfileinfo[i]->mode == 'M') {
			PLAYCLICK();
			tmpbuf1 = concat(FILE_DOWN_PREFIX, downfileinfo[i]->fileName);
			tmpbuf2 = concat(DEFAULT_MOD_PATH, downfileinfo[i]->fileName);
			progbar->isHidden = false;
			progbar->rectangle->amount = 0;
			int ret = downloadFile(tmpbuf1, tmpbuf2, progbar);
			if (ret) {
				bool errorloop = true;
				u32 keys = 0;
				progbar->isHidden = true;
				clearTop(false);
				PLAYBOOP();
				newAppTop(COLOR_RED, MEDIUM | BOLD | CENTER, "Download Failed");
				newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "Error downloading file %d", fileDownCnt);
				newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, "Err: 0x%08X", 5 | (ret << 8));
				newAppTopMultiline(DEFAULT_COLOR, SMALL | CENTER, CURL_lastErrorCode);
				newAppTop(DEFAULT_COLOR, MEDIUM | CENTER, ""FONT_A": Retry       "FONT_B": Exit");
				while (errorloop) {
					if (keys & KEY_A) {
						PLAYBEEP();
						errorloop = false;
						i--;
						//totFileDownCnt--; //oops wrong
						fileDownCnt--;
					}
					if (keys & KEY_B) {
						freeFileInfo(downfileinfo);
						writeFaultyURL(tmpbuf1);
						EnableSleep();
						return 5 | (ret << 8);
					}
					updateUI();
					keys = hidKeysDown();
				}
				progbar->isHidden = false;
			}
			fileDownCnt++;
		}
		if (tmpbuf1) {
			free(tmpbuf1); tmpbuf1 = NULL;
		}
		if (tmpbuf2) {
			free(tmpbuf2); tmpbuf2 = NULL;
		}
	}
	freeFileInfo(downfileinfo);
	downfileinfo = NULL;
	ciaFile = fopen(TOINSTALL_CIA_PATH, "rb");
	if (ciaFile) {
		amInit();
		AM_TitleEntry manInfo = { 0 };
		u64 tid = CTGP7_TID;
		AM_GetTitleInfo(MEDIATYPE_SD, 1, &tid, &manInfo);
		if (manInfo.size > 0) {
			Handle handle;
			Result ret = 0;
			ret = AM_StartCiaInstall(MEDIATYPE_SD, &handle);
			if (ret < 0) {
				EnableSleep();
				return ret;
			}
			fseek(ciaFile, 0L, SEEK_END);
			u32 fileSize = ftell(ciaFile);
			u32 filePos = 0;
			u32 chunk_sz = 0x60000;
			u8* tmpbuf = memalign(0x1000, chunk_sz);
			u32 singlePixel = fileSize / (progbar->rectangle->width);
			u32 oldprog = 0;
			u64 timer1 = Timer_Restart();
			u64 timer2 = Timer_Restart();
			progbar->isHidden = false;
			progbar->rectangle->amount = 0;
			float speed = 0;
			while (filePos < fileSize) {
				if (filePos + chunk_sz >= fileSize) {
					chunk_sz = fileSize - filePos;
				}
				u32 bytesWritten = 0;
				fseek(ciaFile, filePos, SEEK_SET);
				fread(tmpbuf, 1, chunk_sz, ciaFile);
				ret = FSFILE_Write(handle, &bytesWritten, filePos, tmpbuf, chunk_sz, 0);
				if (ret < 0) {
					free(tmpbuf);
					fclose(ciaFile);
					AM_CancelCIAInstall(handle);
					amExit();
					EnableSleep();
					return ret;
				}
				filePos += bytesWritten;
				clearTop(false);
				if (filePos - oldprog > singlePixel) {
					clearTop(false);
					progbar->rectangle->amount = ((float)filePos) / ((float)fileSize);
					timer2 = Timer_Restart();
					u32 delaymsec = getTimeInMsec(timer2) - getTimeInMsec(timer1);
					timer1 = Timer_Restart();
					speed = ((float)(filePos - oldprog)) / ((float)delaymsec);
					clearTop(false);
					newAppTop(DEFAULT_COLOR, CENTER | BOLD | MEDIUM, updatingVer);
					newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "Updating App");
					newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "\n\n%s / %s", getProgText(filePos, 0), getProgText(fileSize, 1));
					newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "%.2f KB/s", speed);
					updateUI();
					oldprog = filePos;
				}
			}
			progbar->rectangle->amount = 1;
			clearTop(false);
			newAppTop(DEFAULT_COLOR, CENTER | BOLD | MEDIUM, updatingVer);
			newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "Updating App");
			newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "\n\n%s / %s", getProgText(filePos, 0), getProgText(fileSize, 1));
			newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "%.2f KB/s", speed);
			updateUI();
			fclose(ciaFile);
			ciaFile = NULL;
			free(tmpbuf);
			ret = AM_FinishCiaInstall(handle);
			amExit();
			if (ret < 0) {
				EnableSleep();
				return ret;
			}
			*restartNeeded = true;
		}
		remove(FINAL_CIA_PATH);
		rename(TOINSTALL_CIA_PATH, FINAL_CIA_PATH);
	}
	brewFile = fopen(TOINSTALL_3DSX_PATH, "rb");
	if (brewFile) {
		fclose(brewFile);
		FILE* endBrewFile = fopen_mkdir(FINAL_3DSX_PATH, "w"); // Generate path
		if (!endBrewFile) {
			EnableSleep();
			return 6;
		}
		fclose(endBrewFile);
		remove(FINAL_3DSX_PATH);
		if (copy_file(TOINSTALL_3DSX_PATH, TEMPORAL_3DSX_PATH)) {
			EnableSleep();
			return 7;
		}
		rename(TOINSTALL_3DSX_PATH, FINAL_3DSX_PATH);
		*restartNeeded = true;
	}
	FILE* file = fopen("/MKW3DS/config/version.bin", "w");
	fwrite(versionList[index - 1], 1, strlen(versionList[index - 1]), file);
	fclose(file);
	progbar->isHidden = true;
	EnableSleep();
	return 0;
}
