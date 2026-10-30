/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "date-time.h"

#include <cstring>
#include <string>
#include <vector>

#include "options.h"

namespace
{

const gchar *conversion_end(const gchar *start)
{
	const auto *code = start + 1;
	const gchar *allowed = "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%";
	if (*code == 'E')
		{
		++code;
		allowed = "cCxXyY";
		}
	else if (*code == 'O')
		{
		++code;
		allowed = "deHImMSuUVwWy";
		}
	return *code && strchr(allowed, *code) ? code + 1 : nullptr;
}

gchar *format_fields(const std::tm &time, const gchar *format, const gchar *subseconds)
{
	g_autoptr(GString) result = g_string_new(nullptr);
	for (const auto *cursor = format; *cursor;)
		{
		const auto *conversion = strchr(cursor, '%');
		if (!conversion)
			{
			g_string_append(result, cursor);
			break;
			}
		g_string_append_len(result, cursor, conversion - cursor);
		const auto *end = conversion_end(conversion);
		if (!end) return nullptr;
		// Prefix with a space so valid conversions producing empty text still succeed.
		const std::string token = " " + std::string(conversion, end);
		std::vector<char> buffer(128);
		std::size_t length = 0;
		while (!(length = std::strftime(buffer.data(), buffer.size(), token.c_str(), &time)))
			{
			if (buffer.size() >= 65536) return nullptr;
			buffer.resize(buffer.size() * 2);
			}
		g_autofree gchar *value = g_locale_to_utf8(buffer.data() + 1, length - 1, nullptr, nullptr, nullptr);
		if (!value) return nullptr;
		g_string_append(result, value);
		if (subseconds && *subseconds && strchr("cSrTX", end[-1]))
			g_string_append_printf(result, ".%s", subseconds);
		cursor = end;
		}
	return g_string_free(g_steal_pointer(&result), FALSE);
}

} // namespace

bool date_time_format_is_valid(const gchar *format)
{
	if (!format) return true;
	if (!g_utf8_validate(format, -1, nullptr)) return false;
	for (const auto *cursor = format; *cursor; ++cursor)
		{
		if (*cursor != '%') continue;
		const auto *end = conversion_end(cursor);
		if (!end) return false;
		cursor = end - 1;
		}
	return true;
}

gchar *date_time_format(const std::tm &time, const gchar *format, const gchar *subseconds)
{
	if (!format) format = options ? options->date_time_format : nullptr;
	if (format && *format && date_time_format_is_valid(format))
		{
		auto *result = format_fields(time, format, subseconds);
		if (result) return result;
		}
	auto *result = format_fields(time, "%x %X", subseconds);
	return result ? result : g_strdup("");
}

gchar *date_time_format(time_t timestamp, const gchar *format)
{
	std::tm time{};
	if (!localtime_r(&timestamp, &time)) return g_strdup("");
	return date_time_format(time, format);
}
