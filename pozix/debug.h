#pragma once
#include "runtime.h"
 
BEGIN_C

void get_stack_trace(char* buffer, int count);

#undef assert

#if defined(DEBUG)

#define trace(...) _trace_(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__) 
#define assertion(exp, ...) (void)( (!!(exp)) || _assertion_(#exp, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__) )
#define trace_hexdump(data, bytes, format, ...) _hex_dump_(false, __FILE__, __LINE__, __FUNCTION__, data, bytes, format, ## __VA_ARGS__)
#define trace_hexdump_v(data, bytes, format, vl) _hex_dump_v_(false, __FILE__, __LINE__, __FUNCTION__, data, bytes, format, vl)
#define print_stack_trace() _print_stack_trace_(__FILE__, __LINE__, __FUNCTION__)
#undef assert
#define assert(exp) (void)( (!!(exp)) || _assertion_(#exp, __FILE__, __LINE__, __FUNCTION__, "") )
#else 
#define trace(...)          (void)(0)
#define assertion(exp, ...) (void)(0)
#define print_stack_trace() (void)(0)
#define assert(exp)         (void)(0)
#define trace_hexdump(data, length, format, ...) (void)0

#endif

#define log_hexdump(data, bytes, format, ...) _hex_dump_(true, __FILE__, __LINE__, __FUNCTION__, data, bytes, format, ## __VA_ARGS__)
#define log_hexdump_v(data, bytes, format, vl) _hex_dump_v_(true, __FILE__, __LINE__, __FUNCTION__, data, bytes, format, vl)
#define rtrace(...) _trace_(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__) 
#define rprint(...) _print_(__VA_ARGS__) 

void get_context_stack_trace(const void* context, char* buffer, int count);
void _print_(const char* format, ...);
void _println_(const char* format, ...);
void _trace_(const char* file, int line, const char* function, const char* format, ...);
void _trace_v(const char* file, int line, const char* function, const char* format, va_list vl);
int  _assertion_(const char* e, const char* file, int line, const char* function, const char* format, ...);
void _print_stack_trace_(const char* file, int line, const char* function);
void _hex_dump_v_(bool log, const char* file, int line, const char* func, const void* data, int bytes, const char* format, va_list);
void _hex_dump_(bool log, const char* file, int line, const char* func, const void* data, int bytes, const char* format, ...);

END_C
