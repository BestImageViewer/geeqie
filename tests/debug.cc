/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <initializer_list>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "debug.h"
#include "debug-operation.h"
#include "filedata.h"
#include "image-load.h"
#include "log-queue.h"
#include "log.h"
#include "logwindow.h"
#include "options.h"

namespace
{

class ScopedLogOutput
{
public:
	ScopedLogOutput()
		: previous(command_line)
	{
		command_line = &settings;
	}

	~ScopedLogOutput()
	{
		g_free(settings.log_file);
		command_line = previous;
	}

	ScopedLogOutput(const ScopedLogOutput &) = delete;
	ScopedLogOutput &operator=(const ScopedLogOutput &) = delete;

private:
	CommandLine settings{};
	CommandLine *previous;
};

TEST(LogSeverity, FormattingLabelsEveryLineAndNormalizesTrailingNewlines)
{
	g_autofree gchar *info = format_log_record(LogSeverity::INFO, "ready");
	g_autofree gchar *warning = format_log_record(LogSeverity::WARNING, "first\nsecond\n\n");
	g_autofree gchar *error = format_log_record(LogSeverity::ERROR, "unable to continue");
	EXPECT_STREQ(info, "[info] ready\n");
	EXPECT_STREQ(warning, "[warning] first\n[warning] second\n");
	EXPECT_STREQ(error, "[error] unable to continue\n");
}

TEST(LogSeverity, EmptyAndUnicodeMessagesAreValid)
{
	g_autofree gchar *empty = format_log_record(LogSeverity::INFO, nullptr);
	g_autofree gchar *unicode = format_log_record(LogSeverity::ERROR, "échec");
	EXPECT_STREQ(empty, "[info] \n");
	EXPECT_STREQ(unicode, "[error] échec\n");
	EXPECT_TRUE(g_utf8_validate(unicode, -1, nullptr));
}

TEST(LogSeverity, WindowStylesFollowSeverityRatherThanMessageText)
{
	EXPECT_EQ(log_severity_type(LogSeverity::INFO), LOG_NORMAL);
	EXPECT_EQ(log_severity_type(LogSeverity::WARNING), LOG_WARN);
	EXPECT_EQ(log_severity_type(LogSeverity::ERROR), LOG_ERROR);
	LogQueue queue;
	queue.push("error in an informational quotation", log_severity_type(LogSeverity::INFO));
	queue.push("disk nearly full", log_severity_type(LogSeverity::WARNING));
	queue.push("cannot read image", log_severity_type(LogSeverity::ERROR));
	const auto batch = queue.take_batch();
	ASSERT_EQ(batch.messages.size(), 3U);
	EXPECT_EQ(batch.messages[0].type, LOG_NORMAL);
	EXPECT_EQ(batch.messages[1].type, LOG_WARN);
	EXPECT_EQ(batch.messages[2].type, LOG_ERROR);
}

TEST(LogSeverity, ExplicitMessagesReachTheConfiguredFile)
{
	ScopedLogOutput output;
	g_autofree gchar *saved_path = g_strdup(command_line->log_file);
	g_autofree gchar *path = nullptr;
	const int fd = g_file_open_tmp("geeqie-log-severity-XXXXXX.txt", &path, nullptr);
	ASSERT_GE(fd, 0);
	close(fd);
	log_set_file(path);
	log_info("ready %d", 42);
	log_warning("disk nearly full: %d", 7);
	log_error("cannot read %s", "image");
	log_set_file(saved_path);
	g_autofree gchar *contents = nullptr;
	const bool read = g_file_get_contents(path, &contents, nullptr, nullptr);
	g_unlink(path);
	ASSERT_TRUE(read);
	EXPECT_NE(std::string(contents).find("[info] ready 42\n"), std::string::npos);
	EXPECT_NE(std::string(contents).find("[warning] disk nearly full: 7\n"), std::string::npos);
	EXPECT_NE(std::string(contents).find("[error] cannot read image\n"), std::string::npos);
}

TEST(LogQueue, BatchesPreserveOrderAndScheduleOnlyOneCallback)
{
	LogQueue queue;
	for (int i = 0; i < 100; ++i)
		{
		EXPECT_EQ(queue.push(std::to_string(i).c_str(), i % 2 == 0 ? LOG_NORMAL : LOG_MSG), i == 0);
		}
	int received = 0;
	do
		{
		const auto batch = queue.take_batch();
		EXPECT_LE(batch.messages.size(), 64U);
		EXPECT_EQ(batch.dropped, 0U);
		for (const auto &message : batch.messages)
			{
			EXPECT_EQ(message.text, std::to_string(received));
			EXPECT_EQ(message.type, received % 2 == 0 ? LOG_NORMAL : LOG_MSG);
			++received;
			}
		} while (queue.finish_batch());
	EXPECT_EQ(received, 100);
	EXPECT_TRUE(queue.push("new callback", LOG_MSG));
}

TEST(LogQueue, CountOverflowIsReportedOnce)
{
	LogQueue queue;
	for (std::size_t i = 0; i < LogQueue::max_messages + 17; ++i)
		{
		queue.push("message", LOG_MSG);
		}
	auto batch = queue.take_batch();
	EXPECT_EQ(batch.dropped, 17U);
	std::size_t received = batch.messages.size();
	while (queue.finish_batch())
		{
		batch = queue.take_batch();
		EXPECT_EQ(batch.dropped, 0U);
		received += batch.messages.size();
		}
	EXPECT_EQ(received, LogQueue::max_messages);
}

TEST(LogQueue, ByteOverflowAndOversizedMessagesAreReported)
{
	LogQueue queue;
	const std::string text(LogQueue::max_message_bytes, 'x');
	const auto capacity = LogQueue::max_bytes / text.size();
	for (std::size_t i = 0; i < capacity + 3; ++i) queue.push(text.c_str(), LOG_MSG);
	const std::string oversized(LogQueue::max_message_bytes + 1, 'x');
	queue.push(oversized.c_str(), LOG_MSG);
	auto batch = queue.take_batch();
	EXPECT_EQ(batch.dropped, 4U);
	std::size_t received = batch.messages.size();
	while (queue.finish_batch()) received += queue.take_batch().messages.size();
	EXPECT_EQ(received, capacity);
}

TEST(LogQueue, ProducersDuringDeliveryDoNotLoseWakeups)
{
	LogQueue queue;
	EXPECT_TRUE(queue.push("first", LOG_MSG));
	EXPECT_EQ(queue.take_batch().messages.size(), 1U);
	EXPECT_FALSE(queue.push("during delivery", LOG_MSG));
	EXPECT_TRUE(queue.finish_batch());
	EXPECT_EQ(queue.take_batch().messages.front().text, "during delivery");
	EXPECT_FALSE(queue.finish_batch());
	EXPECT_TRUE(queue.push("after delivery", LOG_MSG));
}

TEST(LogQueue, ConcurrentProducersAndConsumerAccountForEveryMessage)
{
	LogQueue queue;
	constexpr int producer_count = 4;
	constexpr int per_producer = 2000;
	std::atomic<int> producers{producer_count};
	std::vector<std::thread> workers;
	for (int i = 0; i < producer_count; ++i)
		{
		workers.emplace_back([&]()
			{
			for (int j = 0; j < per_producer; ++j) queue.push("message", LOG_MSG);
			--producers;
			});
		}
	std::size_t received = 0;
	std::size_t dropped = 0;
	do
		{
		const auto batch = queue.take_batch();
		received += batch.messages.size();
		dropped += batch.dropped;
		queue.finish_batch();
		} while (producers.load() != 0);
	for (auto &worker : workers) worker.join();
	do
		{
		const auto batch = queue.take_batch();
		received += batch.messages.size();
		dropped += batch.dropped;
		} while (queue.finish_batch());
	EXPECT_EQ(received + dropped, producer_count * per_producer);
}

TEST(LogQueue, ShutdownDiscardsPendingOutputAndRejectsNewMessages)
{
	LogQueue queue;
	queue.push("pending", LOG_MSG);
	queue.close();
	EXPECT_FALSE(queue.push("after shutdown", LOG_MSG));
	EXPECT_TRUE(queue.take_batch().messages.empty());
	EXPECT_EQ(queue.take_batch().dropped, 0U);
	EXPECT_FALSE(queue.finish_batch());
	queue.close();
}

TEST(LogHistory, MultilineMessagesAreTrimmedAfterInsertion)
{
	g_autoptr(GtkTextBuffer) buffer = gtk_text_buffer_new(nullptr);
	gtk_text_buffer_set_text(buffer, "one\ntwo\nthree\nfour\n", -1);
	log_window_trim_buffer(buffer, 3);
	EXPECT_EQ(gtk_text_buffer_get_line_count(buffer), 3);
	GtkTextIter start;
	GtkTextIter end;
	gtk_text_buffer_get_bounds(buffer, &start, &end);
	g_autofree gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, false);
	EXPECT_STREQ(text, "three\nfour\n");
}

TEST(LogHistory, LongLinesAreBoundedWithoutBreakingUtf8)
{
	g_autoptr(GtkTextBuffer) buffer = gtk_text_buffer_new(nullptr);
	std::string text;
	for (int i = 0; i < 300000; ++i) text += "é";
	gtk_text_buffer_set_text(buffer, text.c_str(), -1);
	log_window_trim_buffer(buffer, 1000);
	EXPECT_EQ(gtk_text_buffer_get_char_count(buffer), 262144);
	GtkTextIter start;
	GtkTextIter end;
	gtk_text_buffer_get_bounds(buffer, &start, &end);
	g_autofree gchar *result = gtk_text_buffer_get_text(buffer, &start, &end, false);
	EXPECT_TRUE(g_utf8_validate(result, -1, nullptr));
	EXPECT_EQ(g_utf8_strlen(result, -1), 262144);
}

TEST(LogHistory, NonpositiveLineLimitUsesSafetyLimit)
{
	g_autoptr(GtkTextBuffer) buffer = gtk_text_buffer_new(nullptr);
	const std::string text(12000, '\n');
	gtk_text_buffer_set_text(buffer, text.c_str(), -1);
	log_window_trim_buffer(buffer, 0);
	EXPECT_EQ(gtk_text_buffer_get_line_count(buffer), 10000);
}

#ifdef DEBUG
class DebugDomains : public testing::Test
{
protected:
	void SetUp() override
	{
		saved_level = get_debug_level();
		saved_domains = get_debug_domains();
		saved_source = get_debug_source();
		saved_timing = get_debug_timing();
		set_debug_source(false);
		set_debug_timing(false);
		set_debug_level(4);
		ASSERT_TRUE(set_debug_domains("all"));
	}

	void TearDown() override
	{
		set_debug_level(saved_level);
		set_debug_domains(saved_domains);
		g_free(saved_domains);
		set_debug_source(saved_source);
		set_debug_timing(saved_timing);
	}

	static std::string record(gint level = 2, const gchar *message = "message")
	{
		g_autofree gchar *text = format_debug_record(DebugCategory::CACHE, level, "sample.cc", 17, "sample", message);
		return text;
	}

	static std::string thread_id(const std::string &text)
	{
		std::smatch match;
		EXPECT_TRUE(std::regex_search(text, match, std::regex{R"(\[thread:([0-9]+)\])"}));
		return match.str(1);
	}

	gint saved_level = 0;
	gchar *saved_domains = nullptr;
	bool saved_source = false;
	bool saved_timing = false;
};

TEST_F(DebugDomains, OperationLifecycleRecordsKeepTheirIdAndEndOnce)
{
	static_assert(std::is_trivial<DebugOperation>::value, "Operation state must support zeroed C allocations");
	ScopedLogOutput output;
	g_autofree gchar *path = nullptr;
	const int fd = g_file_open_tmp("geeqie-operation-XXXXXX.txt", &path, nullptr);
	ASSERT_GE(fd, 0);
	close(fd);
	log_set_file(path);
	DebugOperation operation{};
	DEBUG_OPERATION_START(operation, DebugCategory::IMAGE, 3, "Test operation", "files=%d", 2);
	const auto id = operation.id;
	EXPECT_GT(id, 0U);
	DEBUG_OPERATION_FINISH(operation, DebugOperationResult::COMPLETED, "files=%d", 2);
	const auto finished = operation.finished;
	EXPECT_GE(finished, operation.started);
	EXPECT_FALSE(operation.active);
	DEBUG_OPERATION_FINISH(operation, DebugOperationResult::FAILED, "unexpected duplicate result");
	EXPECT_EQ(operation.finished, finished);
	log_set_file(nullptr);
	g_autofree gchar *contents = nullptr;
	const bool read = g_file_get_contents(path, &contents, nullptr, nullptr);
	g_unlink(path);
	ASSERT_TRUE(read);
	const std::string text = contents;
	const std::string prefix = "[operation:" + std::to_string(id) + "] Test operation ";
	EXPECT_NE(text.find(prefix + "started:"), std::string::npos);
	EXPECT_NE(text.find(prefix + "completed:"), std::string::npos);
	EXPECT_NE(text.find("elapsed="), std::string::npos);
	EXPECT_EQ(text.find("unexpected duplicate result"), std::string::npos);
}

TEST_F(DebugDomains, DeferredImageCancellationReportsItsOperationId)
{
	if (!options) options = conf_options_new();
	ScopedLogOutput output;
	g_autofree gchar *path = nullptr;
	const int fd = g_file_open_tmp("geeqie-operation-cancel-XXXXXX.txt", &path, nullptr);
	ASSERT_GE(fd, 0);
	close(fd);
	log_set_file(path);
	auto *source = file_data_new_simple("/nonexistent/geeqie-operation-pending.jpg");
	auto *loader = image_loader_new(source);
	file_data_unref(source);
	const bool queued = image_loader_start_deferred(loader);
	const auto id = loader->debug_operation.id;
	g_object_unref(loader);
	log_set_file(nullptr);
	g_autofree gchar *contents = nullptr;
	const bool read = g_file_get_contents(path, &contents, nullptr, nullptr);
	g_unlink(path);
	ASSERT_TRUE(queued);
	ASSERT_TRUE(read);
	const std::string text = contents;
	const std::string prefix = "[operation:" + std::to_string(id) + "] Image load ";
	EXPECT_NE(text.find(prefix + "started:"), std::string::npos);
	EXPECT_NE(text.find(prefix + "cancelled:"), std::string::npos);
	EXPECT_EQ(text.find(prefix + "completed:"), std::string::npos);
	EXPECT_EQ(text.find(prefix + "failed:"), std::string::npos);
}

TEST_F(DebugDomains, DirectoryScanFailuresArePairedAndPreserveErrno)
{
	if (!options) options = conf_options_new();
	set_debug_domains("files");
	ScopedLogOutput output;
	g_autofree gchar *path = nullptr;
	const int fd = g_file_open_tmp("geeqie-operation-scan-XXXXXX.txt", &path, nullptr);
	ASSERT_GE(fd, 0);
	close(fd);
	log_set_file(path);
	g_autofree gchar *missing = g_strconcat(path, "-missing", nullptr);
	auto *source = file_data_new_simple(missing);
	g_autoptr(FileDataList) files = nullptr;
	const bool scanned = FileData::FileList::read_list_lstat(source, &files, nullptr);
	const int scan_errno = errno;
	const bool scanned_all = FileData::FileList::read_list_lstat_all(source, &files, nullptr);
	const int all_errno = errno;
	file_data_unref(source);
	log_set_file(nullptr);
	g_autofree gchar *contents = nullptr;
	const bool read = g_file_get_contents(path, &contents, nullptr, nullptr);
	g_unlink(path);
	EXPECT_FALSE(scanned);
	EXPECT_FALSE(scanned_all);
	EXPECT_EQ(scan_errno, ENOENT);
	EXPECT_EQ(all_errno, ENOENT);
	ASSERT_TRUE(read);
	const std::string text = contents;
	const std::regex start{R"(\[operation:([0-9]+)\] Directory scan started:)"};
	int starts = 0;
	std::set<std::string> ids;
	for (auto it = std::sregex_iterator(text.begin(), text.end(), start); it != std::sregex_iterator(); ++it)
		{
		++starts;
		ids.insert((*it)[1].str());
		EXPECT_NE(text.find("[operation:" + (*it)[1].str() + "] Directory scan failed:"), std::string::npos);
		}
	EXPECT_EQ(starts, 2);
	EXPECT_EQ(ids.size(), 2U);
}

TEST_F(DebugDomains, OperationStateSurvivesFilteringWithoutEvaluatingDetails)
{
	set_debug_domains("none");
	DebugOperation operation{};
	int evaluations = 0;
	DEBUG_OPERATION_START(operation, DebugCategory::FILES, 2, "Test scan", "%d", ++evaluations);
	EXPECT_TRUE(operation.active);
	const auto id = operation.id;
	EXPECT_FALSE(debug_operation_begin(operation, DebugCategory::IMAGE, 3, "Deferred continuation"));
	EXPECT_EQ(operation.id, id);
	DEBUG_OPERATION_FINISH(operation, DebugOperationResult::CANCELLED, "%d", ++evaluations);
	EXPECT_FALSE(operation.active);
	EXPECT_EQ(operation.result, DebugOperationResult::CANCELLED);
	EXPECT_EQ(evaluations, 0);
	DEBUG_OPERATION_START(operation, DebugCategory::FILES, 2, "Restarted scan", "%d", ++evaluations);
	EXPECT_NE(operation.id, id);
	DEBUG_OPERATION_FINISH(operation, DebugOperationResult::COMPLETED, "%d", ++evaluations);
	EXPECT_EQ(evaluations, 0);
}

TEST_F(DebugDomains, OperationFailuresUseLevelOneAndRemainCategoryFiltered)
{
	set_debug_level(1);
	DebugOperation operation{};
	EXPECT_TRUE(debug_operation_begin(operation, DebugCategory::IMAGE, 3, "Image load"));
	EXPECT_TRUE(debug_operation_finish(operation, DebugOperationResult::FAILED));
	EXPECT_FALSE(debug_operation_finish(operation, DebugOperationResult::COMPLETED));
	EXPECT_TRUE(debug_operation_begin(operation, DebugCategory::IMAGE, 3, "Image load"));
	EXPECT_FALSE(debug_operation_finish(operation, DebugOperationResult::COMPLETED));
	EXPECT_TRUE(set_debug_domains("files"));
	EXPECT_TRUE(debug_operation_begin(operation, DebugCategory::IMAGE, 3, "Image load"));
	EXPECT_FALSE(debug_operation_finish(operation, DebugOperationResult::FAILED));
}

TEST_F(DebugDomains, OperationIdsAreUniqueAcrossConcurrentProducers)
{
	constexpr int thread_count = 4;
	constexpr int per_thread = 500;
	std::vector<std::vector<guint64>> results(thread_count);
	std::vector<std::thread> workers;
	for (int i = 0; i < thread_count; ++i)
		{
		workers.emplace_back([&, i]()
			{
			for (int j = 0; j < per_thread; ++j)
				{
				DebugOperation operation{};
				debug_operation_begin(operation, DebugCategory::FILES, 2, "Concurrent scan");
				results[i].push_back(operation.id);
				debug_operation_finish(operation, DebugOperationResult::COMPLETED);
				}
			});
		}
	for (auto &worker : workers) worker.join();
	std::set<guint64> ids;
	for (const auto &result : results) ids.insert(result.begin(), result.end());
	EXPECT_EQ(ids.size(), thread_count * per_thread);
	EXPECT_EQ(ids.count(0), 0U);
}

TEST_F(DebugDomains, WarningsAndErrorsBypassDebugAndRegexFilters)
{
	ScopedLogOutput output;
	g_autofree gchar *saved_path = g_strdup(command_line->log_file);
	g_autofree gchar *saved_regexp = get_regexp();
	g_autofree gchar *path = nullptr;
	const int fd = g_file_open_tmp("geeqie-log-filter-XXXXXX.txt", &path, nullptr);
	ASSERT_GE(fd, 0);
	close(fd);
	log_set_file(path);
	set_debug_level(0);
	set_debug_domains("none");
	set_regexp("THIS_WILL_NOT_MATCH");
	log_info("hidden info");
	DEBUG_LOG(DebugCategory::GENERAL, 1, "hidden debug");
	log_warning("visible warning");
	log_error("visible error");
	set_regexp(saved_regexp);
	log_set_file(saved_path);
	g_autofree gchar *contents = nullptr;
	const bool read = g_file_get_contents(path, &contents, nullptr, nullptr);
	g_unlink(path);
	ASSERT_TRUE(read);
	const std::string text = contents;
	EXPECT_EQ(text.find("hidden"), std::string::npos);
	EXPECT_NE(text.find("[warning] visible warning\n"), std::string::npos);
	EXPECT_NE(text.find("[error] visible error\n"), std::string::npos);
}

TEST_F(DebugDomains, FiltersCategoriesAndRespectsVerbosity)
{
	ASSERT_TRUE(set_debug_domains("thumbnails,cache"));
	set_debug_level(2);
	EXPECT_TRUE(debug_enabled(DebugCategory::THUMBNAILS, 1));
	EXPECT_TRUE(debug_enabled(DebugCategory::CACHE, 2));
	EXPECT_FALSE(debug_enabled(DebugCategory::GENERAL, 1));
	EXPECT_FALSE(debug_enabled(DebugCategory::CACHE, 3));
	EXPECT_FALSE(debug_enabled(DebugCategory::CACHE, 0));
	set_debug_level(0);
	EXPECT_FALSE(debug_enabled(DebugCategory::CACHE, 1));
}

TEST_F(DebugDomains, EverySubsystemCanBeSelectedIndependently)
{
	const struct
	{
		DebugCategory category;
		const char *name;
	} domains[] = {
		{DebugCategory::GENERAL, "general"},
		{DebugCategory::THUMBNAILS, "thumbnails"},
		{DebugCategory::CACHE, "cache"},
		{DebugCategory::IMAGE, "image"},
		{DebugCategory::RENDER, "render"},
		{DebugCategory::METADATA, "metadata"},
		{DebugCategory::FILES, "files"},
		{DebugCategory::UI, "ui"},
		{DebugCategory::DUPLICATES, "duplicates"},
		{DebugCategory::CONFIG, "config"},
		{DebugCategory::COLLECTIONS, "collections"},
		{DebugCategory::EXTERNAL, "external"}
	};
	static_assert(G_N_ELEMENTS(domains) == static_cast<guint>(DebugCategory::COUNT));
	for (const auto &domain : domains)
		{
		SCOPED_TRACE(domain.name);
		ASSERT_TRUE(set_debug_domains(domain.name));
		g_autofree gchar *selected = get_debug_domains();
		EXPECT_STREQ(selected, domain.name);
		for (const auto &other : domains)
			{
			EXPECT_EQ(debug_enabled(other.category, 2), domain.category == other.category);
			}
		g_autofree gchar *text = format_debug_record(domain.category, 2, "test.cc", 1, "test", "message");
		EXPECT_NE(std::string(text).find(std::string("[") + domain.name + "]"), std::string::npos);
		}
	for (const auto &domain : domains)
		{
		ASSERT_TRUE(set_debug_domains("all"));
		EXPECT_TRUE(debug_enabled(domain.category, 4));
		ASSERT_TRUE(set_debug_domains("none"));
		EXPECT_FALSE(debug_enabled(domain.category, 1));
		}
	EXPECT_FALSE(debug_enabled(DebugCategory::COUNT, 1));
}

TEST_F(DebugDomains, NormalizesWhitespaceAndDuplicates)
{
	ASSERT_TRUE(set_debug_domains(" cache, thumbnails,cache "));
	g_autofree gchar *domains = get_debug_domains();
	EXPECT_STREQ(domains, "thumbnails,cache");
	EXPECT_TRUE(debug_enabled(DebugCategory::THUMBNAILS, 4));
	EXPECT_TRUE(debug_enabled(DebugCategory::CACHE, 4));
}

TEST_F(DebugDomains, InvalidInputDoesNotChangeFilter)
{
	ASSERT_TRUE(set_debug_domains("cache"));
	for (const gchar *text : {"", " ", "unknown", "cache,unknown", "cache,", ",cache", "all,cache", "none,cache"})
		{
		SCOPED_TRACE(text);
		EXPECT_FALSE(debug_domains_valid(text));
		EXPECT_FALSE(set_debug_domains(text));
		g_autofree gchar *domains = get_debug_domains();
		EXPECT_STREQ(domains, "cache");
		}
	EXPECT_FALSE(set_debug_domains(nullptr));
	EXPECT_FALSE(debug_enabled(static_cast<DebugCategory>(100), 1));
}

TEST_F(DebugDomains, AllAndNoneAreExplicitAndValidationHasNoSideEffects)
{
	ASSERT_TRUE(set_debug_domains("none"));
	EXPECT_FALSE(debug_enabled(DebugCategory::GENERAL, 1));
	EXPECT_FALSE(debug_enabled(DebugCategory::THUMBNAILS, 1));
	EXPECT_FALSE(debug_enabled(DebugCategory::CACHE, 1));
	EXPECT_TRUE(debug_domains_valid("all"));
	EXPECT_FALSE(debug_enabled(DebugCategory::CACHE, 1));
	ASSERT_TRUE(set_debug_domains("all"));
	EXPECT_TRUE(debug_enabled(DebugCategory::GENERAL, 4));
}

TEST_F(DebugDomains, DisabledMessagesDoNotEvaluateArguments)
{
	int evaluations = 0;
	ASSERT_TRUE(set_debug_domains("none"));
	DEBUG_LOG(DebugCategory::CACHE, 1, "%d", ++evaluations);
	DEBUG_LOG(DebugCategory::GENERAL, 1, "%d", ++evaluations);
	EXPECT_EQ(evaluations, 0);
	ASSERT_TRUE(set_debug_domains("all"));
	set_debug_level(0);
	DEBUG_LOG(DebugCategory::CACHE, 1, "%d", ++evaluations);
	DEBUG_LOG(DebugCategory::GENERAL, 1, "%d", ++evaluations);
	EXPECT_EQ(evaluations, 0);
}

TEST_F(DebugDomains, RecordHasStableFieldsAndMonotonicTime)
{
	const auto first = record();
	EXPECT_TRUE(std::regex_match(first,
	    std::regex{R"(\[\+[0-9]+\.[0-9]{6}\] \[cache\] \[debug:2\] \[pid:[0-9]+\] \[thread:[0-9]+\] message)"}));
	init_debug_timer();
	const auto second = record();
	EXPECT_GE(g_ascii_strtod(second.c_str() + 2, nullptr), g_ascii_strtod(first.c_str() + 2, nullptr));
	EXPECT_EQ(thread_id(first), thread_id(second));
}

TEST_F(DebugDomains, SourceLocationsAreIndependentOfVerbosity)
{
	for (const gint level : {1, 4})
		{
		set_debug_level(level);
		set_debug_source(false);
		EXPECT_EQ(record(level).find("sample.cc"), std::string::npos);
		set_debug_source(true);
		EXPECT_NE(record(level).find("sample.cc:17:sample: message"), std::string::npos);
		}
}

TEST_F(DebugDomains, TimingDeltaIsOptionalAndTimeIsAlwaysPresent)
{
	EXPECT_EQ(record().find("[delta:"), std::string::npos);
	set_debug_timing(true);
	const auto text = record();
	EXPECT_EQ(text.find("[+"), 0U);
	EXPECT_NE(text.find("[delta:"), std::string::npos);
}

TEST_F(DebugDomains, EveryMessageLineHasTheSameRecordContext)
{
	const auto text = record(2, "first\nsecond\n");
	const auto prefix = text.substr(0, text.find("first"));
	EXPECT_EQ(text, prefix + "first\n" + prefix + "second");
}

TEST_F(DebugDomains, ThreadIdentifiersAndRecordTimingDeltasAreIndependent)
{
	const auto time_us = [](const std::string &text, bool delta)
		{
		const std::regex pattern{delta ? R"(\[delta:([0-9]+)\.([0-9]{6})\])" : R"(\[\+([0-9]+)\.([0-9]{6})\])"};
		std::smatch match;
		const bool found = std::regex_search(text, match, pattern);
		EXPECT_TRUE(found);
		return found ? static_cast<gint64>(std::stoll(match[1].str()) * G_USEC_PER_SEC + std::stoll(match[2].str())) : gint64{0};
		};
	set_debug_timing(true);
	const auto main_before = record();
	std::string worker_first;
	std::string worker_second;
	std::thread worker([&]()
		{
		worker_first = record();
		for (int i = 0; i < 100; ++i) record();
		worker_second = record();
		});
	worker.join();
	init_debug_timer();
	const auto main_after = record();
	EXPECT_EQ(thread_id(worker_first), thread_id(worker_second));
	EXPECT_NE(thread_id(main_before), thread_id(worker_first));
	EXPECT_EQ(thread_id(main_before), thread_id(main_after));
	EXPECT_EQ(time_us(worker_first, true), 0);
	EXPECT_EQ(time_us(main_after, true), time_us(main_after, false) - time_us(main_before, false));
}

#else
TEST(DebugDomainsRelease, DisabledMessagesDoNotEvaluateArguments)
{
	int evaluations = 0;
	DEBUG_LOG(DebugCategory::CACHE, 1, "%d", ++evaluations);
	DEBUG_LOG(DebugCategory::GENERAL, 1, "%d", ++evaluations);
	DEBUG_OPERATION_START(unused_operation, DebugCategory::IMAGE, ++evaluations, "Unused", "%d", ++evaluations);
	DEBUG_OPERATION_FINISH(unused_operation, DebugOperationResult::FAILED, "%d", ++evaluations);
	EXPECT_EQ(evaluations, 0);
}
#endif

} // namespace
