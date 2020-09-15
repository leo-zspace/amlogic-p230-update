#include "AmlLibusb.h"
#include "Amldbglog.h"
#include "defs.h"
#include "pozix.h"

#pragma warning(disable: 4100) // unreferenced formal parameter

usbio_file_t handle;

/*
 * Standard requests
 */
#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
/* 0x02 is reserved */
#define USB_REQ_SET_FEATURE         0x03
/* 0x04 is reserved */
#define USB_REQ_SET_ADDRESS         0x05
#define USB_REQ_GET_DESCRIPTOR      0x06
#define USB_REQ_SET_DESCRIPTOR      0x07
#define USB_REQ_GET_CONFIGURATION   0x08
#define USB_REQ_SET_CONFIGURATION   0x09
#define USB_REQ_GET_INTERFACE       0x0A
#define USB_REQ_SET_INTERFACE       0x0B
#define USB_REQ_SYNCH_FRAME         0x0C

#define USB_TYPE_STANDARD       (0x00 << 5)
#define USB_TYPE_CLASS          (0x01 << 5)
#define USB_TYPE_VENDOR         (0x02 << 5)
#define USB_TYPE_RESERVED       (0x03 << 5)

#define USB_RECIP_DEVICE        0x00
#define USB_RECIP_INTERFACE     0x01
#define USB_RECIP_ENDPOINT      0x02
#define USB_RECIP_OTHER         0x03

#define USB_ENDPOINT_IN  0x80
#define USB_ENDPOINT_OUT 0x00

int usb_control_msg(usbio_file_t file, int requesttype, int request, int value, int index, char *bytes, int size, int timeout) {
    return -1;
}

int usb_bulk_read(usbio_file_t file, int ep, char *bytes, int size, int timeout) {
    return -1;
}

int usb_bulk_write(usbio_file_t file, int ep, char *bytes, int size, int timeout) {
    return -1;
}

const char* usb_strerror() { return "usb error"; }

int IOCTL_READ_MEM_Handler(usbDevIoCtrl ctrl) {
    if (!ctrl.in_buf || !ctrl.out_buf || ctrl.in_len != 4 || !ctrl.out_len) {
        return 0;
    }
    int ret = usb_control_msg(handle, USB_ENDPOINT_IN | USB_TYPE_VENDOR,
        AML_LIBUSB_REQ_READ_CTRL, SHORT_AT(ctrl.in_buf, 2),
        SHORT_AT(ctrl.in_buf, 0), ctrl.out_buf,
        min(ctrl.out_len, 64u), 50000);
    *ctrl.p_in_data_size = (unsigned int)max(0, ret);
    if (ret < 0) {
        aml_printf("IOCTL_READ_MEM_Handler ret=%d error_msg=%s\n", ret, usb_strerror());
    }
    return ret >= 0;
}

int IOCTL_WRITE_MEM_Handler(usbDevIoCtrl ctrl) {
    if (!ctrl.in_buf || ctrl.in_len <= 2) {
        aml_printf("f(%s)L%d, ivlaid, in_buf=0x, in_len=%d\n", "AmlLibusb/AmlLibusb.cpp",
            47LL, ctrl.in_buf, ctrl.in_len);
        return 0;
    }
    int ret = usb_control_msg(handle, USB_ENDPOINT_OUT | USB_TYPE_VENDOR,
        AML_LIBUSB_REQ_WRITE_CTRL, SHORT_AT(ctrl.in_buf, 2),
        SHORT_AT(ctrl.in_buf, 0), ctrl.in_buf + 4,
        min(ctrl.in_len - 4, 64u), 50000);
    *ctrl.p_in_data_size = (unsigned int)max(0, ret);
    if (ret < 0) {
        aml_printf("IOCTL_WRITE_MEM_Handler ret=%d error_msg=%d\n", ret, usb_strerror());
    }
    return ret >= 0;
}

int IOCTL_READ_AUX_REG_Handler (usbDevIoCtrl ctrl) {
    return 0;
}

int IOCTL_WRITE_AUX_REG_Handler (usbDevIoCtrl ctrl) {
    return 0;
}

int IOCTL_FILL_MEM_Handler(usbDevIoCtrl ctrl) {
    return 0;
}

int IOCTL_MODIFY_MEM_Handler(usbDevIoCtrl ctrl) {
    return 0;
}

int IOCTL_RUN_IN_ADDR_Handler(usbDevIoCtrl ctrl) {
    if (!ctrl.in_buf || ctrl.in_len != 4) {
        return 0;
    }
    ctrl.in_buf[0] |= 0x10;
    usb_control_msg(handle, USB_ENDPOINT_OUT | USB_TYPE_VENDOR,
        AML_LIBUSB_REQ_RUN_ADDR, SHORT_AT(ctrl.in_buf, 2),
        SHORT_AT(ctrl.in_buf, 0), ctrl.in_buf, 4, 50000);
    return 1LL;
}

int IOCTL_DO_LARGE_MEM_Handler(usbDevIoCtrl ctrl, int readOrWrite) {
    if (!ctrl.in_buf || ctrl.in_len <= 0xF) {
        return 0;
    }
    int value = 0;
    if (ctrl.in_len > 32) {
        value = *((unsigned short *)ctrl.in_buf + 32);
    }
    if (value == 0 || value > 4096) {
        value = 4096;
    }
    uint64_t len = LONG_AT(ctrl.in_buf, 4);
    unsigned int index = (unsigned int)(value + len - 1) / value;
    int ret = usb_control_msg(handle, USB_ENDPOINT_OUT | USB_TYPE_VENDOR,
        readOrWrite ? AML_LIBUSB_REQ_READ_MEM
        : AML_LIBUSB_REQ_WRITE_MEM, value, index,
        ctrl.in_buf, 16, 50000);
    *ctrl.p_in_data_size = (unsigned int)max(0, ret);
    if (ret < 0) {
        aml_printf("[%s],value=%x,index=%x,len=%d,ret=%d error_msg=%s\n",
            readOrWrite ? "read" : "write", value, index, len, ret, usb_strerror());
    }
    return ret >= 0;
}

int IOCTL_IDENTIFY_HOST_Handler(usbDevIoCtrl ctrl) {
    if (ctrl.out_buf && ctrl.out_len <= 8) {
        int ret = usb_control_msg(handle, USB_ENDPOINT_IN | USB_TYPE_VENDOR,
            AML_LIBUSB_REQ_IDENTIFY, 0, 0, ctrl.out_buf, ctrl.out_len,
            5000);
        *ctrl.p_in_data_size = (unsigned int)max(0, ret);
        if (ret < 0) {
            aml_printf("IOCTL_IDENTIFY_HOST_Handler ret=%d error_msg=%s\n", ret,
                usb_strerror());
        }
        return ret >= 0;
    } else {
        aml_printf("identify,%p,%d(max 8)\n", ctrl.out_buf, ctrl.out_len);
        return 0;
    }
}

int IOCTL_TPL_CMD_Handler(usbDevIoCtrl ctrl) {
    if (!ctrl.in_buf || ctrl.in_len != 68) {
        return 0;
    }
    int ret = usb_control_msg(handle, USB_ENDPOINT_OUT | USB_TYPE_VENDOR,
        AML_LIBUSB_REQ_TPL_CMD, SHORT_AT(ctrl.in_buf, 64),
        SHORT_AT(ctrl.in_buf, 66), ctrl.in_buf, 64, 50000);
    *ctrl.p_in_data_size = (unsigned int)max(0, ret);
    if (ret < 0) {
        aml_printf("IOCTL_TPL_CMD_Handler ret=%d,tpl_cmd=%s error_msg=%s\n", ret, ctrl.in_buf,
            usb_strerror());
    }
    return ret >= 0;
}

int IOCTL_TPL_STATUS_Handler (usbDevIoCtrl ctrl) {
    return IOCTL_TPL_STATUS_Handler_Ex(ctrl, 60);
}

int IOCTL_TPL_STATUS_Handler_Ex (usbDevIoCtrl ctrl, unsigned int timeout) {
    if (!ctrl.in_buf || !ctrl.out_buf || ctrl.in_len != 4 || ctrl.out_len != 64) {
        return 0;
    }

    int ret = usb_control_msg(handle, USB_ENDPOINT_IN | USB_TYPE_VENDOR,
        AML_LIBUSB_REQ_TPL_STATUS, SHORT_AT(ctrl.in_buf, 0),
        SHORT_AT(ctrl.in_buf, 2), ctrl.out_buf, 64, 1000 * timeout);
    if (ret < 0) {
        aml_printf("IOCTL_TPL_STATUS_Handler ret=%d error_msg=%s\n", ret, usb_strerror());
    }
    return ret >= 0;
}

int IOCTL_WRITE_MEDIA_Handler (usbDevIoCtrl ctrl, unsigned int timeout) {
    if (ctrl.in_buf && ctrl.in_len > 0x1F) {
        *((short *)ctrl.in_buf + 8) = 239;
        *((short *)ctrl.in_buf + 9) = 256;
        int ret = usb_control_msg(handle, USB_ENDPOINT_OUT | USB_TYPE_VENDOR,
            AML_LIBUSB_REQ_WRITE_MEDIA, 1, 0xFFFF, ctrl.in_buf, 32,
            timeout);
        *ctrl.p_in_data_size = (unsigned int)max(0, ret);
        if (ret < 0) {
            aml_printf("[AmlLibUsb]:");
            aml_printf(
                "IOCTL_WRITE_MEDIA_Handler,value=%x,index=%x,len=%d,ret=%d error_msg=%s\n", 1LL,
                0xFFFFLL, 32LL, ret, usb_strerror());
        }
        return ret >= 0;
    } else {
        aml_printf("[AmlLibUsb]:");
        aml_printf("in_buf=%p, in_len=%d\n", ctrl.in_buf, ctrl.in_len);
        return 0;
    }
}

int IOCTL_READ_MEDIA_Handler (usbDevIoCtrl ctrl, unsigned int timeout) {
    if (!ctrl.in_buf || ctrl.in_len <= 0xF) {
        return 0;
    }
    int value = 0;
    if (ctrl.in_len > 32) {
        value = *((short *)ctrl.in_buf + 32);
    }
    if (!value || value > 0x1000) {
        value = 4096;
    }
    uint64_t len = LONG_AT(ctrl.in_buf, 4);
    unsigned int index = (unsigned int)(value + len - 1) / value;
    int ret = usb_control_msg(handle, USB_ENDPOINT_IN | USB_TYPE_VENDOR,
        AML_LIBUSB_REQ_READ_MEDIA, value, index, ctrl.in_buf, 16,
        timeout);
    *ctrl.p_in_data_size = (unsigned int)max(0, ret);
    if (ret < 0) {
        aml_printf("IOCTL_READ_MEDIA_Handler,value=%x,index=%x,len=%d,ret=%d error_msg=%s\n",
            value, index, len, ret, usb_strerror());
    }
    return ret >= 0;
}

int IOCTL_BULK_CMD_Handler (usbDevIoCtrl ctrl, unsigned int a1, unsigned int request) {
    if (!ctrl.in_buf || ctrl.in_len != 68) {
        return 0;
    }
    int ret = usb_control_msg(handle, 64, request, 0, 2, ctrl.in_buf, 64, 50000);
    *ctrl.p_in_data_size = (unsigned int)max(0, ret);
    if (ret < 0) {
        aml_printf("AM_REQ_BULK_CMD_Handler ret=%d,blkcmd=%s error_msg=%s\n", ret,
            ctrl.in_buf, usb_strerror());
    }
    return ret >= 0;
}

int usbDeviceIoControl(AmlUsbDrv *drv, unsigned int control_code, void *in_buf,
    unsigned int in_len, void *out_buf, unsigned int out_len,
    unsigned int *in_data_size, unsigned int *out_data_size) {
    usbDevIoCtrl ctrl = {};
    ctrl.in_buf  = (char*)in_buf;
    ctrl.out_buf = (char*)out_buf;
    ctrl.in_len = in_len;
    ctrl.out_len = out_len;
    ctrl.p_in_data_size = in_data_size;
    ctrl.p_out_data_size = out_data_size;
    usbDevIo v18 = {};
    v18.a = 1;
    v18.b = 0;
    int readOrWrite = 0;
    switch (control_code) {
    case 0x80002000:
        return IOCTL_READ_MEM_Handler(ctrl);
    case 0x80002004:
        return IOCTL_WRITE_MEM_Handler(ctrl);
    case 0x80002008:
        return IOCTL_FILL_MEM_Handler(ctrl);
    case 0x8000200C:
        return IOCTL_RUN_IN_ADDR_Handler(ctrl);
    case 0x80002010:
        readOrWrite = 1;
    case 0x80002014:
        return IOCTL_DO_LARGE_MEM_Handler(ctrl, readOrWrite);
    case 0x80002018:
        return IOCTL_READ_AUX_REG_Handler(ctrl);
    case 0x8000201C:
        return IOCTL_WRITE_AUX_REG_Handler(ctrl);
    case 0x80002020:
        return IOCTL_MODIFY_MEM_Handler(ctrl);
    case 0x80002024:
        return IOCTL_IDENTIFY_HOST_Handler(ctrl);
    case 0x80002040:
        return IOCTL_TPL_CMD_Handler(ctrl);
    case 0x80002044:
        return IOCTL_TPL_STATUS_Handler(ctrl);
    default:
        return 0;
    }
}

int usbDeviceIoControlEx(AmlUsbDrv *drv, unsigned int controlCode, void *in_buf,
    unsigned int in_len, void *out_buf, unsigned int out_len,
    unsigned int *in_data_size, unsigned int *out_data_size,
    unsigned int timeout) {
    usbDevIoCtrl ctrl = {};
    ctrl.in_buf = (char*)in_buf;
    ctrl.out_buf = (char *)out_buf;
    ctrl.in_len = in_len;
    ctrl.out_len = out_len;
    ctrl.p_in_data_size = in_data_size;
    ctrl.p_out_data_size = out_data_size;
    usbDevIo v18 = {};
    v18.a = 1;
    v18.b = 0;
    switch (controlCode) {
    case 0x80002004:
        return IOCTL_WRITE_MEM_Handler(ctrl);
    case 0x80002044:
        return IOCTL_TPL_STATUS_Handler_Ex(ctrl, timeout);
    case 0x80002048:
        return IOCTL_WRITE_MEDIA_Handler(ctrl, timeout);
    case 0x8000204C:
        return IOCTL_READ_MEDIA_Handler(ctrl, timeout);
    case 0x80002050:
        return IOCTL_BULK_CMD_Handler(ctrl, timeout, 0x34);
    case 0x80002054:
        return IOCTL_BULK_CMD_Handler(ctrl, timeout, 0x35);
    default:
        aml_printf("unknown control code 0x%x\n", controlCode);
        return 0;
    }
}

int usbReadFile(AmlUsbDrv *drv, void *buf, unsigned int len, unsigned int *read) {
    int ret = usb_bulk_read(handle, drv->read_ep, (char *)buf, len, 90000);
    *read = (unsigned int)max(0, ret);
    if (ret < 0) {
        aml_printf("usbReadFile len=%d,ret=%d error_msg=%s\n", len, ret, usb_strerror());
    }
    return ret >= 0;
}

int usbWriteFile(AmlUsbDrv *drv, const void *buf, unsigned int len, unsigned int *read) {
    int ret = usb_bulk_write(handle, drv->write_ep, (char *)buf, len, 50000);
    *read = (unsigned int)max(0, ret);
    if (ret < 0) {
        aml_printf("usbWriteFile len=%d,ret=%d error_msg=%s\n", len, ret, usb_strerror());
    }
    return ret >= 0;
}

int OpenUsbDevice(AmlUsbDrv *drv) {
    int r = usbio_open(AML_ID_VENDOR, AML_ID_PRODUCE, &handle, 1);
    assert(r == 0);
    drv->read_ep = (unsigned char)0x81;
    drv->write_ep = 2;
    return r == 0;
}

int CloseUsbDevice (AmlUsbDrv *drv) {
    int r = usbio_close(handle);
    assert(r == 0);
    return r == 0;
}

int ResetDev (AmlUsbDrv *drv) {
    return 0;
}

// Control Read/Write
// Read = 1
int Aml_Libusb_Ctrl_RdWr (void *device, unsigned int offset, char *buf, unsigned int len,
    unsigned int readOrWrite, unsigned int timeout) {
    struct AmlUsbDrv drv = {};
    if (OpenUsbDevice(&drv) != 1) {
        aml_printf("Fail in open dev\n");
        return -647;
    }
    int processedData = 0;
    while (processedData < (int)len) {
        int requestLen = min(len - processedData, 64u);
        usb_control_msg(handle,
            readOrWrite ? USB_ENDPOINT_IN | USB_TYPE_VENDOR : USB_TYPE_VENDOR,
            readOrWrite ? AML_LIBUSB_REQ_READ_CTRL : AML_LIBUSB_REQ_WRITE_CTRL,
            offset >> 16, offset, buf, requestLen, timeout);
        processedData += requestLen;
        buf += requestLen;
        offset += requestLen;
    }
    CloseUsbDevice(&drv);
    return 0;
}

int Aml_Libusb_Password (void *device, char *buf, int size, int timeout) {
    struct AmlUsbDrv drv = {};
    if (OpenUsbDevice(&drv) != 1) {
        aml_printf("Fail in open dev\n");
        return -698;
    }
    if (size > 64) {
        aml_printf("f(%s)size(%d) too large, cannot support it.\n", "Aml_Libusb_Password",
            size);
        return -705;
    }
    int bufPtr = 0;
    int value = 0;
    while (bufPtr < size) {
        value += buf[bufPtr++];
    }
    int result = usb_control_msg(handle, USB_ENDPOINT_OUT | USB_TYPE_VENDOR,
        AML_LIBUSB_REQ_PASSWORD, value, 0, buf, size, timeout);
    CloseUsbDevice(&drv);
    return result;
}

int Aml_Libusb_get_chipinfo (void *device, char *buf, int size, int index, int timeout) {
    struct AmlUsbDrv drv = {};
    if (OpenUsbDevice(&drv) != 1) {
        aml_printf("Fail in open dev\n");
        return -750;
    }
    if (size > 64) {
        aml_printf("f(%s)size(%d) too large, cannot support it.\n", "Aml_Libusb_get_chipinfo",
            (unsigned int)size);
        return -757;
    }
    int ret = usb_control_msg(handle, USB_ENDPOINT_IN | USB_TYPE_VENDOR,
        AML_LIBUSB_REQ_CHIP_INFO, 0, index, buf, size, timeout);
    CloseUsbDevice(&drv);
    return ret;
}
