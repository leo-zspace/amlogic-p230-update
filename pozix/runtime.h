#pragma once

#if defined(WIN32) && !defined(WINDOWS)
#define WINDOWS
#endif
#if defined(_DEBUG) && !defined(DEBUG)
#define DEBUG _DEBUG
#endif
#if defined(DEBUG) && !defined(_DEBUG)
#define _DEBUG DEBUG
#endif

#ifdef WINDOWS
#if !defined(STRICT)
#define STRICT
#endif
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN

#ifdef _MSC_VER
#pragma warning(disable: 4268) // 'const' static / global data initialized with compiler generated default constructor fills the object with zeros
#pragma warning(disable: 4514) // unreferenced inline function has been removed
#pragma warning(disable: 4710) // function not inlined
#pragma warning(disable: 4711) // function selected for automatic inline expansion
#pragma warning(disable: 4820) // '...' bytes padding added after data member
#pragma warning(disable: 4668) // '...' is not defined as a preprocessor macro, replacing with '0' for '#if/#elif'
#pragma warning(disable: 4917) // '...' : a GUID can only be associated with a class, interface or namespace
#pragma warning(disable: 4987) // nonstandard extension used: 'throw (...)'
#pragma warning(disable: 4365) // argument : conversion from 'int' to 'size_t', signed/unsigned mismatch
#pragma warning(error:   4706) // assignment in conditional expression (this is the only way I found to turn it on)
#pragma warning(disable: 6262) // excessive stack usage
#pragma warning(disable: 6263) // using alloca in a loop
#pragma warning(disable: 6255) //_alloca indicates failure by raising a stack overflow exception
#pragma warning(disable: 4200) // nonstandard extension used: zero-sized array in struct/union
#endif

#define _CRT_RAND_S 
#include <stdlib.h>

#if defined(_DEBUG) || defined(DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#else
#include <malloc.h>
#endif
#include <WinSock2.h> /* winsock.h *must* be included before windows.h. */
#include <Windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <comdef.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <direct.h>
#else 
#include <stdbool.h>
#include <stdlib.h>
#endif // WINDOWS

#define __QUOTED_INT__(x) #x
#define __INT_TO_STRING__(x) __QUOTED_INT__(x)
#define __TO_DO__(message) __FILE__ "(" __INT_TO_STRING__(__LINE__) "): TODO: " message
// usage: #pragma message(__TO_DO__("(Joe) foo bar"))

#if defined(_DEBUG) && !defined(DEBUG)
#define DEBUG _DEBUG
#endif
#ifndef __cplusplus
#ifdef WINDOWS
#define bool BOOL // on Linux it is <stdbool.h>
#define true TRUE
#define false FALSE
#endif
#endif
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#define _USE_MATH_DEFINES
#include <math.h>
#ifdef WINDOWS
#include <tchar.h>
#include <intrin.h>
#include <io.h>
#define MSG_NOSIGNAL 0
#define EBADFD EBADF
#define SHUT_RD     SD_RECEIVE
#define SHUT_WR     SD_SEND
#define SHUT_RDWR   SD_BOTH
#ifndef __cplusplus
#define bool BOOL
#define true TRUE
#define false FALSE
#define byte BYTE
#endif
#else
#include <alloca.h>
#include <malloc.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <asm-generic/mman-common.h>
#include <asm-generic/mman.h>
#include <linux/sched.h>
#include <sys/types.h>
#endif
#ifdef __MACH__
# include <mach/mach.h>
# include <mach/task.h> /* defines PAGE_SIZE to 4096 */
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic error "-Wall"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wenum-compare"
#pragma GCC diagnostic warning "-Wunused-function"
#pragma GCC diagnostic error "-Wuninitialized"
#pragma GCC diagnostic ignored "-Wmultichar"
#if !defined(DEBUG) || defined(NDEBUG)  /* because some vars are used only in trace() or assert() */
#pragma GCC diagnostic ignored "-Wunused-variable"
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmultichar"
#endif
#endif
#endif /* defined(__GNUC__) || defined(__clang__) */


#if defined(_DEBUG) && !defined(DEBUG)
#define DEBUG _DEBUG
#endif
#if defined(DEBUG) && !defined(_DEBUG)
#define _DEBUG DEBUG
#endif

#if defined(__GNUC__) || defined(__clang__)
#define attribute_packed __attribute__((packed))
#else
#define attribute_packed
#endif

#ifdef WINDOWS
typedef LPARAM lparam_t;
typedef WPARAM wparam_t;
typedef HWND view_t;
typedef HDC canvas_t;
typedef HFONT font_t;
typedef HBRUSH brush_t;
typedef HPEN pen_t;
typedef UINT_PTR uintptr_t;
typedef INT_PTR intptr_t;
typedef UINT_PTR socket_t;
typedef int32_t socklen_t; // accept() on *nix
#define null nullptr
#define inline_c __forceinline
#else
#define null NULL
#define countof(a) ((int)(sizeof(a) / sizeof((a)[0])))
typedef uint8_t byte;
typedef int socket_t;
#define closesocket(s) close(s) // see: http://stackoverflow.com/questions/35441815/are-close-and-closesocket-interchangable
enum { MAX_PATH = 260 };
#define inline_c inline
#endif

enum {
    MAX_SERIAL_NUMBER_LENGTH = 128, // maximum length of camera serial number
    SERIAL_NUMBER_SIZE = MAX_SERIAL_NUMBER_LENGTH, // TODO: (Leo) fix me
    MEM_ALIGN = 128,
};

#ifdef __cplusplus
#define BEGIN_C extern "C" {
#define END_C } // extern "C" 
#else
#define BEGIN_C
#define END_C
#endif

