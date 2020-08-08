#include "debug.h"
#include "pozix.h"

#ifdef WINDOWS
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp")
#pragma comment(lib, "imagehlp")
#pragma warning(disable: 4740) // flow in or out of inline asm code suppresses global optimization
#endif
#ifdef ANDROID
#include <android/log.h>
#endif

BEGIN_C

#ifdef WINDOWS

static HANDLE ntdll;

static_init(ntdll) {
    ntdll = LoadLibraryA("NTDLL.DLL");
}

static void strwinerr(int e, text256_t* s) {
    int save = errno;
    int gle = GetLastError();
    s->text[0] = 0;
    const DWORD neutral = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
    const DWORD format = FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS;
    e = 0 <= e && e <= 0xFFFF ? HRESULT_FROM_WIN32(e) : e;
    int n = FormatMessageA(format, null, e, neutral, s->text, sizeof(s->text), null);
    if (n == 0) { n = FormatMessageA(format | FORMAT_MESSAGE_FROM_HMODULE, ntdll, e, neutral, s->text, countof(s->text) - 1, null); }
    s->text[countof(s->text) - 1] = 0; // paranoia
    // remove trailing blanks
    int length = (int)strlen(s->text);
    while (length > 0 && (s->text[length - 1] < 0x20 || isblank(s->text[length - 1]) || isspace(s->text[length - 1]))) {
        length--; s->text[length] = 0;
    }
    for (;;) { // FormatMessageA may return text with \r\n inside it
        char* cr = strrchr(s->text, '\r');
        if (cr != null) { *cr = 0x20; }
        char* lf = strrchr(s->text, '\n');
        if (lf != null) { *lf = 0x20; }
        if (cr == null && lf == null) { break; }
    }
    errno = save;
    SetLastError(gle);
}

static const char** posix_supplement_errors;
static int posix_supplement_errors_count;
static void posix_supplement_errors_build();

/* strerr() is not end user facing facility on Windows platform.
   For end user use localized FormatMessageW() with inserts and all that jazz.
   strerr() is strictly for internal and logging use.
   Since 0..posix_supplement_errors_count range belongs to all subsets
     posix, Microsoft Posix Suppliment, Linux Posix Suppliment and Windows System Error Codes
   this function will try to report all of them.
   When/if system software code will need user facing error description on both Linux and Windows
   plaforms the strerrdesc(int e, ...) can be implemented to use UTF-8 to do exactly that and format
   message with inserts and locale information (currency, dates, numerals etc...) */

text256_t strerr_(int e) {
    static text256_t EMPTY_STRING;
    if (e == 0) { return EMPTY_STRING; }
    if (posix_supplement_errors_count == 0) { posix_supplement_errors_build(); }
    text256_t p; // posix message
    int save = errno;
    int gle = GetLastError();
    p.text[0] = 0;
    if (0 <= e && e <= sys_nerr) { // try posix errors first upto errcode == 43 see note on _sys_errlist below
        errno_t en = strerror_s(p.text, countof(p.text), e);
        if (en != 0) { p.text[0] = 0; }
    }
    text256_t x; // posix supplement errors (Microsoft Windows supliment not Linux supliment)
    x.text[0] = 0;
    if (0 <= e && e <= posix_supplement_errors_count && posix_supplement_errors[e] != null) {
        strncpy0(x.text, posix_supplement_errors[e], sizeof(x.text));
    }
    text256_t w; // Windows System Error Code (0..15999) or HRESULT
    w.text[0] = 0;
    strwinerr(e, &w);
    w.text[countof(w.text) - 1] = 0; // make sure it is zero terminated - do not rely on good truncate in FormatMessageA
    for (;;) { // FormatMessageA may return text with \r\n in it
        char* cr = strrchr(w.text, '\r');
        if (cr != null) { *cr = 0x20; }
        char* lf = strrchr(w.text, '\n');
        if (lf != null) { *lf = 0x20; }
        if (cr == null && lf == null) { break; }
    }
    text256_t s; // retulting text string
    s.text[countof(s.text) - 1] = 0; // make sure it is zero terminated - do not rely on good truncate in FormatMessageA
    const char* posix = (p.text[0] != 0 ? p.text : x.text);
    if (posix[0] != 0) {
        if (0 <= e && e <= 0xFFFF) { // no HEX representation is useful because posix errors and windows system error codes are decimal
            if (strlen(w.text) != 0 && e >= 141) { // Windows posix suplicant ends at 140
                snprintf0(s.text, countof(s.text), "error=(%d) posix: \"%s\" win: \"%s\"", e, posix, w.text);
            } else {
                snprintf0(s.text, countof(s.text), "error=(%d) posix: \"%s\"", e, posix);
            }
        } else {
            snprintf0(s.text, countof(s.text), "error=(%d 0x%08X) \"%s\" posix: \"%s\" ", e, e, w.text, posix);
        }
    } else {
        if (0 <= e && e <= 0xFFFF) { // no HEX representation is useful because posix errors and windows system error codes are decimal
            snprintf0(s.text, countof(s.text), "error=(%d) \"%s\"", e, w.text, e);
        } else {
            snprintf0(s.text, countof(s.text), "error=(%d 0x%08X) \"%s\"", e, e, w.text);
        }
    }
    errno = save;
    SetLastError(gle);
    return s;
}

volatile debug_output_hook_t* debug_output_hook;

static __inline void ods(int kind, const char* s) {
/*  TODO: (Leo) I do not think this is correct place to call hook
    volatile debug_output_hook_t* hook = debug_output_hook; // assumed to be atomic
    if (hook != null) {
        hook->output(hook->that, 0, s);
    }
*/
    char buffer[2048]; // OutputDebugStringA behaves badly after 4KB
    strncpy0(buffer, s, countof(buffer));
    OutputDebugStringA(s);
}

enum { NO_LINEFEED = -2 };

static void trace0(const char* file, int line, const char* function, int kind, const char* format, va_list vl) {
    /* TODO: make it UTF-8 to wide characters to OutputDebugStringA */
    /* experimental knowledge OutputDebugStringW is limited to 1023 */
    enum { N = 1023 };
    char buf[1024*64];
    buf[0] = 0;
    char* sb = buf;
    int left = countof(buf) - 1;
    if (file != null && line >= 0) {
        int n  = snprintf0(sb, left, "%s(%d): [%05d] %s ", file, line, GetCurrentThreadId(), function);
        sb += n;
        left -= n;
    }
    vsnprintf0(sb, left, format, vl);
    int n = (int)strlen(buf);
    char* p = buf;
    if (p[n - 1] != '\n' && line != NO_LINEFEED) {
        p[n++] = '\n';
        p[n] = 0;
    }
    while (n > 0) {
        if (n < N) {
            ods(kind, p);
            n = 0;
        } else {
            char chunk[N + 1];
            memcpy(chunk, p, sizeof chunk);
            chunk[N] = 0;
            ods(kind, chunk);
            p += N;
            n -= N;
        }
    }
}

void _trace_v(const char* file, int line, const char* function, const char* format, va_list vl) {
    trace0(file, line, function, 1, format, vl);
}

void _trace_(const char* file, int line, const char* function, const char* format, ...) {
    va_list vl;
    va_start(vl, format);
    trace0(file, line, function, 1, format, vl);
    va_end(vl);
}

void _print_(const char* format, ...) {
    va_list vl;
    va_start(vl, format);
    trace0(null, NO_LINEFEED, null, 0, format, vl);
    va_end(vl);
}

void _println_(const char* format, ...) {
    va_list vl;
    va_start(vl, format);
    trace0(null, -1, null, 0, format, vl);
    va_end(vl);
}

static HANDLE process;
static CRITICAL_SECTION cs;
static bool cs_initialized;

static_init(debug) {
    bool b = InitializeCriticalSectionAndSpinCount(&cs, 0x400); assert(b); (void)b;
    cs_initialized = true;
}

static void load_debug_helpers() {
    if (GetModuleHandleA("dbghelp.dll") == null || GetModuleHandleA("imagehlp.dll") == null) {
#ifdef  _WIN64 // try to load system copy first:
        LoadLibraryA("c:/Windows/System32/dbghelp.dll");
        LoadLibraryA("c:/Windows/System32/imagehlp.dll");
#else
        LoadLibraryA("c:/Windows/SysWOW64/dbghelp.dll");
        LoadLibraryA("c:/Windows/SysWOW64/imagehlp.dll");
#endif
    }
    if (GetModuleHandleA("dbghelp.dll") == null || GetModuleHandleA("imagehlp.dll") == null) {
        LoadLibraryA("dbghelp.dll"); // try PATH
        LoadLibraryA("imagehlp.dll");
    }
    if (GetModuleHandleA("dbghelp.dll") == null || GetModuleHandleA("imagehlp.dll") == null) {
        char pathname[4096]; // see if we have it?
        if (GetModuleFileNameA(GetModuleHandleA(null), pathname, countof(pathname))) {
            char* p = strrchr(pathname, '\\');
            if (p != null) {
                p[1] = 0;
                LoadLibraryA(concat(pathname, "dbghelp.dll"));
                LoadLibraryA(concat(pathname, "imagehlp.dll"));
            }
        }
    }
}

#define USED_CONTEXT_FLAGS CONTEXT_FULL

#if defined(_M_IX86)
#ifdef CURRENT_THREAD_VIA_EXCEPTION
// TODO: The following is not a "good" implementation,
// because the callstack is only valid in the "__except" block...
#define GET_CURRENT_CONTEXT(c, contextFlags) \
    { \
        memset(&c, 0, sizeof(CONTEXT)); \
        EXCEPTION_POINTERS *pExp = NULL; \
        __try { \
        throw 0; \
    } __except( ( (pExp = GetExceptionInformation()) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_EXECUTE_HANDLER)) {} \
        if (pExp != NULL) \
        memcpy(&c, pExp->ContextRecord, sizeof(CONTEXT)); \
        c.ContextFlags = contextFlags; \
    }
#else
// The following should be enough for walking the callstack...
#define GET_CURRENT_CONTEXT(c, contextFlags) \
    { \
        memset(&c, 0, sizeof(CONTEXT)); \
        c.ContextFlags = contextFlags; \
        __asm    call x \
        __asm x: pop eax \
        __asm    mov c.Eip, eax \
        __asm    mov c.Ebp, ebp \
        __asm    mov c.Esp, esp \
    }
#endif
#else // The following is defined for x86 (XP and higher), x64 and IA64:
#define GET_CURRENT_CONTEXT(c, contextFlags) \
    { \
        memset(&c, 0, sizeof(CONTEXT)); \
        c.ContextFlags = contextFlags; \
        RtlCaptureContext(&c); \
    };
#endif

/*    // if not current thread:
      SuspendThread(thread);
      c.ContextFlags = USED_CONTEXT_FLAGS;
      if (GetThreadContext(thread, &c) == FALSE) {
        ResumeThread(thread);
        return false;
      }
      return true;
*/

BOOL __stdcall read_process_memory(HANDLE p, DWORD64 base_address, PVOID buffer, DWORD size, LPDWORD number_of_bytes_read) {
    SIZE_T st = 0;
    BOOL b = ReadProcessMemory(p, (void*)(uintptr_t)base_address, buffer, size, &st);
    *number_of_bytes_read = (DWORD)st;
//  trace("process: %p, base_addr: %p, buffer: %p, size: %d, read: %d, result: %d", p, (void*)((uintptr_t))base_address, buffer, size, (int)st, (int)b);
    return b;
}

enum { MAX_NAMELEN = 1024 }; // max name length for found symbols

static int filename_lineno_length(const char* file, int line) {
    char buf[MAX_NAMELEN];
    buf[0] = 0;
    char* sb = buf;
    int left = countof(buf) - 1;
    if (file != null && line >= 0) {
        snprintf0(sb, left, "%s(%d): [%05d] ", file, line, GetCurrentThreadId());
    }
    int n = (int)strlen(buf);
    return n;
}

static bool show_stopper(const char* symbol) {
    return  strstr(symbol, "BaseThreadInitThunk") != null ||
            strstr(symbol, "CallNextHookEx") != null ||
            strstr(symbol, "beginthreadex") != null ||
            strstr(symbol, "endthreadex") != null ||
            strstr(symbol, "tmainCRTStartup") != null ||
            strstr(symbol, "threadstartex") != null;
}

static void print_stack_trace_local(const char* file, int line_number, const char* function, char* append, int count,
                                    void (*_trace_)(const char* filename, int line, const char* func, const char* format, ...),
                                    const CONTEXT* context) {
    if (file != null && function != null) {
        _trace_(file, line_number, "--- stack trace --- ", "%s", function);
    }
    load_debug_helpers();
    char* a = append;
    enum { N = 128 };
    void* stack[N];
    if (process == null) {
        DWORD options = SymGetOptions();
//      options |= SYMOPT_DEBUG;
        options |= SYMOPT_NO_PROMPTS;
        options |= SYMOPT_LOAD_LINES;
//      options |= SYMOPT_LOAD_ANYTHING;
        SymSetOptions(options);
        process = GetCurrentProcess();
        SymInitialize(process, null, true);
    }
    int max_length = 0;
    for (int pass = 0; pass < 2; pass++) {
        CONTEXT c;
        if (context == null) {
            GET_CURRENT_CONTEXT(c, USED_CONTEXT_FLAGS);
        } else {
            c = *context;
        }
        STACKFRAME64 s = {0};
        memset(&s, 0, sizeof(s));
        DWORD imageType  = IMAGE_FILE_MACHINE_I386;
        s.AddrPC.Mode    = AddrModeFlat;
        s.AddrFrame.Mode = AddrModeFlat;
        s.AddrStack.Mode = AddrModeFlat;
#ifdef _M_IX86
        // normally, call ImageNtHeader() and use machine info from PE header
        imageType = IMAGE_FILE_MACHINE_I386;
        s.AddrPC.Offset = c.Eip;
        s.AddrFrame.Offset = c.Ebp;
        s.AddrStack.Offset = c.Esp;
#elif _M_X64
        imageType = IMAGE_FILE_MACHINE_AMD64;
        s.AddrPC.Offset = c.Rip;
        s.AddrFrame.Offset = c.Rsp;
        s.AddrStack.Offset = c.Rsp;
#elif _M_IA64
        imageType = IMAGE_FILE_MACHINE_IA64;
        s.AddrPC.Offset = c.StIIP;
        s.AddrFrame.Offset = c.IntSp;
        s.AddrStack.Offset = c.IntSp;
        s.AddrBStore.Offset = c.RsBSP;
        s.AddrBStore.Mode = AddrModeFlat;
#else
    #error "Platform not supported!"
#endif
        int frames = CaptureStackBackTrace(1, 100, stack, null);
        SYMBOL_INFO* symbol = (SYMBOL_INFO*)stack_allocz(sizeof(SYMBOL_INFO) + MAX_NAMELEN);
        symbol->MaxNameLen   = MAX_NAMELEN - 1;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        for (int i = 0; i < frames; i++) {
            if (!StackWalk64(imageType, process, GetCurrentThread(), &s, &c, read_process_memory, SymFunctionTableAccess64, SymGetModuleBase64, null)) {
                break;
            }
            if (s.AddrPC.Offset == s.AddrReturn.Offset) {
                break;
            }
    //      trace("s.AddrPC.Offset=0x%016llX stack[%d]=%p", s.AddrPC.Offset, i, stack[i]);
            DWORD64 offsetFromSmybol = 0;
            if (s.AddrPC.Offset != 0 && SymFromAddr(process, (DWORD64)s.AddrPC.Offset, &offsetFromSmybol, symbol)) {
                IMAGEHLP_LINE64 line;
                char undecorated[MAX_NAMELEN];
                int n = UnDecorateSymbolName(symbol->Name, undecorated, sizeof(undecorated), UNDNAME_COMPLETE);
                const char* name = n > 0 ? undecorated : symbol->Name;
                DWORD displacement = 0;
                BOOL b = SymGetLineFromAddr64(process, s.AddrPC.Offset, &displacement, &line);
                if (b) {
                    if (line.FileName != null && name != null) {
                        if (show_stopper(name) || strstr(line.FileName, "crt0.c") != null && strstr(name, "main") != null) {
                            break;  // noise reduction
                        }
                        if (i > 1) {
                            int length = filename_lineno_length(line.FileName, line.LineNumber);
                            if (pass == 1) {
                                if (append != null) {
                                    if (count - 1 <= 0) { break; }
                                    char* fname = strrchr(line.FileName, '\\');
                                    int k = snprintf0(a, count - 1, "%s(%d): %s < ", fname != null ? fname + 1 : line.FileName, line.LineNumber, name);
                                    if (k <= 0) { break; } else { a += k; count -= k; }
                                } else {
                                    _trace_(line.FileName, line.LineNumber, "", "%*s%s", (max_length - length), "", name);
                                }
                            } else {
                                if (length > max_length) { max_length = length; }
                            }
                        }
                    }
                } else {
                    if (show_stopper(symbol->Name)) {
                        break; // noise reduction
                    }
                    if (pass == 1) {
                        if (append != null) {
                            int k = snprintf0(a, count - 1, "%s - 0x%p < ", symbol->Name, symbol->Address);
                            if (k <= 0) { break; } else { a += k; count -= k; }
                        } else {
                            _trace_(null, -1, null, "%*s%s - 0x%p", max_length, "", symbol->Name, symbol->Address);
                        }
                    }
                }
            } else {
                if (pass == 1) {
                    if (append != null) {
                        int k = snprintf0(a, count - 1, "0x%p < ", stack[i]);
                        if (k <= 0) { break; } else { a += k; count -= k; }
                    } else {
                        _trace_(null, -1, null, "%*s0x%p", max_length, "", stack[i]);
                    }
                }
            }
        }
    }
}

void get_stack_trace(char* buffer, int count) {
    if (cs_initialized) { EnterCriticalSection(&cs); }
    print_stack_trace_local(null, 0, null, buffer, count, _trace_, null);
    if (cs_initialized) { LeaveCriticalSection(&cs); }
}

void _print_stack_trace_(const char* file, int line, const char* function) {
    if (cs_initialized) { EnterCriticalSection(&cs); }
    print_stack_trace_local(file, line, function, null, 0, _trace_, null);
    if (cs_initialized) { LeaveCriticalSection(&cs); }
}

void get_context_stack_trace(const void* context, char* buffer, int count) {
    if (cs_initialized) { EnterCriticalSection(&cs); }
    print_stack_trace_local(null, 0, null, buffer, count, _trace_, (const CONTEXT*)context);
    if (cs_initialized) { LeaveCriticalSection(&cs); }
}

int _assertion_(const char* e, const char* file, int line, const char* function, const char* format, ...) {
    va_list vl;
    va_start(vl, format);
    char buffer[1024];
    vsnprintf0(buffer, countof(buffer), format, vl);
    _trace_(file, line, function, " assertion %s failed: %s", e, buffer);
    _print_stack_trace_(file, line, function);
    va_end(vl);
    breakpoint();
    return 0;
}

typedef struct error_info_s {
    int value;            /* The numeric value from <errno.h> */
    const char* name;     /* The equivalent symbolic value */
    const char* message;  /* Short message about this value */
} error_info_t;

#define error_table_entry(value, name, msg) {value, name, msg}

static const error_info_t error_table[] = {
    #if defined (EPERM)
        error_table_entry(EPERM, "EPERM", "Not owner"),
    #endif
    #if defined (ENOENT)
        error_table_entry(ENOENT, "ENOENT", "No such file or directory"),
    #endif
    #if defined (ESRCH)
        error_table_entry(ESRCH, "ESRCH", "No such process"),
    #endif
    #if defined (EINTR)
        error_table_entry(EINTR, "EINTR", "Interrupted system call"),
    #endif
    #if defined (EIO)
        error_table_entry(EIO, "EIO", "I/O error"),
    #endif
    #if defined (ENXIO)
        error_table_entry(ENXIO, "ENXIO", "No such device or address"),
    #endif
    #if defined (E2BIG)
        error_table_entry(E2BIG, "E2BIG", "Arg list too long"),
    #endif
    #if defined (ENOEXEC)
        error_table_entry(ENOEXEC, "ENOEXEC", "Exec format error"),
    #endif
    #if defined (EBADF)
        error_table_entry(EBADF, "EBADF", "Bad file number"),
    #endif
    #if defined (ECHILD)
        error_table_entry(ECHILD, "ECHILD", "No child processes"),
    #endif
    #if defined (EWOULDBLOCK) /* Put before EAGAIN, sometimes aliased */
        error_table_entry(EWOULDBLOCK, "EWOULDBLOCK", "Operation would block"),
    #endif
    #if defined (EAGAIN)
        error_table_entry(EAGAIN, "EAGAIN", "No more processes"),
    #endif
    #if defined (ENOMEM)
        error_table_entry(ENOMEM, "ENOMEM", "Not enough space"),
    #endif
    #if defined (EACCES)
        error_table_entry(EACCES, "EACCES", "Permission denied"),
    #endif
    #if defined (EFAULT)
        error_table_entry(EFAULT, "EFAULT", "Bad address"),
    #endif
    #if defined (ENOTBLK)
        error_table_entry(ENOTBLK, "ENOTBLK", "Block device required"),
    #endif
    #if defined (EBUSY)
        error_table_entry(EBUSY, "EBUSY", "Device busy"),
    #endif
    #if defined (EEXIST)
        error_table_entry(EEXIST, "EEXIST", "File exists"),
    #endif
    #if defined (EXDEV)
        error_table_entry(EXDEV, "EXDEV", "Cross-device link"),
    #endif
    #if defined (ENODEV)
        error_table_entry(ENODEV, "ENODEV", "No such device"),
    #endif
    #if defined (ENOTDIR)
        error_table_entry(ENOTDIR, "ENOTDIR", "Not a directory"),
    #endif
    #if defined (EISDIR)
        error_table_entry(EISDIR, "EISDIR", "Is a directory"),
    #endif
    #if defined (EINVAL)
        error_table_entry(EINVAL, "EINVAL", "Invalid argument"),
    #endif
    #if defined (ENFILE)
        error_table_entry(ENFILE, "ENFILE", "File table overflow"),
    #endif
    #if defined (EMFILE)
        error_table_entry(EMFILE, "EMFILE", "Too many open files"),
    #endif
    #if defined (ENOTTY)
        error_table_entry(ENOTTY, "ENOTTY", "Not a typewriter"),
    #endif
    #if defined (ETXTBSY)
        error_table_entry(ETXTBSY, "ETXTBSY", "Text file busy"),
    #endif
    #if defined (EFBIG)
        error_table_entry(EFBIG, "EFBIG", "File too large"),
    #endif
    #if defined (ENOSPC)
        error_table_entry(ENOSPC, "ENOSPC", "No space left on device"),
    #endif
    #if defined (ESPIPE)
        error_table_entry(ESPIPE, "ESPIPE", "Illegal seek"),
    #endif
    #if defined (EROFS)
        error_table_entry(EROFS, "EROFS", "Read-only file system"),
    #endif
    #if defined (EMLINK)
        error_table_entry(EMLINK, "EMLINK", "Too many links"),
    #endif
    #if defined (EPIPE)
        error_table_entry(EPIPE, "EPIPE", "Broken pipe"),
    #endif
    #if defined (EDOM)
        error_table_entry(EDOM, "EDOM", "Math argument out of domain of func"),
    #endif
    #if defined (ERANGE)
        error_table_entry(ERANGE, "ERANGE", "Math result not representable"),
    #endif
    #if defined (ENOMSG)
        error_table_entry(ENOMSG, "ENOMSG", "No message of desired type"),
    #endif
    #if defined (EIDRM)
        error_table_entry(EIDRM, "EIDRM", "Identifier removed"),
    #endif
    #if defined (ECHRNG)
        error_table_entry(ECHRNG, "ECHRNG", "Channel number out of range"),
    #endif
    #if defined (EL2NSYNC)
        error_table_entry(EL2NSYNC, "EL2NSYNC", "Level 2 not synchronized"),
    #endif
    #if defined (EL3HLT)
        error_table_entry(EL3HLT, "EL3HLT", "Level 3 halted"),
    #endif
    #if defined (EL3RST)
        error_table_entry(EL3RST, "EL3RST", "Level 3 reset"),
    #endif
    #if defined (ELNRNG)
        error_table_entry(ELNRNG, "ELNRNG", "Link number out of range"),
    #endif
    #if defined (EUNATCH)
        error_table_entry(EUNATCH, "EUNATCH", "Protocol driver not attached"),
    #endif
    #if defined (ENOCSI)
        error_table_entry(ENOCSI, "ENOCSI", "No CSI structure available"),
    #endif
    #if defined (EL2HLT)
        error_table_entry(EL2HLT, "EL2HLT", "Level 2 halted"),
    #endif
    #if defined (EDEADLK)
        error_table_entry(EDEADLK, "EDEADLK", "Deadlock condition"),
    #endif
    #if defined (ENOLCK)
        error_table_entry(ENOLCK, "ENOLCK", "No record locks available"),
    #endif
    #if defined (EBADE)
        error_table_entry(EBADE, "EBADE", "Invalid exchange"),
    #endif
    #if defined (EBADR)
        error_table_entry(EBADR, "EBADR", "Invalid request descriptor"),
    #endif
    #if defined (EXFULL)
        error_table_entry(EXFULL, "EXFULL", "Exchange full"),
    #endif
    #if defined (ENOANO)
        error_table_entry(ENOANO, "ENOANO", "No anode"),
    #endif
    #if defined (EBADRQC)
        error_table_entry(EBADRQC, "EBADRQC", "Invalid request code"),
    #endif
    #if defined (EBADSLT)
        error_table_entry(EBADSLT, "EBADSLT", "Invalid slot"),
    #endif
    #if defined (EDEADLOCK)
        error_table_entry(EDEADLOCK, "EDEADLOCK", "File locking deadlock error"),
    #endif
    #if defined (EBFONT)
        error_table_entry(EBFONT, "EBFONT", "Bad font file format"),
    #endif
    #if defined (ENOSTR)
        error_table_entry(ENOSTR, "ENOSTR", "Device not a stream"),
    #endif
    #if defined (ENODATA)
        error_table_entry(ENODATA, "ENODATA", "No data available"),
    #endif
    #if defined (ETIME)
        error_table_entry(ETIME, "ETIME", "Timer expired"),
    #endif
    #if defined (ENOSR)
        error_table_entry(ENOSR, "ENOSR", "Out of streams resources"),
    #endif
    #if defined (ENONET)
        error_table_entry(ENONET, "ENONET", "Machine is not on the network"),
    #endif
    #if defined (ENOPKG)
        error_table_entry(ENOPKG, "ENOPKG", "Package not installed"),
    #endif
    #if defined (EREMOTE)
        error_table_entry(EREMOTE, "EREMOTE", "Object is remote"),
    #endif
    #if defined (ENOLINK)
        error_table_entry(ENOLINK, "ENOLINK", "Link has been severed"),
    #endif
    #if defined (EADV)
        error_table_entry(EADV, "EADV", "Advertise error"),
    #endif
    #if defined (ESRMNT)
        error_table_entry(ESRMNT, "ESRMNT", "Srmount error"),
    #endif
    #if defined (ECOMM)
        error_table_entry(ECOMM, "ECOMM", "Communication error on send"),
    #endif
    #if defined (EPROTO)
        error_table_entry(EPROTO, "EPROTO", "Protocol error"),
    #endif
    #if defined (EMULTIHOP)
        error_table_entry(EMULTIHOP, "EMULTIHOP", "Multihop attempted"),
    #endif
    #if defined (EDOTDOT)
        error_table_entry(EDOTDOT, "EDOTDOT", "RFS specific error"),
    #endif
    #if defined (EBADMSG)
        error_table_entry(EBADMSG, "EBADMSG", "Not a data message"),
    #endif
    #if defined (ENAMETOOLONG)
        error_table_entry(ENAMETOOLONG, "ENAMETOOLONG", "File name too long"),
    #endif
    #if defined (EOVERFLOW)
        error_table_entry(EOVERFLOW, "EOVERFLOW", "Value too large for defined data type"),
    #endif
    #if defined (ENOTUNIQ)
        error_table_entry(ENOTUNIQ, "ENOTUNIQ", "Name not unique on network"),
    #endif
    #if defined (EBADFD)
        error_table_entry(EBADFD, "EBADFD", "File descriptor in bad state"),
    #endif
    #if defined (EREMCHG)
        error_table_entry(EREMCHG, "EREMCHG", "Remote address changed"),
    #endif
    #if defined (ELIBACC)
        error_table_entry(ELIBACC, "ELIBACC", "Can not access a needed shared library"),
    #endif
    #if defined (ELIBBAD)
        error_table_entry(ELIBBAD, "ELIBBAD", "Accessing a corrupted shared library"),
    #endif
    #if defined (ELIBSCN)
        error_table_entry(ELIBSCN, "ELIBSCN", ".lib section in a.out corrupted"),
    #endif
    #if defined (ELIBMAX)
        error_table_entry(ELIBMAX, "ELIBMAX", "Attempting to link in too many shared libraries"),
    #endif
    #if defined (ELIBEXEC)
        error_table_entry(ELIBEXEC, "ELIBEXEC", "Cannot exec a shared library directly"),
    #endif
    #if defined (EILSEQ)
        error_table_entry(EILSEQ, "EILSEQ", "Illegal byte sequence"),
    #endif
    #if defined (ENOSYS)
        error_table_entry(ENOSYS, "ENOSYS", "Operation not applicable"),
    #endif
    #if defined (ELOOP)
        error_table_entry(ELOOP, "ELOOP", "Too many symbolic links encountered"),
    #endif
    #if defined (ERESTART)
        error_table_entry(ERESTART, "ERESTART", "Interrupted system call should be restarted"),
    #endif
    #if defined (ESTRPIPE)
        error_table_entry(ESTRPIPE, "ESTRPIPE", "Streams pipe error"),
    #endif
    #if defined (ENOTEMPTY)
        error_table_entry(ENOTEMPTY, "ENOTEMPTY", "Directory not empty"),
    #endif
    #if defined (EUSERS)
        error_table_entry(EUSERS, "EUSERS", "Too many users"),
    #endif
    #if defined (ENOTSOCK)
        error_table_entry(ENOTSOCK, "ENOTSOCK", "Socket operation on non-socket"),
    #endif
    #if defined (EDESTADDRREQ)
        error_table_entry(EDESTADDRREQ, "EDESTADDRREQ", "Destination address required"),
    #endif
    #if defined (EMSGSIZE)
        error_table_entry(EMSGSIZE, "EMSGSIZE", "Message too long"),
    #endif
    #if defined (EPROTOTYPE)
        error_table_entry(EPROTOTYPE, "EPROTOTYPE", "Protocol wrong type for socket"),
    #endif
    #if defined (ENOPROTOOPT)
        error_table_entry(ENOPROTOOPT, "ENOPROTOOPT", "Protocol not available"),
    #endif
    #if defined (EPROTONOSUPPORT)
        error_table_entry(EPROTONOSUPPORT, "EPROTONOSUPPORT", "Protocol not supported"),
    #endif
    #if defined (ESOCKTNOSUPPORT)
        error_table_entry(ESOCKTNOSUPPORT, "ESOCKTNOSUPPORT", "Socket type not supported"),
    #endif
    #if defined (EOPNOTSUPP)
        error_table_entry(EOPNOTSUPP, "EOPNOTSUPP", "Operation not supported on transport endpoint"),
    #endif
    #if defined (EPFNOSUPPORT)
        error_table_entry(EPFNOSUPPORT, "EPFNOSUPPORT", "Protocol family not supported"),
    #endif
    #if defined (EAFNOSUPPORT)
        error_table_entry(EAFNOSUPPORT, "EAFNOSUPPORT", "Address family not supported by protocol"),
    #endif
    #if defined (EADDRINUSE)
        error_table_entry(EADDRINUSE, "EADDRINUSE", "Address already in use"),
    #endif
    #if defined (EADDRNOTAVAIL)
        error_table_entry(EADDRNOTAVAIL, "EADDRNOTAVAIL","Cannot assign requested address"),
    #endif
    #if defined (ENETDOWN)
        error_table_entry(ENETDOWN, "ENETDOWN", "Network is down"),
    #endif
    #if defined (ENETUNREACH)
        error_table_entry(ENETUNREACH, "ENETUNREACH", "Network is unreachable"),
    #endif
    #if defined (ENETRESET)
        error_table_entry(ENETRESET, "ENETRESET", "Network dropped connection because of reset"),
    #endif
    #if defined (ECONNABORTED)
        error_table_entry(ECONNABORTED, "ECONNABORTED", "Software caused connection abort"),
    #endif
    #if defined (ECONNRESET)
        error_table_entry(ECONNRESET, "ECONNRESET", "Connection reset by peer"),
    #endif
    #if defined (ENOBUFS)
        error_table_entry(ENOBUFS, "ENOBUFS", "No buffer space available"),
    #endif
    #if defined (EISCONN)
        error_table_entry(EISCONN, "EISCONN", "Transport endpoint is already connected"),
    #endif
    #if defined (ENOTCONN)
        error_table_entry(ENOTCONN, "ENOTCONN", "Transport endpoint is not connected"),
    #endif
    #if defined (ESHUTDOWN)
        error_table_entry(ESHUTDOWN, "ESHUTDOWN", "Cannot send after transport endpoint shutdown"),
    #endif
    #if defined (ETOOMANYREFS)
        error_table_entry(ETOOMANYREFS, "ETOOMANYREFS", "Too many references: cannot splice"),
    #endif
    #if defined (ETIMEDOUT)
        error_table_entry(ETIMEDOUT, "ETIMEDOUT", "Connection timed out"),
    #endif
    #if defined (ECONNREFUSED)
        error_table_entry(ECONNREFUSED, "ECONNREFUSED", "Connection refused"),
    #endif
    #if defined (EHOSTDOWN)
        error_table_entry(EHOSTDOWN, "EHOSTDOWN", "Host is down"),
    #endif
    #if defined (EHOSTUNREACH)
        error_table_entry(EHOSTUNREACH, "EHOSTUNREACH", "No route to host"),
    #endif
    #if defined (EALREADY)
        error_table_entry(EALREADY, "EALREADY", "Operation already in progress"),
    #endif
    #if defined (EINPROGRESS)
        error_table_entry(EINPROGRESS, "EINPROGRESS", "Operation now in progress"),
    #endif
    #if defined (ESTALE)
        error_table_entry(ESTALE, "ESTALE", "Stale NFS file handle"),
    #endif
    #if defined (EUCLEAN)
        error_table_entry(EUCLEAN, "EUCLEAN", "Structure needs cleaning"),
    #endif
    #if defined (ENOTNAM)
        error_table_entry(ENOTNAM, "ENOTNAM", "Not a XENIX named type file"),
    #endif
    #if defined (ENAVAIL)
        error_table_entry(ENAVAIL, "ENAVAIL", "No XENIX semaphores available"),
    #endif
    #if defined (EISNAM)
        error_table_entry(EISNAM, "EISNAM", "Is a named type file"),
    #endif
    #if defined (EREMOTEIO)
        error_table_entry(EREMOTEIO, "EREMOTEIO", "Remote I/O error"),
    #endif
        error_table_entry(0, NULL, NULL)
};

static const char* posix_supplement_errors_storage[256];

static void posix_supplement_errors_build() {
    if (posix_supplement_errors == null) {
        posix_supplement_errors = posix_supplement_errors_storage;
        for (int i = 0; i < countof(error_table); i++) {
            if (error_table[i].name != null && error_table[i].value < countof(posix_supplement_errors_storage)) {
                posix_supplement_errors[error_table[i].value] = error_table[i].message;
                posix_supplement_errors_count = max(error_table[i].value, posix_supplement_errors_count);
            }
        }
        assert(posix_supplement_errors != null && posix_supplement_errors_count > 0);
    }
}

static_init(posix_supplement_errors_build) {
    posix_supplement_errors_build();
}

#else // WINDOWS

static bool linux_trace_to_stderr;

static_init(linux_trace_to_stderr) {
    linux_trace_to_stderr = system_option("linux.trace_to_stderr");
}

text256_t strerr_(int e) {
    if (e == 0) {
        static text256_t empty; // "" message
        return empty;
    } else {
        text256_t s; // posix message
        int save = errno;
        s.text[0] = 0;
        snprintf0(s.text, countof(s.text), "error=(%d): \"%s\"", e, strerror(e));
        errno = save;
        return s;
    }
}

static const char* PREFIX[] = {
     "system.software\\",
     "system.software/",
     "..\\..\\src\\",
     "../../src/",
     "..\\..\\inc\\",
     "../../inc/",
     "src\\",
     "src/",
     "inc\\",
     "inc/",
     null
};

static const char* unprefix(const char* file) {
    if (file != null) {
        const char** p = PREFIX;
        while (*p != null) {
            const char* prefix = strstr(file, *p);
            if (prefix != null) {
                file = prefix + strlen(*p);
            }
            p++;
        }
    }
    return file;
}

static char* undecorate(char* function, const char* func) {
    if (func == null || strlen(func) == 0) { return function; }
    strcpy(function, func);
    char* p = function;
    char* column = strchr(p, ':');
    if (column != null && column[1] == ':') { p = column + 2; }
    char* params = strchr(p, '(');
    if (params != null) { *params = 0; }
    return p;
}

int _assertion_(const char* condition, const char* file, int line, const char* func, const char* format, ...) {
    va_list vl;
    va_start(vl, format);
    char buf[16 * 1024];
    char* function = (char*)alloca(func == null ? 1 : strlen(func) + 1);
    function[0] = 0;
    if (file != null) {
        file = unprefix(file);
        if (func != null) {
            sprintf(buf, "%s:%d %s ", file, line, undecorate(function, func));
        } else {
            sprintf(buf, "%s:%d ", file, line);
        }
        sprintf(buf + strlen(buf), "assertion \"%s\" failed ", condition);
        vsprintf(buf + strlen(buf), format, vl);
    } else {
        sprintf(buf, "assertion \"%s\" failed ", condition);
        vsprintf(buf + strlen(buf), format, vl);
    }
    va_end(vl);
    const char* st = stacktrace();
#ifdef ANDROID
    if (st != null) {
        __android_log_print(ANDROID_LOG_DEBUG, "zspace", "%s %s", buf, st);
    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "zspace", "%s", buf);
    }
#elif !defined(WINDOWS)
    fprintf(stderr, "%s %s", buf, st);
#endif
    breakpoint();
    return false;
}

void _trace_v(const char* file, int line, const char* func, const char* format, va_list vl) {
    char buf[16 * 1024];
    char* function = (char*)alloca(func == null ? 1 : strlen(func) + 1);
    function[0] = 0;
    if (file != null) {
        file = unprefix(file);
        if (func != null) {
            sprintf(buf, "%s:%-4d %s ", file, line, undecorate(function, func));
        } else {
            sprintf(buf, "%s:%-4d ", file, line);
        }
        vsprintf(buf + strlen(buf), format, vl);
    } else {
        vsprintf(buf, format, vl);
    }
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_DEBUG, "zspace", "%s", buf);
#elif !defined(WINDOWS)
    if (linux_trace_to_stderr) {
        fprintf(stderr, "%s", buf);
    }
#endif
}

void _trace_(const char* file, int line, const char* func, const char* format, ...) {
    int errno_ = errno;
    va_list vl;
    va_start(vl, format);
    _trace_v(file, line, func, format, vl);
    va_end(vl);
    errno = errno_;
}

#endif // WINDOWS

void _hex_dump_v_(bool log, const char* file, int line, const char* func, const void* data, int bytes, const char* format, va_list vl) {
    int errno_ = errno;
    const unsigned char* p = (const unsigned char*)data;
    char s[1024];
    _trace_v(file, line, func, format, vl);
    for (int i = 0; i < bytes; i += 16) {
        int len = bytes - i < 16 ? bytes - i : 16;
        for (int k = 0; k < 16; k++) {
            if (k < len) {
                sprintf(&s[k * 3 + k / 4], "%02X  ", p[k]);
            } else {
                strcpy(&s[k * 3 + k / 4], "    ");
            }
        }
        int a = (int)strlen(s);
        for (int k = 0; k < len; k++) {
            s[a + k] = (char)(k < len && 32 <= p[k] && p[k] <= 127 ? p[k] : '?');
        }
        s[a + len] = 0;
        _trace_(file, line, func, "%p: %s", p, s);
        p += 16;
    }
    errno = errno_;
}

void _hex_dump_(bool log, const char* file, int line, const char* func, const void* data, int bytes, const char* format, ...) {
    va_list vl;
    va_start(vl, format);
    _hex_dump_v_(log, file, line, func, data, bytes, format, vl);
    va_end(vl);
}

/*
MSVC experimental: strerr, __sys_errlist, _sys_errlist only defined up to sys_nerr == 43
[ 0] "No error"
[ 1] "Operation not permitted"
[ 2] "No such file or directory"
[ 3] "No such process"
[ 4] "Interrupted function call"
[ 5] "Input/output error"
[ 6] "No such device or address"
[ 7] "Arg list too long"
[ 8] "Exec format error"
[ 9] "Bad file descriptor"
[10] "No child processes"
[11] "Resource temporarily unavailable"
[12] "Not enough space"
[13] "Permission denied"
[14] "Bad address"
[15] "Unknown error"
[16] "Resource device"
[17] "File exists"
[18] "Improper link"
[19] "No such device"
[20] "Not a directory"
[21] "Is a directory"
[22] "Invalid argument"
[23] "Too many open files in system"
[24] "Too many open files"
[25] "Inappropriate I/O control operation"
[26] "Unknown error"
[27] "File too large"
[28] "No space left on device"
[29] "Invalid seek"
[30] "Read-only file system"
[31] "Too many links"
[32] "Broken pipe"
[33] "Domain error"
[34] "Result too large"
[35] "Unknown error"
[36] "Resource deadlock avoided"
[37] "Unknown error"
[38] "Filename too long"
[39] "No locks available"
[40] "Function not implemented"
[41] "Directory not empty"
[42] "Illegal byte sequence"
[43] "Unknown error"
*/

END_C
