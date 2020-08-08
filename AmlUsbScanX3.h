#pragma once

struct AmlscanX {
    const char *vendorName;
    struct usb_device **resultDevice;
    char *targetDevice;
    char **candidateDevices;
    int *nDevices;
};

int scanDevices(AmlscanX scan, const char *target);
int AmlScanUsbX3Devices(const char *vendorName, char **candidateDevices);
struct usb_device *AmlGetDeviceHandle(const char *vendorName, char *targetDevice);
int AmlGetMsNumber (char *a1, int a2, char *a3);
int AmlGetNeedDriver(unsigned short idVendor, unsigned short idProduct);
int AmlDisableSuspendForUsb(void);
