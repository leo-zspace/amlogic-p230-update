#pragma once
#include "runtime.h"

#define countof(a) ((int)(sizeof(a) / sizeof((a)[0])))

#ifdef WINDOWS
#if !defined(STRICT)
#define STRICT
#endif
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#ifdef _MSC_VER
#pragma warning(disable: 4710) // function not inlined
#pragma warning(disable: 4820) // '...' bytes padding added after data member
#pragma warning(disable: 4668) // '...' is not defined as a preprocessor macro, replacing with '0' for '#if/#elif'
#pragma warning(disable: 4917) // '...' : a GUID can only be associated with a class, interface or namespace
#pragma warning(disable: 4987) // nonstandard extension used: 'throw (...)'
#pragma warning(disable: 4365) // argument : conversion from 'int' to 'size_t', signed/unsigned mismatch
#pragma warning(disable: 4714) // function  marked as __forceinline not inlined (notoriously HRESULT_FROM_WIN32)
#pragma warning(disable: 5045) // Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
#pragma warning(disable: 5039) // pointer or reference to potentially throwing function passed to extern C function under -EHc
#pragma warning(disable: 4625) // copy constructor was implicitly defined as deleted
#pragma warning(disable: 4626) // assignment operator was implicitly defined as deleted
#pragma warning(disable: 5027) // move assignment operator was implicitly defined as deleted
#pragma warning(disable: 4061) // enumerator in switch of enum is not explicitly handled by a case label
#pragma warning(disable: 4574) // '_HAS_ITERATOR_DEBUGGING' is defined to be '0': did you mean to use '#if _HAS_ITERATOR_DEBUGGING'?
#pragma warning(disable: 4571) // catch(...) semantics changed since Visual C++ 7.1; structured exceptions (SEH) are no longer caught
#pragma warning(disable: 4774) // format string expected in argument 3 is not a string literal (in <xlocnum> Microsoft header)
#pragma warning(disable: 4266) // no override available for virtual member function from base 'DrawableObject2D'; function is hidden (compiling source file ..\..\src\glwnd\GLWnd.cpp)
#pragma warning(disable: 4263) // member function does not override any base class virtual member function (compiling source file ..\..\src\glwnd\GLWnd.cpp)
#pragma warning(disable: 4264) // no override available for virtual member function from base 'DrawableObjectBase'; function is hidden (compiling source file ..\..\src\glwnd\GLWnd.cpp)
#pragma warning(error:   4706) // assignment in conditional expression (this is the only way I found to turn it on)
#endif
#ifdef DEBUG
#ifndef _CRTDBG_MAP_ALLOC
#define _CRTDBG_MAP_ALLOC
#endif
#include <crtdbg.h>
#endif
#include <Windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <comdef.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <direct.h>
#include <process.h>
#endif // WINDOWS

#ifdef WINDOWS
#include "pthreads.h"
#endif
#include "nanotime.h"
#include "debug.h"

BEGIN_C

// This is posix version of if_error_return macro. In most cases != 0 (positive or negative) means error. Be careful with reading functions where only < 0 means error
#define if_error_return(r) do { int _r_ = (r); if (_r_ != 0) { log_err("%s failed %s", #r, strerr(_r_)); errno = _r_; return _r_; } } while (0)

typedef struct text256_s { char text[256]; } text256_t;
#define strerr(e) (strerr_((e)).text) // e is errno or HRESULT

#ifdef WINDOWS

#ifdef _MSC_VER
#pragma warning(disable: 4710) // function not inlined
#pragma warning(disable: 4711) // function selected for automatic inline expansion
#pragma warning(disable: 4100) // unreferenced formal param
//#pragma warning(disable: 4800) // 'BOOL' : forcing value to bool 'true' or 'false'
#pragma warning(disable: 4996) // This function or variable may be unsafe. Consider using *_s instead.
#pragma warning(disable: 6262) // excessive stack usage
#pragma warning(disable: 6263) // using alloca in a loop
#pragma warning(disable: 6255) //_alloca indicates failure by raising a stack overflow exception
// TODO: (Leo) you may reconsider and enable and fix these with /Wall `warning all' compiler option
#pragma warning(disable: 4365) // argument : conversion from 'int' to 'size_t', signed/unsigned mismatch
#pragma warning(disable: 4826) // Conversion from 'void *' to 'DWORD64' is sign-extended. This may cause unexpected runtime behavior.
#pragma warning(disable: 4505) // '...' : unreferenced local function has been removed
#pragma warning(disable: 4514) // unreferenced inline function has been removed
#pragma warning(disable: 4820) // bytes padding added after data member
#endif

#define finite(x) _finite(x)
// This is Windows version of if_failed_return_result HRESULT < 0 macros:
#define if_failed_return_result(r) do { HRESULT _hr_ = (r); if (FAILED(_hr_)) { trace("%s failed %s", #r, strerr(_hr_)); errno = _hr_; return _hr_; } } while (0)
#define if_failed_return(r, e) do { HRESULT _hr_ = (r); if (FAILED(_hr_)) { trace("%s failed %s", #r, strerr(_hr_)); errno = _hr_; return e; } } while (0)

#define breakpoint() do { if (IsDebuggerPresent()) { DebugBreak(); } } while (0)
#define set_errno(e) { errno = e; SetLastError(e); }
#define E_NOTFOUND HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
int measure_text(canvas_t c, font_t f, int *w, int *h, const char* format, ...);
int draw_text(canvas_t c, font_t f, int x, int y, const char* format, ...);
int draw_colored_text(canvas_t c, font_t f, COLORREF color, int x, int y, const char* format, ...);
TCHAR* _wcs2tstr_(TCHAR* s, const wchar_t* wcs);
#define vsnprintf(buf, count, fmt, vl) vsnprintf_s(buf, count, _TRUNCATE, fmt, vl)
#define snprintf(buf, count, fmt, ...) _snprintf_s(buf, count, _TRUNCATE, fmt, __VA_ARGS__)
#define gettid() ((int)GetCurrentThreadId())
#define mkdir(path, mode) _mkdir(path)
#define access(file, flags) _access(file, flags)
#define thread_local_storage __declspec(thread)
TCHAR* _wcs2tstr_(TCHAR* s, const wchar_t* wcs);
#define mem_alloc_aligned(bytes, a) _aligned_malloc(bytes, a)
#define mem_free_aligned(p) { if (p != null) { _aligned_free(p); } }
#define mem_strdup(s) strncpy0((char*)mem_alloc((int)strlen((s)) + 1), s, (int)strlen((s)) + 1)
#define mem_dup(a, bytes) memcpy_(mem_alloc((bytes)), (a), (bytes))
#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100

#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010

#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001
#define stricmp(s1, s2) _stricmp(s1, s2)
#else
#define null NULL
typedef uint8_t byte;
typedef int socket_t;
enum { INVALID_SOCKET = -1 };
enum { SOCKET_ERROR = -1 };
#define closesocket(s) close(s) // see: http://stackoverflow.com/questions/35441815/are-close-and-closesocket-interchangable
#ifdef __MACH__
inline_c static int gettid() { uint64_t tid; pthread_threadid_np(NULL, &tid); return (pid_t)tid; }
#endif
#if defined(__linux__) && !defined(ANDROID)
#define gettid() syscall(SYS_gettid)
#endif

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#define set_errno(e) { errno = (e); }
#define E_OUTOFMEMORY ENOMEM
#define E_NOTIMPL     ENOSYS
#define E_NOTFOUND    ENOENT
#define E_NOT_SET     EINVAL
// see: http://lxr.free-electrons.com/source/include/uapi/asm-generic/errno.h
// and  http://lxr.free-electrons.com/source/include/uapi/asm-generic/errno-base.h
typedef pthread_mutex_t mutex_t;
#define pthread_null 0 // pthread is "int" on *nix and void* on Windows
#define mutex_init pthread_mutex_init
#define mutex_lock pthread_mutex_lock
#define mutex_trylock(m) (pthread_mutex_trylock(m) == 0)
#define mutex_unlock pthread_mutex_unlock
#define mutex_destroy pthread_mutex_destroy
#define if_failed_return_result(r) { int _r_ = (r); if (_r_ != 0) { log_err("%s failed %s", #r, strerr(_r_)); errno = _r_; return _r_; } }
#define if_failed_return(r, e) { int _r_ = (r); if (_r_ != 0) { log_err("%s failed %s", #r, _r_, strerr(_r_)); errno = _r_; return e; } }
#define breakpoint() raise(SIGTRAP)
// Known implementation limitations for Linux and other *nix:
// the first argument of pthread_set_name_np must be pthread_self()
// name must be 16 or less characters
int pthread_set_name_np(pthread_t thread, const char *name_16_characters_or_less);
int pthread_get_name_np(pthread_t thread, char* name, int count);
// int pthread_mutex_timedlock(pthread_mutex_t *mutex, struct timespec *timeout);
int pthread_setaffinity_mask_np(pid_t pid, uint64_t  cpu_mask);
int pthread_getaffinity_mask_np(pid_t pid, uint64_t *cpu_mask);
int pthread_cond_timed_wait_np(pthread_cond_t* cond, mutex_t* mutex, double timeout_in_milliseconds); // ~24 days INT_MAX milliseconds maximum

#define stricmp(s1, s2) strcasecmp((s1), (s2)) // see: https://linux.die.net/man/3/strcasecmp
#endif

text256_t strerr_(int e); // e is errno, HRESULT or WinError

typedef struct debug_output_hook_s {
    void * that;
    /* kind = 0 print, 1 = debug, 2 = release */
    void (*output)(void* that, int kind, const char* text);
} debug_output_hook_t;

extern volatile debug_output_hook_t* debug_output_hook;


static inline_c uint32_t swap_bytes(uint32_t v) {
    return ((v >>  0) & 0xFF) << 24 | ((v >>  8) & 0xFF) << 16 | ((v >> 16) & 0xFF) <<  8 | ((v >> 24) & 0xFF) <<  0;
}

static inline_c int indexof(const int* a, int n, int v) {
    for (int i = 0; i < n; i++) { if (a[i] == v) { return i; } }
    return -1;
}

static inline_c void* memsetz(void* p, int v, int bytes) {
    return p != null ? memset(p, v, bytes) : p;
}

static inline_c void* memcpy_(void* d, void* s, int bytes) {
    return d != null ? memcpy(d, s, bytes) : d;
}

int _utf8_for_wcs_(const wchar_t* wcs);
int _wcs_for_utf8_(const char* s);
char* strtolower(char* str);
char* strtoupper(char* str);
char* _wcs2str_(char* s, const wchar_t* wcs);
wchar_t* _str2wcs_(wchar_t* wcs, const char* s);

int number_of_bits32(const int32_t bitset);
int number_of_bits64(const int64_t bitset);
int number_of_leading_zeros(const int64_t  x);
int chmod_world_777(const char* filename);
const char* i2b(uint64_t v, char* buf /* must be at least 66 */, bool leading_zeros);

#define mem_allocz(bytes) memsetz(mem_alloc(bytes), 0, bytes)
#define mem_allocz_aligned(bytes, a) memsetz(mem_alloc_aligned(bytes, a), 0, bytes)

#define stack_alloc(bytes) alloca(bytes)
#define stack_allocz(bytes)  memsetz(alloca(bytes), 0, (bytes))
#define concat(a, b) strcat(strcpy((char*)stack_alloc(strlen(a) + strlen(b) + 1), a), b)
#define wcs2str(wcs) _wcs2str_((char*)stack_alloc(_utf8_for_wcs_(wcs) + 1), wcs)
#define str2wcs(s)   _str2wcs_((WCHAR*)stack_alloc((_wcs_for_utf8_(s) + 1) * sizeof(WCHAR)), s)
#define wcs2tstr(wcs) _wcs2tstr_((TCHAR*)stack_alloc((wcslen(wcs) + 1) * sizeof(TCHAR)), wcs)
#define strequ(s1, s2)  (((void*)(s1) == (void*)(s2)) || (((void*)(s1) != null && (void*)(s2) != null) && strcmp((s1), (s2)) == 0))
#define striequ(s1, s2)  (((void*)(s1) == (void*)(s2)) || (((void*)(s1) != null && (void*)(s2) != null) && stricmp((s1), (s2)) == 0))

//#define TRACE_HEAP
//#define MEM_ALLOC_CHECK

#ifndef TRACE_HEAP

#ifdef MEM_ALLOC_CHECK

inline_c void* _mem_alloc_check(int bytes) {
    if (bytes == sizeof(void*)) { trace("WARNING: mem_alloc(%d) check sizeof() correcteness", bytes); print_stack_trace(); }
    void* p = malloc(bytes);
    return p;
}

#define mem_alloc(bytes) _mem_alloc_check(bytes)

#else

#define mem_alloc(bytes) malloc(bytes)

#endif

#define mem_realloc(p, bytes) realloc(p, bytes)
#define mem_usable_size(p) _msize(p) /* malloc_usable_size(p) on Un*x */
#define mem_free(p) free(p)

#else // TRACE_HEAP

#define mem_alloc(bytes) _mem_alloc_(bytes)
#define mem_realloc(ptr, bytes) _mem_realloc_(ptr, bytes)
#define mem_free(p) _mem_free_(p)

inline_c void* _mem_alloc_(int bytes) {
    void* p = malloc(bytes);
    trace("0x%p [%d]", p, bytes);
    print_stack_trace();
    return p;
}

inline_c void* _mem_realloc_(void* ptr, int bytes) {
    void* p = realloc(ptr, bytes);
    trace("0x%p [%d]", p, bytes);
    print_stack_trace();
    return p;
}

inline_c void _mem_free_(void* p) {
    trace("0x%p", p);
    print_stack_trace();
    free(p);
}

#endif // TRACE_HEAP


int mem_page_size();        // 4KB or 64KB on Windows
int mem_large_page_size();  // 2MB on Windows

/* Attempts (if possible) to allocate contigous committed physical memory
   Memory guaranteed to be alingned to page boundary.
   Memory is guaranteed to be initialized to zero.
*/

void* mem_alloc_pages(int bytes_multiple_of_page_size);
void  mem_free_pages(void* a, int bytes_multiple_of_page_size);

enum { // subset of matching https://docs.microsoft.com/en-us/windows/win32/memory/memory-protection-constants
    MEM_PROTECT_NOACCESS          = 0x01,
    MEM_PROTECT_READONLY          = 0x02,
    MEM_PROTECT_READWRITE         = 0x04,
    MEM_PROTECT_EXECUTE           = 0x10,
    MEM_PROTECT_EXECUTE_READ      = 0x20,
    MEM_PROTECT_EXECUTE_READWRITE = 0x40
};

int mem_protect(void* a, int bytes_multiple_of_page_size, int protect);

/* Attempts to map entire content of specified existing
 non empty file not larger then 0x7FFFFFFF bytes
 into memory address in readonly mode.
 On success returns the address of mapped file
 on failure returns 0 (zero, aka NULL pointer).
 If 'bytes' pointer is not 0 it receives the number
 of mapped bytes equal to original file size. */
void* mem_map(const char* filename, int* bytes, bool read_only);

/* Unmaps previously mapped file */
void mem_unmap(void* address, int bytes);

int mkdirs(const char* path); // create all folders in the path if needed with 0777 permissions

bool am_i_root();

int scheduler_set_timer_resolution(int64_t nanoseconds); // Windows only, not supported and silently ignored on Linux
int64_t scheduler_get_timer_resolution(); // -1 if failed otherwise schduler timeslice in nanoseconds.

int pthread_getschedpolicy_np(pthread_t thread); // returns SCHED_NORMAL or SCHED_FIFO or SCHED_RR

int pthread_setschedprio_np(pthread_t thread, int prio);
int pthread_getschedprio_np(pthread_t thread);

int pthread_get_priority_max_np(); // sched_get_priority_max
int pthread_get_priority_min_np();
int pthread_get_priority_realtime_np();
int pthread_get_priority_normal_np();

// when/if android ever implements pthread_mutex_timedlock remove this:

pthread_t pthread_start_np(void (*thread_proc)(void*), void* that);

typedef void (*pthread_tor_t)(void* that, bool start); // ctor (constructor) dtor (destructor) monitor

// called at the very entry exit point of every thread created with pthread_start_np
// may not be supported on Windows (noop) yet (check implementation):
void pthread_add_on_tor(pthread_tor_t tor, void* that); // not thread safe(!)
void pthread_remove_on_tor(pthread_tor_t tor); // not thread safe(!)

uint64_t get_cpu_mask();
uint64_t get_fast_cpu_mask(); // for bigLITTLE cores like on Samsung Exynos big.little

void set_affinity_and_inc(volatile int* core_id);

int get_number_of_hardware_cores();
uint64_t get_core_affinity_mask(int core_number);

int is_debugger_present();

#define FATAL_ERROR(format, ...) _fatal_error_(__FILE__, __LINE__, __FUNCTION__, format, ##__VA_ARGS__);
void _fatal_error_(const char* filename, int line, const char* func, const char* format, ...);
void _fatal_error_va_(const char* filename, int line, const char* func, const char* format, va_list va);
extern void (*fatal_error_callback)(const char* filename, int line, const char* func, const char* format, va_list va);

char* strncat0(char* d, const char* s, int n); // always zero terminates destination
char* strncpy0(char* d, const char* s, int n); // always zero terminates destination
// both functions below always guarantee character zero (0) as the last written to "d" (unless n == 0)
// and they also guarantee that return value is NOT negative and either less than "n"
// (all characters written) or >= n when the output was truncated.
// The result value may differ in Linux posix and various versions of Windows runtime.
// The only guarantee is >= n when overflow - do not rely on return value as space needed.
int snprintf0(char* d, int n, const char* format, ...); // always zero terminates destination and returns its character count length.
int vsnprintf0(char* d, int n, const char* format, va_list va); // always zero terminates destination and returns its character count length.

#define if_failed_return_false(result) if_failed_return(result, false);
#define if_failed_return_null(result) if_failed_return(result, null);
#define if_null_return(exp, res) { if ((exp) == null) { return res; } }
#define if_null_return_false(exp) if_null_return(exp, false);
#define if_false_return(exp, res) { if (!(exp)) { return res; } }
#define if_false_return_false(exp) if_false_return(exp, false);
#define if_false_return_null(exp) if_false_return(exp, null);

#if defined(DEBUG) || defined(HYBRID) /* posix_ok(r) asserts that posix result is 0 aka OK */
#define posix_ok(r)   { int _r = (r); int e = errno; assertion(_r == 0, "%s r=%s errno=%s", #r, strerr(_r), strerr(e)); if (_r != 0) { exit(_r); }}
#define posix_info(r) { int _r = (r); int e = errno; if (_r != 0) { log_info("%s r=%s errno=%s", #r, strerr(_r), strerr(e)); } }
#else
#define posix_ok(r)   { int _r = (r); if (_r != 0) { exit(_r); } }
#define posix_info(r) { (void)(r); }
#endif

#ifdef WINDOWS
inline_c static int64_t p2ll(void* p) { return (int64_t)(uintptr_t)p; }
inline_c static void* ll2p(int64_t ll) { return (void*)ll; }
#else
/* warning free inline conversions for pointers to long long and back */
#pragma GCC diagnostic push "-Wpointer-to-int-cast"
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
inline_c static int64_t p2ll(void* p) { return (int64_t)(uintptr_t)p; }
#pragma GCC diagnostic pop "-Wpointer-to-int-cast"

#pragma GCC diagnostic push "-Wint-to-pointer-cast"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
inline_c static void* ll2p(int64_t ll) { return (void*)ll; }
#pragma GCC diagnostic pop "-Wint-to-pointer-cast"
#endif

void* atomics_read(volatile void** a);
int32_t atomics_read32(volatile int32_t *a);
#if defined(_WIN64) || !defined(WINDOWS)
int64_t atomics_read64(volatile int64_t *a); // simply unavailable in 32 bit windows
#endif
void* atomics_exchange_ptr(volatile void** a, void* v);
int32_t atomics_increment_int32(volatile int32_t* a); // returns incremented value
int32_t atomics_decrement_int32(volatile int32_t* a); // returns decremented value
int64_t atomics_increment_int64(volatile int64_t* a); // returns incremented value
int64_t atomics_decrement_int64(volatile int64_t* a); // returns decremented value
int32_t atomics_exchange_int32(volatile int32_t* a, int32_t v);
int64_t atomics_exchange_int64(volatile int64_t* a, int64_t v);
bool atomics_compare_exchange_int64(volatile int64_t* a, int64_t comparand, int64_t v);
bool atomics_compare_exchange_int32(volatile int32_t* a, int32_t comparand, int32_t v);

extern void* _force_symbol_reference_(void* symbol);

/* static_init(unique_name) { ... } */

/* MSVC global optimization section ".CRT$XCU" story here:
https://connect.microsoft.com/VisualStudio/Feedback/Details/1587892
https://stackoverflow.com/questions/36841629/visual-c-2015-gl-whole-program-optimization-and-optref-optimize-referenc
https://bugzilla.gnome.org/show_bug.cgi?id=752837
https://github.com/GNOME/glib/blob/master/glib/gconstructor.h
*/

typedef struct process_global_data_s {
    void* logger;
    void* system_ini;
} process_global_data_t;


process_global_data_t* open_process_global_data(bool* owns); // owns set to true for the first caller, false all consecutive callers

// Windows only APIs

#ifdef WINDOWS

int reg_open_or_create_key(HKEY* key, HKEY root, const char* path);

int reg_write_dword(HKEY root, const char* path, const char* name, int value);

void disable_dll_loading_thread_pool();

int disinherit_handle(HANDLE h);

#else

int disinherit_handle(int fd); // fcntl(fd, F_SETFD, FD_CLOEXEC)

#endif

#ifdef _MSC_VER

#if defined(_WIN64) || defined(_M_X64)
#define MSVC_SYMBOL_PREFIX ""
#else
#define MSVC_SYMBOL_PREFIX "_"
#endif
#ifdef __cplusplus
#define MSVC_EXTERN_C extern "C"
#else
#define MSVC_EXTERN_C extern
#endif
#define MSVC_CTOR(_sym_prefix, func) \
  MSVC_EXTERN_C void func(void); \
  MSVC_EXTERN_C int (* _array ## func)(void); \
  MSVC_EXTERN_C int func ## _wrapper(void); \
  MSVC_EXTERN_C int func ## _wrapper(void) { func(); _force_symbol_reference_((void*)_array ## func); _force_symbol_reference_((void*)func ## _wrapper); return 0; } \
  __pragma(comment(linker, "/include:" _sym_prefix # func "_wrapper")) \
  __pragma(section(".CRT$XCU", read)) \
  __declspec(allocate(".CRT$XCU")) int (* _array ## func)(void) = func ## _wrapper;

#define static_init(func) MSVC_CTOR(MSVC_SYMBOL_PREFIX, func ## _constructor) MSVC_EXTERN_C void func ## _constructor(void)

#else
#define static_init(n) __attribute__((constructor)) static void _init_ ## n ##_ctor(void)
#endif

/*
    same as in Java:
        static { System.out.println("called before main()\n"); }
    sample usage: order is not guaranteed
        static void after_main1(void) { printf("called after main() 1\n"); }
        static void after_main2(void) { printf("called after main() 2\n"); }
        static_init(main_init1)       { printf("called before main 1\n"); atexit(after_main1); }
        static_init(main_init2)       { printf("called before main 2\n"); atexit(after_main2); }
        int main() { printf("main()\n"); }
*/

END_C
