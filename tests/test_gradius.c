#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef int   (__cdecl *init_fn)(void);
typedef void  (__cdecl *uninit_fn)(void);
typedef char* (__cdecl *error_fn)(int);
typedef int   (__cdecl *version_fn)(void);
typedef int   (__cdecl *enabled_fn)(int);
typedef int   (__cdecl *status_fn)(int, int*);
typedef int   (__cdecl *read_fn)(int, char*, int, int);
typedef int   (__cdecl *write_fn)(int, char*, int, int);

static uint32_t crc32(const unsigned char* data, size_t length)
{
    uint32_t crc = 0xffffffffU;
    size_t i;
    for (i = 0; i < length; ++i) {
        int bit;
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320U & (uint32_t)-(int)(crc & 1));
        }
    }
    return ~crc;
}

int main(int argc, char** argv)
{
    HMODULE dll;
    init_fn init;
    uninit_fn uninit;
    error_fn error_text;
    version_fn version;
    enabled_fn enabled;
    status_fn get_status;
    read_fn read_memory;
    write_fn write_memory;
    int status[3] = {0, 0, 0};
    int slot = -1;
    unsigned char* block;
    int result;

    if (argc != 2) {
        fprintf(stderr, "usage: %s path-to-MSXGr.dll\n", argv[0]);
        return 2;
    }
    dll = LoadLibraryA(argv[1]);
    if (dll == NULL) {
        fprintf(stderr, "LoadLibrary failed: %lu\n", (unsigned long)GetLastError());
        return 3;
    }
#define LOAD(name, type) type name = (type)GetProcAddress(dll, "MSXGR_" #name)
    LOAD(Init, init_fn);
    LOAD(Uninit, uninit_fn);
    LOAD(Err2Str, error_fn);
    LOAD(GetVersion, version_fn);
    LOAD(IsSlotEnable, enabled_fn);
    LOAD(GetSlotStatus, status_fn);
    LOAD(ReadMemory, read_fn);
    LOAD(WriteMemory, write_fn);
#undef LOAD
    init = Init; uninit = Uninit; error_text = Err2Str; version = GetVersion;
    enabled = IsSlotEnable; get_status = GetSlotStatus; read_memory = ReadMemory;
    write_memory = WriteMemory;
    if (!init || !uninit || !error_text || !version || !enabled || !get_status || !read_memory || !write_memory) {
        fprintf(stderr, "one or more required exports are missing\n");
        FreeLibrary(dll);
        return 4;
    }
    result = init();
    if (result != 0) {
        fprintf(stderr, "MSXGR_Init failed: %s\n", error_text(result));
        FreeLibrary(dll);
        return 5;
    }
    printf("version=%08x", version());
    for (int i = 0; i < 16; ++i) {
        int active = enabled(i);
        if (active) slot = i;
        printf(" slot%d=%d", i, active);
    }
    printf("\n");
    if (slot < 0) { uninit(); FreeLibrary(dll); return 6; }
    result = get_status(slot, status);
    if (result != 0) {
        fprintf(stderr, "GetSlotStatus failed: %s\n", error_text(result));
        uninit(); FreeLibrary(dll); return 6;
    }
    printf("status=%02x %02x %02x\n", status[0], status[1], status[2]);
    block = (unsigned char*)malloc(0x4000);
    if (!block) { uninit(); FreeLibrary(dll); return 7; }
    result = read_memory(slot, (char*)block, 0x4000, 0x4000);
    if (result != 0) {
        fprintf(stderr, "ReadMemory failed: %s\n", error_text(result));
        free(block); uninit(); FreeLibrary(dll); return 8;
    }
    printf("first16=");
    for (int i = 0; i < 16; ++i) printf("%02x", block[i]);
    printf(" crc32=%08x\n", crc32(block, 0x4000));
    free(block);

    block = (unsigned char*)malloc(0x20000);
    if (!block) { uninit(); FreeLibrary(dll); return 9; }
    for (int bank = 0; bank < 16; bank += 2) {
        char value = (char)bank;
        if (write_memory(slot, &value, 0x8000, 1) != 0) {
            fprintf(stderr, "WriteMemory bank %d failed\n", bank);
            free(block); uninit(); FreeLibrary(dll); return 10;
        }
        value = (char)(bank + 1);
        if (write_memory(slot, &value, 0xa000, 1) != 0 ||
            read_memory(slot, (char*)block + bank * 0x2000, 0x8000, 0x4000) != 0) {
            fprintf(stderr, "bank pair %d-%d failed\n", bank, bank + 1);
            free(block); uninit(); FreeLibrary(dll); return 11;
        }
    }
    {
        char zero = 0;
        write_memory(slot, &zero, 0x8000, 1);
        write_memory(slot, &zero, 0xa000, 1);
    }
    printf("megarom_size=131072 crc32=%08x expected=4dfcc009\n", crc32(block, 0x20000));
    result = crc32(block, 0x20000) == 0x4dfcc009U ? 0 : 12;
    free(block);
    uninit();
    FreeLibrary(dll);
    return result;
}
