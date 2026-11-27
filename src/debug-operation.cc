/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "debug-operation.h"

#ifdef DEBUG

#include <atomic>
#include <cstdarg>

namespace
{
std::atomic<guint64> next_operation_id{1};

const gchar *operation_status(const DebugOperation &operation)
{
	if (operation.active) return "started";
	switch (operation.result)
		{
		case DebugOperationResult::COMPLETED: return "completed";
		case DebugOperationResult::CANCELLED: return "cancelled";
		case DebugOperationResult::FAILED: return "failed";
		}
	return "unknown";
}

gint operation_level(const DebugOperation &operation)
{
	return !operation.active && operation.result == DebugOperationResult::FAILED ? 1 : operation.level;
}
} // namespace

bool debug_operation_begin(DebugOperation &operation, DebugCategory category, gint level, const gchar *name)
{
	if (operation.active) return false;
	operation.id = next_operation_id.fetch_add(1, std::memory_order_relaxed);
	operation.started = g_get_monotonic_time();
	operation.finished = 0;
	operation.category = category;
	operation.level = level;
	operation.name = name;
	operation.result = DebugOperationResult::COMPLETED;
	operation.active = true;
	return true;
}

bool debug_operation_finish(DebugOperation &operation, DebugOperationResult result)
{
	if (!operation.active) return false;
	operation.finished = g_get_monotonic_time();
	operation.result = result;
	operation.active = false;
	return debug_enabled(operation.category, operation_level(operation));
}

void debug_operation_log(const DebugOperation &operation, const gchar *file, int line, const gchar *function,
                         const gchar *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	g_autofree gchar *details = g_strdup_vprintf(format, arguments);
	va_end(arguments);
	const double elapsed = operation.active ? 0.0 : static_cast<double>(operation.finished - operation.started) / G_USEC_PER_SEC;
	log_debug_category(operation.category, operation_level(operation), file, line, function,
	                   "[operation:%" G_GUINT64_FORMAT "] %s %s: elapsed=%.6f s; %s",
	                   operation.id, operation.name, operation_status(operation), elapsed, details);
}

#endif
