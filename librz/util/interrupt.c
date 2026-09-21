// TOOD: change this.. does C file require it??
// SPDX-FileCopyrightText: 2024 The Rizin Developers
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_util/rz_interrupt.h>
#include <rz_util/rz_alloc.h>
#include <rz_util/rz_sys.h>
#include <rz_util/rz_time.h>
#include <stdlib.h>

#if __UNIX__
#include <signal.h>
#endif

typedef struct rz_interrupt_frame_t {
	RzInterruptEvent cb;
	void *user;
} RzInterruptFrame;

static RzInterrupt *active_interrupt = NULL;

#if __UNIX__
static void interrupt_signal_handler(int sig) {
	if (active_interrupt) {
		rz_interrupt_raise(active_interrupt);
	}
}
#endif

RZ_API RzInterrupt *rz_interrupt_new(void) {
	RzInterrupt *intr = RZ_NEW0(RzInterrupt);
	if (!intr) {
		return NULL;
	}
	intr->break_stack = rz_stack_newf(6, free);
	if (!intr->break_stack) {
		free(intr);
		return NULL;
	}
	return intr;
}

RZ_API void rz_interrupt_free(RzInterrupt *intr) {
	if (!intr) {
		return;
	}
	if (active_interrupt == intr) {
		active_interrupt = NULL;
	}
	rz_stack_free(intr->break_stack);
	free(intr);
}

RZ_API void rz_interrupt_raise(RzInterrupt *intr) {
	if (!intr) {
		return;
	}
	intr->is_breaked = true;
	if (intr->current_cb) {
		intr->current_cb(intr->current_user);
	}
}

RZ_API void rz_interrupt_break_push(RzInterrupt *intr, RzInterruptEvent cb, void *user) {
	if (!intr || !intr->break_stack) {
		return;
	}

	RzInterruptFrame *frame = RZ_NEW0(RzInterruptFrame);
	if (!frame) {
		return;
	}

	if (rz_stack_is_empty(intr->break_stack)) {
		active_interrupt = intr;
#if __UNIX__
		if (intr->hook_signals) {
			rz_sys_signal(SIGINT, interrupt_signal_handler);
		}
#endif
		intr->is_breaked = false;
	}

	frame->cb = intr->current_cb;
	frame->user = intr->current_user;
	rz_stack_push(intr->break_stack, frame);

	intr->current_cb = cb;
	intr->current_user = user;
}

RZ_API void rz_interrupt_break_pop(RzInterrupt *intr) {
	if (!intr || !intr->break_stack || rz_stack_is_empty(intr->break_stack)) {
		return;
	}

	RzInterruptFrame *frame = (RzInterruptFrame *)rz_stack_pop(intr->break_stack);
	if (frame) {
		intr->current_cb = frame->cb;
		intr->current_user = frame->user;
		free(frame);
	}

	if (rz_stack_is_empty(intr->break_stack)) {
#if __UNIX__
		if (intr->hook_signals) {
			rz_sys_signal(SIGINT, SIG_IGN);
		}
#endif
		intr->is_breaked = false;
		if (active_interrupt == intr) {
			active_interrupt = NULL;
		}
	}
}

RZ_API void rz_interrupt_timeout(RzInterrupt *intr, int timeout) {
	if (!intr) {
		return;
	}
	intr->timeout = (timeout && !intr->timeout) ? rz_time_now_mono() + ((ut64)timeout << 20) : 0;
}