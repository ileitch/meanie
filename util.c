#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <pthread.h>

#include "util.h"

static pthread_mutex_t printf_mutex = PTHREAD_MUTEX_INITIALIZER;

void mne_print_duration(struct timeval *end, struct timeval *begin) {
  long int diff = (end->tv_usec + 1000000 * end->tv_sec) - (begin->tv_usec + 1000000 * begin->tv_sec);
  long sec = diff / 1000000;
  long usec = diff % 1000000;
  printf("%ld.%06lds", sec, usec);
}

void mne_printf_async(const char *format, ...) {
  pthread_mutex_lock(&printf_mutex);
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  pthread_mutex_unlock(&printf_mutex);
}

void mne_check_error(const char *name, int err, const char *file, int line) {
  if (err != 0) {
    printf("ERROR(%s:%d): %s returned %d.\n", file, line - 1, name, err);  
    exit(1);
  }
}

int mne_detect_logical_cores() {
  const char *cmd;
#ifdef __linux__
  cmd = "cat /proc/cpuinfo | grep processor | wc -l";
#elif __APPLE__
  cmd = "sysctl hw.ncpu | awk '{print 2}'";
#else
  printf("I don't know how to detect the number of logical cores for this OS!\n");
  exit(1);
#endif  

  FILE *fd = popen(cmd, "r");
  assert(fd != NULL);
  char output[3];
  fgets(output, 3, fd);
  pclose(fd);
  output[2] = 0;
  return atoi(output);
}