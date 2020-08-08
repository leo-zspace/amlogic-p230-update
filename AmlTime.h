#pragma once

#ifdef __cplusplus
extern "C" {
#endif

time_t timeGetTime(void);

time_t get_tick_count(void);

#ifdef _MSC_VER
void usleep(__int64 usec);
#endif

#ifdef __cplusplus
}
#endif
