#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "log.h"

static FILE *log_output = NULL;
static int log_owns_file = 0;

void log_init(FILE *output) {
  if (log_owns_file && log_output != NULL) {
    fclose(log_output);
  }
  log_output = output;
  log_owns_file = 0;
}

void log_set_file(const char *filename) {
  if (log_owns_file && log_output != NULL) {
    fclose(log_output);
  }

  log_output = fopen(filename, "a");
  if (log_output == NULL) {
    fprintf(stderr, "Error: Could not open log file '%s'\n", filename);
    log_output = stdout;
    log_owns_file = 0;
  } else {
    log_owns_file = 1;
  }
}

void log_close(void) {
  if (log_owns_file && log_output != NULL) {
    fclose(log_output);
    log_output = NULL;
    log_owns_file = 0;
  }
}

const char* log_level_string(LogLevel level) {
  switch (level) {
    case LOG_DEBUG:   return "DEBUG";
    case LOG_INFO:    return "INFO";
    case LOG_WARNING: return "WARNING";
    case LOG_ERROR:   return "ERROR";
    default:          return "UNKNOWN";
  }
}

void log_message(LogLevel level, const char *format, ...) {
  if (log_output == NULL) {
    log_output = stdout;
  }

  time_t now;
  time(&now);
  struct tm *local_time = localtime(&now);

  char time_str[64];
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", local_time);

  fprintf(log_output, "[%s] [%s] ", time_str, log_level_string(level));

  va_list args;
  va_start(args, format);
  vfprintf(log_output, format, args);
  va_end(args);

  fprintf(log_output, "\n");
  fflush(log_output);
}
