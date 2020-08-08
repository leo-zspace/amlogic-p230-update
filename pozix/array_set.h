#pragma once
#include "runtime.h"

BEGIN_C

/*

Declaration:
    void* array_set(a, 153);
reserves memory and count for an array a[153] of elements of type void* 
for which order of elements is not guaranteed. 
It is very very cheap set like Microsoft's implementation
of FDSET (versus original FDSET Unix which was a bitset)
    array_set_indexof() and array_set_remove() 
    are O(n) operations (last one is via swap).
Iterator is simple and efficient:
    for (int i = 0; i < array_set_countof(a); i++) { ... a[i] ... }
Useful for implementing limited set of e.g pointers to callbacks and alike.
    array_set_indexof() returns -1 if element is not found 
    array_set_remove()  does nothing if element is not in the set
    array_set_add()     does nothing if array if full prio the call
it is responsibility of the caller to detect boundary conditions and
react accordingly.

Usage example:

    struct {
        void*  array_set(p, 3);
    } s = {};

    int a[] = {0x01020304, 0x05060708, 0x0A0B0C0D};

    array_set_add(s.p, &a[0]); assert(array_set_countof(s.p) == 1 && s.p[0] == &a[0]); 
    array_set_add(s.p, &a[1]); assert(array_set_countof(s.p) == 2 && s.p[1] == &a[1]);
    array_set_add(s.p, &a[2]); assert(array_set_countof(s.p) == 3 && s.p[2] == &a[2]);
    assert(array_set_countof(s.p) == 3);
    
    assert(array_set_indexof(s.p, &a[0]) == 0);
    assert(array_set_indexof(s.p, &a[1]) == 1);
    assert(array_set_indexof(s.p, &a[2]) == 2);
    array_set_remove(s.p, &a[1]);
    
    assert(array_set_indexof(s.p, &a[0]) == 0);
    assert(array_set_indexof(s.p, &a[2]) == 1);
    assert(array_set_indexof(s.p, &a[1]) == -1);
    assert(array_set_countof(s.p) == 2);
    
    array_set_remove_at(s.p, 0);
    assert(array_set_countof(s.p) == 1);
    assert(array_set_indexof(s.p, &a[2]) == 0);
    while (array_set_countof(s.p) > 0) { array_set_remove_at(s.p, 0); }

*/
#define array_set_volatile(a, capacity) a[(capacity) + 1]; volatile int a##_count__
#define array_set(a, capacity)    a[(capacity) + 1]; int a##_count__
#define array_set_countof(a)      (a##_count__)
#define array_set_indexof(a, v)   (__array_set_index_of__((void*)(a), array_set_countof(a), sizeof((a)[0]), ((void)((a)[countof(a) - 1] = (v)), (void*)&(a)[countof(a) - 1]))) // O(n)
#define array_set_is_empty(a)     (array_set_countof(a) == 0)
#define array_set_is_full(a)      (array_set_countof(a) >= countof(a) - 1)
#define array_set_add(a, v)       do { if (!array_set_is_full(a)) { a[a##_count__++] = (v); } } while (0)
#define array_set_clear(a)        do { a##_count__ = 0; } while (0)
#define array_set_remove_at(a, i) do { \
    if (0 <= (i) && i < array_set_countof(a)) { \
        __array_set_swap__((void*)(a), array_set_countof(a), sizeof((a)[0]), (i), array_set_countof(a) - 1); \
        a##_count__--; \
    } \
} while (0)

#define array_set_remove(a, v) do { \
    int ix ## __LINE__ = array_set_indexof(a, (v)); \
    if (ix ## __LINE__ >= 0) { array_set_remove_at(a, ix ## __LINE__); } \
} while (0)

int  __array_set_index_of__(void* a, int n, int bytes, void* va);
void __array_set_swap__(void* a, int n, int bytes, int i, int j);

END_C