#include "pozix.h"
#ifdef __MACH__
#include <malloc/malloc.h>
#else
#endif

void* mem_align(void* a, int align) {
    assertion(align > 0 && (align & (align - 1)) == 0, "align %d must be power of 2", align);
    return ll2p(p2ll((char*)a + align - 1) & ~(align - 1));
}

enum { RESERVED_MEMORY_SIZE = 1 * 1024 * 1024 }; /* 1 MB */
static void* reserved;
bool memory_low;

static volatile int64_t total_allocations;
static volatile int64_t total_allocated;

static mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static bool trace_allocs = false;

/* _stacktrace returns global static buffer because it is called from mem_alloc */
extern char* stacktrace_(bool detailed, siginfo_t *sig_info, void* sigdel_context, int from);

void* (*mem_alloc)(int size);
void  (*mem_free)(void* p);
void  (*mem_free_aligned)(void* p);
void* (*mem_alloc_aligned)(int size, int align);

int mem_size(void* a) {
#ifdef __MACH__
    return malloc_size(a);
/*
#elif defined(__ANDROID__)
    size_t* s = &((size_t*)a)[-1];
    int allocated = (*s + 3) & ~0x3; // malloc_usable_size(r);
    return allocated;
*/
#else
    return malloc_usable_size(a);
#endif
}

static void* alloc_aligned(int bytes, int align) {
    assertion(align > 0 && (align & (align - 1)) == 0, "align %d must be power of 2", align);
    if (align == 1) {
        return malloc((size_t)bytes);
    } else {
        // http://pubs.opengroup.org/onlinepubs/009695399/functions/posix_memalign.html
        void* a = null;
        int r = posix_memalign(&a, (size_t)align, (size_t)bytes);
        posix_info(r);
        return r == 0 ? a : null;
    }
}

static void free_aligned(void* p) {
#ifdef WINDOWS
    _aligned_free(p);
#else
    mem_free(p);
#endif
}

#if defined(DEBUG) || defined(MEM) || defined(LEAKS)

static void* alloc(int size, int align) {
    mutex_lock(&mutex);
    void* a = alloc_aligned(size, align);
    if (a == null && reserved != null) {
        trace("MEMORY LOW: allocations=%lld allocated %lld", total_allocations, total_allocated);
        memory_low = true;
        free(reserved);
        reserved = null;
        a = malloc((size_t)size);
    }
    if (a != null) {
        int allocated = mem_size(a);
        const char* st = trace_allocs ? stacktrace() : null;
        if (trace_allocs && st != null)  {
            trace("%p allocated=%d size=%d total: allocations=%lld allocated %lld %s",
                   a, allocated, size, total_allocations, total_allocated, st);
        }
        total_allocations++;
        total_allocated += allocated;
    }
    mutex_unlock(&mutex);
    return a;
}

static void* alloc1(int size) {
    return alloc(size, 1);
}

#endif

void mem_free_(void* a) {
    if (a != null) {
        int allocated = mem_size(a);
        if (trace_allocs) {
            trace("%p allocated=%d total: allocations=%lld allocated %lld", a, allocated, total_allocations, total_allocated);
        }
        mutex_lock(&mutex);
        total_allocations--;
        total_allocated -= allocated;
        mutex_unlock(&mutex);
        free(a);
    }
}

void mem_trace_allocs(bool on) {
    trace_allocs = on;
}

char* mem_strdup(const char* s) {
    assertion(s != null, "cannot be null s=%p", s);
    char* r = null;
    if (s != null) {
        size_t n = strlen(s);
//      trace("strlen(s)=%d", n);
        size_t n1 = n + 1;
        r = mem_alloc(n1);
        assert(r != null);
        if (r != null) {
//          trace("mem_size(%p)=%d", r, mem_size(r));
            strncpy0(r, s, n1);
        }
    }
    return r;
}

void* mem_dup(void* a, int bytes) {
    assertion(a != null && bytes > 0, "a=%p bytes=%d", a, bytes);
    void* r = null;
    if (a != null && bytes > 0) {
        r = mem_alloc(bytes);
        assert(r != null);
        if (r != null) {
            memcpy(r, a, (size_t)bytes);
        }
    }
    return r;
}

int64_t mem_allocated() {
    return total_allocated;
}

int64_t mem_allocations() {
    return total_allocations;
}

typedef void* (*mem_alloc_t)(int bytes);
typedef void (*mem_free_t)(void* p);

void mem_init(void) {
    /* init/fini is called on dlclose on the loading thread.
       IMPORTANT: it is assumed that only on copy of zslib per process and it this
       loaded on main thread to avoid possible mem_init racing conditions.
       Just in case this condition does not hold (bad idea) we lock the global mutex on init;
       "leaks" table is fixed size and won't grow. Always allocated with malloc */
    mutex_lock(&mutex);
    if (reserved == null) {
#if defined(MEM) && defined(LEAKS)
//      #pragma message "#defined MEM && LEAKS"
        if (leaks == null) {
            mem_alloc = (mem_alloc_t)malloc; // to make sure mapll_create_fixed uses runtime malloc()
            mem_free = (mem_free_t)free;
            leaks = mapll_create_fixed(1024*1024); /* map will be allocated with malloc */
        }
        reserved = malloc(RESERVED_MEMORY_SIZE);
        unsigned char* p = (unsigned char*)reserved;
        for (int i = 0; i < RESERVED_MEMORY_SIZE; i += 1024) {
            p[i] = (unsigned char)i; // commit pages
        }
#endif
#if defined(DEBUG) || defined(MEM) || defined(LEAKS)
//      #pragma message "#defined DEBUG || MEM || LEAKS"
        mem_alloc = alloc1;
        mem_alloc_aligned = alloc;
        mem_free = (mem_free_t)mem_free_;
#else
        mem_alloc = (mem_alloc_t)malloc;
        mem_alloc_aligned = alloc_aligned;
        mem_free = (mem_free_t)free;
#endif
        mem_free_aligned = free_aligned;
        total_allocations = 0;
        total_allocated = 0;
    }
    mutex_unlock(&mutex);
}

static_init(mem_init) { mem_init(); } /* alternatively LOCAL_LDFLAGS=-Wl,-init,foo -fini,bar or __attribute__((constructor)) */

__attribute__((destructor))
void mem_fini() {
    /* intentionally empty */
}
