/* SPDX-License-Identifier: MIT */
#ifndef MSXGR_H
#define MSXGR_H
#ifdef __cplusplus
extern "C" {
#endif
enum MSXGR_CacheProfile {
    MSXGR_CACHE_NONE, MSXGR_CACHE_LINEAR, MSXGR_CACHE_KONAMI,
    MSXGR_CACHE_KONAMISCC, MSXGR_CACHE_ASCII8, MSXGR_CACHE_ASCII16
};
int __cdecl MSXGR_Init(void);
void __cdecl MSXGR_Uninit(void);
char* __cdecl MSXGR_Err2Str(int error);
int __cdecl MSXGR_GetVersion(void);
void __cdecl MSXGR_SetDebugMode(int level);
int __cdecl MSXGR_IsSlotEnable(int slot);
int __cdecl MSXGR_GetSlotStatus(int slot, int* status);
int __cdecl MSXGR_ReadMemory(int slot, char* buffer, int address, int length);
int __cdecl MSXGR_WriteMemory(int slot, char* buffer, int address, int length);
int __cdecl MSXGR_ReadIO(int slot, char* buffer, int address, int length);
int __cdecl MSXGR_WriteIO(int slot, char* buffer, int address, int length);
/* Extensions: direct never prefetches; profile applies to the single opened reader. */
int __cdecl MSXGR_ReadMemoryDirect(int slot, char* buffer, int address, int length);
int __cdecl MSXGR_SetCacheProfile(int profile);
#ifdef __cplusplus
}
#endif
#endif
