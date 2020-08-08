#pragma once
#include "runtime.h"
#ifdef WINDOWS
#undef _MP // defined in two contradictory places in Windows header files
#include <winusb.h>
#else
#include <linux/usbdevice_fs.h>
#endif

BEGIN_C

// IMPORTANT: On Windows this only works with WinUSB devices, for anything else use usbdevs.h/c

/*
    There is two modes of operation.
    1. usbio_submit_urb() / usbio_reap_urb()
       It allows to schedule several (e.g. USBIO_BULK_REQUEST_QUEUE_SIZE) USB async 64KB bulk read transfers in advance,
       and reap resulting URBs when they are ready in order. The actual size of reaped URBs may be less then 64KB.
       This mode works well with Tracking Controller because it sends zero length packet on packet boundaries (512 bytes).
       This mode absolutely does NOT work with "adbd" daemon on Android via function fs (f_fs.c) that does NOT send
       zero length packets on packet boundaries. (Ironically everything works with transfers not exactly multiples on
       packet size but gets stack on 512, 1024 etc).
    2. usbio_builk_in()
       It cannot and should not be mixed with usbio_submit_urb() / usbio_reap_urb().
       It shcedules one and only single read of specified size and return when requested number of bytes (or less) has been read.
       If client needs exact numbers of bytes (known from upper level protocol) it will need to call this function
       repeatedly until desired number of bytes is read or error is encountered.
       This is low performance non-overlapped mode of operation and it reduces adb protocol sync pull transfer from 24MB/s to 17MB/s
       but due to strange `aproto` design decisions and absence of ZLPs this is the only way.
*/

enum {
    USBIO_BULK_REQUEST_QUEUE_SIZE =  64,  // number of bulk requests in the queue 32 * 64KB ~ 2MB 11MB/s greyscale traffic
    USBIO_BULK_REQUEST_SIZE  = 64 * 1024, // MUST BE 64KB!
    USBIO_CTRL_RESPONSE_QUEUE_SIZE = 4,   // number of bulk i/o URBs for `ctrl` bulk queue
    USBIO_CTRL_RESPONSE_SIZE = 512,       // for T.C. f/w >= 231 cannot be more then 96 but the bulkio URB must be mutiples of 512
    USBIO_CTRL_EP_TIMEOUT_MS = 1500,      // default control endpoint roundtrip timeout in milliseconds (not suitable for everything)
};

typedef int usbio_file_t;

extern const usbio_file_t usbio_file_invalid; // -1

#pragma pack(push, 1)

typedef struct usbio_ctrl_setup_s { 
    byte rt;  // request type
    byte req; // request
    uint16_t val; // value
    uint16_t ix;  // index
    uint16_t len; // length of data to transfer in bytes
} attribute_packed usbio_ctrl_setup_t;

// usbio_ctrl_setup_t is carved in stone by USB spec https://en.wikipedia.org/wiki/USB_(Communications)#Setup_packet


#pragma pack(pop)

#ifdef WINDOWS

typedef struct usbio_buffer_s usbio_buffer_t;

typedef struct usbio_overlapped_s {
    OVERLAPPED overlapped;
    int magic; // 'USBX'
    usbio_file_t file;
    usbio_buffer_t* urb;
} usbio_overlapped_t;

typedef struct usbio_buffer_s {
    void* data;
    int bytes;
    int endpoint;
    int error; // o.overlapped.Internal translated to Win32 error code upon request completion
    int timeout; // milliseconds (for usb ctrl ep only!)
    usbio_overlapped_t o;
    usbio_buffer_t* prev; // double linked list of buffers
    usbio_buffer_t* next;
    // debugging
    int op; // operation
    int32_t xid;
} usbio_buffer_t;

#else

typedef struct usbio_buffer_s {
    struct usbdevfs_urb urb; // must be first field
    byte* data;
    int bytes;
} usbio_buffer_t;

#endif

int usbio_open(int vid, int pid, usbio_file_t* files, int count); // returns number of opened files in fd[count] array or -1

int  usbio_ep_count(usbio_file_t file); // returns number of endpoints or -1 and errno
byte usbio_pipe_bulk_in1(usbio_file_t file);   // end point 0x81: cameras mipi stream and other data 
byte usbio_pipe_bulk_out1(usbio_file_t file);  // end point 0x01: for adbd
byte usbio_pipe_bulk_in2(usbio_file_t file);   // end point 2: since f/w 233 `control` builk i/o endpoint in
byte usbio_pipe_bulk_out2(usbio_file_t file);  // end point 0x82: since f/w 233 `control` builk i/o endpoint out

int usbio_bulk_in(usbio_file_t file, byte pipe, const void* data, int bytes, int* transferred); // 0 success > 0 error
int usbio_bulk_out(usbio_file_t file, byte pipe, const void* data, int bytes);

// timeout < 0 means default USBIO_CTRL_EP_TIMEOUT_MS, 0 (bad idea) means forever > 0 whatever it is in milliseconds
// data pointer is both in/out. can be null [data, length] is sent to device [data, *bytes] received. return != 0 means error (should be possitive error code)
int usbio_control(usbio_file_t file, usbio_ctrl_setup_t* setup, void* data, int* bytes, int timeout_milliseconds);

int usbio_ctrl(usbio_file_t file, usbio_buffer_t* b, void* data, int bytes); // data must point to usbio_ctrl_setup_t possibly following for data buffer

int usbio_submit_urb(usbio_file_t file, int pipe, usbio_buffer_t* urb); // urb->bytes MUST BE == USBIO_BULK_REQUEST_SIZE
int usbio_discard_urb(usbio_file_t file, usbio_buffer_t* urb);
int usbio_reap_urb(usbio_file_t file, usbio_buffer_t** urb); // returns dequeued urb previously submitted by usbio_submit_urb

int usbio_abort_pipe(usbio_file_t file, int pipe); // aborts all of the pending transfers for a pipe (Windows only)
int usbio_flush_pipe(usbio_file_t file, int pipe); // discards any data that is cached in a pipe (Windows only)
int usbio_reset_pipe(usbio_file_t file, int pipe); // resets the data toggle and clears the stall condition on a pipe.

// usbio_reset_pipe() avoid on both Windows and Linux

int usbio_reset(usbio_file_t file); // avoid = see warnings in implementation
int usbio_clear_halt(usbio_file_t file); // avoid = see warnings in implementation

int usbio_set_timeout(usbio_file_t file, int pipe, int milliseconds); // 0 no timeout

int usbio_set_raw(usbio_file_t file, bool raw); // Linux: always raw

int usbio_close(usbio_file_t file);

bool usbio_is_device_present(int vid, int pid); // attempts to open and close device, true is successful

END_C

