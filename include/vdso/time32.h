#ifndef __VDSO_TIME32_H
#define __VDSO_TIME32_H

#include <heisen/types.h>
#include <heisen/typing.h>

typedef s32		old_time32_t;

struct old_timespec32 {
	old_time32_t	tv_sec;
	s32		tv_nsec;
};

struct old_timeval32 {
	old_time32_t	tv_sec;
	s32		tv_usec;
};

#endif /* __VDSO_TIME32_H */