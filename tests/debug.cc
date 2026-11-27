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
#include "filedata.h"
#include "image-load.h"
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
	EXPECT_EQ(evaluations, 0);
}
#endif

} // namespace
