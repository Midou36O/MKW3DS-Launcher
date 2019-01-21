#include <3ds.h>
#include "main.h"
#include "fsreg.h"
#include "services.h"
#include "ifile.h"
#include "csvc.h"

void *__service_ptr = (void *)0;
u32 g_isInitialized = 0;


/*
stmfd   sp!, {r0-r12, lr}
mrs     r0, cpsr
stmfd   sp!, {r0}
ldr     r0, =0x07000000
ldr     r1, =0x00100000
ldr     r2, [r0]
str     r2, [r1]
ldr     r2, [r0, #4]
str     r2, [r1, #4]
svc     0x92
ldr     r0, =0x07000100
blx     r0
ldmfd   sp!, {r0}
msr     cpsr, r0
ldmfd   sp!, {r0-r12, lr}
ldr     pc, =0x00100000
*/


static u32 payload[] =
{
    0,
    0,
    0xE92D5FFF,
    0xE10F0000,
    0xE92D0001,
    0xE59F002C,
    0xE59F102C,
    0xE5902000,
    0xE5812000,
    0xE5902004,
    0xE5812004,
    0xEF000092,
    0xE59F0018,
    0xE12FFF30,
    0xE8BD0001,
    0xE129F000,
    0xE8BD5FFF,
    0xE59FF008,
    0x07000000,
    0x00100000,
    0x07000100,
    0x00100000
};

void    tryInitFs(void)
{
    if (g_isInitialized)
        return;
    srvSysInit();
    fsregInit();
    fsSysInit();

    g_isInitialized = 1;
}

void    exitSrv(void)
{
    if (!g_isInitialized)
        return;
    fsregExit();
    srvSysExit();
}

static void installHook(Handle process)
{
    u32  procstart = 0x00100000;
    u32  vamap = 0x00200000;

    svcControlProcessMemory(process, procstart, procstart, 0x1000, 6, 7);

    if (R_FAILED(svcMapProcessMemoryEx(process, vamap, procstart, 0x1000)))
        svcBreak(USERBREAK_ASSERT);

    u32  *game = (u32 *)vamap;

    payload[0] = game[0];
    payload[1] = game[1];

    game[0] = 0xE51FF004; // ldr pc, [pc, #-4]
    game[1] = 0x07000008;

    vu32 *dst = (vu32 *)0x07000000;

    // Copy payload
    for (u32 i = 0; i < sizeof(payload) / 4; i++)
        *dst++ = payload[i];
    
    *(u32 *)(0x07000000 + 0xF8) = game[0xD5 * 4];
    *(u32 *)(0x07000000 + 0xFC) = game[0x1D * 0xC];

    svcUnmapProcessMemoryEx(process, vamap, 0x1000);
}

void    kSetCurrentKProcess(u32 ptr);
u32     kGetCurrentKProcess();
u32     kGetKProcessByHandle(u32 handle);

Result  mapRemoteMemoryInSysRegion(Handle hProcess, u32 addr, u32 size)
{
    Result res;
    u32 outaddr = 0;
    u32 newKP = kGetKProcessByHandle(hProcess);
    u32 oldKP = kGetCurrentKProcess();

    kSetCurrentKProcess(newKP);
    res = svcControlMemoryEx(&outaddr, addr, addr, size, 0x203, 3, true);
    kSetCurrentKProcess(oldKP);

    return (res);
}

void    injectPlugin(Handle process)
{
    u64     fileSize;
    u64     tot;
    IFile   plugin;
    u32     plgDst = 0x07000000;

    // Try to open plugin file
    if (IFile_Open(&plugin, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, "/CTGP-7/resources/CTGP-7.plg"), FS_OPEN_READ))
        return;

    IFile_GetSize(&plugin, &fileSize);

    u32 alignedSize = (fileSize + 0x2000) & ~0xFFF;

    if (R_FAILED(mapRemoteMemoryInSysRegion(process, plgDst, alignedSize)))
        return;
    u8 *plg = (u8 *)plgDst + 0x100;

    if (R_FAILED(svcControlProcessMemory(process, plgDst, plgDst, alignedSize, 6, 7)))
        return;
    if (R_FAILED(svcMapProcessMemoryEx(process, plgDst, plgDst, alignedSize)))
        return;

    // Inject plugin
    do
    {
        u32 size = fileSize > 0x1000 ? 0x1000 : fileSize;

        IFile_Read(&plugin, &tot, (void *)plg, size);

        if (tot)
        {
            plg += tot;
        }
        fileSize -= tot;
    
    } while (fileSize > 0);

    IFile_Close(&plugin);

    
    installHook(process);
    svcUnmapProcessMemoryEx(process, plgDst, alignedSize);

    svcFlushEntireDataCache();   
    svcInvalidateEntireInstructionCache();
    svcSleepThread(1000);

}

// Starting point
void  start(Handle process)
{
    u64 tid;
    svcGetProcessInfo((s64 *)&tid, process, 0x10001);

    u32 low = (u32)tid;
    u32 high = (u32)((tid >> 32) & 0xFFFFFFFFULL);
    if ((high != 0x00040000) || !(low == 0x00030600 || low == 0x00030700 || low == 0x00030800))
        return; 
    tryInitFs();
    injectPlugin(process);
    exitSrv();
}
