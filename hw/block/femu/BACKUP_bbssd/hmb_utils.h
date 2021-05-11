#ifndef __SSD__HMB_UTILS_H__
#define __SSD__HMB_UTILS_H__

#include <time.h> /* for clock_gettime() */
#include <stdint.h> /* for data types such as uint64_t */
#include <sys/types.h>

#include "hmb_types.h" /* for HmbTime */

uint64_t hmb_timeDbg_time        (void);
void     hmb_timeDbg_record (HmbTime *t, bool is_start);
uint64_t hmb_timeDbg_get_time_ns (HmbTime *t);
double   hmb_timeDbg_get_time_s  (HmbTime *t);

#endif /* __SSD__HMB_UTILS_H__ */

