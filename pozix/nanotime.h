#pragma once
#include <stdint.h>

BEGIN_C

enum {
    NANOSECONDS_IN_MICROSECOND = 1000,
    NANOSECONDS_IN_MILLISECOND = NANOSECONDS_IN_MICROSECOND * 1000,
    NANOSECONDS_IN_SECOND = NANOSECONDS_IN_MILLISECOND * 1000
};

// since last cpu reset time (system boot) monotonic:

double time_in_seconds(); 
double time_in_milliseconds();
uint64_t time_in_nanoseconds();
uint64_t time_in_nanoseconds_absolute(); 

// non-monotonic:

uint64_t time_in_microseconds_wall(); // since 12:00 A.M. January 1, 1601 Coordinated Universal Time (UTC)

void time_wall_to_utc(uint64_t microseconds, int* year, int* month, int* day, int* hh, int* mm, int* ss, int* ms, int* mc);
void time_wall_to_local(uint64_t microseconds, int* year, int* month, int* day, int* hh, int* mm, int* ss, int* ms, int* mc);

// Nanoseconds to milliseconds conversion. Be aware double is just 48 bits mantissa and 4 days is 49 bits in nanoseconds.
static inline double ns2ms(unsigned long long nanoseconds) { return nanoseconds / (double)NANOSECONDS_IN_MILLISECOND; }
static inline unsigned long long ms2ns(double milliseconds) { return (uint64_t)(milliseconds * NANOSECONDS_IN_MILLISECOND); }

// time formatting functions are useful for debugging timestamps
// ignores everything above minutes (days and hours etc) and return
// thread local storage string "01:23.456:789"
#define time_format_milliseconds(milliseconds_as_double) (time_format_milliseconds_(milliseconds_as_double).text)
#define time_format_nanoseconds(nanoseconds_as_uint64)   (time_format_nanoseconds_(nanoseconds_as_uint64).text)
#define time_format_hhmmss(milliseconds_as_double) (time_format_hhmmss_(milliseconds_as_double).text)

struct time_formatted_s { char text[16]; };
    
const struct time_formatted_s time_format_milliseconds_(double milliseconds); 
const struct time_formatted_s time_format_nanoseconds_(uint64_t nanoseconds);
const struct time_formatted_s time_format_hhmmss_(double milliseconds);

void nanosleep_np(int64_t nanoseconds);
void millisleep(double milliseconds);

#ifdef WINDOWS // only for Windows:

uint64_t time_in_100ns();// since January 1, 1601 "COBOL" epoch adopted by Microsoft
int64_t time_in_seconds_since_1970(uint64_t ns100); // since 1970

#endif

END_C