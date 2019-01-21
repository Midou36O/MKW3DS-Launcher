
// From NTR: todo cleaning or use k11Extensions functions
#include <3ds.h>

u32 kernelArgs[10];
void backdoorHandler(void);
u32 KProcessHandleDataOffset = 0xDC;

void* currentBackdoorHandler = backdoorHandler;

u32 keRefHandle(u32 pHandleTable, u32 handle) {
    u32 handleLow = handle & 0x7fff;
    u32 ptr = *(u32*)(*((u32*)pHandleTable) + (handleLow * 8) + 4);
    return ptr;
}

void kernelCallback(void)
{
    //typedef (*keRefHandleType)(u32, u32);
    
    //keRefHandleType keRefHandle = (keRefHandleType)0xFFF67D9C;
    
    u32 t = kernelArgs[0];
    //u32 i = 0;
    /*if (t == 1) {
        u32 size = kernelArgs[3];
        u32 dst = kernelArgs[1];
        u32 src = kernelArgs[2];
        for (i = 0; i < size; i += 4) {
            *(vu32*)(dst + i) = *(vu32*)(src + i);
        }
    }*/
    
    if (t == 2) {
        //refKProcessByHandle
        u32 hProcess = kernelArgs[1];
        u32 kProcess = keRefHandle(*((u32*)0xFFFF9004) + KProcessHandleDataOffset, hProcess);
        kernelArgs[1] = kProcess;
    }
    
    if (t == 3) {
        //getCurrentKProcess
        kernelArgs[1] = *((u32*)0xFFFF9004);
    }
    
    if (t == 4) {
        //setCurrentKProcess
         *((u32*)0xFFFF9004) = kernelArgs[1];
    }
    
    /*if (t == 5)
    {
        //swapPid
        u32 kProcess = kernelArgs[1];
        u32 newPid = kernelArgs[2];
        kernelArgs[2] = *(u32*)(kProcess + KProcessPIDOffset);
        *(u32*)(kProcess + KProcessPIDOffset) = newPid;
    }*/
}

/*
u32 kSwapProcessPid(u32 kProcess, u32 newPid)
{
    kernelArgs[0] = 5;
    kernelArgs[1] = kProcess;
    kernelArgs[2] = newPid;
    svcBackdoor(currentBackdoorHandler);
    return kernelArgs[2];
}*/

void kSetCurrentKProcess(u32 ptr)
{
    kernelArgs[0] = 4;
    kernelArgs[1] = ptr;
    svcBackdoor(currentBackdoorHandler);
}

u32 kGetCurrentKProcess()
{
    kernelArgs[0] = 3;
    svcBackdoor(currentBackdoorHandler);
    return kernelArgs[1];
}

u32 kGetKProcessByHandle(u32 handle)
{
    kernelArgs[0] = 2;
    kernelArgs[1] = handle;
    svcBackdoor(currentBackdoorHandler);
    return kernelArgs[1];
}
