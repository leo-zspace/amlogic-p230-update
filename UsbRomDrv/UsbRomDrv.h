#ifndef AML_USB_ROM_DRV_H
#define AML_USB_ROM_DRV_H

#include "../libusb/usb.h"
#include "../AmlLibusb/AmlLibusb.h"

struct AmlUsbRomRW {
  struct usb_device *device;
  unsigned int bufferLen;
  char *buffer;
  unsigned int *pDataSize;
  unsigned int address;
};


namespace AmlUsbWriteLargeMem {
  int WriteSeqNum = 0;
  int AmlUsbWriteLargeMem (AmlUsbRomRW *rom);
}

namespace AmlUsbReadLargeMem {
  int ReadSeqNum = 0;
  int AmlUsbReadLargeMem (AmlUsbRomRW *rom);
}

int AmlUsbReadMemCtr (AmlUsbRomRW *rom);
int AmlUsbWriteMemCtr (AmlUsbRomRW *rom);
int AmlUsbRunBinCode (AmlUsbRomRW *rom);
int AmlUsbIdentifyHost (AmlUsbRomRW *rom);
int AmlUsbTplCmd (AmlUsbRomRW *rom);
int AmlUsbburn (struct usb_device *device, const char *filename, unsigned int address,
                const char *memType, int nBytes, size_t bulkTransferSize, int checksum);
int AmlUsbReadStatus (AmlUsbRomRW *rom);
int AmlUsbReadStatusEx (AmlUsbRomRW *rom, unsigned int timeout);
int AmlResetDev (AmlUsbRomRW *rom);
int AmlSetFileCopyComplete (AmlUsbRomRW *rom);
int AmlGetUpdateComplete (AmlUsbRomRW *rom);
int AmlSetFileCopyCompleteEx (AmlUsbRomRW *rom);
int AmlWriteMedia (AmlUsbRomRW *rom);
int AmlReadMedia (AmlUsbRomRW *rom);
int AmlUsbBulkCmd (AmlUsbRomRW *rom);
int AmlUsbCtrlWr (AmlUsbRomRW *rom);

int ValidParamDWORD (unsigned int *);
int ValidParamVOID (void *);
int ValidParamHANDLE (void **);
unsigned char *itoa1 (int value, unsigned char *str);
int RWLargeMemCMD (AmlUsbDrv *drv, unsigned long address, int size, int bulkSize,
                   int checksum, int seqNum, char readOrWrite);
int RWMediaCMD (AmlUsbDrv *drv, int address, int size, int seqNum, int checksum,
                int bulkSize, char readOrWrite, unsigned int timeout);
int WriteLargeMemCMD (AmlUsbDrv *drv, unsigned long address, int size, int bulkSize,
                      int checksum, int writeSeqNum);
int ReadLargeMemCMD (AmlUsbDrv *drv, unsigned long address, int size, int bulkSize,
                     int checksum, int readSeqNum);
int WriteMediaCMD (AmlUsbDrv *drv, int address, int size, int seqNum, int checksum,
                   int bulkSize, unsigned int timeout);
int ReadMediaCMD (AmlUsbDrv *drv, int address, int size, int seqNum, int checksum,
                  int bulkSize, unsigned int timeout);
int write_bulk_usb (AmlUsbDrv *drv, char *buf, unsigned int len);
int read_bulk_usb (AmlUsbDrv *drv, char *buf, unsigned int len);
int fill_mem_usb (AmlUsbDrv *drv, char *buf);
int rw_ctrl_usb (AmlUsbDrv *drv, unsigned long ctrl, char *buf, unsigned long len,
                 int readOrWrite);
int write_control_usb (AmlUsbDrv *drv, unsigned long ctrl, char *buf, unsigned long len);
int read_control_usb (AmlUsbDrv *drv, unsigned long ctrl, char *buf, unsigned long len);
int read_usb_status (void *addr, char *buf, size_t len);
unsigned short checksum_add (unsigned short *buf, int len, int noFlip);
unsigned int checksum_64K (void *buf, int len);
unsigned short originale_add (unsigned short *buf, int len);
unsigned short checksum (unsigned short *buf, int len);
int AmlUsbBurnWrite (AmlUsbRomRW *cmd, char *memType, unsigned long long nBytes,
                     int checksum);

#endif  // AML_USB_ROM_DRV_H
