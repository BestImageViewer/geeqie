/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "action-history.h"

#include <unistd.h>

#include <array>
#include <atomic>
#include <limits>

#include <glib.h>

namespace
{

constexpr unsigned capacity = 50;
constexpr size_t line_size = 192;
constexpr char heading[] = "\n[Recent action requests]\nSeconds since first recorded action; oldest first. Parameters are omitted.\n"; // NOTRANSLATE: Diagnostic heading.

struct Entry
{
	std::atomic<unsigned> sequence{0};
	std::array<std::atomic<unsigned char>, line_size> text{};
};

static_assert(std::atomic<unsigned>::is_always_lock_free && std::atomic<unsigned char>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
std::array<Entry, capacity> entries;
std::atomic<unsigned> published{0};
std::atomic<bool> frozen{false};

size_t copy_entry(unsigned sequence, char (&line)[line_size])
{
	const auto &entry = entries[(sequence - 1) % capacity];
	if (entry.sequence.load() != sequence) return 0;
	size_t length = 0;
	while (length < line_size - 1)
		{
		line[length] = entry.text[length].load();
		if (!line[length]) break;
		++length;
		}
	line[length] = '\0';
	// A writer can wrap onto this slot while another thread handles a signal.
	// Atomic bytes avoid data races; the sequence rejects an interrupted copy.
	return entry.sequence.load() == sequence ? length : 0;
}

void write_all(int fd, const char *text, size_t length)
{
	while (length > 0)
		{
		const auto written = write(fd, text, length);
		if (written <= 0) return;
		text += written;
		length -= written;
		}
}

} // namespace

void action_history_record(const char *identifier)
{
	if (!identifier || frozen.load()) return;
	// Detailed names can include targets. Never copy the target, even if it looks safe.
	char name[line_size - 40]{};
	size_t length = 0;
	while (identifier[length] && identifier[length] != '(')
		{
		const unsigned char character = identifier[length];
		if (length >= sizeof(name) - 1 ||
		    (!g_ascii_isalnum(character) && character != '.' && character != '-' && character != '_')) return;
		name[length++] = character;
		}
	if (length == 0) return;

	static const gint64 started = g_get_monotonic_time();
	const gint64 milliseconds = (g_get_monotonic_time() - started) / 1000;
	char line[line_size];
	g_snprintf(line, sizeof(line), "%lld.%03lld %s\n", // NOTRANSLATE: Elapsed timestamp and action identifier.
	           static_cast<long long>(milliseconds / 1000), static_cast<long long>(milliseconds % 1000), name);
	unsigned sequence = published.load();
	if (sequence == std::numeric_limits<unsigned>::max())
		{
		for (auto &entry : entries) entry.sequence.store(0);
		published.store(0);
		sequence = 0;
		}
	++sequence;
	auto &entry = entries[(sequence - 1) % capacity];
	entry.sequence.store(0);
	for (size_t index = 0;; ++index)
		{
		entry.text[index].store(static_cast<unsigned char>(line[index]));
		if (!line[index]) break;
		}
	entry.sequence.store(sequence);
	published.store(sequence);
}

std::string action_history_report()
{
	std::string report = heading;
	const unsigned end = published.load();
	const unsigned count = end < capacity ? end : capacity;
	for (unsigned index = 0; index < count; ++index)
		{
		char line[line_size];
		const size_t length = copy_entry(end - count + index + 1, line);
		report.append(line, length);
		}
	return report;
}

void action_history_freeze()
{
	frozen.store(true);
}

void action_history_write(int fd)
{
	if (fd < 0) return;
	write_all(fd, heading, sizeof(heading) - 1);
	const unsigned end = published.load();
	const unsigned count = end < capacity ? end : capacity;
	for (unsigned index = 0; index < count; ++index)
		{
		char line[line_size];
		const size_t length = copy_entry(end - count + index + 1, line);
		write_all(fd, line, length);
		}
}
