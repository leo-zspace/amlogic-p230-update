#include "pozix.h"
#include <winioctl.h>
#include <RegStr.h>
#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <usbioctl.h>
#include "usbio.h"
#include "array_set.h"

#pragma comment(lib, "SetupAPI")
#pragma comment(lib, "WinUSB")

BEGIN_C

// see: https://github.com/aosp-mirror/platform_development/tree/master/host/windows/usb/winusb
//  or: https://github.com/leo-zspace/platform_development/tree/master/host/windows/usb/winusb

enum { USBIO_MAGIC ='USBX' };

enum { USBIO_MAX_FILES = 8 }; // maximum number of simultaneously opened files

#define WINERR(e) HRESULT_FROM_WIN32(e) // TODO: move to pozix.h

enum {
    ERR_INVALID_PARAMETER  = WINERR(ERROR_INVALID_PARAMETER),
    ERR_NO_DATA            = WINERR(ERROR_NO_DATA),
    ERR_PIPE_NOT_CONNECTED = WINERR(ERROR_PIPE_NOT_CONNECTED),
    ERR_NOT_ENOUGH_MEMORY  = WINERR(ERROR_NOT_ENOUGH_MEMORY),
    ERR_INVALID_HANDLE     = WINERR(ERROR_INVALID_HANDLE),
    ERR_OUTOFMEMORY        = WINERR(ERROR_OUTOFMEMORY),
    ERR_BUSY               = WINERR(ERROR_BUSY)
};

enum {
    USB_CTRL = 0,
    USB_READ = 1,
    USB_WRITE = 2,
    USB_REAP = 3
};

static const char* OP_NAMES[] = {
    "USB_CTRL ",
    "USB_READ ",
    "USB_WRITE",
    "USB_REAP "
};

typedef USB_DEVICE_DESCRIPTOR usbio_device_descriptor_t;

static int translate_ntstatus_to_windows(int status);
static void quit_polling_thread(usbio_file_t fd);

static double time0; static_init(usbio_time_zero) { time0 = time_in_milliseconds(); }

#define dtrace(f, ...) trace("\t%s \t" f, time_format_milliseconds(time_in_milliseconds() - time0), __VA_ARGS__)
#define dtrace0(f) trace("\t%s \t" f, time_format_milliseconds(time_in_milliseconds() - time0))

#define DEBUG_DONT_TRACE_LOCKS

#ifndef DEBUG_DONT_TRACE_LOCKS
#define trace_lock(a)   dtrace(">" #a "lock");
#define trace_unlock(a) dtrace("<" #a "unlock");
#else
#define trace_lock(a)
#define trace_unlock(a)
#endif

#ifdef DEBUG // To ensure we not using mutexes reentrantly on the same thread (aka 'recursive mutexes' which CriticalSection is)
#define check_lock(a)   atomics_increment_int32(&a->locked); assertion(a->locked == 1, #a ".locked=%d", a->locked); trace_lock(a);
#define check_unlock(a) assertion(a->locked == 1, #a ".locked=%d", a->locked); atomics_decrement_int32(&a->locked); trace_unlock(a);
#else
#define check_lock(a)
#define check_unlock(a)
#endif

#define lock(a)   do { mutex_lock(&a->lock); check_lock(a); } while (0)
#define unlock(a) do { check_unlock(a); mutex_unlock(&a->lock);  } while (0)

typedef struct usbio_file_s {
    HANDLE file;
    usbio_file_t fd; // index of usbio_files[fd] or -1
    WINUSB_INTERFACE_HANDLE usb; // only single interface device supported at the moment
    HANDLE port;       // I/O completion port
    pthread_t thread;  // CompletionIOPort polling thread
    byte pipe_bulk_in1;  // 0x81
    byte pipe_bulk_in2;  // 0x82 ep 2 for f/w >= 231
    byte pipe_bulk_out1;
    byte pipe_bulk_out2;
    int  endpoint_count; // 1 or 3 - only support control endpoint + 1 or 2 pipe_bulk_out/pipe_bulk_in
    mutex_t lock;               // usbio_file lock
    volatile int32_t locked;    // protection against recursive mutexes
    pthread_cond_t   signal;    // signalled to all waiting threads on each overlapped I/O completion
    usbio_buffer_t*  pending;   // double linked list (queue) of pending URBs that were submitted to kernel I/O
    usbio_buffer_t*  ready;     // double linked list of ready URBs that were retrieved by GetQueuedCompletionStatus
    volatile bool    closing;
    // debugging:
    volatile int32_t xid_ctrl;
    volatile int32_t xid_read;
    volatile int32_t xid_wrte;
    volatile int32_t xid_ctrl_last;
    volatile int32_t xid_read_last;
    volatile int32_t xid_wrte_last;
} usbio_file_t_;

static mutex_t usbio_files_mutex;

static usbio_file_t_ usbio_files[USBIO_MAX_FILES];

static void usbio_files_destroy_mutex() {
    for (int i = 0; i < countof(usbio_files); i++) {
        if (usbio_files[i].fd != usbio_file_invalid) {
            trace("usbio file fd=%d is not closed", i);
        }
    }
    mutex_destroy(&usbio_files_mutex);
}

static_init(usbio) {
    mutex_init(&usbio_files_mutex, 0);
    for (int i = 0; i < countof(usbio_files); i++) { usbio_files[i].fd = usbio_file_invalid; }
    atexit(usbio_files_destroy_mutex);
}

const usbio_file_t usbio_file_invalid = -1;

#define valid_fd(fd) (0 < (fd) && (fd) <= countof(usbio_files) && usbio_files[fd].fd == (fd))

typedef struct usbio_open_ctx_s {
    int vid;
    int pid;
    usbio_file_t* files;
    int i;
    int n;
} usbio_open_ctx_t;

enum { // usb_enumerate_callback return value is a set of:
    USBIO_DONT_CLOSE_FD  = 0x0001,
    USBIO_STOP_ITERATION = 0x0002
};

// Thus, return value of 0 is "close fd and keep going"
typedef int (*usbio_enumerate_callback_t)(void* that, usbio_file_t file, const char* name, usbio_device_descriptor_t* desc);

static int winusb_create(usbio_file_t* file, const char* name, usbio_open_ctx_t* ctx, usbio_device_descriptor_t* dd,
                         void* that, usbio_enumerate_callback_t cb, bool* stop_iteration);

static int usb_enumerate(void* that, usbio_open_ctx_t* ctx, usbio_enumerate_callback_t cb) {
    const char* SYSTEM_CURRENTCONTROLSET_ENUM_USB = "SYSTEM\\CurrentControlSet\\Enum\\USB";
    HKEY usb_key = null;
    int r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, SYSTEM_CURRENTCONTROLSET_ENUM_USB, 0, KEY_READ, &usb_key);
    if (r != ERROR_SUCCESS) { return r; }
    DWORD sub_keys = 0;            // Used to store the number of Subkeys
    DWORD max_subkey_length = 0;   // Longest Subkey name length
    DWORD values = 0;              // Used to store the number of Subkeys
    DWORD max_value_length = 0;    // Longest Subkey value length
    r = RegQueryInfoKeyA(usb_key, null, null, null, &sub_keys, &max_subkey_length, null, &values,  &max_value_length, null, null, null);
    bool stop_iteration = false;
    if (r == ERROR_SUCCESS) {
        char* subkey_name = (char*)stack_allocz(max_subkey_length + 1);
        for (int i = 0; i < (int)sub_keys && !stop_iteration; i++) {
            DWORD cch = max_subkey_length + 1;
            int re = RegEnumKeyExA(usb_key, i, subkey_name, &cch, null, null, null, null);
//          dtrace("%s", subkey_name);
            // pray Microsoft keeps this key upper case of implement stristr()
            if (re == 0 && memcmp(subkey_name, "VID_", 4) == 0 && strstr(subkey_name, "&PID_") != null) {
                usbio_device_descriptor_t dd = {};
                char vid[8] = {};
                char pid[8] = {};
                memcpy(vid, subkey_name + 4, 4);
                memcpy(pid, strstr(subkey_name, "&PID_") + 5, 4);
                dd.idVendor = (USHORT)strtol(vid, null, 16);
                dd.idProduct = (USHORT)strtol(pid, null, 16);
                bool match = true;
                if (ctx != null && ctx->vid != 0 && ctx->pid != 0) {
                    match = dd.idVendor == ctx->vid && dd.idProduct == ctx->pid;
                }
                if (match) {
                    char name[4 * 1024];
                    char symbolic[4 * 1024];
                    snprintf0(name, countof(name), "%s\\%s", SYSTEM_CURRENTCONTROLSET_ENUM_USB, subkey_name);
                    HKEY key = null;
                    int r0 = RegOpenKeyExA(HKEY_LOCAL_MACHINE, name, 0, KEY_READ, &key);
                    if (r0 != ERROR_SUCCESS) {
                        dtrace("RegOpenKeyEx(%s) failed: %s", name, strerr(r0));
                    } else {
                        DWORD sub_keys_count = 0;
                        r = RegQueryInfoKeyA(key, null, null, null, &sub_keys_count, null, null, null, null, null, null, null);
                        if (r != ERROR_SUCCESS) {
                            dtrace("RegQueryInfoKey(%s) failed: %s", name, strerr(r));
                        } else {
                            for (int j = 0; j < (int)sub_keys_count && !stop_iteration; j++) {
                                char sn[4 * 1024]; // serial_number
                                DWORD sn_bytes = countof(sn);
                                int r4 = RegEnumKeyExA(key, j, sn, &sn_bytes, null, null, null, null);
                                if (r4 != ERROR_SUCCESS) {
                                    RegCloseKey(key);
                                } else {
                                    assertion(strlen(sn) + strlen("\\Device Parameters") < countof(sn), "name too long: %s", sn);
                                    char device_parameters[4 * 1024];
                                    snprintf0(device_parameters, countof(device_parameters), "%s\\Device Parameters", sn);
                                    HKEY skey = null;
                                    r = RegOpenKeyExA(key, device_parameters, 0, KEY_READ, &skey);
                                    if (r == ERROR_SUCCESS) {
                                        DWORD symbolic_bytes = countof(symbolic);
                                        r = RegQueryValueExA(skey, "SymbolicName", null, null, (byte*)symbolic, &symbolic_bytes);
                                        usbio_file_t usb_file = usbio_file_invalid;
                                        if (r == 0) {
                                            r = winusb_create(&usb_file, symbolic, ctx, &dd, that, cb, &stop_iteration);
                                        }
                                        RegCloseKey(skey);
                                    }
                                }
                            }
                        }
                        RegCloseKey(key);
                    }
                }
            }
        }
    }
    RegCloseKey(usb_key);
    return r;
}

static int list_countof(usbio_buffer_t* list) {
    int k = 0;
    if (list != null) {
        usbio_buffer_t* e = list;
        do { // check list consitency and absence of duplicates:
            assert(e->next->prev == e && e == e->prev->next);
            e = e->next;
            k++;
        } while (e != list);
    }
    return k;
}

static bool urb_present(usbio_buffer_t* list, usbio_buffer_t* urb) {
    int k = 0;
    if (list != null) {
        usbio_buffer_t* e = list;
        do { // check list consitency and absence of duplicates:
            k += e == urb;  assert(e->next->prev == e && e == e->prev->next);
            e = e->next;
        } while (e != list);
    }
    assertion(k == 0 || k == 1, "expected none or exacly one occurence of urb in the list - got %d", k);
    return k;
}

static usbio_buffer_t* urb_link(usbio_buffer_t* list, usbio_buffer_t* urb) {
    assert(!urb_present(list, urb));
    if (list == null) {
        list = urb; urb->prev = urb; urb->next = urb;
    } else {
        usbio_buffer_t* e = list;
        do { // check list consitency and absence of duplicates:
            assert(e != urb);  assert(e->next->prev == e && e == e->prev->next);
            e = e->next;
        } while (e != list);
        urb->next = list;
        urb->prev = list->prev;
        list->prev->next = urb;
        list->prev = urb;
    }
    assert(urb_present(list, urb));
    return list;
}

static usbio_buffer_t* urb_unlink(usbio_buffer_t* list, usbio_buffer_t* urb) {
    assert(urb_present(list, urb));
    if (list == urb) { list = list->next; } // head element?
    if (list == urb) { // single element case
        assert(urb->prev == urb && urb->next == urb);
        list = null;
    } else {
        usbio_buffer_t* n = urb->next;
        usbio_buffer_t* p = urb->prev;
        p->next = urb->next;
        n->prev = urb->prev;
        assert(p->next->prev == p && p == p->prev->next); // I am terrible with double linked lists
        assert(n->next->prev == n && n == n->prev->next); // Thus: paranoia
    }
    urb->prev = null; urb->next = null;
    assert(!urb_present(list, urb));
    return list;
}

static int check_urb_parameter(usbio_buffer_t* urb) {
    // urb->o.overlapped.Internal can be STATUS_PENDING 0x103 or 0x0 or error code
    assert(urb->o.overlapped.Internal == 0 && urb->prev == null && urb->next == null);
    assert(urb->data != null && urb->bytes >= 0 || urb->data == null && urb->bytes == 0 && urb->op == USB_CTRL);
    return urb->o.overlapped.Internal != 0 || urb->prev != null || urb->next != null ||
           urb->data == null && urb->op != USB_CTRL || urb->bytes < 0 ? ERR_INVALID_PARAMETER : 0;
}

static int check_pipe_parameter(usbio_file_t fd, int pipe) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    assertion(pipe == 0 || pipe == usb_file->pipe_bulk_out1 || pipe == usb_file->pipe_bulk_in1, "invalid pipe=0x%02X", pipe);
    return pipe == 0 || pipe == usb_file->pipe_bulk_out1 || pipe == usb_file->pipe_bulk_in1 ? 0 : ERR_INVALID_PARAMETER;
}

static int check_file_parameter(usbio_file_t fd) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    assert(usb_file != null && usb_file->file != INVALID_HANDLE_VALUE && usb_file->file != null);
    if (usb_file == null || usb_file->file == INVALID_HANDLE_VALUE || usb_file->file == null) {
        return ERR_PIPE_NOT_CONNECTED;
    } else {
        return 0;
    }
}

static int schedule_io(usbio_file_t fd, int op, WINUSB_SETUP_PACKET* control_ep_packet, usbio_buffer_t* urb) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    assert(usb_file->locked > 0);
    int r = check_file_parameter(fd);
    if (r == 0) { r = check_urb_parameter(urb); }
    if (r == 0) {
        assert(urb->o.file == fd);
        assert(urb->o.urb  == urb);
        assert(urb->o.magic == USBIO_MAGIC);
        urb->error = 0; // always reset .error to zero
        // https://docs.microsoft.com/en-us/windows/desktop/api/minwinbase/ns-minwinbase-_overlapped
        assert(urb->o.overlapped.Internal == 0);  // not STATUS_PENDING otherwise status code for completed request
        urb->o.overlapped.InternalHigh = 0;       // bytes tranferred in last transfer
        if (r == 0) {
            urb->op = op;
            if (op == USB_CTRL) {
                urb->xid = atomics_increment_int32(&usb_file->xid_ctrl);
                r = usbio_set_timeout(fd, 0, urb->timeout);
                if (r != 0) { trace("usbio_set_timeout(%d, 0, %d) failed %s", fd, urb->timeout, strerr(r)); }
                if (r == 0) {
                    r = WinUsb_ControlTransfer(usb_file->usb, *control_ep_packet, (byte*)urb->data, urb->bytes, null, &urb->o.overlapped) ? 0 : GetLastError();
                    if (r != 0 && r != ERROR_IO_PENDING) { trace("WinUsb_ControlTransfer(%d) failed %s", fd, strerr(r)); }
                }
//              dtrace("WinUsb_ControlTransfer(u=%p o=%p bytes=%d xid=%d) r=%d", urb, &urb->o.overlapped, urb->bytes, urb->xid, r);
            } else if (op == USB_READ || op == USB_REAP) {
                urb->xid = atomics_increment_int32(&usb_file->xid_read);
                if (op == USB_REAP) {
                    assert(urb->bytes == USBIO_BULK_REQUEST_SIZE);
                } else {
                    assert(urb->bytes > 0);
                }
                r = WinUsb_ReadPipe(usb_file->usb, (byte)usb_file->pipe_bulk_in1, (byte*)urb->data, urb->bytes, null, &urb->o.overlapped) ? 0 : GetLastError();
//              dtrace("WinUsb_ReadPipe(u=%p o=%p pipe=0x%02X bytes=%d xid=%d) r=%d", urb, &urb->o.overlapped, usb_file->pipe_bulk_in, urb->bytes, urb->xid, r);
            } else if (op == USB_WRITE) {
                urb->xid = atomics_increment_int32(&usb_file->xid_wrte);
                r = WinUsb_WritePipe(usb_file->usb, (byte)usb_file->pipe_bulk_out1, (byte*)urb->data, urb->bytes, null, &urb->o.overlapped) ? 0 : GetLastError();
//              dtrace("WinUsb_WritePipe(u=%p o=%p pipe=0x%02X bytes=%d xid=%d) r=%d", urb, &urb->o.overlapped, usb_file->pipe_bulk_out, urb->bytes, urb->xid, r);
            } else {
                assertion(false, "unexpected and/or invalid op=%d", op);
            }
            if (r == ERROR_IO_PENDING) { r = 0; } // this is expected
        }
//      By the time control gets here I/O may have been completed thus next line is incorrect
//      if (r == 0) { assert(urb->o.overlapped.Internal == STATUS_PENDING); }
//      Line above is kept here to illustrate how incorrect simple assumptions may be...
    }
    urb->error = r;
    if (r == 0) {
        usb_file->pending = urb_link(usb_file->pending, urb);
    }
    if (r != 0) { dtrace("schedule_io() failed %s", strerr(r)); }
    return r;
}

static int submit_urb(usbio_file_t fd, int pipe, usbio_buffer_t* urb) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = check_file_parameter(fd);
    if (r == 0) { r = check_pipe_parameter(fd, pipe); }
    if (r == 0) { r = check_urb_parameter(urb); }
    if (r == 0) {
        urb->o.magic = USBIO_MAGIC;
        urb->o.file = fd;
        urb->o.urb = urb;
        assert(urb->data != null && urb->bytes > 0);
        assert(urb->prev == null && urb->next == null);
        urb->error = 0;
        lock(usb_file);
//      dtrace("urb=%p overlapped=%p", urb, &urb->o.overlapped);
        urb->bytes = USBIO_BULK_REQUEST_SIZE;
        r = usb_file->closing ? ERROR_FILE_HANDLE_REVOKED : schedule_io(fd, USB_REAP, null, urb);
        unlock(usb_file);
    }
    if (r != 0) {
        trace("error %s", strerr(r));
    }
    return r;
}

int usbio_submit_urb(usbio_file_t fd, int pipe, usbio_buffer_t* urb) {
    int r = check_urb_parameter(urb);
    if (r == 0) {
        assert(urb->data != null && urb->bytes == USBIO_BULK_REQUEST_SIZE);
        r = submit_urb(fd, pipe, urb);
    }
    if (r != 0) {
        trace("error %s", strerr(r));
    }
    return r;
}

static int cancel_overlapped(usbio_file_t fd, usbio_buffer_t* urb) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = 0;
    assertion(usb_file->locked != 0, "must be called in locked state");
    // It is possible that caller did not submit urb it is trying to cancel.
    // Not exactly correct behavior but asserting on it does not do too much good.
    assert(urb->o.file == fd || urb->o.file == usbio_file_invalid);
    assert(urb->o.magic == USBIO_MAGIC || urb->o.magic == 0);
    if (urb->o.magic == USBIO_MAGIC && urb->o.file == fd && urb->o.overlapped.Internal == STATUS_PENDING) {
//      dtrace("CancelIoEx(urb=%p o.=%p) ", urb, &urb->o.overlapped);
        r = CancelIoEx(usb_file->file, (OVERLAPPED*)&urb->o.overlapped) ? 0 : GetLastError();
        if (r == ERROR_NOT_FOUND) { r = 0; } // ERROR_NOT_FOUND is OK because the kernel driver may have completed async transfer
        if (r != 0) { dtrace("CancelIo(usb_file->file=%p) error %s", usb_file->file, strerr(r)); }
    }
    return r;
}

int usbio_discard_urb(usbio_file_t fd, usbio_buffer_t* urb) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    lock(usb_file);
    int r = cancel_overlapped(fd, urb);
    unlock(usb_file);
    return r;
}

static usbio_buffer_t* find_ready_urb(usbio_file_t fd, int op, usbio_buffer_t* u) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    usbio_buffer_t* urb = null;
    // if this is USB_READ|REAP or USB_CTRL we do have urb != null
    // otherwise see if it is present in the ready list:
    if (usb_file->ready != null) {
        usbio_buffer_t* e = usb_file->ready;
        do {
            if (e == u || (u == null && e->op == op)) { urb = e; break; }
            e = e->next;
        } while (e != usb_file->ready);
    }
    return urb;
}

int usbio_urb_no;

static int usb_io(usbio_file_t fd, int op, WINUSB_SETUP_PACKET* packet, usbio_buffer_t* u, usbio_buffer_t** out, int timeout_milliseconds) {
    assertion(timeout_milliseconds != 0, "timeout_milliseconds=%d", timeout_milliseconds); // -1 means forever, == 0 meaningless
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    assert(usb_file->locked > 0);
    assertion(*out == null, "it is good idea to initialize result variables by null");
    *out = null;
    int r = check_file_parameter(fd);
    if (r == 0) { r = out == null ? ERR_INVALID_PARAMETER : 0; }
    // USB_REAP urb requests are submitted in advance and there is more then one that can be ready
    // USB_WRITE and USB_CTRL urbs are allocated on stack and passed via "u" parameter for schedule_io()
    if (op == USB_REAP) {
        assertion(u == null, "must be null, see usbio_submit_urb()");
    } else if (op == USB_CTRL || op == USB_WRITE || op == USB_READ) {
        assertion(u != null, "cannot be null");
    } else {
        assertion(false, "invalid operation %d", op);
    }
    if (r == 0 && u != null) {
        r = schedule_io(fd, op, packet, u);
    }
    if (r == 0) {
        while (*out == null && !usb_file->closing) {
            usbio_buffer_t* urb = find_ready_urb(fd, op, u);
            if (urb == null) {
                if (!usb_file->closing) {
                    atomics_decrement_int32(&usb_file->locked);
                    // TODO: in closing stage - wait with timeout!
//                  dtrace(">pthread_cond_wait(signal) op=%s gettid()=%d", OP_NAMES[op], gettid());
                    r = pthread_cond_timed_wait_np(&usb_file->signal, &usb_file->lock, timeout_milliseconds);
//                  dtrace("<pthread_cond_wait(signal) op=%s gettid()=%d", OP_NAMES[op], gettid());
                    atomics_increment_int32(&usb_file->locked);
                    if (r != 0) {
                        cancel_overlapped(fd, u);
                    }
                    urb = find_ready_urb(fd, op, u);
                }
            }
            bool found = urb != null;
            if (found && u != null && u != urb) {
                rtrace("this is not urb that usb_io() is waiting on!");
                // this is not urb that usb_io() is waiting on
                found = false;
            }
            if (found) {
                int status = (int)urb->o.overlapped.Internal;
                assert(status != (int)STATUS_PENDING);
                r = status == 0 ? 0 : WINERR(translate_ntstatus_to_windows(status));
                urb->error = r;
                usb_file->ready = urb_unlink(usb_file->ready, urb);
                // do not link back into .submitted list because client USB_READ client will do it
                assert(urb->o.file == fd);
                assert(urb->op == op);
                *out = urb;
                // paranoia: check order
                if (op == USB_CTRL) {
                    if (usb_file->xid_ctrl_last != 0) {
                        assertion(urb->xid > usb_file->xid_ctrl_last, "usb->xid=%d out of order last=%d next=%d", urb->xid, usb_file->xid_ctrl_last, usb_file->xid_ctrl);
                    }
                    usb_file->xid_ctrl_last = urb->xid;
                } else if (op == USB_READ || op == USB_REAP) {
                    if (usb_file->xid_read_last != 0) {
                        assertion(urb->xid > usb_file->xid_read_last, "usb->xid=%d out of order last=%d next=%d", urb->xid, usb_file->xid_read_last, usb_file->xid_read);
                    }
                    usb_file->xid_read_last = urb->xid;
                } else if (op == USB_WRITE) {
                    if (usb_file->xid_wrte_last != 0) {
                        assertion(urb->xid > usb_file->xid_wrte_last, "usb->xid=%d out of order last=%d next=%d", urb->xid, usb_file->xid_wrte_last, usb_file->xid_wrte);
                    }
                    usb_file->xid_wrte_last = urb->xid;
                } else {
                    assertion(false, "invalid operation %d", urb->op);
                }
                assert(r == (*out)->error);
            } else {
//              dtrace0("urb not found");
            }
        }
    }
    if (r == 0 && usb_file->closing) {
        r = ERROR_FILE_HANDLE_REVOKED;
    }
    return r;
}

int usbio_reap_urb(usbio_file_t fd, usbio_buffer_t** out) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = check_file_parameter(fd);
    if (r == 0) { r = out == null ? ERR_INVALID_PARAMETER : 0; }
    if (r == 0) {
        lock(usb_file);
        r = usb_io(fd, USB_REAP, null, null, out, -1);
        unlock(usb_file);
        usbio_urb_no++;
    }
    return r;
}

int usbio_bulk_out(usbio_file_t fd, byte pipe, const void* data, int bytes) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    assert(usb_file != null && data != null && bytes >= 0); // zero size packets are allowed and passed to USB driver stack
    int r = check_file_parameter(fd);
    if (r == 0) { r = data == null || bytes < 0 || pipe != usb_file->pipe_bulk_out1 ? ERR_INVALID_PARAMETER : 0; }
    if (r == 0) {
        usbio_buffer_t urb = {};
        urb.data  = (void*)data;
        urb.bytes = bytes;
        urb.o.magic = USBIO_MAGIC;
        urb.o.file  = fd;
        urb.o.urb   = &urb;
        usbio_buffer_t* out = null;
        lock(usb_file);
        r = usb_io(fd, USB_WRITE, null, &urb, &out, 1000); // 1 second TODO: add timeout to API
        if (r == 0) {
            assert(out == &urb);
        }
        unlock(usb_file);
    }
    return r;
}


static bool has_reads(usbio_file_t fd) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    if (usb_file->pending == null) {
        return false;
    } else {
        usbio_buffer_t* urb = usb_file->pending;
        do {
            if (urb->op == USB_REAP || urb->op == USB_READ) { return true; }
            urb = urb->next;
        } while (urb != usb_file->pending);
        return false;
    }
}

int usbio_bulk_in(usbio_file_t fd, byte pipe, const void* data, int bytes, int* transferred) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    assert(usb_file != null && data != null && bytes > 0 && transferred != null);
    int r = check_file_parameter(fd);
    if (r == 0) { r = data == null || bytes <= 0 || pipe != usb_file->pipe_bulk_in1 || transferred == null ? ERR_INVALID_PARAMETER : 0; }
    if (r == 0) {
        usbio_buffer_t urb = {};
        urb.data = (void*)data;
        urb.bytes = bytes;
        urb.o.magic = USBIO_MAGIC;
        urb.o.file = fd;
        urb.o.urb = &urb;
        usbio_buffer_t* out = null;
        lock(usb_file);
        bool pending_read_ops = has_reads(fd);
        assertion(!pending_read_ops, "cannot mix usbio_bulk_in() with submit()/reap() operations");
        if (pending_read_ops) {
            r = ERR_BUSY;
        } else {
            r = usb_io(fd, USB_READ, null, &urb, &out, -1); // forever? TODO: add timeout to API
        }
        if (r == 0) {
            assert(out == &urb);
            *transferred = out->bytes;
        } else {
            *transferred = 0;
            if (urb.error != 0) { r = urb.error; }
        }
        assert(!has_reads(fd));
        unlock(usb_file);
    }
    return r;
}

int usbio_control(usbio_file_t fd, usbio_ctrl_setup_t* setup, void* data, int* bytes, int timeout_milliseconds) {
    assertion(valid_fd(fd), "fd=%d", fd);
    assertion(timeout_milliseconds != 0, "timeout_milliseconds=%d", timeout_milliseconds); // -1 means forever, == 0 meaningless
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    WINUSB_SETUP_PACKET packet = {};
    assertion((setup->rt & 0x01) == 0, "pr->request_type=0x%02X", setup->rt);
    packet.RequestType = setup->rt & 0xFE;  // Plan B: clear off interface bit
    packet.Request = setup->req;
    packet.Value   = setup->val;
    packet.Index   = setup->ix;
    packet.Length  = setup->len;
    usbio_buffer_t urb = {};
    urb.timeout  = timeout_milliseconds < 0 ? USBIO_CTRL_EP_TIMEOUT_MS : timeout_milliseconds; // in milliseconds
    urb.data  = (void*)data;
    urb.bytes = setup->len;
    urb.o.magic = USBIO_MAGIC;
    urb.o.file  = fd;
    urb.o.urb   = &urb;
    usbio_buffer_t* out = null;
    lock(usb_file);
    int r = usb_io(fd, USB_CTRL, &packet, &urb, &out, timeout_milliseconds * 2); //
    unlock(usb_file);
    if (r == 0 && out != null) {
        r = out->error;
        if (r == 0) {
            if (bytes != null) { *bytes = out->bytes; }
            if (data != null) { memcpy(data, out->data, out->bytes); }
        }
    }
    return r;
}

static int usbio_open_callback(void* that, usbio_file_t file, const char* name, usbio_device_descriptor_t* desc) {
    usbio_open_ctx_t* ctx = (usbio_open_ctx_t*)that;
    const int id_vendor = desc->idVendor;
    const int id_product = desc->idProduct;
//  dtrace("%04X:%04X", id_vendor, id_product);
    if (id_vendor == ctx->vid && id_product == ctx->pid) {
        if (ctx->i < ctx->n) {
            ctx->files[ctx->i] = file;
//          dtrace("%04X:%04X files[%d]=%d", id_vendor, id_product, ctx->i, ctx->files[ctx->i]);
            ctx->i++;
            assertion(ctx->i <= ctx->n, "%d out of range [0..%d]", ctx->i, ctx->n);
            return USBIO_DONT_CLOSE_FD | (ctx->i == ctx->n ? USBIO_STOP_ITERATION : 0);
        } else if (ctx->i == ctx->n) {
            dtrace("too many files for vid:pid=%04X:%04X ctx.i=%d ctx.n=%d", ctx->vid, ctx->pid, ctx->i, ctx->n);
        } else {
            assert(false);
        }
    }
    return 0;
}

int usbio_open(int vid, int pid, usbio_file_t* files, int count) { // returns number of openned files
    assertion(count > 0, "count=%d must be >= 1", count);
    for (int i = 0; i < count; i++) { files[i] = usbio_file_invalid; }
    usbio_open_ctx_t ctx = {};
    ctx.vid = vid;
    ctx.pid = pid;
    ctx.files = files;
    ctx.n = count;
    errno = 0;
    usb_enumerate(&ctx, &ctx, usbio_open_callback);
    if (ctx.i == 0) {
//      dtrace("not found any devices: 0x%04X:%04X", vid, pid);
        ctx.i = -1;
        errno = ENODEV;
    } else {
//      dtrace("found %d device(s): 0x%04X:%04X", ctx.i, vid, pid);
    }
    return ctx.i;
}

byte usbio_pipe_bulk_in1(usbio_file_t fd) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    return usb_file->pipe_bulk_in1;
}

byte usbio_pipe_bulk_out1(usbio_file_t fd) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    return usb_file->pipe_bulk_out1;
}

byte usbio_pipe_bulk_in2(usbio_file_t fd) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    return usb_file->pipe_bulk_in2;
}

byte usbio_pipe_bulk_out2(usbio_file_t fd) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    return usb_file->pipe_bulk_out2;
}

static int cancel_all_pending(usbio_file_t fd) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = 0;
    if (usb_file->pending != null) {
        usbio_buffer_t* urb = usb_file->pending;
        do {
//          dtrace("CancelIoEx(urb=%p .o=%p)", usb_file->pending, &usb_file->pending->o.overlapped);
            int rc = cancel_overlapped(fd, usb_file->pending);
            if (rc != 0) { dtrace("cancel_overlapped() failed %s", strerr(rc)); r = rc;  }
            urb = urb->next;
        } while (urb != usb_file->pending);
        millisleep(0); // yeild to i/o kernel thread to post to i/o completion thread
    }
    return r;
}

int usbio_close(usbio_file_t fd) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    if (usb_file == null) {
        dtrace0("ERROR: usb_file == null. Called twice?");
        return ERR_INVALID_HANDLE;
    } else {
        int rc[4] = {}; // results of various close operations
        if (usb_file->thread != pthread_null) {
            lock(usb_file);
            usb_file->closing = true;
            pthread_cond_broadcast(&usb_file->signal);
            if (usb_file->pending != null) {
                rc[0] = cancel_all_pending(fd);
                double deadline = time_in_milliseconds() + 16;
                while (usb_file->pending != null && time_in_milliseconds() < deadline) { millisleep(1); }
            }
            quit_polling_thread(fd); // after all pending processed
            // usb_file->pending must be null at this point. This is Plan B:
            while (usb_file->pending != null) { usb_file->pending = urb_unlink(usb_file->pending, usb_file->pending); }
            unlock(usb_file);
            pthread_join(usb_file->thread, null);
            usb_file->thread = pthread_null;
        }
        if (usb_file->usb != null) {
            rc[1] = WinUsb_Free(usb_file->usb) ? 0 : GetLastError();
            assert(rc[1] == 0); usb_file->usb = null;
            if (rc[1] != 0) { dtrace("WinUsb_Free() failed %s", strerr(rc[1])); }
        }
        if (usb_file->file != null && usb_file->file != INVALID_HANDLE_VALUE) {
            rc[2] = CloseHandle(usb_file->file) ? 0 : GetLastError();
            assert(rc[2] == 0); usb_file->file = INVALID_HANDLE_VALUE;
            if (rc[2] != 0) { dtrace("CloseHandle() failed %s", strerr(rc[2])); }
        }
        if (usb_file->port != null) { rc[3] = CloseHandle(usb_file->port) ? 0 : GetLastError(); }
        pthread_cond_destroy(&usb_file->signal);
        mutex_destroy(&usb_file->lock);
        assert(usb_file->pending == null);
//      Ready queue may still contain urbs that arrived after CancelIOEx() but were not read by the client. It is OK.
//      assert(usb_file->ready == null);
        int r = rc[0]; // report first error of possible errors
        for (int i = 1; i < countof(rc) && r == 0; i++) { r = rc[i]; }
        if (r == 0) {
            mutex_lock(&usbio_files_mutex);
            memset(usb_file, 0, sizeof(*usb_file));
            usb_file->fd = usbio_file_invalid;
            mutex_unlock(&usbio_files_mutex);
        }
        return r;
    }
}

/*
see: https://gist.github.com/leo-zspace/b0d97dab708814a528186cbdb47fb5cb
     http://caxapa.ru/thumbs/161376/WinUsb_HowTo.pdf (March 30, 2009)

|Policy number | Policy name    | Default | comments                              |
|-----|-------------------------|---------|---------------------------------------|
|0x01 | SHORT_PACKET_TERMINATE  | Off     | [out]                                 |
|0x02 | AUTO_CLEAR_STALL        | Off     | [in]                                  |
|0x03 | PIPE_TRANSFER_TIMEOUT   | 5       | [in/out]                              | // milliseconds[^1] for control, 0 (infinity) for others
|0x04 | IGNORE_SHORT_PACKETS    | Off     | [in]                                  | // Completes a read request based on the number of bytes read.
|0x05 | ALLOW_PARTIAL_READS     | On      | [in]                                  | // AUTO_FLUSH=on *discards* data!
|0x06 | AUTO_FLUSH              | Off     | [in]                                  |
|0x07 | RAW_IO                  | Off     | [in]                                  |
|0x09 | RESET_PIPE_ON_RESUME    | Off     | [in/out]                              | // continue after power management suspend/resume
[^1]: actual PIPE_TRANSFER_TIMEOUT value is 5000 because there is evidence (and poor documentation) that it is in microseconds
    ULONG time_out = 0;
    ULONG value_bytes = (uint32_t)sizeof(time_out);
    !WinUsb_GetPipePolicy(usb_file->usb, endpoint, PIPE_TRANSFER_TIMEOUT, &value_bytes, (void*)&time_out);

    old:
        https://github.com/tenderlove/libusb/blob/master/libusb/os/windows_usb.c#L2372 winusb_configure_endpoints()
    newer:
        https://github.com/libusb/libusb/blob/dea5a8e96891134829b97a75adfdf5586f908b6d/libusb/os/windows_winusb.c#L2230
    and:
        http://libusbk.sourceforge.net/UsbK3/usbk_pipe_management.html

    RAW_IO:
        https://msdn.microsoft.com/en-us/library/windows/hardware/ff728833(v=vs.85).aspx
    If enabled, transfers bypass queuing and error handling to boost performance for multiple read requests.
    WinUSB handles read requests as follows:
    A request that is not a multiple of the maximum endpoint packet size fails.
    A request that is greater than the maximum transfer size supported by WinUSB fails.
    All well-formed requests are immediately sent down to the USB core stack to be scheduled in the host controller.
    Enabling this setting significantly improves the performance of multiple read requests by reducing the delay
    between the last packet of one transfer and the first packet of the next transfer.

    IMPORTANT:

    https://patents.google.com/patent/US7577765B2/en
    AUTO_FLUSH (0x06)
    If Value is FALSE (zero) and the device returns more data than was requested, the remaining data will be discarded.
    If Value is TRUE, the behavior on the value of ALLOW_PARTIAL_READS. Either the data will be saved and then returned
    at the beginning of the data for the following read request, or the request will fail. The default is FALSE.

    https://patents.google.com/patent/CN1716225A/en
    Describes AUTO_FLUSH completely in reverse to Microsoft documentation
    [189] AUTO_FLUSH (0x06)
    [190] If the value is FALSE (0) and the device returns the requested data than the remaining data is discarded.
          If the value is TRUE, the behavior depends on the value of ALLOW_PARTIAL_READS. Or data will be saved and
          returned to the starting position for subsequent data read request, or the request failed. The default value is FALSE.

    This is direct contradiction to

    https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb-functions-for-pipe-policy-modification
    AUTO_FLUSH defines WinUSB's behavior when ALLOW_PARTIAL_READS is enabled. If ALLOW_PARTIAL_READS is disabled, the AUTO_FLUSH value is ignored by WinUSB.

    WinUSB can either discard the remaining data or send it with the caller's next read request.

    If enabled (policy parameter value is TRUE or nonzero), WinUSB discards the extra bytes without any error code.
    If disabled (policy parameter value is FALSE or zero), WinUSB saves the extra bytes, adds them to the beginning
    of the caller's next read request, and then sends the data to the caller in the next read operation.

    I did not test it yet but I will.
*/

static int usb_set_policy_byte(usbio_file_t fd, byte endpoint, int policy, const char* name, byte value) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = WinUsb_SetPipePolicy(usb_file->usb, endpoint, policy, sizeof(value), &value) ? 0 : GetLastError();
    if (r != 0) { rtrace("failed to set policy for %s := %d for endpoint %02X error %s", name, value, endpoint, strerr(r)); }
    return r;
}

static int usb_set_policy_long(usbio_file_t fd, byte endpoint, int policy, const char* name, uint32_t value) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = WinUsb_SetPipePolicy(usb_file->usb, endpoint, policy, sizeof(value), &value) ? 0 : GetLastError();
    if (r != 0) { rtrace("failed to set policy for %s := %d for endpoint %02X error %s", name, value, endpoint, strerr(r)); }
    return r;
}

static int usb_set_power_policy_byte(usbio_file_t fd, int policy, const char* name, byte value) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = WinUsb_SetPowerPolicy(usb_file->usb, policy, sizeof(value), &value) ? 0 : GetLastError();
    if (r != 0) { rtrace("failed to set power policy for %s := %d error %s", name, value, strerr(r)); }
    return r;
}

#define set_policy_byte(fd, endpoint, policy, value) usb_set_policy_byte(fd, endpoint, policy, # policy, value)
#define set_policy_long(fd, endpoint, policy, value) usb_set_policy_long(fd, endpoint, policy, # policy, value)

#define set_power_policy_byte(fd, policy, value) usb_set_power_policy_byte(fd, policy, # policy, value)

static int configure_in_endpoint(int fd, byte pipe_bulk_in) {
    int r =      set_policy_byte(fd, pipe_bulk_in, RESET_PIPE_ON_RESUME, 0);
    r = r == 0 ? set_policy_byte(fd, pipe_bulk_in, IGNORE_SHORT_PACKETS, 0) : r;
    r = r == 0 ? set_policy_byte(fd, pipe_bulk_in, AUTO_FLUSH, 1) : r;
    r = r == 0 ? set_policy_byte(fd, pipe_bulk_in, ALLOW_PARTIAL_READS, 1) : r;
    r = r == 0 ? set_policy_byte(fd, pipe_bulk_in, RAW_IO, 0) : r; // default NOT RAW_IO on Windows
    r = r == 0 ? set_policy_byte(fd, pipe_bulk_in, AUTO_CLEAR_STALL, 1) : r;
    return r;
}

static int winusb_configure_endpoints(usbio_file_t fd) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    assertion(usb_file->endpoint_count <= 3, "only supports builk in/out and control endpoints");
    byte array_set(endpoints, 3);
    array_set_clear(endpoints);
    array_set_add(endpoints, 0); // control endpoint, always 0 and always present
    if (usb_file->pipe_bulk_in1  != 0) { array_set_add(endpoints, usb_file->pipe_bulk_in1); }
    if (usb_file->pipe_bulk_out1 != 0) { array_set_add(endpoints, usb_file->pipe_bulk_out1); }
    if (usb_file->pipe_bulk_in2  != 0) { array_set_add(endpoints, usb_file->pipe_bulk_in2); }
    if (usb_file->pipe_bulk_out2 != 0) { array_set_add(endpoints, usb_file->pipe_bulk_out2); }
    // At this time, 2019-06-18, intrinsic calibration board does not have bulk endpoints.
    // Tracking controller has bulk IN endpoint and no bulk OUT endpoint.
    int r = set_power_policy_byte(fd, AUTO_SUSPEND, 0); // default: AUTO_SUSPEND=1 or registry settings
    uint32_t timeout = USBIO_CTRL_EP_TIMEOUT_MS;
    r = r == 0 ? set_policy_long(fd, 0, PIPE_TRANSFER_TIMEOUT, timeout) : r; // only applicable to control endpoint
    if (usb_file->pipe_bulk_out1 != 0) {
        r = r == 0 ? set_policy_byte(fd, usb_file->pipe_bulk_out1, SHORT_PACKET_TERMINATE, 1) : r;
        r = r == 0 ? set_policy_byte(fd, usb_file->pipe_bulk_out1, RESET_PIPE_ON_RESUME, 0) : r;
    }
    if (usb_file->pipe_bulk_out2 != 0) {
        r = r == 0 ? set_policy_byte(fd, usb_file->pipe_bulk_out2, SHORT_PACKET_TERMINATE, 1) : r;
        r = r == 0 ? set_policy_byte(fd, usb_file->pipe_bulk_out2, RESET_PIPE_ON_RESUME, 0) : r;
    }
    if (usb_file->pipe_bulk_in1 != 0) {
        r = configure_in_endpoint(fd, usb_file->pipe_bulk_in1);
    }
    if (usb_file->pipe_bulk_in2 != 0) {
        r = configure_in_endpoint(fd, usb_file->pipe_bulk_in2);
    }
    return r;
}

int usbio_set_raw(usbio_file_t fd, bool raw) { // Linux: always raw
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = check_file_parameter(fd);
    if (r == 0) {  r = set_policy_byte(fd, usb_file->pipe_bulk_in1, RAW_IO, raw); }
    return r;
}

static OVERLAPPED QUIT;

// #include <ntstatus.h>

enum { // cannot include <ntstatus.h> here thus just define some necessary constants
    STATUS_CANCELLED = 0xC0000120
};

static void* usbio_polling_thread(void* p) {
    pthread_set_name_np(pthread_self(), "winusb_polling");
    pthread_setschedprio_np(pthread_self(), pthread_get_priority_realtime_np());
    usbio_file_t fd = (usbio_file_t)(uintptr_t)p;
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    for (;;) {
        DWORD number_of_bytes_transferred = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = null;
        int r = GetQueuedCompletionStatus(usb_file->port, &number_of_bytes_transferred, &key, &overlapped, INFINITE) ? 0 : GetLastError();
//      dtrace("overlapped %p number_of_bytes_transferred %d r %d", overlapped, number_of_bytes_transferred, r);
        if (r != 0 && r != WAIT_TIMEOUT && r != ERROR_OPERATION_ABORTED) { dtrace("GetQueuedCompletionStatus() = %s", strerr(r)); }
        if (overlapped == &QUIT) { // gracefull exit
//          dtrace0("&QUIT");
            break;
        }
        if (overlapped != null) {
            assertion((void*)key == usb_file, "overlapped %p key=%p does not belong to the file %p",  overlapped, key, usb_file);
            int status = (int)overlapped->Internal;
            assert(status != STATUS_PENDING);
            if (r == ERROR_OPERATION_ABORTED) { assert(status == STATUS_CANCELLED); }
            usbio_overlapped_t* o = (usbio_overlapped_t*)overlapped;
            assert(o->magic == USBIO_MAGIC);
            assert(o->file  == fd);
            assert(&o->overlapped  == overlapped);
            usbio_buffer_t* urb = o->urb;
            assert(urb->xid > 0);
            if (urb->error == 0 && r != 0) { urb->error = WINERR(r); }
            if (urb->error == 0 && status != 0) { urb->error = WINERR(translate_ntstatus_to_windows(status)); }
            urb->bytes = (int)o->overlapped.InternalHigh;
            lock(usb_file);
            if (!urb_present(usb_file->pending, urb)) {
                // STATUS_CANCELLED (0xC0000120) because usb_close is canceling requests
                if (status != 0xC0000120) { trace("usb_file->pending() does not contain URB"); }
            } else {
                usb_file->pending = urb_unlink(usb_file->pending, urb);
            }
            assert(urb->prev == null && urb->next == null);
            usb_file->ready = urb_link(usb_file->ready, urb);
//          dtrace("op=%d %s ready=%p urb=%p urb .bytes=%d .error=%d pthread_cond_broadcast(signal)", urb->op, OP_NAMES[urb->op], usb_file->ready, urb, urb->bytes, urb->error);
            pthread_cond_broadcast(&usb_file->signal);
            unlock(usb_file);
/*
        } else if (r == WAIT_TIMEOUT) { // 10 times per second check all pending overlapped requests no matter what
            lock(usb_file);
            pthread_cond_broadcast(&usb_file->signal);
            unlock(usb_file);
        }
*/      } else {
            assertion(overlapped != null, "???!!!");
        }
    }
//  trace("done");
    return null;
}

static void quit_polling_thread(usbio_file_t fd) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = PostQueuedCompletionStatus(usb_file->port, 0, 0, &QUIT) ? 0 : GetLastError();
    assertion(r == 0, "FATAL: PostQueuedCompletionStatus(&QUIT) failed - no recovery possible. %s", strerr(r));
    if (r != 0) { ExitProcess(r); } // this is what fatal means
}

int usbio_ep_count(usbio_file_t fd) {
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    return (usb_file->pipe_bulk_in1 != 0) + (usb_file->pipe_bulk_in2 != 0) + (usb_file->pipe_bulk_out1 != 0) + (usb_file->pipe_bulk_out2 != 0);
}

static int winusb_init(usbio_file_t fd, usbio_open_ctx_t* ctx, usbio_device_descriptor_t* dd) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    SetLastError(0);
    int r = WinUsb_Initialize(usb_file->file, &usb_file->usb) ? 0 : GetLastError();
    if (r != 0) { dtrace("WinUsb_Initialize(%04X:%04X) failed %s", ctx->vid, ctx->pid, strerr(r)); return r; }
    USB_INTERFACE_DESCRIPTOR id = {};
    SetLastError(0);
    r = WinUsb_QueryInterfaceSettings(usb_file->usb, 0, &id) ? 0 : GetLastError();
    if (r != 0) { dtrace("WinUsb_QueryInterfaceSettings() failed %s", strerr(r)); return r; }
    byte speed = 0;
    ULONG one_byte = sizeof(byte);
    bool b = WinUsb_QueryDeviceInformation(usb_file->usb, DEVICE_SPEED, &one_byte, &speed);
    assert(b);
    for (int i = 0; i < id.bNumEndpoints; i++) { // TODO: only single data endpoint is supported for now
        WINUSB_PIPE_INFORMATION  pipe_information;
        b = WinUsb_QueryPipe(usb_file->usb, 0, (byte)i, &pipe_information);
        assert(b);
        if (pipe_information.PipeType == UsbdPipeTypeControl) {
            dtrace("%04X:%04X Endpoint index: %d Pipe type: Control Pipe ID: 0x%02X",
                    ctx->vid, ctx->pid, i, pipe_information.PipeType, pipe_information.PipeId);
        } else if (pipe_information.PipeType == UsbdPipeTypeInterrupt) {
            dtrace("%04X:%04X Endpoint index: %d Pipe type: Interrupt Pipe ID: 0x%02X",
                    ctx->vid, ctx->pid, i, pipe_information.PipeType, pipe_information.PipeId);
        } else if (pipe_information.PipeType == UsbdPipeTypeIsochronous) {
            dtrace("%04X:%04X Endpoint index: %d Pipe type: Isochronous Pipe ID: 0x%02X",
                    ctx->vid, ctx->pid, i, pipe_information.PipeType, pipe_information.PipeId);
        } else if (pipe_information.PipeType == UsbdPipeTypeBulk) {
            if (USB_ENDPOINT_DIRECTION_IN(pipe_information.PipeId)) {
//              dtrace("%04X:%04X Bulk IN Endpoint index: %d Pipe type: %d Pipe ID: 0x%02X",
//                     ctx->vid, ctx->pid, i, pipe_information.PipeType, pipe_information.PipeId);
                ULONG maximum_transfer_size = 0; // 2MB
                ULONG pipe_policy_bytes = sizeof(maximum_transfer_size);
                r = WinUsb_GetPipePolicy(usb_file->usb, pipe_information.PipeId, MAXIMUM_TRANSFER_SIZE, &pipe_policy_bytes, &maximum_transfer_size) ? 0 : GetLastError();
                assert(r == 0);
                if (pipe_information.PipeId == 0x81) {
                    assert(usb_file->pipe_bulk_in1 == 0);
                    if (usb_file->pipe_bulk_in1 == 0) { usb_file->pipe_bulk_in1 = pipe_information.PipeId; }
                } else if (pipe_information.PipeId == 0x82) {
                    assert(usb_file->pipe_bulk_in2 == 0);
                    if (usb_file->pipe_bulk_in2 == 0) { usb_file->pipe_bulk_in2 = pipe_information.PipeId; }
                } else {
                    assertion(false, "unsupported endpoint 0x%02X", pipe_information.PipeId);
                }
            } else if (USB_ENDPOINT_DIRECTION_OUT(pipe_information.PipeId)) {
                if (pipe_information.PipeId == 0x01) {
                    assert(usb_file->pipe_bulk_out1 == 0);
                    if (usb_file->pipe_bulk_out1 == 0) { usb_file->pipe_bulk_out1 = pipe_information.PipeId; }
                } else if (pipe_information.PipeId == 0x02) {
                    assert(usb_file->pipe_bulk_out2 == 0);
                    if (usb_file->pipe_bulk_out2 == 0) { usb_file->pipe_bulk_out2 = pipe_information.PipeId; }
                } else {
                    assertion(false, "unsupported endpoint 0x%02X", pipe_information.PipeId);
                }
//              dtrace("%04X:%04X Bulk OUT Endpoint index: %d Pipe type: %d Pipe ID: 0x%02X",
//                      ctx->vid, ctx->pid, i, pipe_information.PipeType, pipe_information.PipeId);
            }
            usb_file->endpoint_count++;
        }
    }
    assert(id.bNumEndpoints == usb_file->endpoint_count);
    if (r == 0) {
        r = winusb_configure_endpoints(fd);
    }
    if (r == 0) {
        // In Microsoft theory we can have a single completion port created in the line below
        // for all opened USB files and upto number of CPU polling thread.
        // NumberOfConcurrentThreads == 0 means "the default (the number of processors in the system)."
        // https://docs.microsoft.com/en-us/windows/win32/fileio/createiocompletionport
        usb_file->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, null, 0, 1);
        r = usb_file->port != null ? 0 : GetLastError(); assert(r == 0);
        if (r != 0) { dtrace("CreateIoCompletionPort(%04X:%04X) failed %s", ctx->vid, ctx->pid, strerr(r)); return r; }
        // Line below actually 'attached' usb file handle to completion port.
        // Multiple handles can be attached to single port to reduce number of polling threads.
        // We do not do it - be if we decide to do it this is the place.
        HANDLE port = CreateIoCompletionPort(usb_file->file, usb_file->port, (uintptr_t)usb_file, 0);
        // return value is null on error or the same port that was passed in.
        r = port != null ? 0 : GetLastError(); assert(r == 0);
        if (r != 0) { dtrace("CreateIoCompletionPort(%04X:%04X) failed %s", ctx->vid, ctx->pid, strerr(r)); return r; }
        // There is no reason to have single thread per cpu core versus usbio file because we do NOT do any heavy
        // lifting in usbio polling thread. Microsoft was trying to optimize the situation when
        // threads doing GetCompletionStatus() also call callback functions that actually do work.
        // Instead usbio code employs double polling scheme which alleviates pressure on GetCompletionStatus()
        // polling thread to none and amortized for temporary performance degradation in clients polling threads.
    }
    if (r == 0) {
        r = pthread_create(&usb_file->thread, null, usbio_polling_thread, (void*)(uintptr_t)fd); assert(r == 0);
        if (r != 0) { dtrace("pthread_create(%04X:%04X) failed %s", ctx->vid, ctx->pid, strerr(r)); return r; }
    }
    return r;
}

static int winusb_create(usbio_file_t* file, const char* name, usbio_open_ctx_t* ctx, usbio_device_descriptor_t* dd,
                         void* that, usbio_enumerate_callback_t cb, bool* stop_iteration) {
    mutex_lock(&usbio_files_mutex);
    int fd = usbio_file_invalid;
    // file descriptor usbio_files[0] is not used intentionally - it reduces number of mistakes with index zero
    for (int i = 1; i < countof(usbio_files); i++) {
        if (usbio_files[i].fd == usbio_file_invalid) { fd = i; usbio_files[i].fd = fd; break; }
    }
    mutex_unlock(&usbio_files_mutex);
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = usb_file == null ? ERR_OUTOFMEMORY : 0;
    if (r == 0) {
        usb_file->thread = pthread_null;
        pthread_cond_init(&usb_file->signal, null);
        mutex_init(&usb_file->lock, 0);
        usb_file->file = INVALID_HANDLE_VALUE;
        usb_file->usb  = null;
        usb_file->file = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, null, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, null);
        r = usb_file->file == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    }
    if (r == 0) {
        int ri = winusb_init(fd, ctx, dd);
        if (ri != 0) {
            usbio_close(fd); usb_file = null;
        } else {
            int a = cb(that, fd, name, dd);
            if ((a & USBIO_DONT_CLOSE_FD) == 0) {
                usbio_close(fd); usb_file = null;
            }
            if (r == 0 && usb_file != null && (a & USBIO_STOP_ITERATION) != 0) { *stop_iteration = true; } // w/o closing file
        }
    }
    if (r == 0) {
        *file = fd;
    } else {
        *file = usbio_file_invalid;
        if (usb_file != null) {
            usbio_close(fd);
        }
    }
    return r;
}

int usbio_reset(usbio_file_t file) { return E_NOTIMPL; } // meaningless on Windows

int usbio_clear_halt(usbio_file_t file) { return E_NOTIMPL; } // meaningless on Windows

int usbio_abort_pipe(usbio_file_t fd, int pipe) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = usb_file->usb != null ? 0 : ERR_INVALID_HANDLE;
    if (r == 0) {
        // The WinUsb_AbortPipe function aborts all of the pending transfers for a pipe.
        r = WinUsb_AbortPipe(usb_file->usb, (byte)pipe) ? 0 : GetLastError();
        if (r != 0) { rtrace("WinUsb_AbortPipe() error %s", strerr(r)); }
    }
    return r;
}

int usbio_flush_pipe(usbio_file_t fd, int pipe) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = usb_file->usb != null ? 0 : ERR_INVALID_HANDLE;
    if (r == 0) {
        // The WinUsb_FlushPipe function discards any data that is cached in a pipe.
        r = WinUsb_FlushPipe(usb_file->usb, (byte)pipe) ? 0 : GetLastError();
        if (r != 0) { rtrace("WinUsb_AbortPipe() error %s", strerr(r)); }
    }
    return r;
}

int usbio_reset_pipe(usbio_file_t fd, int pipe) {
    // It has been noticed that on Windows 7, if you only call WinUsb_AbortPipe(),
    // without first calling WinUsb_ResetPipe(), the call to WinUsb_AbortPipe() hangs.
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    int r = usb_file->usb != null ? 0 : ERR_INVALID_HANDLE;
    if (r == 0) {
        // The WinUsb_ResetPipe function resets the data toggle and clears the stall condition on a pipe.
        r = WinUsb_ResetPipe(usb_file->usb, (byte)pipe) ? 0 : GetLastError();
        if (r != 0) { rtrace("WinUsb_AbortPipe() error %s", strerr(r)); }
    }
    return r;
}

int usbio_set_timeout(usbio_file_t fd, int pipe, int milliseconds) {
    assertion(valid_fd(fd), "fd=%d", fd);
    usbio_file_t_* usb_file = valid_fd(fd) ? &usbio_files[fd] : null;
    assert(milliseconds >= 0);
    ULONG timeout = (ULONG)milliseconds;
    return WinUsb_SetPipePolicy(usb_file->usb, (byte)pipe, PIPE_TRANSFER_TIMEOUT, sizeof(ULONG), &timeout) ? 0 : GetLastError();
}

enum { STACK_BUFFER_SIZE = 16 * 1024 };

#define usbdevs_get_property(b, device_info_list, info, prop, data_type, buffer, buffer_size) { \
    DWORD size = 0; \
    b = SetupDiGetDeviceRegistryPropertyA(device_info_list, &info, prop, &data_type, (BYTE*)buffer, buffer_size, &size) != false; \
    if (!b) { \
        buffer = (char*)stack_alloc(size * 2); \
        buffer_size = size * 2; \
        b = SetupDiGetDeviceRegistryPropertyA(device_info_list, &info, prop, &data_type, (BYTE*)buffer, buffer_size, &size) && size <= buffer_size; \
    } \
}

static const char* device_problem_description(int problem) {
    const char* s = "";
    switch (problem) {
        case CM_PROB_NOT_CONFIGURED            : s = "CM_PROB_NOT_CONFIGURED";             break;
        case CM_PROB_DEVLOADER_FAILED          : s = "CM_PROB_DEVLOADER_FAILED";           break;
        case CM_PROB_OUT_OF_MEMORY             : s = "CM_PROB_OUT_OF_MEMORY";              break;
        case CM_PROB_ENTRY_IS_WRONG_TYPE       : s = "CM_PROB_ENTRY_IS_WRONG_TYPE";        break;
        case CM_PROB_LACKED_ARBITRATOR         : s = "CM_PROB_LACKED_ARBITRATOR";          break;
        case CM_PROB_BOOT_CONFIG_CONFLICT      : s = "CM_PROB_BOOT_CONFIG_CONFLICT";       break;
        case CM_PROB_FAILED_FILTER             : s = "CM_PROB_FAILED_FILTER";              break;
        case CM_PROB_DEVLOADER_NOT_FOUND       : s = "CM_PROB_DEVLOADER_NOT_FOUND";        break;
        case CM_PROB_INVALID_DATA              : s = "CM_PROB_INVALID_DATA";               break;
        case CM_PROB_FAILED_START              : s = "CM_PROB_FAILED_START";               break;
        case CM_PROB_LIAR                      : s = "CM_PROB_LIAR";                       break;
        case CM_PROB_NORMAL_CONFLICT           : s = "CM_PROB_NORMAL_CONFLICT";            break;
        case CM_PROB_NOT_VERIFIED              : s = "CM_PROB_NOT_VERIFIED";               break;
        case CM_PROB_NEED_RESTART              : s = "CM_PROB_NEED_RESTART";               break;
        case CM_PROB_REENUMERATION             : s = "CM_PROB_REENUMERATION";              break;
        case CM_PROB_PARTIAL_LOG_CONF          : s = "CM_PROB_PARTIAL_LOG_CONF";           break;
        case CM_PROB_UNKNOWN_RESOURCE          : s = "CM_PROB_UNKNOWN_RESOURCE";           break;
        case CM_PROB_REINSTALL                 : s = "CM_PROB_REINSTALL";                  break;
        case CM_PROB_REGISTRY                  : s = "CM_PROB_REGISTRY";                   break;
        case CM_PROB_VXDLDR                    : s = "CM_PROB_VXDLDR";                     break;
        case CM_PROB_WILL_BE_REMOVED           : s = "CM_PROB_WILL_BE_REMOVED";            break;
        case CM_PROB_DISABLED                  : s = "CM_PROB_DISABLED";                   break;
        case CM_PROB_DEVLOADER_NOT_READY       : s = "CM_PROB_DEVLOADER_NOT_READY";        break;
        case CM_PROB_DEVICE_NOT_THERE          : s = "CM_PROB_DEVICE_NOT_THERE";           break;
        case CM_PROB_MOVED                     : s = "CM_PROB_MOVED";                      break;
        case CM_PROB_TOO_EARLY                 : s = "CM_PROB_TOO_EARLY";                  break;
        case CM_PROB_NO_VALID_LOG_CONF         : s = "CM_PROB_NO_VALID_LOG_CONF";          break;
        case CM_PROB_FAILED_INSTALL            : s = "CM_PROB_FAILED_INSTALL";             break;
        case CM_PROB_HARDWARE_DISABLED         : s = "CM_PROB_HARDWARE_DISABLED";          break;
        case CM_PROB_CANT_SHARE_IRQ            : s = "CM_PROB_CANT_SHARE_IRQ";             break;
        case CM_PROB_FAILED_ADD                : s = "CM_PROB_FAILED_ADD";                 break;
        case CM_PROB_DISABLED_SERVICE          : s = "CM_PROB_DISABLED_SERVICE";           break;
        case CM_PROB_TRANSLATION_FAILED        : s = "CM_PROB_TRANSLATION_FAILED";         break;
        case CM_PROB_NO_SOFTCONFIG             : s = "CM_PROB_NO_SOFTCONFIG";              break;
        case CM_PROB_BIOS_TABLE                : s = "CM_PROB_BIOS_TABLE";                 break;
        case CM_PROB_IRQ_TRANSLATION_FAILED    : s = "CM_PROB_IRQ_TRANSLATION_FAILED";     break;
        case CM_PROB_FAILED_DRIVER_ENTRY       : s = "CM_PROB_FAILED_DRIVER_ENTRY";        break;
        case CM_PROB_DRIVER_FAILED_PRIOR_UNLOAD: s = "CM_PROB_DRIVER_FAILED_PRIOR_UNLOAD"; break;
        case CM_PROB_DRIVER_FAILED_LOAD        : s = "CM_PROB_DRIVER_FAILED_LOAD";         break;
        case CM_PROB_DRIVER_SERVICE_KEY_INVALID: s = "CM_PROB_DRIVER_SERVICE_KEY_INVALID"; break;
        case CM_PROB_LEGACY_SERVICE_NO_DEVICES : s = "CM_PROB_LEGACY_SERVICE_NO_DEVICES";  break;
        case CM_PROB_DUPLICATE_DEVICE          : s = "CM_PROB_DUPLICATE_DEVICE";           break;
        case CM_PROB_FAILED_POST_START         : s = "CM_PROB_FAILED_POST_START";          break;
        case CM_PROB_HALTED                    : s = "CM_PROB_HALTED";                     break;
        case CM_PROB_PHANTOM                   : s = "CM_PROB_PHANTOM";                    break;
        case CM_PROB_SYSTEM_SHUTDOWN           : s = "CM_PROB_SYSTEM_SHUTDOWN";            break;
        case CM_PROB_HELD_FOR_EJECT            : s = "CM_PROB_HELD_FOR_EJECT";             break;
        case CM_PROB_DRIVER_BLOCKED            : s = "CM_PROB_DRIVER_BLOCKED";             break;
        case CM_PROB_REGISTRY_TOO_LARGE        : s = "CM_PROB_REGISTRY_TOO_LARGE";         break;
        case CM_PROB_SETPROPERTIES_FAILED      : s = "CM_PROB_SETPROPERTIES_FAILED";       break;
        case CM_PROB_WAITING_ON_DEPENDENCY     : s = "CM_PROB_WAITING_ON_DEPENDENCY";      break;
        case CM_PROB_UNSIGNED_DRIVER           : s = "CM_PROB_UNSIGNED_DRIVER";            break;
        case CM_PROB_USED_BY_DEBUGGER          : s = "CM_PROB_USED_BY_DEBUGGER";           break;
        case CM_PROB_DEVICE_RESET              : s = "CM_PROB_DEVICE_RESET";               break;
        case CM_PROB_CONSOLE_LOCKED            : s = "CM_PROB_CONSOLE_LOCKED";             break;
        case CM_PROB_NEED_CLASS_CONFIG         : s = "CM_PROB_NEED_CLASS_CONFIG";          break;
        default: s = "CM_PROB_UNKNOWN_OR_NEW"; assert(false); break; // fix it if it asserts - MS introduced new problem code
    }
    return s;
}

static void trace_device_status(int vid, int pid, int status) {
    if (status & DN_ROOT_ENUMERATED) { trace("%04X:%04X DN_ROOT_ENUMERATED", vid, pid); }
    if (status & DN_DRIVER_LOADED  ) { trace("%04X:%04X DN_DRIVER_LOADED  ", vid, pid); }
    if (status & DN_ENUM_LOADED    ) { trace("%04X:%04X DN_ENUM_LOADED    ", vid, pid); }
    if (status & DN_STARTED        ) { trace("%04X:%04X DN_STARTED        ", vid, pid); }
    if (status & DN_MANUAL         ) { trace("%04X:%04X DN_MANUAL         ", vid, pid); }
    if (status & DN_NEED_TO_ENUM   ) { trace("%04X:%04X DN_NEED_TO_ENUM   ", vid, pid); }
    if (status & DN_NOT_FIRST_TIME ) { trace("%04X:%04X DN_NOT_FIRST_TIME ", vid, pid); }
    if (status & DN_HARDWARE_ENUM  ) { trace("%04X:%04X DN_HARDWARE_ENUM  ", vid, pid); }
    if (status & DN_LIAR           ) { trace("%04X:%04X DN_LIAR           ", vid, pid); }
    if (status & DN_HAS_MARK       ) { trace("%04X:%04X DN_HAS_MARK       ", vid, pid); }
    if (status & DN_HAS_PROBLEM    ) { trace("%04X:%04X DN_HAS_PROBLEM    ", vid, pid); }
    if (status & DN_FILTERED       ) { trace("%04X:%04X DN_FILTERED       ", vid, pid); }
    if (status & DN_MOVED          ) { trace("%04X:%04X DN_MOVED          ", vid, pid); }
    if (status & DN_DISABLEABLE    ) { trace("%04X:%04X DN_DISABLEABLE    ", vid, pid); }
    if (status & DN_REMOVABLE      ) { trace("%04X:%04X DN_REMOVABLE      ", vid, pid); }
    if (status & DN_PRIVATE_PROBLEM) { trace("%04X:%04X DN_PRIVATE_PROBLEM", vid, pid); }
    if (status & DN_MF_PARENT      ) { trace("%04X:%04X DN_MF_PARENT      ", vid, pid); }
    if (status & DN_MF_CHILD       ) { trace("%04X:%04X DN_MF_CHILD       ", vid, pid); }
    if (status & DN_WILL_BE_REMOVED) { trace("%04X:%04X DN_WILL_BE_REMOVED", vid, pid); }
}

static bool is_windows_usb_device_present(int vid, int pid) {
    // must use SetupAPI here :( slow ~20-30ms (sometimes 64 or 128 which is suspicious)
    // for a device present 0.5 - 1.5ms (sometimes upto 8 or 16ms) for absent device
    // open()/close() is not an option like on Linux
    // (where is may not to be an option either once someone
    // "claims" device interface and we cannot open the
    // same file handle...
    // Prominent example on Windows is
    //     is_windows_usb_device_present(VID_GOOGLE, PID_P230_ADB)
    // Most of the time ADB device is present it is openned as WinUSB device by
    // adb.exe server and thus cannot be opened outside of it as WinUSB device.
    bool present = false;
    char hardware_id[128] = {0};
    snprintf(hardware_id, countof(hardware_id), "VID_%04X&PID_%04X", vid, pid);
    const int k = (int)strlen(hardware_id);
    HDEVINFO device_info_list = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_USB_DEVICE, 0, null, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (device_info_list == INVALID_HANDLE_VALUE) {
        trace("SetupDiGetClassDevsA() failed %s", strerr(GetLastError()));
        return false;
    }
    SP_DEVINFO_DATA info = {0};
    info.cbSize = sizeof(SP_DEVINFO_DATA);
    DWORD buffer_size = STACK_BUFFER_SIZE;
    char* buffer = (char*)stack_alloc(buffer_size);
    bool found = false;
    for (int i = 0; SetupDiEnumDeviceInfo(device_info_list, i, &info) && !present; i++) {
        DWORD data_type = 0;
        bool b = false;
        usbdevs_get_property(b, device_info_list, info, SPDRP_HARDWAREID, data_type, buffer, buffer_size);
        found = b && strstr(buffer, hardware_id) != null;
        if (found) {
            int c = CM_Get_Device_IDA(info.DevInst, buffer, buffer_size, 0);
            char* sn = strstr(buffer, hardware_id);
//          trace("id=%s sn=%s", buffer, sn);
            if (c == CR_SUCCESS) {
                DWORD dev_inst = 0; // only single child (I guess not composite) usb devices are supported:
                // if there is not a single clid we use info.DevInst instead...
                if (CM_Get_Child(&dev_inst, info.DevInst, 0) != CR_SUCCESS || dev_inst == 0) { dev_inst = info.DevInst; }
                ULONG status =0;
                ULONG problem_code = 0;
                CM_Get_DevNode_Status(&status, &problem_code, dev_inst, 0);
                if (sn != null && strstr(sn, "\\") != null) { sn = strstr(sn, "\\") + 1; }
//              dtrace(">%04X:%04X \"%s\"", vid, pid, sn != null ? sn : "<no serial number>");
                if (false) { trace_device_status(vid, pid, status); }
                int problem = 0;
                if (status & DN_HAS_PROBLEM) {
                    problem = problem_code;
                    trace("USB device %04X:%04X has problem: %d %s", vid, pid, problem, device_problem_description(problem));
                }
                bool driver  = (DN_DRIVER_LOADED & status) == DN_DRIVER_LOADED;
                bool started = (DN_STARTED & status) == DN_STARTED;
                present = driver && started && problem == 0;
                if (present) {
//                  dtrace("<%04X:%04X present (driver loaded and started with no problems)", vid, pid);
                } else {
//                  dtrace("<%04X:%04X driver=%d started=%d problem=%d", vid, pid, started, problem);
                }
                // It is useful to test device presence w/o attempt to open file by symbolic name
                // but it is only for debug. Semantically the file can be already or still open when
                // is_windows_usb_device_present() is invoked.
#ifdef FOR_DEBUG_ONLY_CONFIRM_THAT_PRESENT_DEVICE_CAN_BE_OPENED
                if (present) {
                    SP_DEVINFO_DATA device_info_data = {};
                    device_info_data.cbSize = sizeof(device_info_data);
                    device_info_data.DevInst = dev_inst;
                    b = SetupDiOpenDeviceInfoA(device_info_list, buffer, null, 0, &device_info_data);
                    HKEY key = SetupDiOpenDevRegKey(device_info_list, &device_info_data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
                    if (key != null) {
                        char symbolic_name[1024];
                        DWORD reg_type = 0;
                        DWORD count = countof(symbolic_name);
                        int sc = RegQueryValueExA(key, "SymbolicName", null, &reg_type, (BYTE*)symbolic_name, &count);
                        if (sc == ERROR_SUCCESS && reg_type == REG_SZ) {
//                          trace("%s", symbolic_name);
                            HANDLE file = CreateFileA(symbolic_name, GENERIC_READ | GENERIC_WRITE, 0, null, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, null);
                            int r = file == INVALID_HANDLE_VALUE ? GetLastError() : 0;
                            if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); }
                            if (r != 0) { present = false; }
                        }
                        RegCloseKey(key);
                    }
                }
#endif
            }
        }
    }
    SetupDiDestroyDeviceInfoList(device_info_list);
    return present;
}

bool usbio_is_device_present(int vid, int pid) {
    return is_windows_usb_device_present(vid, pid);
}

static int translate_ntstatus_to_windows(int status) {
    typedef uint32_t (*rtl_ntstatus_to_dos_error_t)(uint32_t status);
    static rtl_ntstatus_to_dos_error_t rtl_ntstatus_to_dos_error;
    if (rtl_ntstatus_to_dos_error == null) {
        rtl_ntstatus_to_dos_error = (rtl_ntstatus_to_dos_error_t)GetProcAddress(LoadLibraryA("ntdll"), "RtlNtStatusToDosError");
    }
    // e.g.:
    // STATUS_UNSUCCESSFUL    ERROR_GEN_FAILURE
    // STATUS_CANCELLED       ERROR_OPERATION_ABORTED
    // STATUS_NO_SUCH_DEVICE  ERROR_FILE_NOT_FOUND
    return rtl_ntstatus_to_dos_error((uint32_t)status);
}

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
