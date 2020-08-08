#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include "UsbRomDrv.h"
#include "Amldbglog.h"
#include "AmlTime.h"
#include "defs.h"

#pragma warning(disable: 4100) // unreferenced formal parameter

namespace AmlUsbWriteLargeMem {
    int WriteSeqNum = 0;

    int AmlUsbWriteLargeMem (AmlUsbRomRW *rom) {
        if (ValidParamDWORD(&rom->bufferLen) != 1) {
            return -1;
        }
        if (ValidParamHANDLE((void **)&rom->device) != 1) {
            return -2;
        }
        if (ValidParamVOID(rom->buffer) != 1) {
            return -3;
        }
        struct AmlUsbDrv drv = {};
        if (OpenUsbDevice(&drv) != 1) {
            return -4;
        }

        unsigned int bufferPtr = 0;
        int startTransfer = -1;
        int transferErrorCnt = 0;
        int retry = 0;
        unsigned short checksum = ::checksum((unsigned short *)rom->buffer, rom->bufferLen);
        while (true) {
            unsigned int bufferRemain = rom->bufferLen;
            if (++retry == 4) {
                break;
            }
            while (bufferRemain > 0) {
                unsigned int transferSize = min(bufferRemain, 0x1000u);
                if (startTransfer == -1) {
                    unsigned int maxAllowedSize = min(bufferRemain, 0x10000u);
                    if (WriteLargeMemCMD(&drv, rom->address, maxAllowedSize, transferSize, checksum,
                        AmlUsbWriteLargeMem::WriteSeqNum) == 0) {
                        break;
                    }
                    startTransfer = maxAllowedSize;
                }
                ++AmlUsbWriteLargeMem::WriteSeqNum;
                int actual_len = write_bulk_usb(&drv, (char *)&rom->buffer[bufferPtr],
                    transferSize);
                if (actual_len) {
                    transferErrorCnt = 0;
                } else if (++transferErrorCnt > 5) {
                    goto finish;
                }
                if (actual_len == -1) {
                    goto finish;
                }
                bufferPtr += actual_len;
                bufferRemain -= actual_len;
            }
            if (bufferRemain == 0) {
                break;
            }
        }

    finish:
        CloseUsbDevice(&drv);
        *rom->pDataSize = bufferPtr;
        return rom->bufferLen == bufferPtr ? 0 : -6;
    }

}

namespace AmlUsbReadLargeMem {
    int ReadSeqNum = 0;

    int AmlUsbReadLargeMem (AmlUsbRomRW *rom) {
        if (ValidParamDWORD(&rom->bufferLen) != 1) {
            return -1;
        }
        if (ValidParamHANDLE((void **)&rom->device) != 1) {
            return -2;
        }
        if (ValidParamVOID(rom->buffer) != 1) {
            return -3;
        }
        ++AmlUsbReadLargeMem::ReadSeqNum;
        struct AmlUsbDrv drv = {};
        if (OpenUsbDevice(&drv) != 1) {
            return -4;
        }
        unsigned int bufferPtr = 0;
        int startTransfer = -1;
        int transferErrorCnt = 0;
        int retry = 0;
        unsigned short checksum = ::checksum((unsigned short *)rom->buffer, rom->bufferLen);
        while (true) {
            if (++retry == 4) {
                break;
            }
            unsigned int bufferRemain = rom->bufferLen;
            while (bufferRemain > 0) {
                unsigned int transferSize = min(bufferRemain, 0x10000u);
                if (startTransfer == -1) {
                    if (ReadLargeMemCMD(&drv, rom->address, rom->bufferLen,
                        transferSize >= 0x1000 ? 0x1000 : min(transferSize, 0x200u),
                        checksum, AmlUsbReadLargeMem::ReadSeqNum) == 0) {
                        break;
                    }
                    startTransfer = transferSize;
                }
                int actual_len = read_bulk_usb(&drv, (char *)&rom->buffer[bufferPtr],
                    transferSize);
                if (actual_len) {
                    transferErrorCnt = 0;
                } else if (++transferErrorCnt > 5) {
                    goto finish;
                }
                if (actual_len == -1) {
                    goto finish;
                }
                bufferPtr += actual_len;
                bufferRemain -= actual_len;
            }
            if (bufferRemain == 0) {
                break;
            }
        }

    finish:
        CloseUsbDevice(&drv);
        *rom->pDataSize = bufferPtr;
        return rom->bufferLen == bufferPtr ? 0 : -6;
    }

}

int AmlUsbReadMemCtr (AmlUsbRomRW *rom) {
    struct AmlUsbDrv drv = {};
    if (OpenUsbDevice(&drv) == 0) {
        return 0;
    }

    unsigned int bufferRemain = rom->bufferLen;
    char *bufferPtr = rom->buffer;
    while (bufferRemain > 0) {
        unsigned int transferSize = min(rom->bufferLen, 64u);
        if (read_control_usb(&drv, rom->address, bufferPtr, transferSize) != 1) {
            break;
        }
        rom->address += transferSize;
        bufferPtr += transferSize;
        bufferRemain -= transferSize;
    }

    CloseUsbDevice(&drv);
    *rom->pDataSize = *(unsigned int *)bufferPtr - *(unsigned short *)(rom->buffer);
    return bufferRemain ? -1 : 0;
}

int AmlUsbWriteMemCtr (AmlUsbRomRW *rom) {
    struct AmlUsbDrv drv = {};
    if (OpenUsbDevice(&drv) == 0) {
        return 0;
    }

    unsigned int bufferRemain = rom->bufferLen;
    char *bufferPtr = rom->buffer;
    while (bufferRemain > 0) {
        unsigned int transferSize = min(rom->bufferLen, 64u);
        if (write_control_usb(&drv, rom->address, bufferPtr, transferSize) == 0) {
            break;
        }
        rom->address += transferSize;
        bufferPtr += transferSize;
        bufferRemain -= transferSize;
    }

    CloseUsbDevice(&drv);
    return bufferRemain == 0;
}

int AmlUsbRunBinCode (AmlUsbRomRW *rom) {
    struct AmlUsbDrv drv = {};
    aml_printf("AmlUsbRunBinCode:ram_addr=%08x\n", rom->address);
    if (OpenUsbDevice(&drv) == 0) {
        return -1;
    }

    int ret = usbDeviceIoControl(&drv, 0x8000200C, &rom->address, 4, nullptr, 0, nullptr,
        nullptr);
    CloseUsbDevice(&drv);
    return ret == 1 ? 0 : -2;
}

int AmlUsbIdentifyHost (AmlUsbRomRW *rom) {
    struct AmlUsbDrv drv = {};
    aml_printf("AmlUsbIdentifyHost\n");
    if (OpenUsbDevice(&drv) == 0) {
        return -1;
    }

    int ret = usbDeviceIoControl(&drv, 0x80002024, nullptr, 0, rom->buffer, rom->bufferLen,
        rom->pDataSize, nullptr);
    CloseUsbDevice(&drv);
    return ret == 1 ? 0 : -2;
}

int AmlUsbTplCmd (AmlUsbRomRW *rom) {
    struct AmlUsbDrv drv = {};
    aml_printf("AmlUsbTplCmd = %s ", rom->buffer);
    if (OpenUsbDevice(&drv) == 0) {
        return -1;
    }
    int ret = usbDeviceIoControl(&drv, 0x80002040, rom->buffer, rom->bufferLen, nullptr, 0,
        rom->pDataSize, nullptr);
    aml_printf("rettemp = %d buffer = %s\n", ret, rom->buffer);
    CloseUsbDevice(&drv);
    return ret == 1 ? 0 : -2;
}

int AmlUsbburn (struct usb_device *device, const char *filename, unsigned int address,
    const char *memType, int nBytes, size_t bulkTransferSize, int checksum) {
    if (!device) {
        return -15;
    }

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        return -25;
    }

    AmlUsbRomRW rom = {};
    rom.device = device,
        rom.address = address;
    char* buffer = (char *)malloc(bulkTransferSize);
    fseek(fp, 0, 2);
    int filePtr = 0;
    int ret = 0;
    size_t len = (size_t)ftell(fp);
    fseek(fp, 0, 0);
    while (len) {
        size_t transferSize = min(len, bulkTransferSize);
        fread(buffer, 1, transferSize, fp);
        rom.buffer = buffer;
        rom.bufferLen = (int)transferSize;
        unsigned int dataSize;
        rom.pDataSize = &dataSize;
        ret = AmlUsbBurnWrite(&rom, (char *)memType, nBytes, checksum);
        if (ret) {
            break;
        }
        len -= dataSize;
        rom.address += dataSize;
        filePtr += dataSize;
        fseek(fp, filePtr, 0);
    }
    if (buffer) {
        free(buffer);
    }
    if (fp) {
        fclose(fp);
    }
    return ret ? ret : filePtr;
}

int AmlUsbReadStatus (AmlUsbRomRW *rom) {
    struct AmlUsbDrv drv = {};
    aml_printf("AmlUsbReadStatus ");
    if (OpenUsbDevice(&drv) != 1) {
        return -1;
    }

    int retusb = usbDeviceIoControl(&drv, 0x80002044, rom->buffer, 4, rom->buffer,
        rom->bufferLen, rom->pDataSize, nullptr);
    aml_printf("retusb = %d\n", retusb);
    CloseUsbDevice(&drv);
    return retusb == 1 ? 0 : -2;
}

int AmlUsbReadStatusEx (AmlUsbRomRW *rom, unsigned int timeout) {
    struct AmlUsbDrv drv = {};
    aml_printf("AmlUsbReadStatus ");
    if (OpenUsbDevice(&drv) != 1) {
        return -1;
    }

    int retusb = usbDeviceIoControlEx(&drv, 0x80002044, rom->buffer, 4, rom->buffer,
        rom->bufferLen, rom->pDataSize, nullptr, timeout);
    aml_printf("retusb = %d\n", retusb);
    CloseUsbDevice(&drv);
    return retusb == 1 ? 0 : -2;
}

int AmlResetDev (AmlUsbRomRW *rom) {
    if (!rom->device) {
        return -1;
    }
    struct AmlUsbDrv drv = {};
    if (OpenUsbDevice(&drv) != 1) { return 2; }

    aml_printf("reset worldcup device\n");
    int result = ResetDev(&drv);
    CloseUsbDevice(&drv);
    return result;
}

int AmlSetFileCopyComplete (AmlUsbRomRW *rom) {
    (void)rom;
    aml_printf("%s L%d not implemented", "AmlSetFileCopyComplete", 596);
    return 0;
}

int AmlGetUpdateComplete (AmlUsbRomRW *rom) {
    (void)rom;
    aml_printf("%s L%d not implemented", "AmlGetUpdateComplete", 662);
    return 0;
}

int AmlSetFileCopyCompleteEx (AmlUsbRomRW *rom) {
    (void)rom;
    aml_printf("%s L%d not implemented", "AmlSetFileCopyCompleteEx", 730);
    return 0;
}

int AmlWriteMedia (AmlUsbRomRW *rom) {
    int result = 0;
    unsigned int checksum = 0;
    struct AmlUsbDrv drv = {};
    if (ValidParamDWORD(&rom->bufferLen) != 1) {
        return -1;
    }
    if (ValidParamHANDLE((void **)&rom->device) != 1) {
        return -2;
    }
    if (ValidParamVOID(rom->buffer) != 1) {
        return -3;
    }

    if (OpenUsbDevice(&drv) != 1) {
        aml_printf("Open device failed\n");
        return -4;
    }

    checksum = checksum_64K(rom->buffer, rom->bufferLen);

    for (int address = 0; address <= 2; ++address) {
        unsigned int want_write = min(rom->bufferLen, 0x10000u);
        unsigned int cmd = 16 * rom->address;
        if (WriteMediaCMD(&drv, address, rom->bufferLen, checksum, cmd, want_write, 5000) !=
            1) {
            aml_printf("Write media command %d failed\n", cmd);
            CloseUsbDevice(&drv);
            return -6;
        }
        unsigned int actual_len = 0;
        int ret = usbWriteFile(&drv, rom->buffer, want_write, &actual_len);
        if (ret != 1) {
            aml_printf("usbReadFile failed ret=%d", ret);
            CloseUsbDevice(&drv);
            return -7;
        }
        if (want_write != actual_len) {
            aml_printf("[AmlUsbRom]Err:");
            aml_printf("Want Write 0x%x, but actual_len 0x%x\n", want_write, actual_len);
            result = -797;
            break;
        }
        result = 0;
        unsigned char buf[512] = {};
        for (time_t startTime = get_tick_count(), curTime = startTime;
            curTime - startTime < 12 * 60 * 1000; curTime = get_tick_count()) {
            if (usbReadFile(&drv, buf, sizeof(buf), &actual_len) != 1) {
                aml_printf("[AmlUsbRom]Err:");
                aml_printf("usbReadFile failed ret=%d", 0);
                result = -809;
                break;
            }
            char strBusy[] = "Continue:32";
            size_t strLenBusy = strlen(strBusy);
            if (strLenBusy > actual_len) {
                aml_printf("[AmlUsbRom]Err:");
                aml_printf("return size %d < strLenBusy %d\n", actual_len, strLenBusy);
                result = -814;
                break;
            }
            if (strncmp((const char *)buf, strBusy, strLenBusy) != 0) {
                break;
            }
            usleep(500000);
        }
        if (!result) {
            result = strncmp((const char *)buf, "OK!!", 4);
            if (result) {
                continue;
            }
        }
        break;
    }

    CloseUsbDevice(&drv);
    return result;
}

int AmlReadMedia (AmlUsbRomRW *rom) {
    struct AmlUsbDrv drv = {};
//  unsigned char buf[200] = {};
    unsigned int len = rom->bufferLen;
    unsigned int read = 0;
    unsigned int checksum = 0;

    if (ValidParamDWORD(&rom->bufferLen) != 1) {
        return -1;
    }
    if (ValidParamHANDLE((void **)&rom->device) != 1) {
        return -2;
    }
    if (ValidParamVOID(rom->buffer) != 1) {
        return -3;
    }

    if (OpenUsbDevice(&drv) != 1) {
        aml_printf("Open device failed\n");
        return -4;
    }

    if (ReadMediaCMD(&drv, 0, len, checksum, 0, 0, 5000) != 1) {
        aml_printf("Read media command failed\n", 0);
        CloseUsbDevice(&drv);
        return -5;
    }

    memset(rom->buffer, 0, rom->bufferLen);
    int ret = usbReadFile(&drv, rom->buffer, len, &read);
    if (ret == 0) {
        aml_printf("usbReadFile failed ret=%d\n", ret);
        CloseUsbDevice(&drv);
        return -6;
    }

    if (len != read) {
        aml_printf("Want read %d bytes, actual len %d\n", rom->bufferLen, read);
        CloseUsbDevice(&drv);
        return -7;
    }

    *rom->pDataSize = read;
    CloseUsbDevice(&drv);
    return 0;
}

int AmlUsbBulkCmd (AmlUsbRomRW *rom) {
    AmlUsbDrv drv = {};
    drv.read_ep = 2;
    if (OpenUsbDevice(&drv) == 0) {
        aml_printf("[AmlUsbRom]Err:");
        aml_printf("FAil in OpenUsbDevice\n");
        return -924;
    }
    aml_printf("AmlUsbBulkCmd[%s]\n", rom->buffer);
    if (!usbDeviceIoControlEx(&drv, 0x80002050, rom->buffer, rom->bufferLen, nullptr, 0,
        rom->pDataSize, nullptr, 5000)) {
        aml_printf("[AmlUsbRom]Err:");
        aml_printf("rettemp = %d buffer = [%s]\n", 0, rom->buffer);
        CloseUsbDevice(&drv);
        return -2;
    }
    char buf[512] = {};
    bool success = true;
    for (time_t startTime = get_tick_count(), curTime = startTime;
        curTime - startTime < 20 * 60 * 1000; curTime = get_tick_count()) {
        int ret = read_bulk_usb(&drv, buf, sizeof(buf));
        char strBusy[] = "Continue:34";
        size_t strLenBusy = strlen(strBusy);
        if (strLenBusy > ret) {
            aml_printf("[AmlUsbRom]Err:", buf);
            aml_printf("return len=%d < strLenBusy %d\n", ret, strLenBusy);
            success = false;
            break;
        }
        if (strncmp(buf, strBusy, strLenBusy) != 0) {
            break;
        }
        usleep(3000000);
    }
    if (success) {
        success = strncmp(buf, "success", 7) == 0;
    }
    if (!success) {
        aml_printf("[AmlUsbRom]Err:");
        aml_printf("bulkInReply=[%s]\n", buf);
    }
    CloseUsbDevice(&drv);
    return success ? 0 : -2;
}

int AmlUsbCtrlWr (AmlUsbRomRW *rom) {
    AmlUsbDrv drv = {};
    drv.read_ep = 2;
    if (OpenUsbDevice(&drv) != 1) {
        return -1;
    }
    int ret = usbDeviceIoControlEx(&drv, 0x80002004, rom->buffer, rom->bufferLen, nullptr,
        0, rom->pDataSize, nullptr, 5000);
    CloseUsbDevice(&drv);
    return ret == 1 ? 0 : -2;
}

int ValidParamDWORD (unsigned int *a1) {
//  unsigned int v1 = *a1;
    return 1;
}

int ValidParamVOID (void *a1) {
//  char v1 = *(char *)a1;
    return 1;
}

int ValidParamHANDLE (void **a1) {
//  void *v1 = *a1;
    return 1;
}

unsigned char *itoa1 (int value, unsigned char *str) {
    unsigned char *result; // rax

    for (int i = 0; i <= 3; ++i) {
        result = &str[i];
        *result = (unsigned char)(value >> 8 * i);
    }
    return result;
}

int RWLargeMemCMD (AmlUsbDrv *drv, unsigned long address, int size, int bulkSize,
    int checksum, int seqNum, char readOrWrite) {
    unsigned int inDataLen = 0; // [rsp+38h] [rbp-98h]
    unsigned char buf[128] = {}; // [rsp+40h] [rbp-90h]

    itoa1(address, buf);
    itoa1(size, &buf[4]);
    itoa1(checksum, &buf[8]);
    itoa1(seqNum, &buf[12]);
    itoa1(bulkSize, &buf[64]);
    return usbDeviceIoControl(drv, readOrWrite ? 0x80002010 : 0x80002014, buf, sizeof(buf),
        nullptr, 0, &inDataLen, nullptr);
}

int RWMediaCMD (AmlUsbDrv *drv, int address, int size, int seqNum, int checksum,
    int bulkSize, char readOrWrite, unsigned int timeout) {
    // esi
    unsigned int inDataLen = 0; // [rsp+38h] [rbp-98h]
    unsigned char buf[128] = {}; // [rsp+40h] [rbp-90h]

    itoa1(address, buf);
    itoa1(size, &buf[4]);
    itoa1(checksum, &buf[8]);
    itoa1(seqNum, &buf[12]);
    itoa1(bulkSize, &buf[64]);
    return usbDeviceIoControlEx(drv, readOrWrite ? 0x8000204C : 0x80002048, buf,
        sizeof(buf), nullptr, 0, &inDataLen, nullptr, timeout);
}

int WriteLargeMemCMD (AmlUsbDrv *drv, unsigned long address, int size, int bulkSize,
    int checksum, int writeSeqNum) {
    return RWLargeMemCMD(drv, address, size, bulkSize, checksum, writeSeqNum, 0);
}

int ReadLargeMemCMD (AmlUsbDrv *drv, unsigned long address, int size, int bulkSize,
    int checksum, int readSeqNum) {
    return RWLargeMemCMD(drv, address, size, bulkSize, checksum, readSeqNum, 1);
}

int WriteMediaCMD (AmlUsbDrv *drv, int address, int size, int seqNum, int checksum,
    int bulkSize, unsigned int timeout) {
    return RWMediaCMD(drv, address, size, seqNum, checksum, bulkSize, 0, timeout);
}

int ReadMediaCMD (AmlUsbDrv *drv, int address, int size, int seqNum, int checksum,
    int bulkSize, unsigned int timeout) {
    return RWMediaCMD(drv, address, size, seqNum, checksum, bulkSize, 1, timeout);
}

int write_bulk_usb (AmlUsbDrv *drv, char *buf, unsigned int len) {
    int result;

    result = 0;
    if (usbWriteFile(drv, buf, len, (unsigned int *)&result) != 1) {
        result = -1;
    }
    return result;
}

int read_bulk_usb (AmlUsbDrv *drv, char *buf, unsigned int len) {
    int result;

    result = 0;
    if (usbReadFile(drv, buf, len, (unsigned int *)&result) != 1) {
        result = -1;
    }
    return result;
}

int fill_mem_usb (AmlUsbDrv *drv, char *buf) {
    return -1;
}

int rw_ctrl_usb (AmlUsbDrv *drv, unsigned long ctrl, char *buf, unsigned long len,
    int readOrWrite) {
    unsigned int read = 0;
    if (readOrWrite) {
        return usbDeviceIoControl(drv, 0x80002000, &ctrl, 4, buf, len, &read, nullptr);
    } else {
        unsigned char in_buf[64];
        itoa1(ctrl, in_buf);
        memcpy(&in_buf[4], buf, len);
        return usbDeviceIoControl(drv, 0x80002004, in_buf, len + 4, nullptr, 0, &read,
            nullptr);
    }
}

int write_control_usb (AmlUsbDrv *drv, unsigned long ctrl, char *buf, unsigned long len) {
    return rw_ctrl_usb(drv, ctrl, buf, len, 0);
}

int read_control_usb (AmlUsbDrv *drv, unsigned long ctrl, char *buf, unsigned long len) {
    return rw_ctrl_usb(drv, ctrl, buf, len, 1);
}

int read_usb_status (void *addr, char *buf, size_t len) {
    return -1;
}

unsigned short checksum_add (unsigned short *buf, int len, int noFlip) {
    unsigned int checksum = 0;

    for (; len > 1; len -= 2) {
        checksum += le16toh(*buf);
        ++buf;
    }
    if (len) {
        checksum += *(unsigned char *)buf;
    }
    checksum = (checksum >> 16) + (unsigned short)checksum;
    checksum = (checksum >> 16) + (unsigned short)checksum;
    if (!noFlip) {
        checksum = ~checksum;
    }
    return (unsigned short)checksum;
}

unsigned int checksum_64K (void *buf, int len) {
    unsigned int checksum = 0;

    // process an int every time
    for (int div = len >> 2; div >= 0; div--) {
        checksum += le32toh(*(unsigned int *)buf);
        buf = (char *)buf + 4;
    }
    switch (len & 3) {
    case 1:
        checksum += *(unsigned char *)buf;
        break;
    case 2:
        checksum += le16toh(*(unsigned short *)buf);
        break;
    case 3:
        checksum += le32toh(*(unsigned int *)buf) & 0xFFFFFF;
        break;
    default:
        break;
    }
    return checksum;
}

unsigned short originale_add (unsigned short *buf, int len) {
    return checksum_add(buf, len, 1);
}

unsigned short checksum (unsigned short *buf, int len) {
    return checksum_add(buf, len, 0);
}

int AmlUsbBurnWrite (AmlUsbRomRW *cmd, char *memType, unsigned long long nBytes,
    int checksum) {
    unsigned int oldDataSize = *cmd->pDataSize;

    int ret = AmlUsbWriteLargeMem::AmlUsbWriteLargeMem(cmd);
    if (ret) {
        return ret;
    }

    char buf[88] = {};
    SET_INT_AT(buf, 0, cmd->address);
    SET_INT_AT(buf, 4, 0);
    SET_INT_AT(buf, 8, checksum);
    SET_INT_AT(buf, 24, cmd->bufferLen);
    SET_INT_AT(buf, 28, checksum);
    SET_LONG_AT(buf, 32, nBytes);
    memcpy(&buf[48], memType, strlen(memType));
    SET_INT_AT(buf, 80, 0);
    cmd->bufferLen = 88;
    cmd->buffer = buf;
    ret = AmlUsbTplCmd(cmd);
    if (ret) {
        *cmd->pDataSize = 0;
    } else {
        *cmd->pDataSize = oldDataSize;
    }
    return ret;
}
