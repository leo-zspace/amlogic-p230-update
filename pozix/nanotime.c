#include "pozix.h"
#include "muldiv128.h"

BEGIN_C

#ifdef WINDOWS

static int64_t freq = 0;

double time_in_seconds() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    if (freq == 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = f.QuadPart;
    }
    return (double)li.QuadPart / freq;
}

double time_in_milliseconds() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    if (freq == 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = f.QuadPart;
    }
    return (li.QuadPart * 1000.0) / freq;
}

uint64_t time_in_nanoseconds() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    if (freq == 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = f.QuadPart; // usually ~3,000,000 which corresponds to ~300 nanoseconds precision
    }
    uint64_t nanos = (uint64_t)muldiv128(li.QuadPart, NANOSECONDS_IN_SECOND, freq);
    assert(nanos >= 0);
    return nanos;
}

uint64_t time_in_nanoseconds_absolute() {
    return time_in_nanoseconds();
}

typedef void (WINAPI *get_system_time_precise_as_file_time_t)(FILETIME* ft);
static get_system_time_precise_as_file_time_t get_system_time_precise_as_file_time;

static inline get_system_time_precise_as_file_time_t getSystemTimePreciseAsFileTime() {
    if (get_system_time_precise_as_file_time == null) {
        get_system_time_precise_as_file_time = (get_system_time_precise_as_file_time_t)(void*)GetProcAddress(LoadLibraryA("kernel32.dll"), "GetSystemTimePreciseAsFileTime");
        if (get_system_time_precise_as_file_time == null) { // Windows < 8.x fallback
            get_system_time_precise_as_file_time = (get_system_time_precise_as_file_time_t)(void*)GetProcAddress(LoadLibraryA("kernel32.dll"), "GetSystemTimeAsFileTime");
        }
        assertion(get_system_time_precise_as_file_time != null, "binding to GetSystemTimePreciseAsFileTime/GetSystemTimeAsFileTime failed");
    }
    return get_system_time_precise_as_file_time;
}

uint64_t time_in_microseconds_wall() { // guarantied NOT to be monotonic
    FILETIME ft; // time in 100ns interval (tenth of microsecond)
    getSystemTimePreciseAsFileTime()(&ft);  // since 12:00 A.M. January 1, 1601 Coordinated Universal Time (UTC)
    uint64_t microseconds = (((uint64_t)ft.dwHighDateTime) << 32 | ft.dwLowDateTime) / 10;
    assert(microseconds > 0);
    return microseconds;
}

void time_wall_to_utc(uint64_t microseconds, int* year, int* month, int* day, int* hh, int* mm, int* ss, int* ms, int* mc) {
    uint64_t time_in_100ns = microseconds * 10;
    FILETIME mst = { (DWORD)(time_in_100ns & 0xFFFFFFFF), (DWORD)(time_in_100ns >> 32) };
    SYSTEMTIME utc;
    FileTimeToSystemTime(&mst, &utc);
    *year = utc.wYear;
    *month = utc.wMonth;
    *day = utc.wDay;
    *hh = utc.wHour;
    *mm = utc.wMinute;
    *ss = utc.wSecond;
    *ms = utc.wMilliseconds;
    *mc = microseconds % 1000;
}

void time_wall_to_local(uint64_t microseconds, int* year, int* month, int* day, int* hh, int* mm, int* ss, int* ms, int* mc) {
    uint64_t time_in_100ns = microseconds * 10;
    FILETIME mst = { (DWORD)(time_in_100ns & 0xFFFFFFFF), (DWORD)(time_in_100ns >> 32) };
    SYSTEMTIME utc;
    FileTimeToSystemTime(&mst, &utc);
    DYNAMIC_TIME_ZONE_INFORMATION tzi;
    GetDynamicTimeZoneInformation(&tzi);
    SYSTEMTIME lt = {0};
    SystemTimeToTzSpecificLocalTimeEx(&tzi, &utc, &lt);
    *year = lt.wYear;
    *month = lt.wMonth;
    *day = lt.wDay;
    *hh = lt.wHour;
    *mm = lt.wMinute;
    *ss = lt.wSecond;
    *ms = lt.wMilliseconds;
    *mc = microseconds % 1000;
}

void nanosleep_np(int64_t nanoseconds) {
    typedef int (FAR __stdcall *fnNtDelayExecution)(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval);
    static fnNtDelayExecution NtDelayExecution;
    LARGE_INTEGER delay;
    delay.QuadPart = - ((nanoseconds + 99) / 100); // delay in 100-ns units. negative value means delay relative to current.
    if (NtDelayExecution == null) {
        HMODULE ntdll = LoadLibraryA("ntdll.dll");
        assert(ntdll != null);
        if (ntdll != null) {
            NtDelayExecution = (fnNtDelayExecution)(void*)GetProcAddress(ntdll, "NtDelayExecution");
        }
    }
    assert(NtDelayExecution != null);
    if (NtDelayExecution != null) {
        NtDelayExecution(false, &delay); //  If "alertable" is set, execution can break in a result of NtAlertThread call.
    } else {
        ExitProcess(ERROR_FATAL_APP_EXIT);
    }
}

uint64_t time_in_100ns() { // since January 1, 1601 "COBOL" epoch adopted by Microsoft
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ft.dwLowDateTime | (((uint64_t)ft.dwHighDateTime) << 32);
}

int64_t time_in_seconds_since_1970(uint64_t ns100) { // since 1970
    typedef BOOL (*time_to_seconds_since_1970_t)(PLARGE_INTEGER, PULONG);
    static time_to_seconds_since_1970_t time_to_seconds_since_1970;
    if (time_to_seconds_since_1970 == null) {
        time_to_seconds_since_1970 = (time_to_seconds_since_1970_t)
            GetProcAddress(LoadLibrary("NTDLL.dll"), "RtlTimeToSecondsSince1970");
    }
    uint64_t cobol_epoch_time_in_100ns_intervals = ns100;
    ULONG elapsed_seconds = 0;
    time_to_seconds_since_1970((PLARGE_INTEGER)&cobol_epoch_time_in_100ns_intervals, &elapsed_seconds);
    return elapsed_seconds;
}


#else

enum { N = 1024 }; /* 2 x 8KB hashmaps */

// See:
// http://stackoverflow.com/questions/3523442/difference-between-clock-realtime-and-clock-monotonic
// and
// http://stackoverflow.com/questions/14726401/starting-point-for-clock-monotonic

uint64_t time_in_nanoseconds_absolute() {
    struct timespec tm = {0};
    clock_gettime(CLOCK_MONOTONIC, &tm);
    return NANOSECONDS_IN_SECOND * (int64_t)tm.tv_sec + tm.tv_nsec;
}

static uint64_t start_time_in_nanoseconds;

uint64_t time_in_nanoseconds() {
    return time_in_nanoseconds_absolute() - start_time_in_nanoseconds;
}

uint64_t time_in_microseconds_wall() { // guarantied NOT to be monotonic on Windows and Linux
    struct timeval tv = {};
    struct timezone tz = {};
    gettimeofday(&tv, &tz); // The time returned by gettimeofday() is affected by discontinuous jumps in the system time
    // not sure do we want tz.tz_minuteswest and tz.tz_dsttime
    return tv.tv_sec * (1000LL * 1000LL) + tv.tv_usec;
}

void time_wall_to_local(uint64_t microseconds, int* year, int* month, int* day, int* hh, int* mm, int* ss, int* ms, int* mc) {
    struct tm lt = {};
    time_t time = (time_t)(microseconds / (1000 * 1000));
    localtime_r(&time, &lt);
    *year  = lt.tm_year + 1900; // see: https://linux.die.net/man/3/localtime_r
    *month = lt.tm_mon + 1;
    *day   = lt.tm_mday;
    *hh    = lt.tm_hour;
    *mm    = lt.tm_min;
    *ss    = lt.tm_sec;
    *ms    = (microseconds / 1000) % 1000;
    *mc    = microseconds % 1000;
    // unused lt.tm_gmtoff lt.tm_zone lt.tm_isdst
}

static_init(start_time_in_nanoseconds) {
    start_time_in_nanoseconds = time_in_nanoseconds_absolute();
}

double time_in_milliseconds() {
    return time_in_nanoseconds() / (double)NANOSECONDS_IN_MILLISECOND;
}

double time_in_seconds() {
    return time_in_milliseconds() / 1000;
}

int64_t time_thread_in_nanoseconds() {
    struct timespec tm = {0};
#ifdef __ANDROID__
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tm);
#elif __MACH__
    mach_clock_get_thread_time(&tm);
#else
    clock_gettime(CLOCK_MONOTONIC, &tm);
#endif
    return NANOSECONDS_IN_SECOND * (int64_t)tm.tv_sec + tm.tv_nsec;
}

void nanosleep_np(int64_t nanoseconds) {
    struct timespec rq = { (long)(nanoseconds / NANOSECONDS_IN_SECOND), (long)(nanoseconds % NANOSECONDS_IN_SECOND) };
    struct timespec rm = { 0, 0 };
    int64_t t0 = time_in_nanoseconds_absolute();
    int r = nanosleep(&rq, &rm);
    int64_t t1 = time_in_nanoseconds_absolute();
//  log_info("nanosleep(%lld) slept %lld", nanoseconds, t1 - t0);
    if (r == 0) {
        assert(t1 >= t0 + nanoseconds); (void)t0; (void)t1;
    } else {
        assert(r == EINTR); // killing process with sleeping thread
    }
}

#endif

void millisleep(double milliseconds) {
    assert(milliseconds >= 0);
    nanosleep_np((int64_t)(milliseconds * NANOSECONDS_IN_MILLISECOND));
}

const struct time_formatted_s time_format_milliseconds_(double milliseconds) {
    uint64_t us = (uint64_t)(milliseconds * 1000); // micro
    uint64_t ms = us / 1000; // milli
    uint64_t ss = ms / 1000; // seconds
    uint64_t mm = ss / 60;   // minutes
    struct time_formatted_s r;
    snprintf0(r.text, countof(r.text), "%02d:%02d.%03d.%03d", (int)(mm % 60), (int)(ss % 60), (int)(ms % 1000), (int)(us % 1000));
    return r;
}

const struct time_formatted_s time_format_nanoseconds_(uint64_t nanoseconds) {
    uint64_t us = nanoseconds / 1000; // micro
    uint64_t ms = us / 1000; // milli
    uint64_t ss = ms / 1000; // seconds
    uint64_t mm = ss / 60;   // minutes
    struct time_formatted_s r;
    snprintf0(r.text, countof(r.text), "%02d:%02d.%03d.%03d", (int)(mm % 60), (int)(ss % 60), (int)(ms % 1000), (int)(us % 1000));
    return r;
}

const struct time_formatted_s time_format_hhmmss_(double milliseconds) {
    uint64_t us = (uint64_t)(milliseconds * 1000); // micro
    uint64_t ms = us / 1000; // milli
    uint64_t ss = ms / 1000; // seconds
    uint64_t mm = ss / 60;   // minutes
    uint64_t hh = mm / 60;   // hours
    struct time_formatted_s r;
    snprintf0(r.text, countof(r.text), "%02d:%02d:%02d", (int)hh, (int)(mm % 60), (int)(ss % 60));
    return r;
}

/*
    number of cpu clocks:
    uint64_t ts = __rdtsc(); // repotedly available with gcc #include <x86intrin.h>
    trace("%lld ticks", ts);
    // asm "rdtscp" is better - google it but have problems see:
    https://msdn.microsoft.com/en-us/library/windows/desktop/dn553408(v=vs.85).aspx
    https://en.wikipedia.org/wiki/Time_Stamp_Counter

    thus for now we will rely on:

    GetSystemTimePreciseAsFileTime(&ft); // ~12 nanoseconds on i3-4130 3.4GHz
    time_in_nanoseconds_absolute(); // ~30 nanoseconds
    QueryPerformanceCounter(&pc); // ~7 nanoseconds
    time_in_milliseconds(); // ~13 nanoseconds

    Also at the time of writing nanoseconds since
    microsoft epoch 12:00 A.M. January 1, 1601 Coordinated Universal Time (UTC)
    has overlaped to negative domain:
    Jun 19 2016 is 0xB5F2991DED4EB6E4
    Thus to keep it positive and not to use obscure 100ns intervals we implement
    time_in_microseconds_wall()
    and
    time_wall_to_local()
    time_wall_to_utc()



*/

/*
static void test_performance() {
    nanosleep_np(100LL * NANOSECONDS_IN_MILLISECOND);
    FILETIME ft;
    double time = time_in_milliseconds();
    int64_t sum = 0; // to make sure compiler does not otimize us out completely
    for (int i = 0; i < 1000000; i++) {
        GetSystemTimePreciseAsFileTime(&ft); // ~12 nanoseconds on i3-4130 3.4GHz
        sum += ft.dwLowDateTime;
    }
    time = time_in_milliseconds() - time;
    trace("sum=%d GetSystemTimePreciseAsFileTime=%8.6f", (int)sum, time);

    nanosleep_np(100LL * NANOSECONDS_IN_MILLISECOND);
    time = time_in_milliseconds();
    sum = 0;
    for (int i = 0; i < 1000000; i++) {
        sum += time_in_nanoseconds_absolute(); // ~30 nanoseconds
    }
    time = time_in_milliseconds() - time;
    trace("sum=%d time_in_nanoseconds_absolute=%8.6f", (int)sum, time);

    nanosleep_np(100LL * NANOSECONDS_IN_MILLISECOND);
    time = time_in_milliseconds();
    sum = 0;
    LARGE_INTEGER pc = {0};
    for (int i = 0; i < 1000000; i++) {
        QueryPerformanceCounter(&pc); // ~7 nanoseconds
        sum += pc.QuadPart;
    }
    time = time_in_milliseconds() - time;
    trace("sum=%d QueryPerformanceCounter=%8.6f", (int)sum, time);

    nanosleep_np(100LL * NANOSECONDS_IN_MILLISECOND);
    time = time_in_milliseconds();
    sum = 0;
    for (int i = 0; i < 1000000; i++) {
        double t = time_in_milliseconds(); // ~13 nanoseconds
        sum += (int)(t * 1000);
    }
    time = time_in_milliseconds() - time;
    trace("sum=%d time_in_milliseconds=%8.6f", (int)sum, time);
}
*/

END_C
