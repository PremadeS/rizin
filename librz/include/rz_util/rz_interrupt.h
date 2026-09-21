// TODO: update these
// SPDX-FileCopyrightText: 2024 The Rizin Developers
// SPDX-License-Identifier: LGPL-3.0-only

/*! \file */

#ifndef RZ_INTERRUPT_H
#define RZ_INTERRUPT_H

#include <stdbool.h>
#include <rz_types.h>
#include <rz_util/rz_stack.h>

// TODO: add DOCSS

typedef void (*RzInterruptEvent)(void *user);
typedef void *(*RzInterruptSleepBegin)(void *user);
typedef void (*RzInterruptSleepEnd)(void *user, void *bed);

typedef struct rz_interrupt_t {
	bool is_breaked;
	bool hook_signals;
	ut64 timeout;

	RzStack *break_stack;
	RzInterruptEvent current_cb;
	void *current_user;

	void *user;
	RzInterruptSleepBegin sleep_begin;
	RzInterruptSleepEnd sleep_end;
} RzInterrupt;

RZ_API RzInterrupt *rz_interrupt_new(void);
RZ_API void rz_interrupt_free(RzInterrupt *intr);
RZ_API void rz_interrupt_raise(RzInterrupt *intr);
RZ_API void rz_interrupt_break_push(RzInterrupt *intr, RzInterruptEvent cb, void *user);
RZ_API void rz_interrupt_break_pop(RzInterrupt *intr);
RZ_API void rz_interrupt_timeout(RzInterrupt *intr, int timeout);

static inline bool rz_interrupt_is_breaked(RzInterrupt *intr) {
	if (!intr) {
		return false;
	}
	return intr->is_breaked;
}

static inline void *rz_interrupt_sleep_begin(RzInterrupt *intr) {
	if (intr && intr->sleep_begin) {
		return intr->sleep_begin(intr->user);
	}
	return NULL;
}

static inline void rz_interrupt_sleep_end(RzInterrupt *intr, void *bed) {
	if (intr && intr->sleep_end) {
		intr->sleep_end(intr->user, bed);
	}
}

#endif