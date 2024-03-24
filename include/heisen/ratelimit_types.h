/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _HEISEN_RATELIMIT_TYPES_H
#define _HEISEN_RATELIMIT_TYPES_H

#include <heisen/bits.h>
#include <heisen/param.h>
#include <heisen/spinlock_types_raw.h>

#define DEFAULT_RATELIMIT_INTERVAL	(5 * HZ)
#define DEFAULT_RATELIMIT_BURST		10

/* issue num suppressed message on exit */
#define RATELIMIT_MSG_ON_RELEASE	BIT(0)

struct ratelimit_state {
	raw_spinlock_t	lock;		/* protect the state */

	int		interval;
	int		burst;
	int		printed;
	int		missed;
	unsigned long	begin;
	unsigned long	flags;
};

#define RATELIMIT_STATE_INIT_FLAGS(name, interval_init, burst_init, flags_init) { \
		.lock		= __RAW_SPIN_LOCK_UNLOCKED(name.lock),		  \
		.interval	= interval_init,				  \
		.burst		= burst_init,					  \
		.flags		= flags_init,					  \
	}

#define RATELIMIT_STATE_INIT(name, interval_init, burst_init) \
	RATELIMIT_STATE_INIT_FLAGS(name, interval_init, burst_init, 0)

#define RATELIMIT_STATE_INIT_DISABLED					\
	RATELIMIT_STATE_INIT(ratelimit_state, 0, DEFAULT_RATELIMIT_BURST)

#define DEFINE_RATELIMIT_STATE(name, interval_init, burst_init)		\
									\
	struct ratelimit_state name =					\
		RATELIMIT_STATE_INIT(name, interval_init, burst_init)	\

extern int ___ratelimit(struct ratelimit_state *rs, const char *func);
#define __ratelimit(state) ___ratelimit(state, __func__)

#endif /* _HEISEN_RATELIMIT_TYPES_H */