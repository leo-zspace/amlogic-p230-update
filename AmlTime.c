#include <stddef.h>
#include "AmlTime.h"
#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h> // portable: uint64_t   MSVC: __int64 
#endif

#ifdef _MSC_VER

// MSVC defines this in winsock2.h!?
typedef struct timeval {
    long tv_sec;
    long tv_usec;
} timeval;

int gettimeofday(struct timeval * tp, struct timezone * tzp) {
    (void)tzp;
    // Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
    // This magic number is the number of 100 nanosecond intervals since January 1, 1601 (UTC)
    // until 00:00:00 January 1, 1970 
    static const uint64_t EPOCH = ((uint64_t) 116444736000000000ULL);
    SYSTEMTIME  system_time;
    FILETIME    file_time;
    uint64_t    time;
    GetSystemTime( &system_time );
    SystemTimeToFileTime( &system_time, &file_time );
    time =  ((uint64_t)file_time.dwLowDateTime )      ;
    time += ((uint64_t)file_time.dwHighDateTime) << 32;
    tp->tv_sec  = (long) ((time - EPOCH) / 10000000L);
    tp->tv_usec = (long) (system_time.wMilliseconds * 1000);
    return 0;
}

void usleep(__int64 usec) { // idiotic copy/paste from stackoverflow 
    HANDLE timer; 
    LARGE_INTEGER ft; 
    ft.QuadPart = -(10*usec); // Convert to 100 nanosecond interval, negative value indicates relative time
    timer = CreateWaitableTimer(NULL, TRUE, NULL); 
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0); 
    WaitForSingleObject(timer, INFINITE); 
    CloseHandle(timer); 
}

time_t get_tick_count(void) {
    return GetTickCount();
}

#else

time_t GetTickCount(void) {
  return timeGetTime();
}

#endif

time_t timeGetTime (void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (time_t) (tv.tv_usec / 1000.0 + 1000 * tv.tv_sec);
}


