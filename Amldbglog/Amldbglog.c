#include <stdio.h>
#include <stdarg.h>

#include "Amldbglog.h"


FILE *log_fp;

int aml_printf (const char *format, ...) {
  int result;
  va_list args;

  va_start(args, format);
  if (log_fp) {
    result = vfprintf(log_fp, format, args);
  } else {
    result = vprintf(format, args);
  }
  va_end(args);
  return result;
}

int aml_open_logfile (const char *filename) {
  if (filename) {
    log_fp = fopen(filename, "a+");
  }
  return log_fp ? 0 : -1;
}

int aml_close_logfile (void) {
  if (log_fp) {
    int result = fclose(log_fp);
    log_fp = 0;
    return result;
  } else {
    return 0;
  }
}

int aml_init (void) {
  return 0;
}

int aml_uninit (void) {
  return 0;
}
