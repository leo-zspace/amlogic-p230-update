#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define mutex_t CRITICAL_SECTION
#define mutex_init(m, a) { BOOL b = InitializeCriticalSectionAndSpinCount(m, a); assert(b); (void)b; }
#define mutex_lock(m) EnterCriticalSection(m)
#define mutex_trylock(m) TryEnterCriticalSection(m)
#define mutex_unlock(m) LeaveCriticalSection(m)
#define mutex_destroy(m) DeleteCriticalSection(m)

typedef void* pthread_t;
typedef void* pthread_attr_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef void* pthread_condattr_t;

// On Windows: in addition to EDEADLK, EINVAL, EINVAL, ESRCH 
// pthread_join() may return: 
// WAIT_ABANDONED=0x00000080, WAIT_TIMEOUT=0x00000102, ERROR_INVALID_HANDLE 
// and anything else from GetLastError() (both positive and negative)
// all of them are good candidates for assertion() and
// a starting point for looking for memory corruption of
// incorrect threading code.

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void* (*thread_routine)(void*), void* arg);
int pthread_join(pthread_t thread, void **value_ptr);
int pthread_setschedprio_np(pthread_t thread, int prio);
int pthread_getschedprio_np(pthread_t thread);

int pthread_setaffinity_mask_np(pthread_t thread, uint64_t mask); // thread == null: current thread
int pthread_get_priority_max_np(); // sched_get_priority_max
int pthread_get_priority_min_np();
int pthread_get_priority_realtime_np();
int pthread_get_priority_normal_np();

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attributes);
int pthread_cond_timed_wait_np(pthread_cond_t* cond, mutex_t* mutex, double timeout_in_milliseconds); // ~24 days INT_MAX milliseconds maximum
int pthread_cond_wait(pthread_cond_t* cond, mutex_t* mutex); 
int pthread_cond_broadcast(pthread_cond_t* cond); // unblocks all waiting threads
int pthread_cond_signal(pthread_cond_t *cond); // unblocks one waiting thread
int pthread_cond_destroy(pthread_cond_t* cond);

int get_number_of_hardware_cores();
uint64_t get_core_affinity_mask(int core_number);

int pthread_set_name_np(pthread_t thread, const char* name);
void pthread_get_name_np(pthread_t thread, char* name, int count);

pthread_t pthread_self();
#define pthread_null null // pthread is "int" on *nix and void* on Windows

#ifdef ANYBODY_EVER_NEEDS_TRY_JOIN // which is very strange need because any reasonable thread may do it with a bool flag itself

int pthread_tryjoin_np(pthread_t thread, void **value_ptr);
int pthread_timedjoin_np(pthread_t thread, void **value_ptr, const struct timespec *abstime);

#endif

#ifdef __cplusplus
} // extern "C"
#endif
