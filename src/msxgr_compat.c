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
#define MSXGR_VERSION 0x01000007
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
static int g_debug = 0;
static int g_last_error = 0;
static char g_error_text[256] = "No error";
static MemoryCacheEntry g_memory_cache[MSXGR_CACHE_ENTRY_COUNT];
static unsigned char g_mapper_value[0x10000];
static unsigned char g_mapper_value_valid[0x10000 / 8];
static uint64_t g_mapper_state = 0;
static uint64_t g_cache_clock = 0;
static unsigned char g_last_status[3];
static int g_last_status_valid = 0;

static void trace_message(const char* message)
{
    if (g_debug) {
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

static void clear_error(void)
{
    g_last_error = 0;
    strcpy(g_error_text, "No error");
}

static uint64_t mapper_token(unsigned short address, unsigned char value)
{
    uint64_t token = ((uint64_t)address << 8) | value;
    token += UINT64_C(0x9e3779b97f4a7c15);
    token = (token ^ (token >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    token = (token ^ (token >> 27)) * UINT64_C(0x94d049bb133111eb);
    return token ^ (token >> 31);
}

static void reset_memory_cache_locked(void)
{
    memset(g_memory_cache, 0, sizeof(g_memory_cache));
    memset(g_mapper_value, 0, sizeof(g_mapper_value));
    memset(g_mapper_value_valid, 0, sizeof(g_mapper_value_valid));
    g_mapper_state = 0;
    g_cache_clock = 0;
}

static int is_scc_address(unsigned short address)
{
    return (address >= 0x9800 && address <= 0x98ff) ||
           (address >= 0xb800 && address <= 0xb8ff);
}

static int is_direct_memory_address(unsigned short address)
{
    return is_scc_address(address);
}

static int is_non_mapper_write(unsigned short address)
{
    return is_scc_address(address) || address == 0xbffe;
}

static void record_memory_write_locked(int address,
                                       const unsigned char* buffer, int length)
{
    int i;
    for (i = 0; i < length; ++i) {
        unsigned short current = (unsigned short)(address + i);
        unsigned int byte_index = current >> 3;
        unsigned char bit = (unsigned char)(1u << (current & 7));
        if (is_non_mapper_write(current)) {
            continue;
        }
        if (g_mapper_value_valid[byte_index] & bit) {
            g_mapper_state ^= mapper_token(current, g_mapper_value[current]);
        }
        else {
            g_mapper_value_valid[byte_index] |= bit;
        }
        g_mapper_value[current] = buffer[i];
        g_mapper_state ^= mapper_token(current, buffer[i]);
    }
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
    SP_DEVICE_INTERFACE_DATA interface_data;
    DWORD index;

    if (g_usb != NULL) {
        return 0;
    }
    clear_error();

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
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
        free(detail);
        if (file == INVALID_HANDLE_VALUE) {
            fail_with_win32("CreateFile");
            continue;
        }
        if (!WinUsb_Initialize(file, &usb)) {
            fail_with_win32("WinUsb_Initialize");
            CloseHandle(file);
            continue;
        }
        memset(&descriptor, 0, sizeof(descriptor));
        if (!WinUsb_GetDescriptor(usb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0,
                                  (PUCHAR)&descriptor, sizeof(descriptor), &transferred) ||
            transferred != sizeof(descriptor) ||
            descriptor.idVendor != MSXGR_VID || descriptor.idProduct != MSXGR_PID) {
            WinUsb_Free(usb);
            CloseHandle(file);
            continue;
        }

        g_device = file;
        g_usb = usb;
        {
            ULONG timeout = MSXGR_TIMEOUT_MS;
            BOOL auto_clear = TRUE;
            WinUsb_SetPipePolicy(g_usb, MSXGR_EP_IN, PIPE_TRANSFER_TIMEOUT,
                                 sizeof(timeout), &timeout);
            WinUsb_SetPipePolicy(g_usb, MSXGR_EP_OUT, PIPE_TRANSFER_TIMEOUT,
                                 sizeof(timeout), &timeout);
            WinUsb_SetPipePolicy(g_usb, MSXGR_EP_IN, AUTO_CLEAR_STALL,
                                 sizeof(auto_clear), &auto_clear);
            WinUsb_SetPipePolicy(g_usb, MSXGR_EP_OUT, AUTO_CLEAR_STALL,
                                 sizeof(auto_clear), &auto_clear);
        }
        SetupDiDestroyDeviceInfoList(devices);
        reset_memory_cache_locked();
        g_last_status_valid = 0;
        clear_error();
        trace_message("Game Reader 1125:AC01 opened");
        return 0;
    }

    SetupDiDestroyDeviceInfoList(devices);
    if (g_last_error != 0) return g_last_error;
    return fail_text("MSX Game Reader 1125:AC01 not found or busy");
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
        return fail_with_win32("WinUsb_ControlTransfer");
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
            return fail_with_win32("WinUsb_ReadPipe");
        }
        if (transferred == 0) {
            return fail_text("WinUSB returned a zero-length read");
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
            return fail_with_win32("WinUsb_WritePipe");
        }
        if (transferred == 0) {
            return fail_text("WinUSB returned a zero-length write");
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
        clear_error();
    }
    return result;
}

static int is_active_slot_locked(int slot)
{
    if (g_usb == NULL || slot < 0 || slot >= 16) {
        return 0;
    }
    if (!g_last_status_valid && refresh_status_locked() != 0) {
        return 0;
    }
    return g_last_status[0] != 0 && g_last_status[2] == (unsigned char)slot;
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
    clear_error();
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
    clear_error();
    return 0;
}

static int read_memory_cached_locked(unsigned char* buffer, int address, int length)
{
    int remaining = length;
    int current = address;
    unsigned char* output = buffer;

    if (buffer == NULL || address < 0 || address > 0xffff || length < 0 ||
        length > 0x10000 || address + length > 0x10000) {
        return fail_text("Invalid read parameters");
    }

    while (remaining > 0) {
        unsigned short page = (unsigned short)(current / MSXGR_CACHE_PAGE_SIZE);
        int page_address = page * MSXGR_CACHE_PAGE_SIZE;
        int page_offset = current - page_address;
        int available = MSXGR_CACHE_PAGE_SIZE - page_offset;
        int copy_length = remaining < available ? remaining : available;
        MemoryCacheEntry* entry;

        if (is_direct_memory_address((unsigned short)current)) {
            int direct_length = 1;
            if (is_scc_address((unsigned short)current)) {
                int scc_end = (current & 0xff00) + 0x100;
                direct_length = remaining < scc_end - current ?
                                remaining : scc_end - current;
            }
            int result = read_device_locked(2, output, current, direct_length);
            if (result != 0) return result;
            current += direct_length;
            output += direct_length;
            remaining -= direct_length;
            continue;
        }

        entry = find_memory_cache_locked(page);
        if (entry == NULL) {
            int result;
            entry = allocate_memory_cache_locked();
            entry->valid = 0;
            result = read_device_locked(2, entry->data, page_address,
                                        MSXGR_CACHE_PAGE_SIZE);
            if (result != 0) return result;
            entry->mapper_state = g_mapper_state;
            entry->page = page;
            entry->last_use = ++g_cache_clock;
            entry->valid = 1;
        }

        memcpy(output, entry->data + page_offset, (size_t)copy_length);
        current += copy_length;
        output += copy_length;
        remaining -= copy_length;
    }
    clear_error();
    return 0;
}

__declspec(dllexport) int __cdecl MSXGR_Init(void)
{
    int result;
    AcquireSRWLockExclusive(&g_lock);
    result = open_reader_locked();
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

__declspec(dllexport) void __cdecl MSXGR_Uninit(void)
{
    AcquireSRWLockExclusive(&g_lock);
    close_reader_locked();
    clear_error();
    ReleaseSRWLockExclusive(&g_lock);
}

__declspec(dllexport) char* __cdecl MSXGR_Err2Str(int error)
{
    (void)error;
    return g_error_text;
}

__declspec(dllexport) int __cdecl MSXGR_GetVersion(void)
{
    return MSXGR_VERSION;
}

__declspec(dllexport) void __cdecl MSXGR_SetDebugMode(int level)
{
    g_debug = level != 0;
}

__declspec(dllexport) int __cdecl MSXGR_IsSlotEnable(int slot)
{
    int enabled;
    AcquireSRWLockExclusive(&g_lock);
    enabled = is_active_slot_locked(slot);
    ReleaseSRWLockExclusive(&g_lock);
    return enabled;
}

__declspec(dllexport) int __cdecl MSXGR_GetSlotStatus(int slot, int* status)
{
    unsigned char raw[3];
    int result;
    if (slot < 0 || slot >= 16 || status == NULL) {
        return fail_text("Invalid Game Reader slot or status buffer");
    }
    AcquireSRWLockExclusive(&g_lock);
    result = refresh_status_locked();
    if (result == 0) {
        memcpy(raw, g_last_status, sizeof(raw));
        if (raw[2] != (unsigned char)slot) {
            result = fail_text("Game Reader is assigned to another logical slot");
        }
        else {
            status[0] = raw[0];
            status[1] = raw[1];
            status[2] = raw[2];
            clear_error();
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

__declspec(dllexport) int __cdecl MSXGR_ReadMemory(int slot, char* buffer,
                                                   int address, int length)
{
    int result;
    AcquireSRWLockExclusive(&g_lock);
    result = is_active_slot_locked(slot) ?
             read_memory_cached_locked((unsigned char*)buffer, address, length) :
             fail_text("Invalid Game Reader slot");
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

__declspec(dllexport) int __cdecl MSXGR_WriteMemory(int slot, char* buffer,
                                                    int address, int length)
{
    int result;
    AcquireSRWLockExclusive(&g_lock);
    result = is_active_slot_locked(slot) ?
             write_device_locked(3, (const unsigned char*)buffer, address, length) :
             fail_text("Invalid Game Reader slot");
    if (result == 0) {
        record_memory_write_locked(address, (const unsigned char*)buffer, length);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

__declspec(dllexport) int __cdecl MSXGR_ReadIO(int slot, char* buffer,
                                               int address, int length)
{
    int result;
    AcquireSRWLockExclusive(&g_lock);
    result = is_active_slot_locked(slot) ?
             read_device_locked(4, (unsigned char*)buffer, address, length) :
             fail_text("Invalid Game Reader slot");
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

__declspec(dllexport) int __cdecl MSXGR_WriteIO(int slot, char* buffer,
                                                int address, int length)
{
    int result;
    AcquireSRWLockExclusive(&g_lock);
    result = is_active_slot_locked(slot) ?
             write_device_locked(5, (const unsigned char*)buffer, address, length) :
             fail_text("Invalid Game Reader slot");
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}
