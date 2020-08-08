#include "pozix.h"

BEGIN_C

// alloca code analysis warnings:
#pragma warning(disable: 6262) // excessive stack usage
#pragma warning(disable: 6263) // using alloca in a loop
#pragma warning(disable: 6255) //_alloca indicates failure by raising a stack overflow exception

typedef struct pthread_s {
    HANDLE h;
    void*  a;
    void*  r;
    void*  (*thread_routine)(void*);
} pthread_t_;

static DWORD WINAPI thread_proc(void* thread) {
    pthread_t_* t = (pthread_t_*)thread;
    if_failed_return_result(CoInitializeEx(0, COINIT_MULTITHREADED | COINIT_SPEED_OVER_MEMORY));
    t->r = t->thread_routine(t->a);
    CoUninitialize();
    // DO NOT CloseHandle(t->h) because pthread_join() needs it and will close it
    return (DWORD)(uintptr_t)t->r;
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*thread_routine)(void*), void* arg) {
    pthread_t_* t = (pthread_t_*)mem_allocz(sizeof(pthread_t_));
//  trace("thread=0x%p", t); print_stack_trace();
    if (t != null) {
        t->thread_routine = thread_routine;
        t->a = arg;
//      http://stackoverflow.com/questions/331536/windows-threading-beginthread-vs-beginthreadex-vs-createthread-c
        t->h = CreateThread(null, 4 * 1024 * 1024, thread_proc, t, 0, null);
        if (t->h != null) {
            *thread = (pthread_t)t;
        } else {
            mem_free(t);
            t = null;
        }
    }
    return t != null ? 0 : -1;
}

int pthread_setaffinity_mask_np(pthread_t thread, uint64_t mask) {
    pthread_t_* t = (pthread_t_*)thread;
    bool b = SetThreadAffinityMask(t == null ? GetCurrentThread() : t->h, (uintptr_t)mask);
    return b ? 0 : GetLastError();
    // see: pthread_self() and http://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html for linux
}

int pthread_setschedprio_np(pthread_t thread, int prio) {
    pthread_t_* t = (pthread_t_*)thread;
    if (prio == THREAD_PRIORITY_TIME_CRITICAL) {
        if_false_return(SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS), GetLastError());
    }
    return SetThreadPriority(t == null ? GetCurrentThread() : t->h, prio) ? 0 : GetLastError();
}

int pthread_getschedprio_np(pthread_t thread) {
    pthread_t_* t = (pthread_t_*)thread;
    return GetThreadPriority(t == null ? GetCurrentThread() : t->h);
}

int pthread_get_priority_realtime_np() {
    // sched_get_priority_max()
    return THREAD_PRIORITY_TIME_CRITICAL;
}

int pthread_get_priority_normal_np() {
    return THREAD_PRIORITY_NORMAL;
}

int pthread_get_priority_max_np() {
    // sched_get_priority_max()
    return THREAD_BASE_PRIORITY_MAX; // aka ABOVE_NORMAL
}

int pthread_get_priority_min_np() {
    return THREAD_BASE_PRIORITY_MIN; // aka BELOW_NORMAL
}

static int join(pthread_t thread, void **value_ptr, int timeout) {
    pthread_t_* t = (pthread_t_*)thread;
    int r = 0;
    if (t != null) {
        if (t->h != null) {
            r = WaitForSingleObject(t->h, timeout);
            assert(WAIT_OBJECT_0 == 0); // and if it ever changes Windows are gone...
            if (r == WAIT_OBJECT_0) {
                // CloseHandle failure usually indicate memory corruption (invalid handle)
                // or the code being really really incorrect somewhere
                r = CloseHandle(t->h) ? 0 : GetLastError();
                assertion(r == 0, "CloseHandle(%p) thread=%p failed %s", t->h, t, strerr(r));
            } else if (r == WAIT_TIMEOUT)   {
                r = ERROR_TIMEOUT;
            } else if (r == WAIT_FAILED)    {
                r = ERROR_FUNCTION_FAILED;
            } else if (r == WAIT_ABANDONED) {
                r = ERROR_ABANDONED_WAIT_0;
            }
            assertion(r == 0, "WaitForSingleObject(0x%p) failed %s", t->h, strerr(r));
            if (r == 0) {
                t->h = null;
                if (value_ptr != null) { *value_ptr = t->r; }
            }
        }
        // The memory will NOT be freed if CloseHandle() did not work
        // only for a reason to assist further debugging in case of
        // memory corruption or incorrect code with a price of a tiny memory leak:
        if (r == 0) { mem_free(t); }
    } else {
        r = EINVAL;
    }
    return r;
}

int pthread_cond_timed_wait_np(pthread_cond_t* cond, mutex_t* mutex, double timeout_in_milliseconds) {
    if (timeout_in_milliseconds < 0) { assertion(timeout_in_milliseconds == -1, "only -1 is allowed %g", timeout_in_milliseconds); timeout_in_milliseconds = -1; }
    if (timeout_in_milliseconds >= (double)INT_MAX) { // ~ 24 days
        assertion(timeout_in_milliseconds <= INT_MAX, "timeout_in_milliseconds=%g > maximum", timeout_in_milliseconds);
        timeout_in_milliseconds = INT_MAX;
    }
    DWORD milliseconds = timeout_in_milliseconds < 0 ? INFINITE : (int)timeout_in_milliseconds;
    return SleepConditionVariableCS(cond, mutex, milliseconds) ? 0 : GetLastError(); // ERROR_TIMEOUT (1460 0x5B4) or other errors
}

int pthread_cond_wait(pthread_cond_t* cond, mutex_t* mutex) {
    return pthread_cond_timed_wait_np(cond, mutex, -1);
}

int pthread_cond_broadcast(pthread_cond_t* cond) { WakeAllConditionVariable(cond); return 0; }
int pthread_cond_signal(pthread_cond_t* cond) { WakeConditionVariable(cond); return 0; }

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attributes) {
    InitializeConditionVariable(cond); (void)attributes; return 0;
}

int pthread_cond_destroy(pthread_cond_t* cond) { return 0; } // Microsoft implement cond variables in TLS hashmap

int pthread_join(pthread_t thread, void **value_ptr) { return join(thread, value_ptr, INFINITE); }

#ifdef ANYBODY_EVER_NEEDS_TRY_JOIN

// http://man7.org/linux/man-pages/man3/pthread_tryjoin_np.3.html

int pthread_tryjoin_np(pthread_t thread, void **value_ptr) { return join(thread, value_ptr, 0); }

int pthread_timedjoin_np(pthread_t thread, void **value_ptr, const struct timespec *abstime) {
    int64_t ns = (int64_t)abstime->tv_sec * NANOSECONDS_IN_SECOND + abstime->tv_nsec;
    static const int64_t SECONDS_TO_UNIX_EPOCH = 11644473600LL;
    ns = time_in_nanoseconds_absolute() - ns;
    int64_t ns100 = ns / 100 - SECONDS_TO_UNIX_EPOCH * (NANOSECONDS_IN_SECOND / 100);
    return join(thread, value_ptr, ns100 < 0 ? 0 : (ns100 > INT32_MAX ? INFINITE : (int)ns100));
}

#endif // ANYBODY_EVER_NEEDS_TRY_JOIN

pthread_t pthread_self() { return 0; }

int pthread_set_name_np(pthread_t thread, const char* name) {
    pthread_t_* t = (pthread_t_*)thread;
    int n = (int)strlen(name);
    wchar_t* wname = (wchar_t*)stack_alloc((n + 1) * 2);
    for (int i = 0; i <= n; i++) {
        wname[i] = name[i];
    }
    SetThreadDescription(thread == null ? GetCurrentThread() : t->h, wname);
#if 0
//  trace("thread=0x%p name=%s", thread, name);
    const int MS_VC_EXCEPTION=0x406D1388;
    #pragma pack(push, 8)
    struct {
       DWORD dwType; // Must be 0x1000.
       LPCSTR szName; // Pointer to name (in user address space).
       DWORD dwThreadID; // Thread ID (-1=caller thread).
       DWORD dwFlags; // Reserved for future use, must be zero.
    } info;
    #pragma pack(pop)
    info.dwType = 0x1000;
    info.szName = name;
    info.dwThreadID = t == null ? GetCurrentThreadId() : GetThreadId(t->h);
    info.dwFlags = 0;
   __try { RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info);
   } __except(EXCEPTION_EXECUTE_HANDLER) {
   }
#endif
   return 0;
}

void pthread_get_name_np(pthread_t thread, char* name, int count) {
    wchar_t* wname = null;
    HRESULT hr = GetThreadDescription(GetCurrentThread(), &wname);
    char* s = (char*)stack_allocz(count + 1);
    if (SUCCEEDED(hr)) {
        for (int i = 0; i <= count && wname[i] != 0; i++) {
            s[i] = (byte)(wname[i] & 0x7F);
        }
        LocalFree(wname);
    }
    snprintf0(name, count, "%s", s);
}

/*
    https://msdn.microsoft.com/en-us/library/windows/desktop/dd405488(v=vs.85).aspx
    If a 32-bit process running under WOW64 calls this function on a system with more than 64 processors,
    some of the processor affinity masks returned by the function may be incorrect. This is because
    the high-order DWORD of the 64-bit KAFFINITY structure that represents all 64 processors is
    "folded" into a 32-bit KAFFINITY structure in the caller's buffer. As a result, the affinity
    masks for processors 32 through 63 are incorrectly represented as duplicates of the masks for
    processors 0 through 31.
    In addition, the sum of all per-group ActiveProcessorCount and MaximumProcessorCount values
    reported in PROCESSOR_GROUP_INFO structures may exclude some active logical processors.

    Leo: "If we are still on Win32 by the tine we have more than 32 processors we are doing something fundamentally wrong."
*/

static uint64_t core_affinity[32];

uint64_t get_core_affinity_mask(int i) {
    return 0 <= i && i <= get_number_of_hardware_cores() ? core_affinity[i] : 0x1;
}

static const char* rel[] = { "ProcessorCore", "NumaNode", "Cache", "ProcessorPackage", "Group" };
static const char* ct[] = { "Unified", "Instruction", "Data", "Trace" };

int get_number_of_hardware_cores() {
    static volatile int cores;
    if (cores == 0) {
        core_affinity[0] = 1;
        DWORD bytes = 0;
        GetLogicalProcessorInformationEx(RelationAll, null, &bytes);
        int k = 0;
        if (bytes > 0) {
            byte* p = (byte*)stack_allocz(bytes);
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)p;
            if_false_return(GetLogicalProcessorInformationEx(RelationAll, info, &bytes), -1);
            byte* e = p + bytes;
            for (int i = 0; p < e; i++) {
#if 0
                trace("info[%d].Size=%d relationship=%s", i, info->Size, rel[(int)info->Relationship]);
                if (info->Relationship == RelationCache) {
                    trace(" L%d cache associativity=%d line_size=%d cach_size=%dKB",
                          info->Cache.Level, info->Cache.Associativity, info->Cache.LineSize,
                          info->Cache.CacheSize / 1024, ct[(int)info->Cache.Type]);
                }
#endif
#if 0
                if (info->Relationship == RelationGroup) {
                    trace("group max=%d active=%d",
                          info->Group.MaximumGroupCount, info->Group.ActiveGroupCount);
                    for (int j = 0; j < info->Group.MaximumGroupCount; j++) {
                        trace("  [%d] active=%d max=%d active processors mask=%p", j,
                        info->Group.GroupInfo[j].ActiveProcessorCount,
                        info->Group.GroupInfo[j].MaximumProcessorCount,
                        info->Group.GroupInfo[j].ActiveProcessorMask);
                    }
                }
#endif
                if (info->Relationship == RelationProcessorCore) {
#if 0
                    trace("processor core flags=%d 0x%08X group_count=%d",
                          info->Processor.Flags, info->Processor.Flags,
                          info->Processor.GroupCount);
#endif
                    for (int j = 0; j < info->Processor.GroupCount; j++) {
#if 0
                        trace("  [%d] processor group=%d affinity mask=%p", j,
                            info->Processor.GroupMask[j].Group,
                            info->Processor.GroupMask[j].Mask);
#endif
                        if (j < countof(core_affinity)) {
                            core_affinity[k] = (uint64_t)info->Processor.GroupMask[j].Mask;
                        }
                    }
                }
#if 0
                if (info->Relationship == RelationProcessorPackage) {
                    trace("processor package flags=%d 0x%08X group_count=%d",
                          info->Processor.Flags, info->Processor.Flags,
                          info->Processor.GroupCount);
                    for (int j = 0; j < info->Processor.GroupCount; j++) {
                        trace("  [%d] processor group=%d affinity mask=%p", j,
                        info->Processor.GroupMask[j].Group,
                        info->Processor.GroupMask[j].Mask);
                    }
                }
#endif
                k += info->Relationship == RelationProcessorCore;
                p += info->Size;
                info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)p;
            }
        }
        cores = k < 1 ? 1 : k;
    }
    assertion(cores >= 1, "unexpected number of cores %d", cores);
    return cores;
}

static int pthread_tors_count;
static pthread_tor_t pthread_tors[16];
static void* pthread_tors_that[16];

void pthread_add_on_tor(pthread_tor_t tor, void* that) {
    assertion(pthread_tors_count < countof(pthread_tors), "> %d threads ctor watchers?! expected ~1", pthread_tors_count);
    for (int i = 0; i < pthread_tors_count; i++) { assert(pthread_tors[i] != tor); }
    for (int i = 0; i < pthread_tors_count; i++) {
        if (pthread_tors[i] == null) {
            pthread_tors[i] = tor;
            pthread_tors_that[i] = that;
            return;
        }
    }
    pthread_tors[pthread_tors_count] = tor;
    pthread_tors_that[pthread_tors_count] = that;
    pthread_tors_count++;
}

void pthread_remove_on_tor(pthread_tor_t tor) {
    for (int i = 0; i < pthread_tors_count; i++) {
        if (pthread_tors[i] == tor) {
            pthread_tors[i] = null;
            pthread_tors_that[i] = null;
        }
    }
}


typedef void (*thread_proc_t)(void* that);

typedef struct pthread_params_s {
    void* that;
    thread_proc_t proc;
} pthread_params_t;

static void* pthread_proc(void *params) {
    pthread_params_t* p = (pthread_params_t*)params;
//  trace("thread=0x%p gettid()=%d", p, gettid());
    void* that = p->that;
    thread_proc_t proc = p->proc;
    mem_free(p);
    for (int i = 0; i < pthread_tors_count; i++) {
        if (pthread_tors[i] != null) { pthread_tors[i](pthread_tors_that[i], true); }
    }
    proc(that);
    for (int i = 0; i < pthread_tors_count; i++) {
        if (pthread_tors[i] != null) { pthread_tors[i](pthread_tors_that[i], false); }
    }
    return null;
}

pthread_t pthread_start_np(void (*proc)(void* that_), void* that) {
    pthread_t t = 0;
    pthread_params_t* p = (pthread_params_t*)mem_alloc(sizeof(pthread_params_t));
    if (p == null) { set_errno(ENOMEM); return (pthread_t)0; }
    p->that = that;
    p->proc = proc;
    int r = pthread_create(&t, null, pthread_proc, p);
    posix_ok(r);
    return r == 0 ? t : (pthread_t)0;
}

END_C
