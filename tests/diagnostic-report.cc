/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <string>

#include <glib.h>

#include "diagnostic-report.h"
#include "layout.h"
#include "options.h"
#include "view-dir.h"
#include "view-file.h"

namespace
{

class DiagnosticReport : public testing::Test
{
protected:
	void SetUp() override
	{
		previous = options;
		options = &settings;
	}

	void TearDown() override
	{
		options = previous;
	}

	ConfOptions *previous = nullptr;
	ConfOptions settings{};
	LayoutWindow layout{};
};

TEST_F(DiagnosticReport, IncludesRuntimeViewsAndSafePreferences)
{
	settings.image.read_ahead_images = 7;
	settings.image.read_ahead_until_full = TRUE;
	settings.thumbnails.size.width = 123;
	layout.options.file_view_type = FILEVIEW_ICON;
	layout.options.dir_view_type = DIRVIEW_TREE;
	layout.options.show_thumbnails = TRUE;
	auto report = diagnostic_report_create(&layout);
	EXPECT_NE(report.find("file_view=icons\n"), std::string::npos);
	EXPECT_NE(report.find("directory_view=tree\n"), std::string::npos);
	EXPECT_NE(report.find("image.read_ahead_images=7\n"), std::string::npos);
	EXPECT_NE(report.find("image.read_ahead_until_full=1\n"), std::string::npos);
	EXPECT_NE(report.find("thumbnails.size.width=123\n"), std::string::npos);
	layout.options.file_view_type = FILEVIEW_LIST;
	layout.options.dir_view_type = DIRVIEW_LIST;
	report = diagnostic_report_create(&layout);
	EXPECT_NE(report.find("file_view=list\n"), std::string::npos);
	EXPECT_NE(report.find("directory_view=list\n"), std::string::npos);
}

TEST_F(DiagnosticReport, ExcludesPrivateStrings)
{
	g_autofree gchar *secret = g_strdup("/home/private-person/secret-image-and-command");
	layout.options.id = secret;
	layout.options.home_path = secret;
	layout.options.order = secret;
	settings.file_ops.safe_delete_path = secret;
	settings.thumbnails.cache_dir = secret;
	settings.shell.path = secret;
	settings.shell.options = secret;
	settings.help_search_engine = secret;
	settings.sidecar.ext = secret;
	settings.marks_tooltips[0] = secret;
	settings.disabled_plugins.emplace_back(secret);
	const auto report = diagnostic_report_create(&layout);
	EXPECT_EQ(report.find(secret), std::string::npos);
	EXPECT_EQ(report.find("/home/"), std::string::npos);
}

TEST(DiagnosticDesktop, OnlyReturnsKnownDesktopIdentifiers)
{
	EXPECT_EQ(diagnostic_report_desktop_hint(nullptr), "unknown");
	EXPECT_EQ(diagnostic_report_desktop_hint("/home/private-user/custom-desktop"), "unknown");
	EXPECT_EQ(diagnostic_report_desktop_hint("private-user:GNOME;gnome"), "GNOME");
	EXPECT_EQ(diagnostic_report_desktop_hint("kde:i3"), "KDE, i3");
}

} // namespace
