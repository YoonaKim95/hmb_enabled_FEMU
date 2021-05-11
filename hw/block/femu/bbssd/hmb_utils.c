#include "hmb_utils.h"
#include "qemu/osdep.h"
#include "qemu/timer.h"

#include <sys/syscall.h>

HmbDebugTime HMB_DEBUG_TIME;
HmbDebugTimePerRequest HMB_DEBUG_TIME_PER_REQUEST;

uint64_t hmb_timeDbg_time(void)
{
	return qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
}

void hmb_timeDbg_record(HmbTime *t, bool is_start)
{
	uint64_t t_cur = hmb_timeDbg_time();

	if(is_start)
	{
		t->t_prev_ns = t_cur;
	}
	else
	{
		t->t_acc_ns += (t_cur - t->t_prev_ns);
	}
}

uint64_t hmb_timeDbg_get_time_ns(HmbTime *t)
{
	return t->t_acc_ns;
}

double hmb_timeDbg_get_time_s(HmbTime *t)
{
	return ((double)t->t_acc_ns)/1E9;
}

