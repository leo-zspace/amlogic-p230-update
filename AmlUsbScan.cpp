#include <stdio.h>
#include <string.h>
#include "AmlUsbScan.h"
#include "Amldbglog.h"
#include "UsbRomDrv.h"
#include "AmlUsbScanX3.h"

char *DeviceName[8];

bool simg_probe (const unsigned char *buf, unsigned int len) {
    const unsigned char magic[] = { 0x3A, 0xFF, 0x26, 0xED };
    const unsigned char magic2[] = { 0x28, 0, 0x12, 0 };

    if (len < 0x1C) {
        return false;
    }
    if (strncmp((const char *)buf, (const char *)magic, 4) != 0) {
        return false;
    }
    if (*((short *)buf + 2) != 1 ||
        strncmp((const char *)buf + 8, (const char *)magic2, 4) != 0) {
        return false;
    }
    aml_printf("[update]sparse format detected\n");
    return true;
}

bool is_file_format_sparse (const char *filename) {
    FILE *fp = fopen(filename, "rb");

    if (fp == nullptr) {
        aml_printf("[update]ERR(L%d):", 68);
        aml_printf("Fail to open file in mode rb\n");
        return false;
    }

    auto buf = new unsigned char[0x2000];
    size_t len = fread(buf, 1, 0x2000, fp);
    fclose(fp);
    bool result = simg_probe(buf, (unsigned int)len);
    if (buf) {
        delete[] buf;
    }
    return result;
}

int aml_scan_init () {
    printf("aml_scan_usbdev");
    for (int i = 0; i <= 7; ++i) {
        DeviceName[i] = (char *)malloc(0x100);
    }
    return 0;
}

int aml_scan_close () {
    for (int i = 0; i <= 7; ++i) {
        free(DeviceName[i]);
        DeviceName[i] = nullptr;
    }
    return 0;
}

int aml_scan_usbdev (char **candidate_devices) {
    int result;

    for (int i = 0; i <= 7; ++i) {
        memset(DeviceName[i], 0, 0x100);
    }
    result = AmlScanUsbX3Devices("WorldCup Device", DeviceName);
    printf("--%i \n", result);
    if (result <= 0) {
        return 0;
    }

    for (int i = 0; i < result; ++i) {
        candidate_devices[i] = DeviceName[i];
    }
    return result;
}

int aml_send_command (void *device, char *mem_type, int retry, char *reply) {
    unsigned int data_len;
    char buffer[128] = {};
    memcpy(buffer, mem_type, strlen(mem_type));
    buffer[66] = 1;

    struct AmlUsbRomRW rom = { .device = (struct usb_device *) device,.bufferLen = 68,.buffer = buffer,.pDataSize = &data_len };

    if (AmlUsbTplCmd(&rom) == 0) {
        struct AmlUsbRomRW rom = { .device = (struct usb_device *) device,.bufferLen = 64,.buffer = reply };
        while (--retry > 0) {
            if (AmlUsbReadStatus(&rom) == 0) {
                printf("reply %s \n", reply);
                return 0;
            }
            usleep(100000);
        }
    }
    return -1;
}

//----- (0000000000407898) ----------------------------------------------------
int aml_get_sn (char *target_device, char *usid) {
    struct usb_device *device = AmlGetDeviceHandle("WorldCup Device", target_device);
    if (!device) {
        return -1;
    }

    char buffer[8] = {};
    struct AmlUsbRomRW rom = { .device = device,.bufferLen = 4,.buffer = buffer };

    if (AmlUsbIdentifyHost(&rom)) {
        device = nullptr;
        return -1;
    }

    if (buffer[3] != '\x10') {
        device = nullptr;
        return -1;
    }

    aml_send_command(device, "efuse write version", 50, usid);
    int ret = aml_send_command(device, "efuse read usid", 50, usid);
    printf("%s \n", usid);
    if (ret < 0) {
        device = nullptr;
        return -1;
    }

    char s[256];
    char tmp1[256];
    char tmp2[256];
    memset(s, 0, 8);
    memset(tmp1, 0, 8);
    memset(tmp2, 0, 8);
    memcpy(s, usid, strlen(usid));
    if (strcmp(tmp1, "success") != 0 || sscanf(s, "%[^:]:(%[^)])", tmp1, tmp2) != 2) {
        return 0;
    }

    memset(usid, 0, 8);
    memcpy(usid, tmp2, strlen(tmp2));
    device = nullptr;
    return strlen(tmp2);
}

//----- (0000000000407BA5) ----------------------------------------------------
int aml_set_sn (char *target_device, char *usid) {
    struct usb_device *device = AmlGetDeviceHandle("WorldCup Device", target_device);
    if (!device) {
        return -1;
    }

    char buffer[8];
    struct AmlUsbRomRW rom = { .device = device,.bufferLen = 4,.buffer = buffer };

    if (AmlUsbIdentifyHost(&rom)) {
        device = nullptr;
        return -1;
    }

    if (buffer[3] != '\x10') {
        device = nullptr;
        return -1;
    }

    char cmd[256];
    char src[256];
    sprintf(cmd, "efuse write usid %s", usid);
    aml_send_command(device, "efuse write version", 50, src);
    if (aml_send_command(device, cmd, 50, src) < 0) {
        device = nullptr;
        return -1;
    }

    char dest[256] = {};
    char tmp1[256] = {};
    char tmp2[256] = {};
    memcpy(dest, src, strlen(src));
    sscanf(dest, "%[^:]:(%[^)])", tmp1, tmp2);
    printf("tmp1=%s,tmp2=%s \n", tmp1, tmp2);
    if (strcmp(tmp1, "success") != 0) {
        return 0;
    }

    device = nullptr;
    return strlen(tmp2);
}