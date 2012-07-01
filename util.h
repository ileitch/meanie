#ifndef MEANIE_UTIL_H
#define MEANIE_UTIL_H

#include <sys/time.h>

void mne_printf_async(const char *format, ...);
void mne_print_duration(struct timeval*, struct timeval*);
void mne_check_error(const char*, int, const char*, int);
int mne_detect_logical_cores();

#endif