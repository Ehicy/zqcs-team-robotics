/* CRTC restricted DFU transport. LeafLabs/ST USB stack notices in vendor/. */
#include "hardware.h"
#include "dfu.h"
#include "update_core.h"

static DFUStatus status;
static update_t update;
static uint8_t buffer[UPDATE_PAGE] __attribute__((aligned(4)));
static uint16_t block_size, block_number;
static bool block_ready, receiving, manifest_pending;
volatile bool dfuBusy;

static bool erase_page(uint32_t a) { return flashErasePage(a); }
static bool write_word(uint32_t a, uint32_t w) { return flashWriteWord(a,w); }
static uint32_t read_word(uint32_t a) { return GET_REG(a); }
static const update_io_t io = {erase_page, write_word, read_word, boot_feed};

static bool error(u8 code) {
    flashLock(); status.bStatus=code; status.bState=dfuERROR;
    receiving=block_ready=manifest_pending=FALSE;
    return FALSE;
}
void dfuInit(void) {
    status=(DFUStatus){OK,0,0,0,dfuIDLE,0};
    update_init(&update); block_size=block_number=0;
    receiving=block_ready=manifest_pending=FALSE;
}
/* Called before parsing every fresh SETUP, not on deferred PAUSE processing. */
void dfuNewSetup(void) {
    if(receiving || manifest_pending) (void)error(errUSBR);
}
void dfuStatusIn(void) {
    if(receiving && pInformation->USBbRequest==DFU_DNLOAD) {
        receiving=FALSE;
        if(pInformation->Ctrl_Info.Usb_rLength) { (void)error(errNOTDONE); return; }
        block_ready=TRUE;
    } else if(manifest_pending && pInformation->USBbRequest==DFU_DNLOAD) {
        manifest_pending=FALSE;
        block_ready=TRUE;
    }
}
bool dfuUpdateByRequest(void) {
    u8 request=pInformation->USBbRequest;
    u16 length=pInformation->USBwLength;
    u16 value=pInformation->USBwValue;
    if(pInformation->USBwIndex || pInformation->Current_Configuration!=1) return error(errSTALLEDPKT);
    if(request==DFU_GETSTATUS || request==DFU_GETSTATE) {
        if(pInformation->USBbmRequestType!=0xA1 || value ||
           length!=(request==DFU_GETSTATUS?6:1)) return error(errSTALLEDPKT);
        if(request==DFU_GETSTATUS && status.bState==dfuDNLOAD_SYNC) {
            if(!block_ready) return error(errNOTDONE);
            block_ready=FALSE;
            if(!update_accept(&update,&io,block_number,buffer,block_size)) return error(errVERIFY);
            status.bState=dfuDNLOAD_IDLE;
        } else if(request==DFU_GETSTATUS && status.bState==dfuMANIFEST_SYNC) {
            if(!block_ready) return error(errNOTDONE);
            /* Commit during GETSTATUS SETUP, before preparing its IN response.
             * Heavy Flash/CRC work in the previous Status_IN callback can stall
             * the host's already-pending next SETUP on this legacy USB stack. */
            if(!update_finish(&update,&io,block_number)) return error(errVERIFY);
            block_ready=FALSE; flashLock();
            bkp10Write(RTC_BOOTLOADER_JUST_UPLOADED);
            status.bState=dfuMANIFEST_WAIT_RESET;
        }
        return TRUE;
    }
    if(pInformation->USBbmRequestType!=0x21) return error(errSTALLEDPKT);
    if(request==DFU_CLRSTATUS && !length && !value && status.bState==dfuERROR) {
        dfuInit(); return TRUE;
    }
    if(request==DFU_ABORT && !length && !value && status.bState!=dfuMANIFEST_WAIT_RESET) {
        flashLock(); dfuInit(); return TRUE;
    }
    if(request!=DFU_DNLOAD || pInformation->Current_AlternateSetting!=2 ||
       (status.bState!=dfuIDLE && status.bState!=dfuDNLOAD_IDLE) ||
       value!=update.next_block || length>UPDATE_PAGE) return error(errSTALLEDPKT);
    dfuBusy=TRUE;
    block_number=value; block_size=length; block_ready=FALSE;
    if(length) {
        if(status.bState==dfuIDLE && (value || length!=UPDATE_PAGE)) return error(errNOTDONE);
        if(!value) { setupFLASH(); flashUnlock(); }
        receiving=TRUE; status.bState=dfuDNLOAD_SYNC;
    } else {
        if(!update.active || update.received!=update.length) return error(errNOTDONE);
        manifest_pending=TRUE; status.bState=dfuMANIFEST_SYNC;
    }
    return TRUE;
}
void dfuUpdateByReset(void) {
    bool complete=update.complete;
    flashLock(); dfuInit();
    if(complete) systemHardReset();
}
u8 *dfuCopyState(u16 length) {
    if(!length) { pInformation->Ctrl_Info.Usb_wLength=1; return NULL; }
    return &status.bState;
}
u8 *dfuCopyStatus(u16 length) {
    if(!length) { pInformation->Ctrl_Info.Usb_wLength=6; return NULL; }
    return (u8 *)&status;
}
u8 *dfuCopyDNLOAD(u16 length) {
    u16 off=pInformation->Ctrl_Info.Usb_wOffset;
    if(off>block_size || length>block_size-off) { (void)error(errADDRESS); return NULL; }
    if(!length) { pInformation->Ctrl_Info.Usb_wLength=block_size-off; return NULL; }
    return buffer+off;
}
u8 *dfuCopyUPLOAD(u16 length) { (void)length; return NULL; } /* readback disabled */
u8 dfuGetState(void) { return status.bState; }
void dfuSetState(u8 state) { status.bState=state; }
bool dfuUploadStarted(void) { return dfuBusy; }
void dfuFinishUpload(void) { for(;;) boot_feed(); }
