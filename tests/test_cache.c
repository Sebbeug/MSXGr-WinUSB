/* SPDX-License-Identifier: MIT
 * Deterministic fake USB device: this executable never opens physical hardware. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static WINUSB_SETUP_PACKET request;
static unsigned char bank, scc, status_bytes[3] = {1,255,0};
static int fail_write, fail_read, fail_control, zero_read, reads, writes, controls, frees;
static int present=1, product=0xac01, fail_policy, policies, scc_enabled;
static ULONG position, partial;
static ULONGLONG ticks=1000;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static ULONGLONG WINAPI fake_ticks(void) { return ticks; }
static HDEVINFO WINAPI fake_devices(const GUID* g,PCWSTR e,HWND w,DWORD f) {
    (void)g;(void)e;(void)w;(void)f; return (HDEVINFO)(uintptr_t)2;
}
static BOOL WINAPI fake_enum(HDEVINFO d,PSP_DEVINFO_DATA i,const GUID* g,DWORD n,PSP_DEVICE_INTERFACE_DATA o) {
    (void)d;(void)i;(void)g;(void)o;
    if (!present || n) { SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; } return TRUE;
}
static BOOL WINAPI fake_detail(HDEVINFO d,PSP_DEVICE_INTERFACE_DATA i,PSP_DEVICE_INTERFACE_DETAIL_DATA_W o,DWORD n,PDWORD r,PSP_DEVINFO_DATA v) {
    (void)d;(void)i;(void)n;(void)v;
    if(r) *r=sizeof(*o)+32;
    if(!o) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    wcscpy(o->DevicePath,L"fake"); return TRUE;
}
static BOOL WINAPI fake_destroy(HDEVINFO d) { (void)d; return TRUE; }
static HANDLE WINAPI fake_file(LPCWSTR p,DWORD a,DWORD share,LPSECURITY_ATTRIBUTES sa,DWORD c,DWORD f,HANDLE t) {
    (void)p;(void)a;(void)sa;(void)c;(void)f;(void)t; CHECK(share==0); return (HANDLE)(uintptr_t)3;
}
static BOOL WINAPI fake_close(HANDLE h) { (void)h; return TRUE; }
static BOOL WINAPI fake_init(HANDLE h,PWINUSB_INTERFACE_HANDLE u) { (void)h; *u=(void*)(uintptr_t)1; return TRUE; }
static BOOL WINAPI fake_free(WINUSB_INTERFACE_HANDLE u) { (void)u; ++frees; return TRUE; }
static BOOL WINAPI fake_descriptor(WINUSB_INTERFACE_HANDLE u,UCHAR t,UCHAR i,USHORT l,PUCHAR b,ULONG n,PULONG out) {
    (void)u;(void)t;(void)i;(void)l; CHECK(n==sizeof(USB_DEVICE_DESCRIPTOR));
    USB_DEVICE_DESCRIPTOR d={0}; d.idVendor=0x1125; d.idProduct=(USHORT)product;
    memcpy(b,&d,sizeof(d)); *out=sizeof(d); return TRUE;
}
static BOOL WINAPI fake_policy(WINUSB_INTERFACE_HANDLE u,UCHAR p,ULONG t,ULONG n,PVOID b) {
    (void)u;(void)b; ++policies;
    if(t==AUTO_CLEAR_STALL) CHECK(p==0x82 && n==sizeof(UCHAR));
    else CHECK(t==PIPE_TRANSFER_TIMEOUT && n==sizeof(ULONG));
    if(fail_policy) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; } return TRUE;
}
static BOOL WINAPI fake_control(WINUSB_INTERFACE_HANDLE h,WINUSB_SETUP_PACKET p,PUCHAR b,ULONG n,PULONG out,LPOVERLAPPED o) {
    (void)h;(void)b;(void)o; CHECK(n==0 && p.RequestType==0xc0 && p.Length==0);
    ++controls; request=p; position=0; *out=0;
    if(fail_control) { SetLastError(ERROR_DEVICE_NOT_CONNECTED); return FALSE; } return TRUE;
}
static BOOL WINAPI fake_read(WINUSB_INTERFACE_HANDLE h,UCHAR pipe,PUCHAR b,ULONG n,PULONG out,LPOVERLAPPED o) {
    (void)h;(void)o; CHECK(pipe==0x82); ++reads;
    if(fail_read) { SetLastError(ERROR_SEM_TIMEOUT); return FALSE; }
    if(zero_read) { *out=0; return TRUE; }
    if(partial && n>partial) n=partial;
    for(ULONG i=0;i<n;i++) {
        ULONG a=request.Value+position+i;
        b[i]= request.Request==1 ? status_bytes[position+i] :
              (scc_enabled && ((a>=0x9800 && a<0xa000)||(a>=0xb800 && a<0xc000))) ? scc : bank;
    }
    position+=n; *out=n; return TRUE;
}
static BOOL WINAPI fake_write(WINUSB_INTERFACE_HANDLE h,UCHAR pipe,PUCHAR b,ULONG n,PULONG out,LPOVERLAPPED o) {
    (void)h;(void)o; CHECK(pipe==0x02); ++writes;
    if(n) bank=b[n-1];
    *out=n;
    if(fail_write) { SetLastError(ERROR_SEM_TIMEOUT); return FALSE; } return TRUE;
}
#define GetTickCount64 fake_ticks
#define SetupDiGetClassDevsW fake_devices
#define SetupDiEnumDeviceInterfaces fake_enum
#define SetupDiGetDeviceInterfaceDetailW fake_detail
#define SetupDiDestroyDeviceInfoList fake_destroy
#define CreateFileW fake_file
#define CloseHandle fake_close
#define WinUsb_Initialize fake_init
#define WinUsb_Free fake_free
#define WinUsb_GetDescriptor fake_descriptor
#define WinUsb_SetPipePolicy fake_policy
#define WinUsb_ControlTransfer fake_control
#define WinUsb_ReadPipe fake_read
#define WinUsb_WritePipe fake_write
#include "../src/msxgr_compat.c"
static void reset(int profile) {
    MSXGR_Uninit(); bank=0;scc=0;fail_write=fail_read=fail_control=zero_read=0;
    reads=writes=controls=frees=policies=0; partial=0; present=1;product=0xac01;fail_policy=0;scc_enabled=0;
    status_bytes[0]=1;status_bytes[1]=255;status_bytes[2]=0;
    CHECK(MSXGR_SetCacheProfile(profile)==0); CHECK(MSXGR_Init()==0);
}
static void put(int address,char value) { CHECK(MSXGR_WriteMemory(0,&value,address,1)==0); }
static unsigned char get(int address) { char b; CHECK(MSXGR_ReadMemory(0,&b,address,1)==0); return (unsigned char)b; }
static DWORD WINAPI thread_error(LPVOID p) {
    (void)p; CHECK(MSXGR_GetSlotStatus(-1,NULL)!=0); CHECK(strstr(MSXGR_Err2Str(1),"Invalid")); return 0;
}
int main(void) {
    char b[64],v=2; int before, error;
    const int profiles[]={MSXGR_CACHE_KONAMI,MSXGR_CACHE_KONAMISCC,MSXGR_CACHE_ASCII8,MSXGR_CACHE_ASCII16};
    const int aliases[]={0x6000,0x5000,0x6000,0x6000};
    for(int i=0;i<4;i++) {
        reset(profiles[i]); int a=aliases[i];
        put(a,1);put(a+1,2); CHECK(get(0x6000)==2);
        put(a,3);put(a,1); CHECK(get(0x6000)==1);
        put(a,2); before=reads; CHECK(get(0x6000)==2); CHECK(reads==before);
    }
    puts("PASS canonical mapper aliases and cached bank revisit (4 profiles)");
    reset(MSXGR_CACHE_KONAMISCC); scc_enabled=1;scc=17;
    CHECK(get(0x8000)==0); CHECK(request.Value==0x8000 && request.Index==0x1800);
    CHECK(MSXGR_ReadMemory(0,b,0x97f0,32)==0); scc=34;
    CHECK(MSXGR_ReadMemory(0,b,0x97f0,32)==0); CHECK(b[16]==34);
    scc=51; CHECK(get(0x9900)==51); CHECK(get(0xbf00)==51);
    CHECK(MSXGR_ReadMemory(0,b,0xb7f0,32)==0); CHECK(b[16]==51);
    puts("PASS SCC boundaries, mirrors and prefetch exclusion");
    reset(MSXGR_CACHE_ASCII16); CHECK(get(0x9900)==0); before=reads;
    CHECK(get(0x9900)==0 && reads==before);
    reset(MSXGR_CACHE_KONAMI); put(0x9800,1); CHECK(get(0x8000)==1);
    put(0x9800,2);CHECK(get(0x8000)==2);
    puts("PASS SCC rules do not leak into other mappers");
    reset(MSXGR_CACHE_NONE); CHECK(get(0x6000)==0);before=reads;bank=7;
    CHECK(get(0x6000)==7 && reads==before+1 && request.Index==1);
    CHECK(MSXGR_SetCacheProfile(MSXGR_CACHE_LINEAR)==0); CHECK(get(0x6000)==7);
    bank=8;CHECK(MSXGR_ReadMemoryDirect(0,b,0x6000,1)==0 && b[0]==8);
    bank=9;CHECK(MSXGR_ReadMemoryDirect(0,b,0x6000,1)==0 && b[0]==9);
    CHECK(get(0x6000)==9);
    puts("PASS raw reads and explicit direct API, no prefetch");
    reset(MSXGR_CACHE_KONAMI);put(0x6000,1);CHECK(get(0x6000)==1);
    fail_write=1;before=writes;error=MSXGR_WriteMemory(0,&v,0x6000,1);
    CHECK(error==ERROR_SEM_TIMEOUT && bank==2 && writes==before+1);
    CHECK(g_usb==NULL && !g_last_status_valid && frees==1);
    CHECK(strstr(MSXGR_Err2Str(error),"WritePipe"));
    fail_write=0;CHECK(get(0x6000)==2); CHECK(strstr(MSXGR_Err2Str(error),"WritePipe"));
    puts("PASS applied write timeout, no replay, close and rediscovery");
    reset(MSXGR_CACHE_LINEAR);CHECK(get(0x6000)==0);
    CHECK(MSXGR_WriteIO(0,&v,0xfc,1)==0); CHECK(get(0x6000)==2);
    puts("PASS IO invalidates memory cache");
    reset(MSXGR_CACHE_LINEAR);CHECK(get(0x6000)==0);
    ticks+=250; fail_control=1; CHECK(MSXGR_ReadMemory(0,b,0x6000,1)==ERROR_DEVICE_NOT_CONNECTED);
    CHECK(g_usb==NULL); fail_control=0;bank=3;CHECK(get(0x6000)==3);
    fail_control=1;CHECK(MSXGR_Init()==ERROR_DEVICE_NOT_CONNECTED);CHECK(g_usb==NULL);
    fail_control=0;CHECK(MSXGR_Init()==0);
    puts("PASS status poll detects disconnect despite cached data and Init probes handle");
    reset(MSXGR_CACHE_LINEAR);CHECK(get(0x6000)==0);
    status_bytes[1]=0;ticks+=250;CHECK(MSXGR_GetSlotStatus(0,(int[3]){0})==0);
    status_bytes[1]=255;bank=4;ticks+=250;CHECK(get(0x6000)==4);
    status_bytes[2]=1;ticks+=250;CHECK(!MSXGR_IsSlotEnable(0));CHECK(MSXGR_IsSlotEnable(1));
    puts("PASS cartridge status and DIP changes invalidate cache");
    reset(MSXGR_CACHE_NONE);fail_read=1;CHECK(MSXGR_ReadMemory(0,b,0x4000,1)==ERROR_SEM_TIMEOUT);CHECK(g_usb==NULL);
    reset(MSXGR_CACHE_NONE);zero_read=1;CHECK(MSXGR_ReadMemory(0,b,0x4000,1)!=0);CHECK(g_usb==NULL);
    reset(MSXGR_CACHE_NONE);partial=3;bank=5;CHECK(MSXGR_ReadMemory(0,b,0x4000,32)==0);
    for(int i=0;i<32;i++) CHECK(b[i]==5);
    before=controls;CHECK(MSXGR_ReadMemory(0,b,0xffff,2)!=0);CHECK(MSXGR_WriteMemory(0,b,-1,1)!=0);
    CHECK(MSXGR_ReadMemory(0,b,0,INT_MAX)!=0);CHECK(controls==before);
    puts("PASS read failure, zero length USB, partial transfers, argument bounds");
    reset(MSXGR_CACHE_NONE);MSXGR_Uninit();product=0xac02;CHECK(MSXGR_Init()==0 && policies==6);
    MSXGR_Uninit();fail_policy=1;CHECK(MSXGR_Init()==ERROR_INVALID_PARAMETER && g_usb==NULL);
    fail_policy=0;product=0xffff;CHECK(MSXGR_Init()!=0 && g_usb==NULL);
    puts("PASS AC02 discovery, foreign PID rejection, pipe policy errors");
    reset(MSXGR_CACHE_NONE);fail_text("parent context");
    HANDLE thread=CreateThread(NULL,0,thread_error,NULL,0,NULL);CHECK(thread!=NULL);
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
    #undef CloseHandle
    CloseHandle(thread);
    CHECK(strstr(MSXGR_Err2Str(1),"parent context"));CHECK(!strcmp(MSXGR_Err2Str(0),"No error"));
    CHECK(!strstr(MSXGR_Err2Str(ERROR_FILE_NOT_FOUND),"parent context"));
    puts("PASS thread-local errors and Err2Str argument");
    MSXGR_Uninit();SetEnvironmentVariableA("MSXGR_MAPPER","ascii16");
    CHECK(MSXGR_Init()==0 && g_profile==MSXGR_CACHE_ASCII16);
    MSXGR_Uninit();SetEnvironmentVariableA("MSXGR_MAPPER","typo");CHECK(MSXGR_Init()!=0);
    SetEnvironmentVariableA("MSXGR_MAPPER",NULL);CHECK(MSXGR_Init()==0 && g_profile==MSXGR_CACHE_NONE);
    MSXGR_Uninit(); puts("PASS environment profiles and conservative default");
    puts("ALL TESTS PASSED (simulated hardware)"); return 0;
}
