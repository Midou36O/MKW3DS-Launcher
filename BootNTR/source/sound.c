#include "sound.h"
#include "main.h"

Result csndPlaySoundFixed(int chn, u32 flags, u32 sampleRate, float vol, float pan, void* data0, void* data1, u32 size)
{
	if (!(csndChannels & BIT(chn)))
		return 1;

	u32 paddr0 = 0, paddr1 = 0;

	int encoding = (flags >> 12) & 3;
	int loopMode = (flags >> 10) & 3;

	if (!loopMode) flags |= SOUND_ONE_SHOT;

	if (encoding != CSND_ENCODING_PSG)
	{
		if (data0) paddr0 = osConvertVirtToPhys(data0);
		if (data1) paddr1 = osConvertVirtToPhys(data1);
	}

	u32 timer = CSND_TIMER(sampleRate);
	if (timer < 0x0042) timer = 0x0042;
	else if (timer > 0xFFFF) timer = 0xFFFF;
	flags &= ~0xFFFF001F;
	flags |= SOUND_ENABLE | SOUND_CHANNEL(chn) | (timer << 16);

	u32 volumes = CSND_VOL(vol, pan);
	CSND_SetChnRegs(flags, paddr0, paddr1, size, volumes, volumes);

	if (loopMode == CSND_LOOPMODE_NORMAL && paddr1 > paddr0)
	{
		// Now that the first block is playing, configure the size of the subsequent blocks
		size -= paddr1 - paddr0;
		CSND_SetBlock(chn, 1, paddr1, size);
	}

	return csndExecCmds(true);
}

u32 parseInfoBlock(cwav_t* cwav) {
	u32 infoSize = cwav->cwavHeader->info_blck.size;
	cwav->cwavInfo = (cwavInfoBlock_t *)((u32)(cwav->fileBuf) + cwav->cwavHeader->info_blck.ref.offset);
	if (cwav->cwavInfo->header.magic != 0x4F464E49 || cwav->cwavInfo->header.size != infoSize) return 1; //magic INFO or size mismatch
	cwav->channelcount = cwav->cwavInfo->channelInfoRefs.count;
	u32 encoding = cwav->cwavInfo->encoding;
	if (encoding != IMA_ADPCM) return 2; //Only IMA ADPCM supported for now
	cwav->channelInfos = (cwavchannelInfo_t**)malloc(4 * cwav->channelcount);
	if (!cwav->channelInfos) return 3;
	for (int i = 0; i < cwav->channelcount; i++) {
		cwavReference_t *currRef = &(cwav->cwavInfo->channelInfoRefs.references[i]);
		if (currRef->refType != CHANNEL_INFO) {
			return 4;
		}
		cwav->channelInfos[i] = (cwavchannelInfo_t*)((u8*)(&(cwav->cwavInfo->channelInfoRefs.count)) + currRef->offset);
		if (cwav->channelInfos[i]->samples.refType != SAMPLE_DATA) return 5;
	}
	cwav->IMAADPCMInfos = (cwavIMAADPCMInfo_t**)malloc(4 * cwav->channelcount);
	if (!cwav->IMAADPCMInfos) return 6;
	for (int i = 0; i < cwav->channelcount; i++) {
		if (cwav->channelInfos[i]->ADPCMInfo.refType != IMA_ADPCM_INFO) return 7;
		cwav->IMAADPCMInfos[i] = (cwavIMAADPCMInfo_t*)(cwav->channelInfos[i]->ADPCMInfo.offset + (u8*)(&(cwav->channelInfos[i]->samples)));
	}
	return 0;
}

void freeCwav(cwav_t* cwav) {
	if (!cwav) return;
	if (cwav->playingchanids) stopcwav(cwav, -1);
	if (cwav->fileBuf) linearFree(cwav->fileBuf);
	if (cwav->channelInfos) free(cwav->channelInfos);
	if (cwav->IMAADPCMInfos) free(cwav->IMAADPCMInfos);
	free(cwav);
}

/*void dumpCwavInfo(cwav_t* cwav, char* filename) {
	FILE* file = fopen("/cwavdump.txt", "w");
	char buf[100] = { 0 };
	sprintf(buf, "Filename: %s\n\n", filename);
	fwrite(buf, 1, strlen(buf), file);
	fseek(file, 0, SEEK_END);
	sprintf(buf, "Info block: Offset: 0x%08lX, Size: 0x%08lX\n", (u32)cwav->cwavInfo - (u32)cwav->fileBuf, cwav->cwavInfo->header.size);
	fwrite(buf, 1, strlen(buf), file);
	fseek(file, 0, SEEK_END);
	sprintf(buf, "Data block: Offset: 0x%08lX, Size: 0x%08lX\n\n", (u32)cwav->cwavData - (u32)cwav->fileBuf, cwav->cwavData->header.size);
	fwrite(buf, 1, strlen(buf), file);
	fseek(file, 0, SEEK_END);
	sprintf(buf, "Encoding: %d\n", cwav->cwavInfo->encoding);
	fwrite(buf, 1, strlen(buf), file);
	fseek(file, 0, SEEK_END);
	sprintf(buf, "Num samples: %ld\n", cwav->cwavInfo->LoopEnd);
	fwrite(buf, 1, strlen(buf), file);
	fseek(file, 0, SEEK_END);
	sprintf(buf, "Is looped: %d\n", cwav->cwavInfo->isLooped);
	fwrite(buf, 1, strlen(buf), file);
	fseek(file, 0, SEEK_END);
	sprintf(buf, "Sample rate: %ld\n", cwav->cwavInfo->sampleRate);
	fwrite(buf, 1, strlen(buf), file);
	fseek(file, 0, SEEK_END);
	sprintf(buf, "Channel count: %ld\n\n", cwav->cwavInfo->channelInfoRefs.count);
	fwrite(buf, 1, strlen(buf), file);
	fseek(file, 0, SEEK_END);
	for (int i = 0; i < cwav->cwavInfo->channelInfoRefs.count; i++) {
		sprintf(buf, "Channel id: %d\n", i);
		fwrite(buf, 1, strlen(buf), file);
		fseek(file, 0, SEEK_END);
		sprintf(buf, "Sample offset: 0x%08lX\n", (u32)((u32)cwav->channelInfos[i]->samples.offset + (u8*)(&(cwav->cwavData->data))) - (u32)(cwav->fileBuf));
		fwrite(buf, 1, strlen(buf), file);
		fseek(file, 0, SEEK_END);
		sprintf(buf, "ADPCM index: 0x%02X\n", cwav->DSPADPCMInfos[i]->context.predScale);
		fwrite(buf, 1, strlen(buf), file);
		fseek(file, 0, SEEK_END);
		sprintf(buf, "ADPCM history: 0x%08lX 0x%08lX\n", (u32)cwav->DSPADPCMInfos[i]->context.prevSample, (u32)cwav->DSPADPCMInfos[i]->context.secondPrevSample);
		fwrite(buf, 1, strlen(buf), file);
		fseek(file, 0, SEEK_END);
		sprintf(buf, "ADPCM params addr: 0x%08lX\n\n", (u32)&(cwav->DSPADPCMInfos[i]->param) - (u32)(cwav->fileBuf));
		fwrite(buf, 1, strlen(buf), file);
		fseek(file, 0, SEEK_END);
	}
}*/

u32 newCwav(char* filename, cwav_t** out) {
	cwav_t* cwav = NULL; 
	FILE* file = NULL;
	u32 ret = 0; 
	file = fopen(filename, "rb");
	if (!file) return 1;
	cwav = malloc(sizeof(cwav_t)); 
	if (!cwav) return 2;
	memset(cwav, 0, sizeof(cwav_t));
	fseek(file, 0, SEEK_END);
	u32 fileSize = ftell(file); 
	cwav->fileBuf = linearAlloc(fileSize);
	if (!cwav->fileBuf) {
		fclose(file);
		freeCwav(cwav);
		return 3;
	}
	fseek(file, 0, SEEK_SET); 
	fread(cwav->fileBuf, 1, fileSize, file);
	fclose(file);
	cwav->cwavHeader = (cwavHeader_t*)cwav->fileBuf; 
	if (cwav->cwavHeader->magic != 0x56415743 || cwav->cwavHeader->endian != 0xFEFF || cwav->cwavHeader->version != 0x02010000 || cwav->cwavHeader->blockCount != 2) {
		freeCwav(cwav);
		return 4;
	}
	ret = parseInfoBlock(cwav); 
	if (ret) {
		freeCwav(cwav);
		return 5;
	}
	cwav->cwavData = (cwavDataBlock_t*)((u32)(cwav->fileBuf) + cwav->cwavHeader->data_blck.ref.offset); 
	if (cwav->cwavData->header.magic != 0x41544144) {
		freeCwav(cwav);
		return 6;
	}
	cwav->playingchanids = (int*)malloc(cwav->channelcount * 4); 
	if (!(cwav->playingchanids)) {
		freeCwav(cwav);
		return 7;
	}
	for (int i = 0; i < cwav->channelcount; i++) cwav->playingchanids[i] = -1; 
	*out = cwav;
	return 0;
}

void stopcwav(cwav_t* cwav, int channel) {
	if (!cwav) return;
	if (channel >= 0 && channel < cwav->channelcount) {
		if (cwav->playingchanids[channel] != -1) {
			CSND_SetPlayState(cwav->playingchanids[channel], 0);
			cwav->playingchanids[channel] = -1;
		}
	}
	else {
		for (int i = 0; i < cwav->channelcount; i++) {
			if (cwav->playingchanids[i] != -1) {
				CSND_SetPlayState(cwav->playingchanids[i], 0);
				cwav->playingchanids[i] = -1;
			}
		}
	}
	csndExecCmds(true);
}

bool playcwav(cwav_t* cwav, int channel1, int channel2) {
	if (!cwav) return false;
	bool stereo = true;
	if (channel2 < 0) {
		stereo = false;
	}
	if (channel1 < 0 || channel1 >= (int)(cwav->channelcount) || channel2 >= (int)(cwav->channelcount)) {
		return false;
	}
	stopcwav(cwav, channel1);
	if (stereo) stopcwav(cwav, channel2);
	int prevchan = -1;
	for (int i = 0; i < ((stereo) ? 2 : 1); i++) {
		for (int j = 0; j < 32; j++) {
			if (!((csndChannels >> j) & 1)) continue;
			u8 stat = 0;
			csndIsPlaying(j, &stat);
			csndExecCmds(true);
			if (stat || j == prevchan) continue;
			cwav->playingchanids[i ? channel2 : channel1] = j;
			break;
		}
		if (cwav->playingchanids[i ? channel2 : channel1] == -1) {
			return false;
		}
		prevchan = cwav->playingchanids[i ? channel2 : channel1];
		u8* block0 = NULL;
		u8* block1 = NULL;
		u32 size;
		block0 = (u8*)((u32)cwav->channelInfos[i ? channel2 : channel1]->samples.offset + (u8*)(&(cwav->cwavData->data)));
		size = (cwav->cwavInfo->LoopEnd / 2);
		CSND_SetAdpcmState(cwav->playingchanids[i ? channel2 : channel1], 0, cwav->IMAADPCMInfos[i ? channel2 : channel1]->context.data, cwav->IMAADPCMInfos[i ? channel2 : channel1]->context.tableIndex);
		if (cwav->cwavInfo->isLooped) {
			block1 = ((cwav->cwavInfo->loopStart) / 2) + block0;
			CSND_SetAdpcmState(cwav->playingchanids[i ? channel2 : channel1], 1, cwav->IMAADPCMInfos[i ? channel2 : channel1]->loopContext.data, cwav->IMAADPCMInfos[i ? channel2 : channel1]->loopContext.tableIndex);
		}
		else {
			block1 = block0;
			CSND_SetAdpcmState(cwav->playingchanids[i ? channel2 : channel1], 1, cwav->IMAADPCMInfos[i ? channel2 : channel1]->context.data, cwav->IMAADPCMInfos[i ? channel2 : channel1]->context.tableIndex);
		}
		csndExecCmds(true);
		float pan = 0.f;
		if (stereo) {
			if (i == 0)
			{
				pan = -1.0f;
			}
			else 
			{
				pan = 1.0f;
			}
		}
		csndPlaySoundFixed(cwav->playingchanids[i ? channel2 : channel1], (cwav->cwavInfo->isLooped ? SOUND_REPEAT : SOUND_ONE_SHOT) | SOUND_FORMAT_ADPCM, cwav->cwavInfo->sampleRate, 1, pan, block0, block1, size);
		csndExecCmds(true);
	}
	return true;
}