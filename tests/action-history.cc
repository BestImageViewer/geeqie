/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <unistd.h>

#include <cstdio>
#include <string>

#include <glib.h>

#include "action-history.h"
#include "actions.h"
#include "ui-fileops.h"

namespace
{

TEST(ActionHistory, BoundsHistoryAndOmitsDetailedTargets)
{
	for (int i = 0; i < 60; ++i)
		{
		const auto name = std::string("win.history-test-") + std::to_string(i);
		action_history_record(name.c_str());
		}
	const auto report = action_history_report();
	EXPECT_EQ(report.find("win.history-test-9\n"), std::string::npos);
	EXPECT_NE(report.find("win.history-test-10\n"), std::string::npos);
	EXPECT_LT(report.find("win.history-test-10\n"), report.find("win.history-test-59\n"));
	action_history_record("win.open-file('/home/private-user/secret.jpg')");
	action_history_record("/home/private-user/not-an-action");
	const auto safe = action_history_report();
	EXPECT_NE(safe.find("win.open-file\n"), std::string::npos);
	EXPECT_EQ(safe.find("private-user"), std::string::npos);
	EXPECT_EQ(safe.find("secret.jpg"), std::string::npos);
}

TEST(ActionHistory, SignalWriterMatchesNormalReport)
{
	action_history_record("win.history-writer-test");
	g_autoptr(FILE) file = tmpfile();
	ASSERT_NE(file, nullptr);
	action_history_write(fileno(file));
	ASSERT_EQ(fseek(file, 0, SEEK_END), 0);
	const auto size = ftell(file);
	ASSERT_GT(size, 0);
	ASSERT_EQ(fseek(file, 0, SEEK_SET), 0);
	std::string report(static_cast<size_t>(size), '\0');
	ASSERT_EQ(fread(report.data(), 1, report.size(), file), report.size());
	EXPECT_EQ(report, action_history_report());
}

TEST(ActionHistory, RecordsWidgetActionBeforeCallback)
{
	if (!gtk_init_check()) GTEST_SKIP() << "Requires a display";
	g_autoptr(GtkWidget) widget = GTK_WIDGET(g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0)));
	bool invoked = false;
	const ActionDef actions[] =
		{
		{"history.request", nullptr, nullptr,
		 +[](GSimpleAction *, GVariant *, gpointer data)
			 {
			 EXPECT_NE(action_history_report().find("history.request\n"), std::string::npos);
			 *static_cast<bool *>(data) = true;
			 }, nullptr, ActionStateType::NONE, FALSE, nullptr, 0, nullptr},
		{}
		};
	register_widget_context_shortcuts(widget, "history", actions, &invoked);
	ASSERT_TRUE(gtk_widget_activate_action(widget, "history.request", nullptr));
	EXPECT_TRUE(invoked);
}

} // namespace
