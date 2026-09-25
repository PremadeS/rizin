// SPDX-FileCopyrightText: 2024 The Rizin Developers
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_util/rz_interrupt.h>
#include <rz_util/rz_alloc.h>
#include <rz_util/rz_sys.h>
#include <rz_util/rz_time.h>
#include <rz_util/rz_assert.h>
#include <rz_util/rz_stack.h>
#include <stdlib.h>

#if __UNIX__
#include <signal.h>
#endif

// Portable atomic counter. Swap this for rizin's own atomic wrapper
// (e.g. from rz_th.h) if one already exists in the codebase.
#include <stdatomic.h>

typedef struct rz_interrupt_frame_t {
	RzInterruptEvent cb;
	void *user;
} RzInterruptFrame;

// Two process-wide values. Neither one stores WHICH RzInterrupt anything
// belongs to -- that's the part of the old design that broke with
// multiple cores. They only ever answer:
//   g_sigint_flag  - "has SIGINT happened since the last time nobody cared"
//   g_hook_count   - "how many live cores currently want SIGINT hooked"
static volatile sig_atomic_t g_sigint_flag = 0;
static atomic_int g_hook_count = 0;

#if __UNIX__
// Runs in real OS signal context: arbitrary thread, arbitrary point in
// execution, extremely restricted rules (async-signal-safe functions
// only). This is why it does the absolute minimum -- one flag write,
// nothing else. It doesn't know or route to any specific RzInterrupt;
// the kernel gives it no way to know which one the user "meant".
static void interrupt_signal_handler(int sig) {
	(void)sig;
	g_sigint_flag = 1;
}

// TODOe: verify this
// Only the transition 0 -> 1 actually touches the OS: the first core
// to want SIGINT hooked installs the real handler. Any core that hooks
// while others are already hooked just bumps the counter.
static void rz_interrupt_hook(void) {
	if (atomic_fetch_add(&g_hook_count, 1) == 0) {
		g_sigint_flag = 0;
		rz_sys_signal(SIGINT, interrupt_signal_handler);
	}
}

// Only the transition 1 -> 0 touches the OS: the last core to stop
// caring uninstalls the handler. Any other core popping while others
// are still hooked just decrements the counter and leaves the OS state
// alone -- this is exactly what stops one core's pop from silently
// cutting off another core's ability to ever see Ctrl-C.
static void rz_interrupt_unhook(void) {
	if (atomic_fetch_sub(&g_hook_count, 1) == 1) {
		rz_sys_signal(SIGINT, SIG_IGN);
		g_sigint_flag = 0;
	}
}
#endif

typedef struct {
	bool breaked;
	RzInterruptEvent event_interrupt;
	void *event_interrupt_data;
} RzInterruptBreakStack;

static void break_stack_free(void *ptr) {
	RzInterruptBreakStack *b = (RzInterruptBreakStack *)ptr;
	free(b);
}

RZ_API RzInterrupt *rz_interrupt_new(void) {
	RzInterrupt *intr = RZ_NEW0(RzInterrupt);
	if (!intr) {
		return NULL;
	}
	intr->break_stack = rz_stack_newf(6, break_stack_free);
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
#if __UNIX__
	// Safety net: if this instance is freed while still mid-region
	// (unbalanced push, early teardown, etc.), make sure the shared
	// hook count doesn't stay permanently inflated because of it.
	if (intr->hook_signals && !rz_stack_is_empty(intr->break_stack)) {
		rz_interrupt_unhook();
	}
#endif
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
#if __UNIX__
		if (intr->hook_signals) {
			rz_interrupt_hook();
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
			rz_interrupt_unhook();
		}
#endif
		intr->is_breaked = false;
	}
}

RZ_API void rz_interrupt_timeout(RzInterrupt *intr, int timeout) {
	if (!intr) {
		return;
	}
	intr->timeout = (timeout && !intr->timeout) ? rz_time_now_mono() + ((ut64)timeout << 20) : 0;
}

RZ_API void rz_interrupt_break_clear(RzInterrupt *intr) {
	rz_return_if_fail(intr);
	intr->is_breaked = false;
}

RZ_API bool rz_interrupt_is_breaked(RzInterrupt *intr) {
	/*return*/ false; // TODO: change
	if (!intr) {
		return false;
	}

	// Pull, not push: this core notices a process-wide SIGINT on its own
	// poll, rather than the signal handler trying to reach into it.
	// The flag is only ever cleared at hook (0->1) / unhook (1->0), so
	// every core that polls between those two points is guaranteed to
	// see it -- not just whichever core happens to poll first.
	if (intr->hook_signals && g_sigint_flag) {
		intr->is_breaked = true;
	}

	if (intr->current_cb) {
		intr->current_cb(intr->current_user);
	}

	if (intr->timeout > 0) {
		if (rz_time_now_mono() > intr->timeout) {
			intr->is_breaked = true;
			intr->timeout = 0;
		}
	}

	return intr->is_breaked;
}

RZ_API void rz_interrupt_break_timeout(RzInterrupt *intr, int timeout) {
	intr->timeout = (timeout && !intr->timeout)
		? rz_time_now_mono() + ((ut64)timeout << 20)
		: 0;
}