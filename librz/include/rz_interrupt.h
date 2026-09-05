// TODO: update these
// SPDX-FileCopyrightText: 2024 The Rizin Developers
// SPDX-License-Identifier: LGPL-3.0-only

/*! \file */
#ifndef RZ_INTERRUPT_H
#define RZ_INTERRUPT_H

// TODO: read them docs

#include <stdbool.h>

/*!
 * \brief Interface for handling thread-safe process interruptions.
 *
 * This structure acts as an environment-agnostic cancellation token.
 * It allows lower-level subsystems (like network drivers or analysis loops)
 * to safely check for interrupt requests without depending on a specific UI 
 * implementation (like RzCons CLI or Cutter GUI).
 */
typedef struct rz_interrupt_t {
    void *user; /*! Context pointer (e.g., specific GUI tab state or NULL for CLI) */

    /*! 
     * \brief Checks if an interrupt has been requested.
     * \param user The context pointer.
     * \returns true if the current operation should abort, false otherwise.
     */
    bool (*is_breaked)(void *user);

    /*! 
     * \brief Pushes a callback onto the interrupt handler stack.
     * \param user The context pointer.
     * \param cb The callback function to execute on interrupt.
     * \param data The data to pass to the callback.
     */
    void (*break_push)(void *user, void *cb, void *data);

    /*! 
     * \brief Pops the last callback from the interrupt handler stack.
     * \param user The context pointer.
     */
    void (*break_pop)(void *user);

    // TODO: should these be here??===============
    /*!
     * \brief Called before a blocking operation (like waiting on a lock or socket)
     */
    void *(*sleep_begin)(void *user);

    /*!
     * \brief Called immediately after the blocking operation finishes
     * \param bed The context pointer returned by sleep_begin
     */
    void (*sleep_end)(void *user, void *bed);
    // ============================================

} RzInterrupt;

// TODO: maybe make them RZ_API and put in rz_util
static inline bool rz_interrupt_is_breaked(RzInterrupt *intr) {
	if (intr && intr->is_breaked) {
		return intr->is_breaked(intr->user);
	}
	return false;
}

static inline void rz_interrupt_break_push(RzInterrupt *intr, void *cb, void *data) {
	if (intr && intr->break_push) {
		intr->break_push(intr->user, cb, data);
	}
}

static inline void rz_interrupt_break_pop(RzInterrupt *intr) {
	if (intr && intr->break_pop) {
		intr->break_pop(intr->user);
	}
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

#endif // RZ_INTERRUPT_H
