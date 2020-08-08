#include "pozix.h"
#include "usbio.h"
#include "system.h"
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>
#include <asm/ioctl.h>

BEGIN_C

// see: https://github.com/aosp-mirror/platform_development/tree/master/host/windows/usb/winusb
//  or: https://github.com/leo-zspace/platform_development/tree/master/host/windows/usb/winusb

static bool log_ioctl;   // enable all ioctl logging
static bool log_control; // enable only ioctl(lUSBDEVFS_CONTROL)
static bool serialize;   // use mutex to serialize all ioctl(usbfs) calls (except USBDEVFS_REAPURB)

static_init(usbio_nix) {
    log_ioctl   = system_option("usbio_nix.log_ioctl");
    log_control = system_option("usbio_nix.log_control");
    serialize   = system_option("usbio_nix.serialize");
}

enum {
    PIPE_BULK_IN1   = 0x81,
    PIPE_BULK_IN2   = 0x82,
    PIPE_BULK_OUT1  = 0x01,
    PIPE_BULK_OUT2  = 0x02
};

typedef struct usbio_open_ctx_s {
    int vid;
    int pid;
    usbio_file_t* files;
    int i;
    int n;
} usbio_open_ctx_t;


#define case_return(v) case (int)v: return #v;

static const char* ioctl_request_str(int request) {
    switch (request) {
        case_return(USBDEVFS_CONTROL)
        case_return(USBDEVFS_IOCTL)
        case_return(USBDEVFS_GETDRIVER)
        case_return(USBDEVFS_DISCONNECT)
        case_return(USBDEVFS_CLAIMINTERFACE)
        case_return(USBDEVFS_DISCONNECT_CLAIM)
        case_return(USBDEVFS_DISCONNECT_CLAIM_EXCEPT_DRIVER)
        case_return(USBDEVFS_BULK)
        case_return(USBDEVFS_SUBMITURB)
        case_return(USBDEVFS_REAPURB)
        case_return(USBDEVFS_DISCARDURB)
        case_return(USBDEVFS_RESET)
        case_return(USBDEVFS_RESETEP)
        case_return(USBDEVFS_CLEAR_HALT)
        default: assertion(false, "add string value for 0x%08X", request); return "???";
    }
}

// strictly speaking this mutex is not needed and ioctl() is thread safe by design
// however with not much trust in usbfs and strange use of serialization of calls inside it
// it is possible to turn this mutex on from system.ini to see if it makes a differencce.
// It also makes it a bit easier to read the debug logs in some concurent scenarious

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // experimental

static int dioctl(int fd, int request, void* arg, int* bytes) {
    assert(fd >= 0 && bytes != null);
//  double time = time_in_milliseconds();
    errno = 0;
    if (serialize && request != USBDEVFS_REAPURB) { mutex_lock(&mutex); }
    int r = ioctl(fd, request, arg);
    if (serialize && request != USBDEVFS_REAPURB) { mutex_unlock(&mutex); }
    if (log_control && request == (int)USBDEVFS_CONTROL || log_ioctl) {
        int e = errno;
        log_info("ioctl(%s) r=%d errno=%d", ioctl_request_str(request), r, e);
        errno = e;
    }
    *bytes = r >= 0 ? r : 0;
    r = r >= 0 ? 0 : errno;
    if (r == EINTR) { log_err("EINTR"); }
//  if (r == ETIMEDOUT) { log_err("ETIMEOUT %.3f ms", time_in_milliseconds() - time); }
    return r;
}

static int detach_kernel_driver(usbio_file_t file, int iface) {
    struct usbdevfs_getdriver getdrv = {};
    getdrv.interface = iface;
    int transferred = 0;
    int r = dioctl(file, USBDEVFS_GETDRIVER, &getdrv, &transferred);
    if (r != 0 || strcmp(getdrv.driver, "usbfs") != 0) {
        return r == 0 ? ENOENT : r;
    }
    struct usbdevfs_ioctl command = {};
    command.ifno = iface;
    command.ioctl_code = USBDEVFS_DISCONNECT;
    command.data = null;
//  rtrace("USBDEVFS_IOCTL USBDEVFS_DISCONNECT");
    return dioctl(file, USBDEVFS_IOCTL, &command, &transferred);
}

static int claim_interface(usbio_file_t file, int iface) {
//  rtrace("USBDEVFS_CLAIMINTERFACE");
    int transferred = 0;
    return dioctl(file, USBDEVFS_CLAIMINTERFACE, &iface, &transferred);
}

static int detach_kernel_driver_and_claim(usbio_file_t file, int iface) {
    struct usbdevfs_disconnect_claim dc = {};
    dc.interface = iface;
    dc.flags = USBDEVFS_DISCONNECT_CLAIM_EXCEPT_DRIVER;
    strncpy0(dc.driver, "usbfs", countof(dc.driver));
//  trace(">USBDEVFS_DISCONNECT_CLAIM");
    int transferred = 0;
    int r = dioctl(file, USBDEVFS_DISCONNECT_CLAIM, &dc, &transferred);
//  trace("<USBDEVFS_DISCONNECT_CLAIM r=%d", r);
    if (r != 0 && r != ENOTTY) { // no idea why 'ENOTTY' is special fallback case
        return r;
    } else if (r == 0) {
        return 0;
    }
    /* Fallback code for kernels which don't support the disconnect-and-claim ioctl */
    r = detach_kernel_driver(file, iface);
    if (r != 0 && r != ENOENT) {
        return r;
    }
    return claim_interface(file, iface);
}

typedef int (*directory_entry_callback)(void* that, const char* name, struct dirent* entry);

static int directory_enumerate(void* that, usbio_open_ctx_t* open_ctx, const char* path, directory_entry_callback callback) {
    DIR* pd = opendir(path);
    if (pd == null) {
        rtrace("failed to open %s %s", path, strerr(errno));
        return errno;
    }
//  rtrace(">enumerating: %s", path);
    int r = 0;
    struct dirent* e = readdir(pd);
    while (e != null) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            char name[1024];
            if (snprintf0(name, sizeof(name), "%s/%s", path, e->d_name) >= (int)countof(name) - 1) {
                rtrace("filename is too long! path=%s e->d_name=%s", path, e->d_name);
            } else {
                r = callback(that, name, e);
//              rtrace("callback(e->d_name=\"%s\", name=\"%s\") return=%d", e->d_name, name, r);
                if (r != 0) {
                    break;
                }
                if (e->d_type == DT_DIR) {
                    r = directory_enumerate(that, open_ctx, name, callback);
                    if (r != 0) {
                        break;
                    }
                }
            }
        }
        errno = 0; // TODO: see below
        e = readdir(pd);
    }
    if (r == 0) {
        r = errno; // TODO: this is fragile because it depends on readdir() not setting errno for last entry. Not sure about Andrey's intent here...
    }
    closedir(pd);
//  rtrace("<enumerating: %s", path);
    return r;
}


enum { // usb_enumerate_callback return value is a set of:
    USBIO_DONT_CLOSE_FD  = 0x0001,
    USBIO_STOP_ITERATION = 0x0002
};

typedef struct usbio_device_descriptor_s {
    uint8_t  length;
    uint8_t  descriptor_type;
    uint16_t bcd_USB;
    uint8_t  device_class;
    uint8_t  device_sub_class;
    uint8_t  device_protocol;
    uint8_t  max_packet_size0;
    uint16_t id_vendor;
    uint16_t id_product;
    uint16_t bcd_device;
    uint8_t  manufacturer;
    uint8_t  product;
    uint8_t  serial_number;
    uint8_t  number_of_configurations;
} attribute_packed usbio_device_descriptor_t;

// Thus, return value of 0 is "close fd and keep going"
typedef int (*usbio_enumerate_callback_t)(void* that, usbio_file_t file, const char* name, usbio_device_descriptor_t* desc);

typedef struct usbio_open_context_s {
    void* that;
    usbio_enumerate_callback_t callback;
} usbio_open_context_t;

static int usb_enumerate_callback(void* that, const char* name, struct dirent* entry) {
    usbio_open_context_t* ctx = (usbio_open_context_t*)that;
    if ((entry->d_type & DT_DIR) == 0) {
        usbio_file_t file = open(name, O_RDWR);
        if (file < 0) {
            rtrace("Failed to open file %s: %d 0x%08X %s", name, errno, errno, strerr(errno));
        } else {
            int r = 0;
            struct usbio_device_descriptor_s dd = {0};
            if (read(file, &dd, sizeof(dd)) < 0) {
                rtrace("Cannot read device descriptor from %s: %d", name, errno);
            } else {
                r = ctx->callback(ctx->that, file, name, &dd);
            }
            if ((r & USBIO_DONT_CLOSE_FD) == 0) {
//              rtrace("close(%d) %04X:%04X", file, dd.id_vendor, dd.id_product);
                close(file);
            }
            if (r & USBIO_STOP_ITERATION) {
                return 0;
            }
        }
    }
    return 0;
}

static int usb_enumerate(void* that, usbio_open_ctx_t* open_ctx, usbio_enumerate_callback_t uc) { // iterate over all connected devices
    usbio_open_context_t ctx = { that, uc };
    return directory_enumerate(&ctx, open_ctx, "/dev/bus/usb", usb_enumerate_callback);
}

static int usb_lock_and_claim_interface_0(usbio_file_t file, const char* name) {
    int e = 0;
    // why fcntl and not flock() is used? - see notes at the bottom of thefile.
    struct flock fl = {};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    int r = fcntl(file, F_SETLKW, &fl);
    if (r != 0) {
        e = errno;
        rtrace("Failed to lock file %s: r=%d errno %d 0x%08X %s", name, r, e, e, strerr(e));
    }
    if (r == 0) {
//      trace("detach_kernel_driver_and_claim(%d, 0)", file);
        r = detach_kernel_driver_and_claim(file, 0);
        if (r != 0) {
            e = errno;
            rtrace("Failed to claim interface 0 on %s: r=%d errno %d 0x%08X %s", name, r, e, e, strerr(e));
        }
    }
    return e;
}

byte usbio_pipe_bulk_in1(usbio_file_t fd) { return PIPE_BULK_IN1; }

byte usbio_pipe_bulk_out1(usbio_file_t fd) { return PIPE_BULK_OUT1; }

byte usbio_pipe_bulk_in2(usbio_file_t fd) { return PIPE_BULK_IN2; }

byte usbio_pipe_bulk_out2(usbio_file_t fd) { return PIPE_BULK_OUT2; }

int usbio_bulk_in(usbio_file_t file, byte pipe, const void* data, int bytes, int* transferred) {
    struct usbdevfs_bulktransfer req = {};
    req.ep = pipe;
    req.len = (unsigned int)bytes;
    req.timeout = 0; // milliseconds
    req.data = (void*)data;
    return dioctl(file, USBDEVFS_BULK, &req, transferred);
}

int usbio_bulk_out(usbio_file_t file, byte pipe, const void* data, int bytes) {
    struct usbdevfs_bulktransfer req = {};
    req.ep = pipe;
    req.len = (unsigned int)bytes;
    req.timeout = 0; // milliseconds
    req.data = (void*)data;
    assert(file > 0);
    int transferred = 0;
    int r = dioctl(file, USBDEVFS_BULK, &req, &transferred);
    assertion(r != 0 || transferred == bytes, "r=%d transferred=%d", r, transferred);
    if (r == 0 && transferred != bytes) { r = EINVAL; }
    return r;
}

int usbio_control(usbio_file_t file, usbio_ctrl_setup_t* setup, void* data, int* bytes, int timeout_milliseconds) {
//  bool host_to_device = (0x80 & request_type) == 0;
    assertion(timeout_milliseconds != 0, "control endpoint never returns with timeout==0");
    if (bytes != null) { *bytes = 0; } // in case called did not initialize bytes to zero and we have an error or empty transfer from device
    struct usbdevfs_ctrltransfer ct = {};
    assertion((setup->rt & 0x01) == 0, "pr->request_type=0x%02X", setup->rt);
    ct.bRequestType = setup->rt & 0xFE;  // Plan B: clear off interface bit
    ct.bRequest = setup->req;
    ct.wValue   = setup->val;
    ct.wIndex   = setup->ix;
    ct.wLength  = setup->len;
    ct.timeout  = timeout_milliseconds < 0 ? USBIO_CTRL_EP_TIMEOUT_MS : timeout_milliseconds; // in milliseconds
    ct.data     = data;
    int transferred = 0;
    int r = dioctl(file, USBDEVFS_CONTROL, &ct, &transferred);
    if (log_control) {
        log_info("ioctl(fd=%d, rt=%02X request=%02X value=%04X index=%04X len=%d transferred=%d)=%d", file, setup->rt, setup->req, setup->val, setup->ix, setup->len, transferred, r);
    }
    if (r != 0) {
        assertion(r > 0, "expected positive r=%d", r);
        r = abs(r); // PlanB not to deal with negative value's of errno in callers code
        trace("ioctl(USBDEVFS_CONTROL) failed %s", strerr(r));
    } else if (transferred > 0) {
        assertion(data != null || bytes != null,
                  "expected bytes != null ioctl(fd=%d, rt=%02X request=%02X value=%04X index=%04X length=%d data=%p bytes=%p transferred=%d)=%d",
                  file, setup->rt, setup->req, setup->val, setup->ix, setup->len, data, bytes, transferred, r);
        if (bytes != null) { *bytes = transferred; }
    }
    return r;
}

int usbio_ctrl(usbio_file_t file, usbio_buffer_t* b, void* data, int bytes) {
    if (file < 0) { return EBADF; }
    memset(&b->urb, 0, sizeof(b->urb)); // just in case more fields are added by the kernel
    b->urb.type = USBDEVFS_URB_TYPE_CONTROL;
//  b->urb.endpoint          = 0; // control endpoint is always zero
//  b->urb.flags             = 0;
//  b->urb.start_frame       = 0;
//  b->urb.number_of_packets = 0;
//  b->urb.error_count       = 0;
//  b->urb.signr             = 0;
    b->urb.buffer            = b->data;
    b->urb.buffer_length     = b->bytes;
    b->urb.usercontext = b;
    int transferred = 0;
    return dioctl(file, USBDEVFS_SUBMITURB, &b->urb, &transferred);
}

int usbio_submit_urb(usbio_file_t file, int pipe, usbio_buffer_t* b) {
    if (file < 0) { return EBADF; }
    assert(pipe == PIPE_BULK_IN1 || pipe == PIPE_BULK_IN2);
    if (pipe == PIPE_BULK_IN1) { assert(b->bytes == USBIO_BULK_REQUEST_SIZE); }
    if (pipe == PIPE_BULK_IN2) { assert(b->bytes == USBIO_CTRL_RESPONSE_SIZE); }
    memset(&b->urb, 0, sizeof(b->urb)); // just in case more fields are added by the kernel
    b->urb.type = USBDEVFS_URB_TYPE_BULK;
    b->urb.endpoint = pipe;
//  b->urb.flags             = 0;
//  b->urb.start_frame       = 0;
//  b->urb.number_of_packets = 0;
//  b->urb.error_count       = 0;
//  b->urb.signr             = 0;
    b->urb.buffer            = b->data;
    b->urb.buffer_length     = b->bytes;
    b->urb.usercontext = b;
//  trace("b->urb.buffer=%p", b->urb.buffer);
    int transferred = 0;
    return dioctl(file, USBDEVFS_SUBMITURB, &b->urb, &transferred);
}

int usbio_discard_urb(usbio_file_t file, usbio_buffer_t* b) {
    int ignore_transferred = 0;
    return dioctl(file, USBDEVFS_DISCARDURB, &b->urb, &ignore_transferred);
}

int usbio_urb_no;

int usbio_reap_urb(usbio_file_t file, usbio_buffer_t** b) {
    if (file < 0) {
        return EBADF;
    }
    int transferred = 0;
    struct usbdevfs_urb* urb = null;
    int r = dioctl(file, USBDEVFS_REAPURB, &urb, &transferred); // transferred will be zero on success
    if (r == 0) {
//      log_info("dioctl(file, USBDEVFS_REAPURB, ...) urb->endpoint=0x%02X urb->actual_length=%d", urb->endpoint, urb->actual_length);
//      log_hexdump(urb->buffer, urb->actual_length, "urb->buffer_length=%d urb->actual_length=%d", urb->buffer_length, urb->actual_length);
        usbio_buffer_t* ub = (usbio_buffer_t*)urb->usercontext;
        assertion(&ub->urb == urb, "ub=%p ub->urb=%p urb=%p", ub, &ub->urb, urb);
        ub->bytes = ub->urb.actual_length;
        if (ub->urb.error_count != 0) {
            trace("urb actual_length=%d buffer_length=%d error_count=%d", ub->urb.actual_length, ub->urb.buffer_length, ub->urb.error_count);
            r = EIO;
        }
//      if (urb->endpoint == 0x81) { log_info("endpoint=0x%02X bytes=%d", ub->urb.endpoint, ub->bytes); }
        *b = ub;
#ifdef HEX_DUMP_URBS
        if (ub->urb.actual_length <= 128) {
            log_hexdump(ub->urb.buffer, ub->urb.actual_length, "urb.buffer=%p, urb.actual_length=%d", ub->urb.buffer, ub->urb.actual_length);
        } else {
            log_info("urb.buffer=%p, urb.actual_length=%d", ub->urb.buffer, ub->urb.actual_length);
        }
#endif
#ifdef SAVE_URBS
        if (ub->urb.actual_length > 0) {
            char filename[128];
            mkdir("/data/urbs", 0777);
            snprintf0(filename, countof(filename), "/data/urbs/urb_%04d.data", usbio_urb_no);
            unlink(filename);
            int fd = open(filename, O_WRONLY | O_CREAT);
            if (fd >= 0) {
                write(fd, ub->urb.buffer, ub->urb.actual_length);
                close(fd);
            }
        }
#endif
        usbio_urb_no++;
    } else {
        trace("USBDEVFS_REAPURB failed %s", strerr(r));
    }
    return r;
}


int usbio_abort_pipe(usbio_file_t file, int pipe) { return ENOSYS; } // meaningless on Linux

int usbio_flush_pipe(usbio_file_t file, int pipe) { return ENOSYS; } // meaningless on Linux

// usbio_reset_pipe() Avoid using this request. It should probably be removed
// Using it typically means the device and driver will lose toggle synchronization.
// If you really lost synchronization, you likely need to completely handshake with the device, using a request like CLEAR_HALT or SET_INTERFACE.

int usbio_reset_pipe(usbio_file_t file, int pipe) { return ioctl(file, USBDEVFS_RESETEP, pipe); }

// usbio_reset() soes a USB level device reset. The ioctl parameter is ignored.
// After the reset, this rebinds all device interfaces. (similar to unpluging/repluging the device)
// Warning:
// Avoid using this call until some usbcore bugs get fixed, since it does not fully synchronize device, interface, and driver (not just usbfs) state.

int usbio_reset(usbio_file_t file) { return ioctl(file, USBDEVFS_RESET, 0); }

// usbio_clear_halt() clears endpoint halt (stall) and resets the endpoint toggle. This is only meaningful for bulk or interrupt endpoints.
// The ioctl parameter is an integer endpoint number (1 to 15, as identified in an endpoint descriptor), masked with USB_DIR_IN
// when referring to an endpoint which sends data to the host from the device.
// Use this on bulk or interrupt endpoints which have stalled, returning -EPIPE status to a data transfer request.
// Do not issue the control request directly, since that could invalidate the host’s record of the data toggle.

int usbio_clear_halt(usbio_file_t file) { return ioctl(file, USBDEVFS_CLEAR_HALT, 0); }

int usbio_set_timeout(usbio_file_t file, int pipe, int milliseconds)  { return 0; } // TODO: implement for all pipes (most meaningful got ep0)

int usbio_set_raw(usbio_file_t usb_file, bool raw) { // Linux: always raw
    return 0;
}

int usbio_close(usbio_file_t usb_file) {
    return close(usb_file);
}

static int usbio_open_callback(void* that, usbio_file_t file, const char* name, usbio_device_descriptor_t* desc) {
    usbio_open_ctx_t* ctx = (usbio_open_ctx_t*)that;
    const int id_vendor = desc->id_vendor;
    const int id_product = desc->id_product;
//  trace("%04X:%04X", id_vendor, id_product);
    if (id_vendor == ctx->vid && id_product == ctx->pid) {
        if (ctx->i < ctx->n) {
            ctx->files[ctx->i] = file;
//          trace("%04X:%04X files[%d]=%d", id_vendor, id_product, ctx->i, ctx->files[ctx->i]);
            ctx->i++;
            assertion(ctx->i <= ctx->n, "%d out of range [0..%d]", ctx->i, ctx->n);
            usb_lock_and_claim_interface_0(file, name);
            return USBIO_DONT_CLOSE_FD | (ctx->i == ctx->n ? USBIO_STOP_ITERATION : 0);
        } else if (ctx->i == ctx->n) {
            rtrace("too many files for vid:pid=%04X:%04X ctx.i=%d ctx.n=%d", ctx->vid, ctx->pid, ctx->i, ctx->n);
        } else {
            assert(false);
        }
    }
    return 0;
}

int usbio_open(int vid, int pid, usbio_file_t* files, int count) { // returns number of openned files
    assertion(count > 0, "count=%d must be >= 1", count);
    memset(files, 0, sizeof(usbio_file_t));
    usbio_open_ctx_t ctx = {};
    ctx.vid = vid;
    ctx.pid = pid;
    ctx.files = files;
    ctx.n = count;
    errno = 0;
    usb_enumerate(&ctx, &ctx, usbio_open_callback);
    if (ctx.i == 0) {
//      rtrace("not found any devices: 0x%04X:%04X", vid, pid);
        ctx.i = -1;
        errno = ENODEV;
    } else {
//      rtrace("found %d device(s): 0x%04X:%04X", ctx.i, vid, pid);
    }
    return ctx.i;
}

bool usbio_is_device_present(int vid, int pid) {
    usbio_file_t file = (usbio_file_t)0;
    int count = usbio_open(vid, pid, &file, 1);
    int r = count == 1 ? 0 : errno;
    bool b = count == 1 && r == 0 && file != (usbio_file_t)0;
    if (file != (usbio_file_t)0) {
        usbio_close(file);
    }
    return b;
}

// #define dump_field(label, value) log_info(label, value, value)
#define dump_field(label, value) (void)value

static void dump_usb_device_descriptor(struct usb_device_descriptor* d) {
    dump_field("bLength            0x%04X (%d)", d->bLength);
    dump_field("bDescriptorType    0x%04X (%d)", d->bDescriptorType);
    dump_field("bcdUSB             0x%04X (%d)", d->bcdUSB);
    dump_field("bDeviceClass       0x%04X (%d)", d->bDeviceClass);
    dump_field("bDeviceSubClass    0x%04X (%d)", d->bDeviceSubClass);
    dump_field("bDeviceProtocol    0x%04X (%d)", d->bDeviceProtocol);
    dump_field("bMaxPacketSize0    0x%04X (%d)", d->bMaxPacketSize0);
    dump_field("idVendor           0x%04X (%d)", d->idVendor);
    dump_field("idProduct          0x%04X (%d)", d->idProduct);
    dump_field("bcdDevice          0x%04X (%d)", d->bcdDevice);
    dump_field("iManufacturer      0x%04X (%d)", d->iManufacturer);
    dump_field("iProduct           0x%04X (%d)", d->iProduct);
    dump_field("iSerialNumber      0x%04X (%d)", d->iSerialNumber);
    dump_field("bNumConfigurations 0x%04X (%d)", d->bNumConfigurations);
}

static void dump_usb_config_descriptor(struct usb_config_descriptor* c) {
    dump_field("bLength             0x%04X (%d)", c->bLength);
    dump_field("bDescriptorType     0x%04X (%d)", c->bDescriptorType);
    dump_field("wTotalLength        0x%04X (%d)", c->wTotalLength);
    dump_field("bNumInterfaces      0x%04X (%d)", c->bNumInterfaces);
    dump_field("bConfigurationValue 0x%04X (%d)", c->bConfigurationValue);
    dump_field("iConfiguration      0x%04X (%d)", c->iConfiguration);
    dump_field("bmAttributes        0x%04X (%d)", c->bmAttributes);
    dump_field("bMaxPower           0x%04X (%d)", c->bMaxPower);
}

static void dump_usb_interface_descriptor(struct usb_interface_descriptor* i) {
    dump_field("bLength             0x%04X (%d)", i->bLength);
    dump_field("bDescriptorType     0x%04X (%d)", i->bDescriptorType);
    dump_field("bInterfaceNumber    0x%04X (%d)", i->bInterfaceNumber);
    dump_field("bAlternateSetting   0x%04X (%d)", i->bAlternateSetting);
    dump_field("bNumEndpoints       0x%04X (%d)", i->bNumEndpoints);
    dump_field("bInterfaceClass     0x%04X (%d)", i->bInterfaceClass);
    dump_field("bInterfaceSubClass  0x%04X (%d)", i->bInterfaceSubClass);
    dump_field("bInterfaceProtocol  0x%04X (%d)", i->bInterfaceProtocol);
    dump_field("iInterface          0x%04X (%d)", i->iInterface);
}

static int read_descriptors(usbio_file_t fd) { // returns -1 and errno or number of endpoints of on the first interface
    // this function makes a lot of assumptions: single device (not a composite) single config single interface
    // it also does not enumerate endpoints because calling code has hardcoded assumptions about 0x81, 0x02 and 0x83
    byte data[4096] = {};
    byte* p = data;
    lseek(fd, 0, SEEK_SET); // because the iterator actually read device descriptor from this file
    int bytes = read(fd, data, sizeof(data));
//  trace_hexdump(data, bytes, "ALL");
    struct usb_device_descriptor* dd = (struct usb_device_descriptor*)data;
    if (dd->bLength != USB_DT_DEVICE_SIZE || dd->bDescriptorType != USB_DT_DEVICE) {
        trace_hexdump(data, bytes, "invalid usb_config_descriptor");
        errno = EINVAL;
        return -1;
    }
    dump_usb_device_descriptor(dd);
    p += USB_DT_DEVICE_SIZE;
    struct usb_config_descriptor* cd = (struct usb_config_descriptor *)p;
    if (cd->bLength != USB_DT_CONFIG_SIZE || cd->bDescriptorType != USB_DT_CONFIG) {
        trace_hexdump(data, bytes, "invalid usb_config_descriptor");
        errno = EINVAL;
        return -1;
    }
    dump_usb_config_descriptor(cd);
    // https://www.beyondlogic.org/usbnutshell/usb5.shtml
    // bmAttributes specify power parameters for the configuration. If a device is self powered, it sets D6.
    // Bit D7 was used in USB 1.0 to indicate a bus powered device, but this is now done by bMaxPower.
    // *** If a device uses any power from the bus ***, whether it be as a bus powered device or as a self powered device,
    // it must report its power consumption in bMaxPower.
    // Devices can also support remote wakeup which allows the device to wake up the host when the host is in suspend.
    // bMaxPower defines the maximum power the device will drain from the bus. This is in 2mA units, thus a maximum of
    // approximately 500mA can be specified. The specification allows a high powered bus powered device to drain no more
    // than 500mA from Vbus. If a device loses external power, then it must not drain more than indicated in bMaxPower.
    // It should fail any operation it cannot perform without external power.
    if ((cd->bmAttributes & (1U << 6)) == 0) {
        log_err("USB %04X:%04X BUS POWERED DEVICE (D6) bmAttributes=0x%02X", dd->idProduct, dd->idVendor, cd->bmAttributes);
    }
    // Cryptic description: "D7 Reserved, set to 1. (USB 1.0 Bus Powered)" - set by whom? device? always to 1 or only for USB 1.0?
//  if ((cd->bmAttributes & (1U << 7)) == 0) { log_err("LEGACY USB 1.0 (D7) should be 1?"); }
    // if I read phraseology above "If a device uses any power from the bus" and understand it correctly
    // bMaxPower should be zero for self powered device that does NOT draw any power from USB bus. I might be wrong...
    // TODO: (Leo) uncomment and verify when someone will be able to build next TC FPGA f/w
    if (cd->bMaxPower != 0) { log_warn("USB %04X:%04X bMaxPower=%d must be zero", dd->idProduct, dd->idVendor, cd->bMaxPower); }
    p += USB_DT_CONFIG_SIZE;
    while (p < data + bytes) {
        byte length = p[0];
        byte type   = p[1];
        if (type == USB_DT_INTERFACE) {
            struct usb_interface_descriptor* interface = (struct usb_interface_descriptor*)p;
            dump_usb_interface_descriptor(interface);
            p += length;
            if (length != USB_DT_INTERFACE_SIZE) {
                log_hexdump(data, bytes, "invalid usb_interface_descriptor");
                errno = EINVAL;
                return -1;
            }
//          trace("interface->bNumEndpoints=%d", interface->bNumEndpoints);
            // interface descriptor is following by number or endpoint descriptors:
            // struct usb_endpoint_descriptor *ep1, *ep2;
            // but because usbio is specific to tracking controller we just need to know the number
            return interface->bNumEndpoints;
        } else {
            p += length; // skip
        }
    }
    errno = EINVAL;
    return -1;
}

int usbio_ep_count(usbio_file_t fd) { return read_descriptors(fd); }

#ifdef USBIO_SMOKE_TEST // change to #ifndef to run

#include "system.h"

static_init(usbio_smoke_test) {
    double time = time_in_milliseconds();
    bool b = is_windows_usb_device_present(VID_GOOGLE, PID_P230_ADB); // time=19.097 (17.36 GUID_DEVINTERFACE_USB_DEVICE, 0, null, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT )
    time = time_in_milliseconds() - time;
    trace("VID_GOOGLE:PID_P230_ADB present=%d time=%.3f", b, time);
    time = time_in_milliseconds();
    b = is_windows_usb_device_present(VID_ZSPACE, PID_TC); // time=3.7 (0.58)
    time = time_in_milliseconds() - time;
    trace("VID_ZSPACE:PID_TC present=%d time=%.3f", b, time);
    trace("");
}

#endif

END_C

/*
    Why not flock() versus fcntl(file, F_SETLKW, &fl)?
    http://man7.org/linux/man-pages/man2/flock.2.html:
        flock() places advisory locks only; given suitable permissions on a
                file, a process is free to ignore the use of flock() and perform I/O
                on the file.
    https://www.kernel.org/doc/Documentation/filesystems/mandatory-locking.txt
    https://gavv.github.io/blog/file-locks/

*/