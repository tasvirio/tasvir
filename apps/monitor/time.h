#ifndef _TIME__H_
#define _TIME__H_

#include <stdint.h>

uint64_t gettime_us();
uint64_t gettime_ns();

double get_epoch_time(bool set = false);

#endif // #ifndef _TIME__H_
