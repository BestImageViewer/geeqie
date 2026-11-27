/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef GQ_LOG_QUEUE_H
#define GQ_LOG_QUEUE_H

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "logwindow.h"

/** @brief Bounded producer queue, drained only by the main thread. */
class LogQueue
{
public:
	struct Message
	{
		std::string text;
		LogType type;
	};

	struct Batch
	{
		std::vector<Message> messages;
		std::size_t dropped = 0;
	};

	static constexpr std::size_t max_messages = 1024;
	static constexpr std::size_t max_bytes = 1024 * 1024;
	static constexpr std::size_t max_message_bytes = 64 * 1024;

	/** @return True only when the caller must schedule a drain callback. */
	bool push(const char *text, LogType type)
	{
		const std::lock_guard<std::mutex> lock(mutex);
		if (closed) return false;
		const std::size_t length = std::char_traits<char>::length(text);
		if (messages.size() >= max_messages || length > max_message_bytes || length > max_bytes - bytes)
			{
			++dropped;
			}
		else
			{
			messages.push_back({text, type});
			bytes += length;
			}
		return !std::exchange(scheduled, true);
	}

	Batch take_batch()
	{
		const std::lock_guard<std::mutex> lock(mutex);
		Batch batch;
		batch.dropped = std::exchange(dropped, 0);
		std::size_t batch_bytes = 0;
		while (!messages.empty() && batch.messages.size() < 64)
			{
			if (messages.front().text.size() > max_message_bytes - batch_bytes) break;
			batch_bytes += messages.front().text.size();
			bytes -= messages.front().text.size();
			batch.messages.push_back(std::move(messages.front()));
			messages.pop_front();
			}
		return batch;
	}

	/** @brief Call after displaying a batch; producers cannot lose a wakeup. */
	bool finish_batch()
	{
		const std::lock_guard<std::mutex> lock(mutex);
		scheduled = !closed && (!messages.empty() || dropped != 0);
		return scheduled;
	}

	/** @brief Stop accepting messages and release pending window output. */
	void close()
	{
		const std::lock_guard<std::mutex> lock(mutex);
		closed = true;
		messages.clear();
		bytes = 0;
		dropped = 0;
	}

private:
	std::mutex mutex;
	std::deque<Message> messages;
	std::size_t bytes = 0;
	std::size_t dropped = 0;
	bool scheduled = false;
	bool closed = false;
};

void log_queue_shutdown();

#endif
