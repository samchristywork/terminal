#ifndef LOG_H
#define LOG_H

#include <stdio.h>

typedef enum {
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARNING,
  LOG_ERROR
} LogLevel;

void log_init(FILE *output);
void log_set_file(const char *filename);
void log_close(void);
void log_message(LogLevel level, const char *format, ...);

#define LOG_DEBUG_MSG(...) log_message(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO_MSG(...) log_message(LOG_INFO, __VA_ARGS__)
#define LOG_WARNING_MSG(...) log_message(LOG_WARNING, __VA_ARGS__)
#define LOG_ERROR_MSG(...) log_message(LOG_ERROR, __VA_ARGS__)

#endif
