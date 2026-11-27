/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef GQ_DEBUG_OPERATION_H
#define GQ_DEBUG_OPERATION_H

#include "debug.h"

/** @brief Terminal result of an operation, independent of message severity. */
enum class DebugOperationResult
{
	COMPLETED,
	CANCELLED,
	FAILED
};

/** @brief Zero-initialized operation state; its owner synchronizes access.
 * This is plain data suitable for GObject and g_new0() allocations. The name
 * passed to begin() is borrowed and must outlive the operation (use a literal).
 */
struct DebugOperation
{
#ifdef DEBUG
	guint64 id;
	gint64 started;
	gint64 finished;
	DebugCategory category;
	gint level;
	const gchar *name;
	DebugOperationResult result;
	bool active;
#endif
};

#ifdef DEBUG
/** @return False if already active; a deferred start keeps its existing ID. */
bool debug_operation_begin(DebugOperation &operation, DebugCategory category, gint level, const gchar *name);
/** @brief End once, even when logging is disabled. @return Whether to log. */
bool debug_operation_finish(DebugOperation &operation, DebugOperationResult result);
void debug_operation_log(const DebugOperation &operation, const gchar *file, int line, const gchar *function,
                         const gchar *format, ...) G_GNUC_PRINTF(5, 6);

/** @brief Track every operation, formatting details only when enabled. */
#define DEBUG_OPERATION_START(operation, operation_category, operation_level, operation_name, ...) \
	G_STMT_START \
		{ \
		auto &gq_operation = (operation); \
		if (debug_operation_begin(gq_operation, operation_category, operation_level, operation_name) && \
		    debug_enabled(gq_operation.category, gq_operation.level)) \
			{ \
			debug_operation_log(gq_operation, __FILE__, __LINE__, __func__, __VA_ARGS__); \
			} \
		} \
	G_STMT_END

#define DEBUG_OPERATION_FINISH(operation, result, ...) \
	G_STMT_START \
		{ \
		auto &gq_operation = (operation); \
		if (debug_operation_finish(gq_operation, result)) \
			{ \
			debug_operation_log(gq_operation, __FILE__, __LINE__, __func__, __VA_ARGS__); \
			} \
		} \
	G_STMT_END
#else
#define DEBUG_OPERATION_START(operation, operation_category, operation_level, operation_name, ...) G_STMT_START { } G_STMT_END
#define DEBUG_OPERATION_FINISH(operation, result, ...) G_STMT_START { } G_STMT_END
#endif

#endif
