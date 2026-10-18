#include "dfu.h"
#include "update_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static fake_info_t info;
fake_info_t *pInformation=&info;
static uint8_t flash[65536],header[1024],app[1028];
static unsigned writes,erases,resets,flags;
uint32_t test_read(uint32_t a) { uint32_t w;memcpy(&w,flash+a-0x08000000,4);return w; }
bool flashErasePage(u32 a) {assert(a>=UPDATE_BASE && a<UPDATE_END);memset(flash+a-0x08000000,255,1024);erases++;return true;}
bool flashWriteWord(u32 a,u32 w) {assert(a>=UPDATE_BASE && a<UPDATE_END);memcpy(flash+a-0x08000000,&w,4);writes++;return true;}
void flashLock(void){} void flashUnlock(void){} void setupFLASH(void){} void boot_feed(void){}
void bkp10Write(u16 v) {assert(v==RTC_BOOTLOADER_JUST_UPLOADED);flags++;}
void systemHardReset(void) {resets++;}
static void put(uint8_t *p,uint32_t v){memcpy(p,&v,4);}
static void init(void) {
    memset(&info,0,sizeof info);memset(flash,255,sizeof flash);
    info.Current_Configuration=1;info.Current_AlternateSetting=2;
    writes=erases=resets=flags=0;dfuInit();
    memset(app,0x65,sizeof app);put(app,0x20005000);put(app+4,UPDATE_BASE+0x101);
    memset(header,255,sizeof header);memcpy(header,"CRTCDFU1",8);
    put(header+8,1);put(header+12,UPDATE_BASE);put(header+16,sizeof app);
    put(header+20,update_crc(UINT32_MAX,app,sizeof app)^UINT32_MAX);
    put(header+24,update_crc(UINT32_MAX,header,24)^UINT32_MAX);
}
static bool req(u8 r,u8 type,u16 block,u16 size) {
    dfuNewSetup();info.USBbRequest=r;info.USBbmRequestType=type;info.USBwValue=block;
    info.USBwLength=size;info.Ctrl_Info.Usb_wOffset=0;return dfuUpdateByRequest();
}
static void download(unsigned block,uint8_t *data,unsigned n) {
    assert(req(DFU_DNLOAD,0x21,(u16)block,(u16)n));
    dfuCopyDNLOAD(0);
    for(unsigned i=0;i<n;i+=64) {
        unsigned part=n-i<64?n-i:64;info.Ctrl_Info.Usb_wOffset=(u16)i;
        u8 *dest=dfuCopyDNLOAD((u16)part);assert(dest);memcpy(dest,data+i,part);
    }
    info.Ctrl_Info.Usb_rLength=0;dfuStatusIn();
    assert(req(DFU_GETSTATUS,0xA1,0,6));assert(dfuGetState()==dfuDNLOAD_IDLE);
}
int main(void) {
    init();download(0,header,1024);download(1,app,1024);download(2,app+1024,4);
    assert(!update_vectors_valid(test_read(UPDATE_BASE),test_read(UPDATE_BASE+4),UPDATE_CAPACITY));
    assert(req(DFU_DNLOAD,0x21,3,0));assert(!flags);
    dfuStatusIn();assert(!flags && !update_vectors_valid(test_read(UPDATE_BASE),test_read(UPDATE_BASE+4),UPDATE_CAPACITY));
    assert(req(DFU_GETSTATUS,0xA1,0,6));assert(dfuGetState()==dfuMANIFEST_WAIT_RESET);
    assert(flags==1 && update_vectors_valid(test_read(UPDATE_BASE),test_read(UPDATE_BASE+4),UPDATE_CAPACITY));
    unsigned saved=writes;assert(req(DFU_GETSTATUS,0xA1,0,6));assert(writes==saved);
    dfuUpdateByReset();assert(resets==1);
    init();assert(req(DFU_DNLOAD,0x21,0,1024)); /* New SETUP before status IN aborts reception. */
    assert(req(DFU_CLRSTATUS,0x21,0,0));assert(!writes && !erases);
    init();download(0,header,1024);dfuUpdateByReset();assert(!resets);
    assert(!req(DFU_DNLOAD,0x21,1,1024));assert(!writes);
    init();info.Current_AlternateSetting=0;assert(!req(DFU_DNLOAD,0x21,0,1024));
    init();info.Current_AlternateSetting=1;assert(!req(DFU_DNLOAD,0x21,0,1024));
    init();assert(!req(DFU_DNLOAD,0xA1,0,1024));
    init();assert(!req(DFU_DNLOAD,0x21,0,1025));
    init();assert(!req(DFU_UPLOAD,0xA1,0,1024));
    init();assert(!req(DFU_GETSTATUS,0x21,0,6));
    init();info.USBwIndex=1;assert(!req(DFU_GETSTATUS,0xA1,0,6));
    init();info.Current_Configuration=0;assert(!req(DFU_DNLOAD,0x21,0,1024));
    init();assert(req(DFU_DNLOAD,0x21,0,1024));info.Ctrl_Info.Usb_wOffset=1020;
    assert(!dfuCopyDNLOAD(8));assert(!erases && !writes);
    puts("PASS production DFU: complete/final-status commit, query idempotence, interrupted SETUP/reset, alt/direction/size/interface/config/offset guards");
    return 0;
}
