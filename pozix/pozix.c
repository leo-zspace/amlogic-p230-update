#include "pozix.h"
#ifdef WINDOWS
#include <AccCtrl.h>
#include <AclAPI.h>
#include <timeapi.h>
#pragma comment(lib, "winmm")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "mincore.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "shell32") // PathIsDirectoryA
#else
#include <stdatomic.h>
#endif

BEGIN_C

char* strncat0(char* d, const char* s, int n) {
    assert(d != null && s != null && n > 0);
    int len = (int)strlen(d);
    int bytes = min((int)strlen(s), n - len - 1);
    if (bytes > 0) {
        memcpy(d + len, s, (size_t)bytes);
        d[len + bytes] = 0;
    }
    return d;
}

char* strncpy0(char* d, const char* s, int n) {
    assert(d != null && s != null && n > 0);
    strncpy(d, s, (size_t)n); // on Windows may result in non-zero-terminated destination
    d[n - 1] = 0;
    return d;
}

int snprintf0(char* d, int n, const char* format, ...) {
    assert(d != null && format != null && n > 0);
    va_list vl;
    va_start(vl, format);
    int r = vsnprintf0(d, n, format, vl);
    va_end(vl);
    return r;
}

int vsnprintf0(char* d, int n, const char* format, va_list vl) {
    assert(d != null && format != null && n > 0);
    // https://stackoverflow.com/questions/9091960/can-vsnprintf-return-negative-value-of-magnitude-greater-than-1
    int r = vsnprintf(d, (size_t)n, format, vl); // on Windows may result in non-zero-terminated destination
    d[n - 1] = 0; // guarantee 0 at the end of the string
#ifdef DEBUG
    // I simply and universally cannot rely on the fact that Windows runtime
    // (known to be broken many times) will return correct output length.
    if (r >= 0) { assert(r == (int)strlen(d)); }
#endif
    // because Windows return -1 on destination overflow
    // pretend that "n" characters is needed (we only know for sure at least 1 character did not fit)
    return r < 0 ? n : r;
}

int number_of_bits32(const int32_t b) { // http://en.wikipedia.org/wiki/Hamming_weight
    uint32_t bitset = b;
    bitset = bitset - ((bitset >> 1) & 0x55555555U);
    bitset = (bitset & 0x33333333U) + ((bitset >> 2) & 0x33333333U);
    return (((bitset + (bitset >> 4)) & 0x0F0F0F0FU) * 0x01010101U) >> 24;
}

int number_of_bits64(const int64_t b) {
    uint64_t bitset = b;
    unsigned int lo = bitset & 0xFFFFFFFF;
    unsigned int hi = bitset >> 32;
    return number_of_bits32(lo) + number_of_bits32(hi);
}

#if defined(__clang__) || defined(__GNUC__)
#define clzll(x) __builtin_clzll(x)
#else
static int __inline clzll(uint64_t value) {
    DWORD leading_zero = 0;
    return BitScanReverse64(&leading_zero, value) ? 63 - leading_zero : 64;
}
#endif

int number_of_leading_zeros(int64_t x) {
    // if not defined use: http://www.hackersdelight.org/hdcodetxt/nlz.c.txt
    return x == 0 ? sizeof(x) * 8 : clzll(x);
}

const char* i2b(uint64_t v, char* buf /* must be at least 66 */, bool leading_zeros) {
    int n = sizeof(uint64_t) * 8;
    if (!leading_zeros) {
        n = n - number_of_leading_zeros(v) + 1;
    }
    for (int i = 0; i < n; i++) {
        buf[n - 1 - i] = (char)((1ULL << i) & v ? '0' : '1');
    }
    buf[n] = 0;
    return buf;
}

int mkdirs(const char* dir) {
    int err = 0;
    char* tmp = (char*)stack_allocz((int)strlen(dir) + 1);
    const char* next = strchr(dir, '\\');
    if (next == null) { next = strchr(dir, '/'); }
    while (next != null) {
        if (next > dir && *(next - 1) != ':') {
            memcpy(tmp, dir, next - dir);
            err = mkdir(tmp, 0777);
            if (err != 0) {
                err = errno;
                if (err != EEXIST) { break; }
//              trace("mkdir(%s)=%d", tmp, err);
            }
        }
        const char* prev = ++next;
        next = strchr(prev, '\\');
        if (next == null) { next = strchr(prev, '/'); }
    }
    if (err == 0 || err == EEXIST) {
        err = mkdir(dir, 0777);
        if (err != 0) { err = errno; }
    }
//  trace("mkdir(%s)=%d", dir, err);
    return err;
}

char* strtolower(char* str) {
    char* s = str;
    while (*s != 0) { *s = (char)tolower(*s); s++; }
    return str;
}

char* strtoupper(char* str) {
    char* s = str;
    while (*s != 0) { *s = (char)toupper(*s); s++; }
    return str;
}

#ifdef WINDOWS

int is_debugger_present() {
    return IsDebuggerPresent();
}

#pragma comment(lib, "Advapi32") // SetFileSecurityA

int chmod_world_777(const char* filename) {
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
    PSID everyone = null; // Create a well-known SID for the Everyone group.
    BOOL b = AllocateAndInitializeSid(&SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &everyone);
    assert(b && everyone != null);
    EXPLICIT_ACCESSA ea[1] = {}; // Initialize an EXPLICIT_ACCESS structure for an ACE.
    ea[0].grfAccessPermissions = 0xFFFFFFFF;
    ea[0].grfAccessMode  = GRANT_ACCESS; // The ACE will allow everyone all access.
    ea[0].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[0].Trustee.ptstrName  = (LPSTR)everyone;
    // Create a new ACL that contains the new ACEs.
    ACL* acl = null;
    b = b && SetEntriesInAclA(1, ea, null, &acl) == ERROR_SUCCESS;
    assert(b && acl != null); (void)b;
    // Initialize a security descriptor.
    SECURITY_DESCRIPTOR* sd = (SECURITY_DESCRIPTOR*)stack_alloc(SECURITY_DESCRIPTOR_MIN_LENGTH);
    b = InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
    assert(b);
    // Add the ACL to the security descriptor.
    b = b && SetSecurityDescriptorDacl(sd, /* bDaclPresent flag: */ true, acl, /* not a default DACL: */  false);
    assert(b);
    // Change the security attributes
    b = b && SetFileSecurityA(filename, DACL_SECURITY_INFORMATION, sd);
    if (!b) {
        trace("WARNING: SetFileSecurity failed");
    }
    if (everyone) { FreeSid(everyone); }
    if (acl == null) { LocalFree(acl); }
    if (b) { return 0; } else { errno = GetLastError(); return -1; }
}

/*
static void test_chmod_world_777() {
    int r = chmod_world_777("C:\\ProgramData\\zSpace");
    assertion(r == 0, "%s", strerr(errno));
    r = chmod_world_777("C:\\ProgramData\\zSpace\\logs");
    assertion(r == 0, "%s", strerr(errno));
    r = chmod_world_777("C:\\ProgramData\\zSpace\\logs\\system");
    assertion(r == 0, "%s", strerr(errno));
}
*/

/*
LOGFONT "Consolas Bold" -14 (pixels)
"Consolas Bold"
GetTextExtentPoint32 56x17 DrawText(DT_CALCRECT) 48x17 buf="S&tylus" because of "&" and absense of DT_NOPREFIX
*/

static int draw_text_with_color_v(canvas_t c, font_t f, COLORREF color, int x, int y, int *w, int *h,
                                  bool draw, const char* format, va_list vl) {
    int n = 1024;
    char* text = (char*)stack_allocz(n);
    int k = vsnprintf(text, n - 1, format, vl);
    while (k > n - 1 || k < 0) { // Microsoft returns -1 not posix required sizeof buffer
        n = n * 2;
        text = (char*)stack_allocz(n);
        k = vsnprintf(text, n - 1, format, vl);
    }
    assertion(k >= 0 && k <= n, "k=%d n=%d fmt=%s", k, n, format);
    font_t font = (HFONT)SelectObject(c, f);
    int mode = SetBkMode(c, TRANSPARENT);
    RECT r = {x, y, x + 2000, y + 100};
    BOOL b = DrawTextA(c, text, -1, &r, DT_LEFT|DT_NOCLIP|DT_SINGLELINE|DT_CALCRECT);
    assertion(b, "DrawTextA(%s) failed", text); (void)b;
    int width = r.right - r.left;
    int height = r.bottom - r.top;
#ifdef DRAW_TEXT_COMPARE_MEASUREMENTS
    SIZE size = {0, 0};
    b = GetTextExtentPoint32A(c, text, (int)strlen(text), &size);
    assertion(b, "GetTextExtentPoint32(%s) failed", text); (void)b;
    if (width != size.cx || height != size.cy) { trace("GetTextExtentPoint32 %dx%d DrawText %dx%d \"%s\"", size.cx, size.cy, width, height, text); }
    if ((size.cx == 0  || size.cy == 0) && text[0] != 0) {
        trace("What?! GetTextExtentPoint32 %dx%d DrawText %dx%d \"%s\"", size.cx, size.cy, width, height, text);
    }
#endif
    *w = width;
    *h = height;
    if (draw) {
        RECT rc = {x, y, x + 2000, y + 100};
        COLORREF text_color = SetTextColor(c, color);
//      COLORREF background_color = SetBkColor(c, RGB(0, 0, 0));
        b = DrawTextA(c, text, -1, &rc, DT_LEFT|DT_NOCLIP|DT_SINGLELINE);
        assertion(b, "DrawTextA(%s) failed", text); (void)b;
        SetTextColor(c, text_color);
//      SetBkColor(c, background_color);
    }
    SetBkMode(c, mode);
    SelectObject(c, font);
    return x + width;
}

int measure_text(canvas_t c, font_t f, int *w, int *h, const char* format, ...) {
    va_list vl;
    va_start(vl, format);
    int rx = draw_text_with_color_v(c, f, 0, 0, 0, w, h, false, format, vl);
    va_end(vl);
    return rx;
}

int draw_colored_text(canvas_t c, font_t f, COLORREF color, int x, int y, const char* format, ...) {
    va_list vl;
    va_start(vl, format);
    int w = 0, h = 0;
    int rx = draw_text_with_color_v(c, f, color, x, y, &w, &h, true, format, vl);
    va_end(vl);
    return rx;
}

int draw_text(canvas_t c, font_t f, int x, int y, const char* fmt, ...) {
    va_list vl;
    va_start(vl, fmt);
    int w = 0, h = 0;
    int rx = draw_text_with_color_v(c, f, RGB(255, 255, 240), x, y, &w, &h, true, fmt, vl);
    va_end(vl);
    return rx;
}

bool am_i_root() {
    bool elevated = false;
    HANDLE token = null;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation = {0};
        DWORD size = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            elevated = elevation.TokenIsElevated;
        }
    }
    if (token != null) {
        CloseHandle(token);
    }
    return elevated;
}

// https://stackoverflow.com/a/11743614 & https://stackoverflow.com/a/21264373

typedef int (*nt_query_timer_resolution_t)(ULONG* minimum_resolution, ULONG* maximum_resolution, ULONG* actual_resolution);
typedef int (*nt_settimer_resolution_t)(ULONG RequestedResolution, BOOLEAN Set, ULONG* ActualResolution);

int scheduler_set_timer_resolution(int64_t nanoseconds) {
    const int ns100 = (int)(nanoseconds / 100);
    nt_query_timer_resolution_t NtQueryTimerResolution = (nt_query_timer_resolution_t)
        GetProcAddress(LoadLibraryA("NtDll.dll"), "NtQueryTimerResolution");
    nt_settimer_resolution_t NtSetTimerResolution = (nt_settimer_resolution_t)
        GetProcAddress(LoadLibraryA("NtDll.dll"), "NtSetTimerResolution");
    unsigned long minimum_100ns = 16 * 10 * 1000; // it is resolution not frequency
    unsigned long maximum_100ns =  1 * 10 * 1000; // this is why it is in reverse to common sense
    unsigned long actual_100ns  =  0;             // and what is not on Windows?
    int r = 0;
    if (NtQueryTimerResolution != null && NtQueryTimerResolution(&minimum_100ns, &maximum_100ns, &actual_100ns) == 0) {
        int64_t minimum_ns = minimum_100ns * 100LL;
        int64_t maximum_ns = maximum_100ns * 100LL;
//      int64_t actual_ns  = actual_100ns  * 100LL; // note that maximum resolution is actually < minimum
        if (NtSetTimerResolution == null) {
            const int milliseconds = (int)(ns2ms(nanoseconds) + 0.5);
            r = (int)maximum_ns <= nanoseconds && nanoseconds <= (int)minimum_ns ? timeBeginPeriod(milliseconds) : EINVAL;
        } else {
            r = (int)maximum_ns <= nanoseconds && nanoseconds <= (int)minimum_ns ? NtSetTimerResolution(ns100, true, &actual_100ns) : EINVAL;
//          trace("NtSetTimerResolution(%.3f -> %.3f)", ns2ms(ns100 * 100LL), ns2ms(actual_100ns * 100LL));
        }
        NtQueryTimerResolution(&minimum_100ns, &maximum_100ns, &actual_100ns);
//      int64_t became_ns  = actual_100ns  * 100LL;
//      trace("timer resolution was [%.3f %.3f %.3f] := %0.3f -> %0.3f", ns2ms(maximum_ns), ns2ms(actual_ns), ns2ms(minimum_ns), ns2ms(nanoseconds), ns2ms(became_ns));
    } else {
        const int milliseconds = (int)(ns2ms(nanoseconds) + 0.5);
        r = 1 <= milliseconds && milliseconds <= 16 ? timeBeginPeriod(milliseconds) : EINVAL;
    }
    return r;
}

int64_t scheduler_get_timer_resolution() {
    nt_query_timer_resolution_t NtQueryTimerResolution = (nt_query_timer_resolution_t)
        GetProcAddress(LoadLibraryA("NtDll.dll"), "NtQueryTimerResolution");
    unsigned long minimum_100ns =  1 * 10 * 1000;
    unsigned long maximum_100ns = 16 * 10 * 1000;
    unsigned long actual_100ns  =  0;
    if (NtQueryTimerResolution != null && NtQueryTimerResolution(&minimum_100ns, &maximum_100ns, &actual_100ns) == 0) {
        int64_t actual_ns  = actual_100ns  * 100LL; // note that maximum resolution is actually < minimum
//      int64_t minimum_ns = minimum_100ns * 100LL;
//      int64_t maximum_ns = maximum_100ns * 100LL;
//      trace("timer resolution [%.3f %.3f %.3f]", ns2ms(maximum_ns), ns2ms(actual_ns), ns2ms(minimum_ns));
        return actual_ns;
    } else {
        return -1;
    }
}

int _utf8_for_wcs_(const wchar_t* wcs) {
    int required_bytes_count = WideCharToMultiByte(
        CP_UTF8, // codepage
        WC_ERR_INVALID_CHARS, // flags
        wcs,
        -1,   // -1 means "wcs" is 0x0000 terminated
        null,
        0,
        null, //  default char*
        null  //  bool* used default char
    );
    assert(required_bytes_count > 0);
    return required_bytes_count;
}

int _wcs_for_utf8_(const char* s) {
    int required_bytes_count = MultiByteToWideChar(
        CP_UTF8, // codepage,
        0, // flags,
        s,
        -1,   //-1 means "s" 0x00 terminated
        null,
        0
    );
    assert(required_bytes_count > 0);
    return required_bytes_count;
}

char* _wcs2str_(char* s, const wchar_t* wcs) {
#ifdef NO_UTF8
    char* r = s;
    while (*wcs != 0) { *s++ = (char)*wcs++; }
    *s = 0;
    return r;
#else
    int r = WideCharToMultiByte(
        CP_UTF8, // codepage,
        WC_ERR_INVALID_CHARS, // flags,
        wcs,
        -1,   // -1 means 0x0000 terminated
        s,
        _utf8_for_wcs_(wcs),
        null, //  default char*,
        null  // bool* used default char
    );
    if (r == 0) {
        trace("WideCharToMultiByte() failed %s", strerr(GetLastError()));
    }
    return s;
#endif
}

TCHAR* _wcs2tstr_(TCHAR* s, const wchar_t* wcs) {
    TCHAR* r = s;
    while (*wcs != 0) { *s++ = (char)*wcs++; }
    *s = 0;
    return r;
}

WCHAR* _str2wcs_(WCHAR* wcs, const char* s) {
#ifdef NO_UTF8
    WCHAR* r = wcs;
    while (*s != 0) { *wcs++ = (WCHAR)*s++; }
    *wcs = 0;
    return r;
#else
    int r = MultiByteToWideChar(
        CP_UTF8, // codepage,
        0, // flags,
        s,
        -1,   //-1 means "s" 0x00 terminated
        wcs,
        _wcs_for_utf8_(s)
    );
    if (r == 0) {
        trace("WideCharToMultiByte() failed %s", strerr(GetLastError()));
    }
    return wcs;
#endif
}

#pragma warning(push)
#pragma warning(disable: 4127) // conditional expression is constant

// Over the years Microsoft keep changing parameter type of Interlocked*crement() from bad (long) to worse (unsigned)...
// No cure for the crippled... :(
int32_t atomics_increment_int32(volatile int32_t* a) { return InterlockedIncrement((unsigned volatile *)a); }
int32_t atomics_decrement_int32(volatile int32_t* a) { return InterlockedDecrement((unsigned volatile *)a); }
int64_t atomics_increment_int64(volatile int64_t* a) { return InterlockedIncrement64((__int64 volatile *)a); }
int64_t atomics_decrement_int64(volatile int64_t* a) { return InterlockedDecrement64((__int64 volatile *)a); }

void* atomics_read(volatile void** a) {
#if defined(_WIN64) || defined(_M_X64)
//  assertion(sizeof(void*) == 8 && sizeof(uintptr_t) == 8, "expected 64 bit architecture");
    return (void*)_InterlockedOr64((__int64 volatile *)a, (__int64)0LL);
#else
//  assertion(sizeof(void*) == 4 && sizeof(uintptr_t) == 4, "expected 32 bit architecture");
    return (void*)_InterlockedOr((long volatile *)a, (long)0);
#endif
}

int32_t atomics_read32(volatile int32_t *a) {
    return (int32_t)_InterlockedOr((long volatile *)a, 0);
}

#ifdef _WIN64
int64_t atomics_read64(volatile int64_t *a) {
    return (int64_t)_InterlockedOr64((__int64 volatile *)a, (__int64)0LL);
}
#endif

int64_t atomics_exchange_int64(volatile int64_t* a, int64_t v) {
    return (int64_t)InterlockedExchange64((LONGLONG*)a, (LONGLONG)v);
}

int32_t atomics_exchange_int32(volatile int32_t* a, int32_t v) {
    assert(sizeof(int32_t) == sizeof(unsigned long));
    return (int32_t)InterlockedExchange((unsigned long*)a, (unsigned long)v);
}

bool atomics_compare_exchange_int64(volatile int64_t* a, int64_t comparand, int64_t v) {
    return (int64_t)InterlockedCompareExchange64((LONGLONG*)a, (LONGLONG)v, (LONGLONG)comparand) == comparand;
}

bool atomics_compare_exchange_int32(volatile int32_t* a, int32_t comparand, int32_t v) {
    return (int64_t)InterlockedCompareExchange((LONG*)a, (LONG)v, (LONG)comparand) == comparand;
}

double atomics_exchange_double(volatile double* a, double v) {
    LONGLONG r = InterlockedExchange64((LONGLONG*)a, *(LONGLONG*)&v);
    return *(double*)&r;
}

void* atomics_exchange_ptr(volatile void** a, void* v) {
    size_t s = sizeof(void*); (void)s;
#if defined(_WIN64) || defined(_M_X64)
//  assertion(sizeof(void*) == 8 && sizeof(uintptr_t) == 8, "expected 64 bit architecture");
    assertion(s == sizeof(uint64_t), "expected 64 bit architecture");
    return (void*)atomics_exchange_int64((int64_t*)a, (int64_t)v);
#else
//  assertion(sizeof(void*) == 4 && sizeof(uintptr_t) == 4, "expected 32 bit architecture");
    assertion(s == sizeof(uint32_t), "expected 32 bit architecture");
    return (void*)atomics_exchange_int32((int32_t*)a, (int32_t)v);
#endif
}

#pragma warning(pop)

static void* symbol_reference[1024];
static int symbol_reference_count;

void* _force_symbol_reference_(void* symbol) {
    assertion(symbol_reference_count <= countof(symbol_reference), "increase size of symbol_reference[%d] to at least %d", countof(symbol_reference), symbol_reference);
    symbol_reference[symbol_reference_count] = symbol;
//  trace("symbol_reference[%d] = %p", symbol_reference_count, symbol_reference[symbol_reference_count]);
    symbol_reference_count++;
    return symbol;
}

// process_global_data_t on Windows is used to share access to process
// global variables between process and DLL build with pozix static library

#define PROCESS_GLOBAL_DATA "Local\\process_global_data.pid.%d" // use with getpid() only makes sense on Windows

static HANDLE process_global_data_handle;
static process_global_data_t* process_global_data;
static process_global_data_t* process_global_data_shared;
static volatile int32_t process_global_data_spinlock;

static void close_process_global_data() {
    if (process_global_data_shared != null) {
        UnmapViewOfFile(process_global_data_shared);
    } else {
        UnmapViewOfFile(process_global_data);
        CloseHandle(process_global_data_handle);
    }
}

process_global_data_t* open_process_global_data(bool* owns) {
    // lock out earlier concurrent multi-core racing
    while (!atomics_compare_exchange_int32(&process_global_data_spinlock, 0, 1)) { }
    assert(process_global_data_spinlock == 1);
    process_global_data_t* data = null;
    if (process_global_data != null) {
        data = process_global_data;
        *owns = true;
    } else if (process_global_data_shared != null) {
        data = process_global_data_shared;
        *owns = false;
    } else {
        char name[countof(PROCESS_GLOBAL_DATA) + 128];
        snprintf0(name, countof(name), PROCESS_GLOBAL_DATA, getpid());
        HANDLE handle = OpenFileMapping(PAGE_READWRITE, FALSE, name);
        if (handle != null) {
            process_global_data_shared = (process_global_data_t*)MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 4096);
//          trace("process_global_data_shared=%p", process_global_data_shared);
            data = process_global_data_shared;
            assert(process_global_data_shared != null);
            CloseHandle(handle);
            *owns = false;
//          trace("process_global_data_shared=%p owns=%d", process_global_data_shared, *owns);
        } else {
            process_global_data_handle = CreateFileMapping(INVALID_HANDLE_VALUE, null, PAGE_READWRITE, 0, 4096, name);
            SetSecurityInfo(handle, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, null, null, null, null);
            process_global_data = (process_global_data_t*)MapViewOfFile(process_global_data_handle, FILE_MAP_READ|FILE_MAP_WRITE, 0, 0, 4096);
            data = process_global_data;
            assert(process_global_data != null);
            *owns = true;
//          trace("process_global_data=%p owns=%d", process_global_data, *owns);
        }
        atexit(close_process_global_data);
    }
    process_global_data_spinlock = 0;
    return data;
}

int reg_open_or_create_key(HKEY* key, HKEY root, const char* path) { // registry does not understand forward slashes
    *key = null;
    const DWORD sam = KEY_QUERY_VALUE | KEY_READ | KEY_WRITE | KEY_ENUMERATE_SUB_KEYS | DELETE;
    int r = RegOpenKeyExA(root, path, 0, KEY_READ | KEY_WRITE, key);
    if (r != 0) {
        r = RegCreateKeyExA(root, path, 0, null, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, null, key, null);
    }
    if (r != 0) {
        trace("Open or create registry key \"%s\" failed %s.", path, strerr(r));
    }
    return r;
}

int reg_write_dword(HKEY root, const char* path, const char* name, int value) {
    HKEY key = null;
    int r = reg_open_or_create_key(&key, root, path);
    if (r == 0) {
        assert(key != null);
        DWORD dword = (DWORD)value;
        r = RegSetKeyValueA(key, null, name, REG_DWORD, (const char*)&dword, (DWORD)sizeof(dword));
        if (r != 0) {
            trace("Set registry value \"%s\\%s\" failed %s.", path, name, strerr(r));
        }
        int rc = key != null ? RegCloseKey(key) : 0; assert(rc == 0); (void)rc;
    }
    return r;
}

void disable_dll_loading_thread_pool() {
    //  TODO: disable DLL loading thread pool:
    //  see: https://stackoverflow.com/questions/42789199/why-there-are-three-unexpected-worker-threads-when-a-win32-console-application-s
    static const char* IMAGE_FILE_EXECUTION_OPTIONS = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options";
    char pathname[4096];
    if (GetModuleFileNameA(GetModuleHandleA(null), pathname, countof(pathname))) {
        char* p = strrchr(pathname, '\\');
        if (p != null) { reg_write_dword(HKEY_LOCAL_MACHINE, concat(IMAGE_FILE_EXECUTION_OPTIONS, p), "MaxLoaderThreads", 1); }
    }
}

int disinherit_handle(HANDLE h) {
    DWORD flags = 0;
    int r = GetHandleInformation(h, &flags) ? 0 : GetLastError();
    if (r != 0) { log_err("GetHandleInformation(%p) failed %s", h, strerr(r)); }
    if (r == 0) {
        //      if (flags & HANDLE_FLAG_INHERIT) { log_info("%p flags=0x%08X (HANDLE_FLAG_INHERIT)", h, flags); }
        //      if (flags & HANDLE_FLAG_PROTECT_FROM_CLOSE) { log_info("%p flags=0x%08X (HANDLE_FLAG_PROTECT_FROM_CLOSE)", h, flags); }
        r = SetHandleInformation(h, HANDLE_FLAG_INHERIT | HANDLE_FLAG_PROTECT_FROM_CLOSE, 0) ? 0 : GetLastError();
        if (r != 0) { log_err("SetHandleInformation(%p) failed %s", h, strerr(r)); }
    }
    return r;
}

#else

int disinherit_handle(int fd) {
    return fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ? errno : 0;
}

// see: https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

#if defined(__STDC_NO_ATOMICS__)

void* atomics_read(volatile void **a) { uintptr_t r; __atomic_load((uintptr_t*)a, &r, __ATOMIC_SEQ_CST); return (void*)r; }

int32_t atomics_read32(volatile int32_t* a) { int32_t r; __atomic_load((int32_t*)a, &r, __ATOMIC_SEQ_CST); return r; }
int64_t atomics_read64(volatile int64_t* a) { int64_t r; __atomic_load((int64_t*)a, &r, __ATOMIC_SEQ_CST); return r; }

int32_t atomics_increment_int32(volatile int32_t *a) { return __atomic_add_fetch(a, 1, __ATOMIC_SEQ_CST); } // returns incremented value (__atomic_fetch_add returns previous)
int32_t atomics_decrement_int32(volatile int32_t *a) { return __atomic_sub_fetch(a, 1, __ATOMIC_SEQ_CST); } // returns decremented value

int64_t atomics_increment_int64(volatile int64_t *a) { return __atomic_add_fetch(a, 1LL, __ATOMIC_SEQ_CST); } // returns incremented value
int64_t atomics_decrement_int64(volatile int64_t *a) { return __atomic_sub_fetch(a, 1LL, __ATOMIC_SEQ_CST); } // returns decremented value

double atomics_exchange_double(volatile double *a, double v) { double r; __atomic_exchange(a, &v, &r, __ATOMIC_SEQ_CST); return r; }

int32_t atomics_exchange_int32(volatile int32_t *a, int32_t v) { return __atomic_exchange_n(a, v, __ATOMIC_SEQ_CST); }
int64_t atomics_exchange_int64(volatile int64_t *a, int64_t v) { return __atomic_exchange_n(a, v, __ATOMIC_SEQ_CST); }

void* atomics_exchange_ptr(volatile void **a, void *v) {
    int sv = sizeof(void*);
    int si = sizeof(int32_t);
    if (sv == si) {
        volatile int32_t* a32 = (volatile int32_t*)a;
        return ll2p(atomics_exchange_int32(a32, (intptr_t) v));
    } else {
        volatile int64_t* a64 = (volatile int64_t*)a;
        return ll2p(atomics_exchange_int64(a64, (intptr_t) v));
    }
}

bool atomics_compare_exchange_int64(volatile int64_t *a, int64_t comparand, int64_t v) { bool b = __atomic_compare_exchange(a, &comparand, &v, true, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE); return b; }

#else

void* atomics_read(volatile void** a) { return (void*)atomic_load((_Atomic(intptr_t)*)a); }

int32_t atomics_read32(volatile int32_t* a) { return atomic_load((_Atomic(int32_t)*)a); }
int64_t atomics_read64(volatile int64_t* a) { return atomic_load((_Atomic(int64_t)*)a); }

// NOTE: Microsoft`s InterlockedIncrement/Decrement returns already incremented/decremented value.
//       Code below keeps this contract:

int32_t atomics_increment_int32(volatile int32_t* a) { int32_t r = atomic_fetch_add((_Atomic(int32_t)*)a, 1); return r + 1; } // returns incremented value
int32_t atomics_decrement_int32(volatile int32_t* a) { int32_t r = atomic_fetch_sub((_Atomic(int32_t)*)a, 1); return r - 1; } // returns decremented value

int64_t atomics_increment_int64(volatile int64_t* a) { int64_t r = atomic_fetch_add((_Atomic(int64_t)*)a, 1); return r + 1; } // returns incremented value
int64_t atomics_decrement_int64(volatile int64_t* a) { int64_t r = atomic_fetch_sub((_Atomic(int64_t)*)a, 1); return r - 1; } // returns decremented value

int32_t atomics_exchange_int32(volatile int32_t* a, int32_t v) { return atomic_exchange((_Atomic(int32_t)*)a, v); }
int64_t atomics_exchange_int64(volatile int64_t* a, int64_t v) { return atomic_exchange((_Atomic(int64_t)*)a, v); }

void* atomics_exchange_ptr(volatile void **a, void *v) {
    int sv = sizeof(void*);
    int si = sizeof(int32_t);
    return sv == si ?
        (void*)(uintptr_t)atomics_exchange_int32((int32_t*)a, (int32_t)(intptr_t)v) :
        (void*)(uintptr_t)atomics_exchange_int64((int64_t*)a, (int64_t)(intptr_t)v);
}

bool atomics_compare_exchange_int32(volatile int32_t* a, int32_t comparand, int32_t v) {
    return atomic_compare_exchange_strong((_Atomic(int32_t)*)a, &comparand, v);
}

bool atomics_compare_exchange_int64(volatile int64_t* a, int64_t comparand, int64_t v) {
    return atomic_compare_exchange_strong((_Atomic(int64_t)*)a, &comparand, v);
}

#endif

#endif

#ifdef WINDOWS
#pragma warning(push)
#pragma warning(disable: 4702) // unreachable code because of exit(153)
#endif

void (*fatal_error_callback)(const char* filename, int line, const char* func, const char* format, va_list va);

void _fatal_error_(const char* filename, int line, const char* func, const char* format, ...) {
    va_list vl;
    va_start(vl, format);
    _fatal_error_va_(filename, line, func, format, vl);
    va_end(vl);
}

void _fatal_error_va_(const char* filename, int line, const char* func, const char* format, va_list vl) {
    char location[1024];
    snprintf0(location, countof(location), "%s(%d): %s", filename, line, func);
    char error[1024];
    vsnprintf(error, countof(error), format, vl);
    error[countof(error) - 1] = 0;
    char message[2048];
    snprintf0(message, countof(message), "%s %s", location, error);
    _trace_(null, 0, null, message);
    millisleep(100); // for UI applications if FATAL_ERROR is not on dispatch thread - helps to push messages to logger.c
    if (fatal_error_callback != null) {
        fatal_error_callback(filename, line, func, format, vl);
    }
    exit(153);
}

#ifdef WINDOWS
#pragma warning(pop)
#endif

#ifdef WINDOWS

void* mem_map(const char* filename, int* bytes, bool read_only) {
    void* address = null;
    *bytes = 0; // important for empty files - which result in (null, 0) and errno == 0
    errno = 0;
    DWORD access = read_only ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    // w/o FILE_SHARE_DELETE RAM-based files: FILE_FLAG_DELETE_ON_CLOSE | FILE_ATTRIBUTE_TEMPORARY won't open
    DWORD share  = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE file = CreateFileA(filename, access, share, null, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, null);
    if (file == INVALID_HANDLE_VALUE) {
        errno = GetLastError();
    } else {
        LARGE_INTEGER size = {{0, 0}};
        if (GetFileSizeEx(file, &size) && 0 < size.QuadPart && size.QuadPart <= 0x7FFFFFFF) {
            HANDLE map_file = CreateFileMapping(file, NULL, read_only ? PAGE_READONLY : PAGE_READWRITE, 0, (DWORD)size.QuadPart, null);
            if (map_file == null) {
                errno = GetLastError();
            } else {
                address = MapViewOfFile(map_file, read_only ? FILE_MAP_READ : FILE_MAP_READ|SECTION_MAP_WRITE, 0, 0, (int)size.QuadPart);
                if (address != null) {
                    *bytes = (int)size.QuadPart;
                } else {
                    errno = GetLastError();
                }
                int b = CloseHandle(map_file); // not setting errno because CloseHandle is expected to work here
                assert(b); (void)b;
            }
        } else {
            errno = GetLastError();
        }
        int b = CloseHandle(file); // not setting errno because CloseHandle is expected to work here
        assert(b); (void)b;
    }
    return address;
}

void mem_unmap(void* address, int bytes) {
    if (address != null) {
        int b = UnmapViewOfFile(address); (void)bytes; /* bytes unused, need by posix version */
        assert(b); (void)b;
    }
}

int mem_page_size() {
    static SYSTEM_INFO system_info;
    if (system_info.dwPageSize == 0) {
        GetSystemInfo(&system_info);
    }
    return (int)system_info.dwPageSize;
}

int mem_large_page_size() {
    static SIZE_T large_page_minimum;
    if (large_page_minimum == 0) { large_page_minimum = GetLargePageMinimum(); }
    return (int)large_page_minimum;
}

void* mem_alloc_pages(int bytes_multiple_of_page_size) {
    int page_size = mem_page_size();
    assert(bytes_multiple_of_page_size > 0 && bytes_multiple_of_page_size % page_size == 0);
    int r = 0;
    void* a = null;
    if (bytes_multiple_of_page_size < 0 && bytes_multiple_of_page_size % page_size != 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        r = EINVAL;
    } else {
        a = VirtualAlloc(null, bytes_multiple_of_page_size, MEM_COMMIT | MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE);
        if (a == null) { a = VirtualAlloc(null, bytes_multiple_of_page_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); }
        if (a == null) {
            r = GetLastError();
            if (r != 0) { rtrace("VirtualAlloc(%d) failed %s", bytes_multiple_of_page_size, strerr(r)); errno = r; }
        } else {
            r = VirtualLock(a, bytes_multiple_of_page_size) ? 0 : GetLastError();
            if (r == ERROR_WORKING_SET_QUOTA) {
                // The default size is 345 pages (for example, this is 1,413,120 bytes on systems with a 4K page size).
                SIZE_T min_mem = 0, max_mem = 0;
                r = GetProcessWorkingSetSize(GetCurrentProcess(), &min_mem, &max_mem) ? 0 : GetLastError();
                if (r != 0) {
                    rtrace("GetProcessWorkingSetSize() failed %s", strerr(r));
                } else {
                    max_mem =  max_mem + bytes_multiple_of_page_size * 2;
                    max_mem = (max_mem + page_size - 1) / page_size * page_size;
                    if (min_mem < max_mem) { min_mem = max_mem; }
                    r = SetProcessWorkingSetSize(GetCurrentProcess(), min_mem, max_mem) ? 0 : GetLastError();
                    if (r != 0) {
                        rtrace("SetProcessWorkingSetSize(%d, %d) failed %s", (int)min_mem, (int)max_mem, strerr(r));
                    } else {
                        r = VirtualLock(a, bytes_multiple_of_page_size) ? 0 : GetLastError();
                    }
                }
            }
            if (r != 0) {
                rtrace("VirtualLock(%d) failed %s", bytes_multiple_of_page_size, strerr(r));
                errno = r;
            }
        }
    }
    if (r != 0) {
        rtrace("mem_alloc_pages(%d) failed %s", bytes_multiple_of_page_size, strerr(r));
        assert(a == null);
        assert(errno == r);
        errno = r;
    }
    return a;
}

void mem_free_pages(void* a, int bytes_multiple_of_page_size) {
    int r = 0;
    if (a != null) {
        r = VirtualUnlock(a, bytes_multiple_of_page_size) ? 0 : GetLastError(); // in case it was successfully locked
        if (r != 0) {
            rtrace("VirtualUnlock() failed %s", strerr(r));
        }
    }
    // If the "dwFreeType" parameter is MEM_RELEASE, "dwSize" parameter must be the base address
    // returned by the VirtualAlloc function when the region of pages is reserved.
    r = VirtualFree(a, 0, MEM_RELEASE) ? 0 : GetLastError();
    if (r != 0) { rtrace("VirtuaFree() failed %s", strerr(r)); }
    (void)bytes_multiple_of_page_size; // unused
}

int mem_protect(void* a, int bytes_multiple_of_page_size, int protect) {
    int page_size = mem_page_size();
    assert(bytes_multiple_of_page_size > 0 && bytes_multiple_of_page_size % page_size == 0);
    int r = 0;
    if (bytes_multiple_of_page_size < 0 && bytes_multiple_of_page_size % page_size != 0) {
        r = EINVAL;
    } else {
        DWORD was = 0;
        assert(PAGE_NOACCESS          == MEM_PROTECT_NOACCESS);
        assert(PAGE_READONLY          == MEM_PROTECT_READONLY);
        assert(PAGE_READWRITE         == MEM_PROTECT_READWRITE);
        assert(PAGE_EXECUTE           == MEM_PROTECT_EXECUTE);
        assert(PAGE_EXECUTE_READ      == MEM_PROTECT_EXECUTE_READ);
        assert(PAGE_EXECUTE_READWRITE == MEM_PROTECT_EXECUTE_READWRITE);
        r = VirtualProtect(a, bytes_multiple_of_page_size, (DWORD)protect, &was);
    }
    return r;
}

#else

void* mem_map(const char* filename, int* bytes, bool read_only) {
    void* address = null;
    int fd = open(filename, read_only ? O_RDONLY : O_RDWR);
    if (fd >= 0) {
        int length = (int)lseek(fd, 0, SEEK_END);
        if (0 < length && length <= 0x7FFFFFFF) {
            address = mmap(0, length, read_only ? PROT_READ : PROT_READ|PROT_WRITE, read_only ? MAP_PRIVATE : MAP_SHARED, fd, 0);
            if (address != null) {
                *bytes = (int)length;
            }
        }
        close(fd);
    }
    return address;
}

void mem_unmap(void* address, int bytes) {
    if (address != null) {
        munmap(address, bytes);
    }
}

int mem_page_size() {
    static int page_size;
    if (page_size == 0) { page_size = sysconf(_SC_PAGE_SIZE); }
    return page_size;
}

int mem_large_page_size() {
    return mem_page_size(); // for now
}

void* mem_alloc_pages(int bytes_multiple_of_page_size) {
    int page_size = mem_page_size();
    int r = 0;
    assert(bytes_multiple_of_page_size > 0 && bytes_multiple_of_page_size % page_size == 0);
    void* a = null;
    if (bytes_multiple_of_page_size < 0 && bytes_multiple_of_page_size % page_size != 0) {
        r = EINVAL;
    } else {
        a = mmap(null, bytes_multiple_of_page_size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_LOCKED, -1, 0); // MAP_LOCKED
        if (a == (void*)-1) {
            r = errno;
            rtrace("mmap() returned %p failed %s", a, strerr(r));
            a = null;
        } else if (a == null) {
            r = errno;
            rtrace("mmap() failed %s", strerr(r));
        }
    }
    if (r != 0) { errno = r; }
    return a;
}

void mem_free_pages(void* a, int bytes_multiple_of_page_size) {
    if (a != null) {
        int r = munmap(a, bytes_multiple_of_page_size) == 0 ? 0 : errno;
        if (r != 0) { rtrace("munmap() failed %s", strerr(r)); }
    }
}

int mem_protect(void* a, int bytes_multiple_of_page_size, int protect) {
    int page_size = mem_page_size();
    int r = 0;
    assert(bytes_multiple_of_page_size > 0 && bytes_multiple_of_page_size % page_size == 0);
    if (bytes_multiple_of_page_size < 0 && bytes_multiple_of_page_size % page_size != 0) {
        r = EINVAL;
    } else {
        int prot = 0;
        if      (protect == MEM_PROTECT_NOACCESS)  { prot = PROT_NONE; }
        else if (protect == MEM_PROTECT_READONLY)  { prot = PROT_READ; }
        else if (protect == MEM_PROTECT_READWRITE) { prot = PROT_READ | PROT_WRITE; }
        else if (protect == MEM_PROTECT_EXECUTE)   { prot = PROT_EXEC; }
        else if (protect == MEM_PROTECT_EXECUTE_READ) { prot = PROT_READ | PROT_EXEC; }
        else if (protect == MEM_PROTECT_EXECUTE_READWRITE) { prot = PROT_READ | PROT_WRITE | PROT_EXEC; }
        else { r = EINVAL; }
        if (r == 0)  { r = mprotect(a, bytes_multiple_of_page_size, prot); }
    }
    return r;
}

#endif

#ifndef WINDOWS

#include <sys/prctl.h>
#include <sys/syscall.h>

int scheduler_set_timer_resolution(int64_t nanoseconds) {
    return 0; // ignore for now on all Linuxes
}

int64_t scheduler_get_timer_resolution() {
    return NANOSECONDS_IN_MILLISECOND; // empirical knowledge - most of Linux kernels in 2018 are at 1 millisecond time sharing
}

int pthread_set_name_np(pthread_t thread, const char* name) {
    char s[16];
    strncpy0(s, name, sizeof(s));
    // http://man7.org/linux/man-pages/man3/pthread_setname_np.3.html
    if (thread == pthread_null) { thread = pthread_self(); }
    int r = pthread_setname_np(thread, s);
    if (r != 0) {
        assert(thread == pthread_null || thread == pthread_self());
        r = prctl(PR_SET_NAME, (uintptr_t)s, 0, 0, 0);
        if (r != 0) {
            trace("name=%s failed %s", s, strerr(r)); (void)r;
        }
    }
    return r;
}

int pthread_get_name_np(pthread_t thread, char* name, int count) {
    char s[16] = {0};
    // http://man7.org/linux/man-pages/man3/pthread_setname_np.3.html
    if (thread == pthread_null) { thread = pthread_self(); }
    assert(thread == pthread_null || thread == pthread_self());
    int r = prctl(PR_GET_NAME, (uintptr_t)s, 0, 0, 0);
    if (r != 0) {
        trace("name=%s failed %s", s, strerr(r)); (void)r;
    }
    snprintf0(name, count, "%s", s);
    return r;
}

uint64_t get_cpu_mask() {
    return (1ULL << android_getCpuCount()) - 1;
}

uint64_t get_fast_cpu_mask() {
    if (android_getCpuCount() == 8) { // Odroid XU3 HMP 0..3 Cortex A7 1.4GHz 4..7 Cortex A15 2.0GHz
        return ((1ULL << android_getCpuCount()) - 1) & ~(0xF);
    }
    return (1ULL << android_getCpuCount()) - 1;
}


void set_affinity_and_inc(volatile int* core_id) {
    uint64_t mask = get_cpu_mask();
    int n = number_of_bits64(mask);
    if (n > 1) {
        int next = __sync_fetch_and_add(core_id, 1) % n;
        mask = (1ULL << next);
        int r = pthread_setaffinity_mask_np(pthread_self(), mask);
        posix_info(r);
        if (r == 0) {
            uint64_t now = 0;
            r = pthread_getaffinity_mask_np(pthread_self(), &now);
            posix_info(r);
            if (r == 0 && now != mask) {
                char buf[128];
                trace("thread %d affinity: %s (MSBF)", pthread_self(), i2b(mask, buf, false));
            }
        }
    }
}

int get_number_of_hardware_cores() { return number_of_bits64(get_cpu_mask()); }

uint64_t get_core_affinity_mask(int core_number) { return (uint64_t)1LL << core_number; }

// see: http://man7.org/linux/man-pages/man3/pthread_getschedparam.3.html
// and  http://man7.org/linux/man-pages/man7/sched.7.html

// at the time of implementation SCHED_RR is highest available (exposed) policy on Android kernels
// for our Real Time purposes with possible exception of "3D poses" processing (we can address it later)
// SCHED_FIFO is better then SCHED_RR and SCHED_DEADLINE. We want all realtime threads to run
// till they are done with whatever they want to do and not to be preempted.

int pthread_getschedpolicy_np(pthread_t thread) {
    struct sched_param sp = {};
    int policy = SCHED_NORMAL;
    int r = pthread_getschedparam(thread, &policy, &sp);
    posix_ok(r);
    return policy;
}

int pthread_setschedprio_np(pthread_t thread, int prio) {
    struct sched_param sp = {};
    sp.sched_priority = prio;
    int policy = prio == pthread_get_priority_normal_np() ? SCHED_NORMAL : SCHED_FIFO;
    int r = pthread_setschedparam(thread, policy, &sp);
//  posix_info(r); // annoying in Android zigota apps - does not and will not work only works in chmod 6777 command_line_executables
    return r;
}

int pthread_getschedprio_np(pthread_t thread) {
    struct sched_param sp = {};
    int policy = SCHED_NORMAL;
    pthread_getschedparam(thread, &policy, &sp);
    return sp.sched_priority;
}

int pthread_get_priority_max_np() {
    return sched_get_priority_max(SCHED_FIFO); // 99
}
int pthread_get_priority_min_np() {
    return sched_get_priority_min(SCHED_FIFO); // 1
}

int pthread_get_priority_realtime_np() {
    return pthread_get_priority_max_np(); // 99
}

int pthread_get_priority_normal_np() {
    return 0;
}

int pthread_setaffinity_mask_np(pid_t pid, uint64_t cpuset) {
    return syscall(__NR_sched_setaffinity, pid, sizeof(uint64_t), &cpuset);
}

int pthread_getaffinity_mask_np(pid_t pid, uint64_t *cpuset) {
    int r = syscall(__NR_sched_getaffinity, pid, sizeof(uint64_t), cpuset);
    return r > 0 && errno == 0 ? 0 : r; // seems to be a bug in __NR_sched_getaffinity returning 4 and errno == 0
}

int pthread_cond_timed_wait_np(pthread_cond_t* cond, mutex_t* mutex, double timeout_in_milliseconds) {
    int r = 0;
    if (timeout_in_milliseconds < 0) { assertion(timeout_in_milliseconds == -1, "only -1 is allowed %g", timeout_in_milliseconds); timeout_in_milliseconds = -1; }
    if (timeout_in_milliseconds >= (double)INT_MAX) { // ~ 24 days
        assertion(timeout_in_milliseconds <= INT_MAX, "timeout_in_milliseconds=%g > maximum", timeout_in_milliseconds);
        timeout_in_milliseconds = INT_MAX;
    }
    if (timeout_in_milliseconds >= 0) {
        struct timespec abstime;
        r = clock_gettime(CLOCK_REALTIME, &abstime);
        if (r == 0) {
            uint64_t tons = (uint64_t)(timeout_in_milliseconds * NANOSECONDS_IN_MILLISECOND);
            uint64_t nsec = (((uint64_t)abstime.tv_sec) << 32) | abstime.tv_nsec;
            if (nsec >= ULLONG_MAX - tons) {
                r = E2BIG;
            } else {
                nsec += tons;
                abstime.tv_sec  = nsec / NANOSECONDS_IN_SECOND;
                abstime.tv_nsec = nsec % NANOSECONDS_IN_SECOND;
                r = pthread_cond_timedwait(cond, mutex, &abstime);
            }
        }
    } else {
        r = pthread_cond_wait(cond, mutex);
    }
    return r;
}

int is_debugger_present() {
    static int debugger_present = -1;
    if (debugger_present == -1) {
        char buffer[16 * 1024];
        int fd = open("/proc/self/status", O_RDONLY);
        if (fd == -1) {
            debugger_present = 0;
        } else {
            ssize_t n = read(fd, buffer, sizeof(buffer));
            if (0 < n && n < sizeof(buffer)) {
                buffer[n] = 0;
                static const char* tp = "TracerPid:";
                const char* pid = strstr(buffer, tp);
                if (pid != null) {
                    pid += strlen(tp);
                    while (*pid == ' ' || *pid == '\t') { pid++; }
                    debugger_present = atoi(pid) > 0;
                }
            }
        }
    }
    return debugger_present;
}


#endif

END_C
