/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Sebbeug */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Stable interface GUID used by our WinUSB package and the current Zadig install. */
static const GUID GUID_DEVINTERFACE_MSX_GAME_READER =
    {0xd6caadcf, 0x5d01, 0x4b7f, {0xb6, 0x9d, 0x31, 0x33, 0x50, 0x78, 0x58, 0x7d}};

#define MSXGR_VID 0x1125
#define MSXGR_PID 0xAC01
#define MSXGR_EP_IN  0x82
#define MSXGR_EP_OUT 0x02
#define MSXGR_TIMEOUT_MS 5000UL
#define MSXGR_CHUNK_SIZE 0x4000
#define MSXGR_VERSION 0x01000008
#include "msxgr.h"
#define MSXGR_CACHE_PAGE_SIZE 0x2000
#define MSXGR_CACHE_ENTRY_COUNT 256

typedef struct MemoryCacheEntry {
    int valid;
    uint64_t mapper_state;
    unsigned short page;
    uint64_t last_use;
    unsigned char data[MSXGR_CACHE_PAGE_SIZE];
} MemoryCacheEntry;

static SRWLOCK g_lock = SRWLOCK_INIT;
static HANDLE g_device = INVALID_HANDLE_VALUE;
static WINUSB_INTERFACE_HANDLE g_usb = NULL;
static volatile LONG g_debug = 0;
static _Thread_local int g_last_error = 0;
static _Thread_local char g_error_text[256] = "No error";
static MemoryCacheEntry g_memory_cache[MSXGR_CACHE_ENTRY_COUNT];
static unsigned char g_banks[4], g_known;
static int g_profile = MSXGR_CACHE_NONE;
static int g_profile_initialized;
static ULONGLONG g_status_time;
static int refresh_status_locked(void);
static void close_reader_locked(void);
static uint64_t g_mapper_state = 0;
static uint64_t g_cache_clock = 0;
static unsigned char g_last_status[3];
static int g_last_status_valid = 0;

static void trace_message(const char* message)
{
    if (InterlockedCompareExchange(&g_debug, 0, 0)) {
        OutputDebugStringA("[MSXGrCompat] ");
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
    }
}

static int fail_with_win32(const char* operation)
{
    DWORD error = GetLastError();
    g_last_error = error ? (int)error : 1;
    _snprintf(g_error_text, sizeof(g_error_text) - 1,
              "%s failed (Win32 error %lu)", operation, (unsigned long)error);
    g_error_text[sizeof(g_error_text) - 1] = '\0';
    trace_message(g_error_text);
    return g_last_error;
}

static int fail_text(const char* text)
{
    g_last_error = 1;
    strncpy(g_error_text, text, sizeof(g_error_text) - 1);
    g_error_text[sizeof(g_error_text) - 1] = '\0';
    trace_message(g_error_text);
    return g_last_error;
}

static void reset_memory_cache_locked(void)
{
    memset(g_memory_cache, 0, sizeof(g_memory_cache));
    memset(g_banks, 0, sizeof(g_banks));
    g_known = 0;
    g_mapper_state = 0;
    g_cache_clock = 0;
}

static int initialize_profile_locked(void)
{
    static const char* names[] = {"none", "linear", "konami", "konamiscc", "ascii8", "ascii16"};
    char name[32];
    DWORD n;
    if (g_profile_initialized) return 0;
    n = GetEnvironmentVariableA("MSXGR_MAPPER", name, sizeof(name));
    g_profile = MSXGR_CACHE_NONE;
    if (n) {
        int i;
        if (n >= sizeof(name)) return fail_text("Invalid MSXGR_MAPPER profile");
        for (i = 0; i < 6; ++i) if (_stricmp(name, names[i]) == 0) break;
        if (i == 6) return fail_text("Invalid MSXGR_MAPPER profile");
        g_profile = i;
    }
    g_profile_initialized = 1;
    return 0;
}

/* Profiles describe plain ROM mappers only. SRAM, flash and SCC+ use NONE. */
static void record_memory_write_locked(int address,
                                       const unsigned char* buffer, int length)
{
    for (int i = 0; i < length; ++i) {
        int a = address + i, reg = -1;
        if (g_profile == MSXGR_CACHE_KONAMI && a >= 0x6000 && a < 0xc000)
            reg = (a - 0x4000) / 0x2000;
        if (g_profile == MSXGR_CACHE_KONAMISCC) {
            if (a >= 0x5000 && a < 0xb800 && (a & 0x1800) == 0x1000)
                reg = (a - 0x5000) / 0x2000;
            if (a >= 0x9800 && a < 0xa000 && (g_known & 4) && (g_banks[2] & 63) == 63)
                continue;
        }
        if (g_profile == MSXGR_CACHE_ASCII8 && a >= 0x6000 && a < 0x8000)
            reg = (a - 0x6000) / 0x800;
        if (g_profile == MSXGR_CACHE_ASCII16 && a >= 0x6000 && a < 0x7800 && !(a & 0x800))
            reg = (a - 0x6000) / 0x1000;
        if (reg < 0) { reset_memory_cache_locked(); continue; }
        g_banks[reg] = buffer[i];
        g_known |= (unsigned char)(1u << reg);
        g_mapper_state = (uint64_t)g_known << 32;
        for (int j = 0; j < 4; ++j) g_mapper_state |= (uint64_t)g_banks[j] << (j * 8);
    }
}

static int transport_error_locked(const char* operation)
{
    int result = fail_with_win32(operation);
    close_reader_locked();
    return result;
}

static int short_transfer_locked(void)
{
    int result = fail_text("WinUSB returned an invalid transfer length");
    close_reader_locked();
    return result;
}

static MemoryCacheEntry* find_memory_cache_locked(unsigned short page)
{
    int i;
    for (i = 0; i < MSXGR_CACHE_ENTRY_COUNT; ++i) {
        MemoryCacheEntry* entry = &g_memory_cache[i];
        if (entry->valid && entry->mapper_state == g_mapper_state &&
            entry->page == page) {
            entry->last_use = ++g_cache_clock;
            return entry;
        }
    }
    return NULL;
}

static MemoryCacheEntry* allocate_memory_cache_locked(void)
{
    int i;
    MemoryCacheEntry* oldest = &g_memory_cache[0];
    for (i = 0; i < MSXGR_CACHE_ENTRY_COUNT; ++i) {
        MemoryCacheEntry* entry = &g_memory_cache[i];
        if (!entry->valid) {
            return entry;
        }
        if (entry->last_use < oldest->last_use) {
            oldest = entry;
        }
    }
    return oldest;
}

static void close_reader_locked(void)
{
    if (g_usb != NULL) {
        WinUsb_Free(g_usb);
        g_usb = NULL;
    }
    if (g_device != INVALID_HANDLE_VALUE) {
        CloseHandle(g_device);
        g_device = INVALID_HANDLE_VALUE;
    }
    reset_memory_cache_locked();
    g_last_status_valid = 0;
}

static int open_reader_locked(void)
{
    HDEVINFO devices;
    int discovery_error = 0;
    SP_DEVICE_INTERFACE_DATA interface_data;
    DWORD index;

    if (g_usb != NULL) return refresh_status_locked();
    if (initialize_profile_locked()) return g_last_error;

    devices = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_MSX_GAME_READER, NULL, NULL,
                                   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        return fail_with_win32("SetupDiGetClassDevs");
    }

    memset(&interface_data, 0, sizeof(interface_data));
    interface_data.cbSize = sizeof(interface_data);

    for (index = 0; SetupDiEnumDeviceInterfaces(devices, NULL,
                                                &GUID_DEVINTERFACE_MSX_GAME_READER,
                                                index, &interface_data); ++index) {
        DWORD required = 0;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
        HANDLE file;
        WINUSB_INTERFACE_HANDLE usb = NULL;
        USB_DEVICE_DESCRIPTOR descriptor;
        ULONG transferred = 0;

        SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, NULL, 0, &required, NULL);
        if (required == 0) {
            continue;
        }
        detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(required);
        if (detail == NULL) {
            SetupDiDestroyDeviceInfoList(devices);
            return fail_text("Out of memory while enumerating USB devices");
        }
        memset(detail, 0, required);
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, detail,
                                               required, NULL, NULL)) {
            free(detail);
            continue;
        }

        file = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
        free(detail);
        if (file == INVALID_HANDLE_VALUE) {
            discovery_error = fail_with_win32("CreateFile");
            continue;
        }
        if (!WinUsb_Initialize(file, &usb)) {
            discovery_error = fail_with_win32("WinUsb_Initialize");
            CloseHandle(file);
            continue;
        }
        memset(&descriptor, 0, sizeof(descriptor));
        if (!WinUsb_GetDescriptor(usb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0,
                                  (PUCHAR)&descriptor, sizeof(descriptor), &transferred) ||
            transferred != sizeof(descriptor) ||
            descriptor.idVendor != MSXGR_VID ||
            (descriptor.idProduct != MSXGR_PID && descriptor.idProduct != 0xAC02)) {
            WinUsb_Free(usb);
            CloseHandle(file);
            continue;
        }

        g_device = file;
        g_usb = usb;
        {
            ULONG timeout = MSXGR_TIMEOUT_MS;
            UCHAR auto_clear = TRUE;
            if (!WinUsb_SetPipePolicy(g_usb, MSXGR_EP_IN, PIPE_TRANSFER_TIMEOUT,
                                      sizeof(timeout), &timeout) ||
                !WinUsb_SetPipePolicy(g_usb, MSXGR_EP_OUT, PIPE_TRANSFER_TIMEOUT,
                                      sizeof(timeout), &timeout) ||
                !WinUsb_SetPipePolicy(g_usb, MSXGR_EP_IN, AUTO_CLEAR_STALL,
                                      sizeof(auto_clear), &auto_clear)) {
                discovery_error = transport_error_locked("WinUsb_SetPipePolicy");
                continue;
            }
        }
        SetupDiDestroyDeviceInfoList(devices);
        reset_memory_cache_locked();
        g_last_status_valid = 0;
        trace_message("Game Reader opened");
        return refresh_status_locked();
    }

    SetupDiDestroyDeviceInfoList(devices);
    if (discovery_error != 0) return discovery_error;
    return fail_text("MSX Game Reader 1125:AC01/AC02 not found or busy (WinUSB interface GUID required)");
}

static int send_command_locked(UCHAR command, USHORT value, USHORT index)
{
    WINUSB_SETUP_PACKET packet;
    ULONG transferred = 0;

    if (g_usb == NULL) {
        return fail_text("MSX Game Reader is not initialized");
    }
    memset(&packet, 0, sizeof(packet));
    packet.RequestType = 0xC0;
    packet.Request = command;
    packet.Value = value;
    packet.Index = index;
    packet.Length = 0;
    if (!WinUsb_ControlTransfer(g_usb, packet, NULL, 0, &transferred, NULL)) {
        return transport_error_locked("WinUsb_ControlTransfer");
    }
    return 0;
}

static int read_pipe_locked(unsigned char* buffer, ULONG length)
{
    ULONG offset = 0;
    while (offset < length) {
        ULONG transferred = 0;
        if (!WinUsb_ReadPipe(g_usb, MSXGR_EP_IN, buffer + offset,
                             length - offset, &transferred, NULL)) {
            return transport_error_locked("WinUsb_ReadPipe");
        }
        if (transferred == 0 || transferred > length - offset) {
            return short_transfer_locked();
        }
        offset += transferred;
    }
    return 0;
}

static int write_pipe_locked(const unsigned char* buffer, ULONG length)
{
    ULONG offset = 0;
    while (offset < length) {
        ULONG transferred = 0;
        if (!WinUsb_WritePipe(g_usb, MSXGR_EP_OUT, (PUCHAR)(buffer + offset),
                              length - offset, &transferred, NULL)) {
            return transport_error_locked("WinUsb_WritePipe");
        }
        if (transferred == 0 || transferred > length - offset) {
            return short_transfer_locked();
        }
        offset += transferred;
    }
    return 0;
}

static int refresh_status_locked(void)
{
    unsigned char raw[3];
    int result = send_command_locked(1, 0, 3);
    if (result == 0) {
        result = read_pipe_locked(raw, sizeof(raw));
    }
    if (result == 0) {
        if (!g_last_status_valid || memcmp(g_last_status, raw, sizeof(raw)) != 0) {
            reset_memory_cache_locked();
            memcpy(g_last_status, raw, sizeof(raw));
            g_last_status_valid = 1;
        }
        g_status_time = GetTickCount64();
    }
    return result;
}

static int is_active_slot_locked(int slot)
{
    if (slot < 0 || slot >= 16) { fail_text("Invalid Game Reader slot"); return 0; }
    if (g_usb == NULL && open_reader_locked()) return 0;
    if ((!g_last_status_valid || GetTickCount64() - g_status_time >= 250) &&
        refresh_status_locked()) return 0;
    if (!g_last_status[0] || g_last_status[2] != (unsigned char)slot) {
        fail_text("Game Reader disabled or assigned to another logical slot"); return 0;
    }
    return 1;
}

static int read_device_locked(UCHAR command, unsigned char* buffer,
                              int address, int length)
{
    int remaining = length;
    int current = address;
    unsigned char* output = buffer;

    if (buffer == NULL || address < 0 || address > 0xffff || length < 0 ||
        length > 0x10000 || address + length > 0x10000) {
        return fail_text("Invalid read parameters");
    }
    while (remaining > 0) {
        int chunk = remaining > MSXGR_CHUNK_SIZE ? MSXGR_CHUNK_SIZE : remaining;
        int result = send_command_locked(command, (USHORT)current, (USHORT)chunk);
        if (result != 0) {
            return result;
        }
        result = read_pipe_locked(output, (ULONG)chunk);
        if (result != 0) {
            return result;
        }
        current += chunk;
        output += chunk;
        remaining -= chunk;
    }
    return 0;
}

static int write_device_locked(UCHAR command, const unsigned char* buffer,
                               int address, int length)
{
    int remaining = length;
    int current = address;
    const unsigned char* input = buffer;

    if (buffer == NULL || address < 0 || address > 0xffff || length < 0 ||
        length > 0x10000 || address + length > 0x10000) {
        return fail_text("Invalid write parameters");
    }
    while (remaining > 0) {
        int chunk = remaining > MSXGR_CHUNK_SIZE ? MSXGR_CHUNK_SIZE : remaining;
        int result = send_command_locked(command, (USHORT)current, (USHORT)chunk);
        if (result != 0) {
            return result;
        }
        result = write_pipe_locked(input, (ULONG)chunk);
        if (result != 0) {
            return result;
        }
        current += chunk;
        input += chunk;
        remaining -= chunk;
    }
    return 0;
}

static int read_memory_cached_locked(unsigned char* buffer, int address, int length)
{
    if (buffer == NULL || address < 0 || address > 0xffff || length < 0 ||
        length > 0x10000 || address + length > 0x10000)
        return fail_text("Invalid read parameters");
    if (g_profile == MSXGR_CACHE_NONE) return read_device_locked(2, buffer, address, length);
    while (length > 0) {
        int start = address & ~0x1fff, end = start + 0x2000;
        int direct = address < 0x4000 || address >= 0xc000;
        if (g_profile == MSXGR_CACHE_KONAMISCC && (start == 0x8000 || start == 0xa000)) {
            int boundary = start + 0x1800;
            if (address >= boundary) direct = 1;
            else end = boundary;
        }
        int count = length < end - address ? length : end - address;
        if (direct) {
            int result = read_device_locked(2, buffer, address, count);
            if (result) return result;
        } else {
            unsigned short page = (unsigned short)(start / MSXGR_CACHE_PAGE_SIZE);
            MemoryCacheEntry* entry = find_memory_cache_locked(page);
            if (!entry) {
                entry = allocate_memory_cache_locked();
                entry->valid = 0;
                int result = read_device_locked(2, entry->data, start, end - start);
                if (result) return result;
                entry->mapper_state = g_mapper_state;
                entry->page = page;
                entry->last_use = ++g_cache_clock;
                entry->valid = 1;
            }
            memcpy(buffer, entry->data + address - start, (size_t)count);
        }
        address += count; buffer += count; length -= count;
    }
    return 0;
}

int __cdecl MSXGR_SetCacheProfile(int profile)
{
    if (profile < MSXGR_CACHE_NONE || profile > MSXGR_CACHE_ASCII16)
        return fail_text("Invalid cache profile");
    AcquireSRWLockExclusive(&g_lock);
    reset_memory_cache_locked();
    g_profile = profile;
    g_profile_initialized = 1;
    ReleaseSRWLockExclusive(&g_lock);
    return 0;
}

int __cdecl MSXGR_ReadMemoryDirect(int slot, char* buffer, int address, int length)
{
    int result;
    AcquireSRWLockExclusive(&g_lock);
    reset_memory_cache_locked();
    result = is_active_slot_locked(slot) ?
        read_device_locked(2, (unsigned char*)buffer, address, length) : g_last_error;
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

int __cdecl MSXGR_Init(void)
{
    int result;
    AcquireSRWLockExclusive(&g_lock);
    result = open_reader_locked();
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

void __cdecl MSXGR_Uninit(void)
{
    AcquireSRWLockExclusive(&g_lock);
    close_reader_locked();
    g_profile_initialized = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

char* __cdecl MSXGR_Err2Str(int error)
{
    static _Thread_local char text[256];
    if (!error) return "No error";
    if (error == g_last_error) return g_error_text;
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        NULL, (DWORD)error, 0, text, sizeof(text), NULL))
        snprintf(text, sizeof(text), "Error %d", error);
    return text;
}

int __cdecl MSXGR_GetVersion(void)
{
    return MSXGR_VERSION;
}

void __cdecl MSXGR_SetDebugMode(int level)
{
    InterlockedExchange(&g_debug, level != 0);
}

int __cdecl MSXGR_IsSlotEnable(int slot)
{
    int enabled;
    AcquireSRWLockExclusive(&g_lock);
    enabled = is_active_slot_locked(slot);
    ReleaseSRWLockExclusive(&g_lock);
    return enabled;
}

int __cdecl MSXGR_GetSlotStatus(int slot, int* status)
{
    unsigned char raw[3];
    int result;
    if (slot < 0 || slot >= 16 || status == NULL) {
        return fail_text("Invalid Game Reader slot or status buffer");
    }
    AcquireSRWLockExclusive(&g_lock);
    result = g_usb == NULL ? open_reader_locked() : refresh_status_locked();
    if (result == 0) {
        memcpy(raw, g_last_status, sizeof(raw));
        if (raw[2] != (unsigned char)slot) {
            result = fail_text("Game Reader is assigned to another logical slot");
        }
        else {
            status[0] = raw[0];
            status[1] = raw[1];
            status[2] = raw[2];
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

int __cdecl MSXGR_ReadMemory(int slot, char* buffer,
                                                   int address, int length)
{
    int result;
    AcquireSRWLockExclusive(&g_lock);
    result = is_active_slot_locked(slot) ?
             read_memory_cached_locked((unsigned char*)buffer, address, length) :
             g_last_error;
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

int __cdecl MSXGR_WriteMemory(int slot, char* buffer,
                                                    int address, int length)
{
    int result;
    AcquireSRWLockExclusive(&g_lock);
    result = is_active_slot_locked(slot) ?
             write_device_locked(3, (const unsigned char*)buffer, address, length) :
             g_last_error;
    if (result == 0) {
        record_memory_write_locked(address, (const unsigned char*)buffer, length);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

int __cdecl MSXGR_ReadIO(int slot, char* buffer,
                                               int address, int length)
{
    int result;
    AcquireSRWLockExclusive(&g_lock);
    result = is_active_slot_locked(slot) ?
             read_device_locked(4, (unsigned char*)buffer, address, length) :
             g_last_error;
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

int __cdecl MSXGR_WriteIO(int slot, char* buffer,
                                                int address, int length)
{
    int result;
    AcquireSRWLockExclusive(&g_lock);
    reset_memory_cache_locked();
    result = is_active_slot_locked(slot) ?
             write_device_locked(5, (const unsigned char*)buffer, address, length) :
             g_last_error;
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}
