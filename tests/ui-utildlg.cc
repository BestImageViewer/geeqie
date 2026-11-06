/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <gtk/gtk.h>

#include "gtest/gtest.h"

#include "options.h"
#include "ui-utildlg.h"

namespace
{

TEST(FolderContentsDialog, CheckboxSkipAndCancel)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	GApplication *previous_app = g_application_get_default();
	g_autoptr(GtkApplication) app = gtk_application_new("org.geeqie.FolderContentsTest", G_APPLICATION_NON_UNIQUE);
	ASSERT_TRUE(g_application_register(G_APPLICATION(app), nullptr, nullptr));
	g_application_set_default(G_APPLICATION(app));
	for (int response = 0; response < 4; ++response)
		{
		int calls = 0;
		bool folders = false;
		bool recursive = false;
		auto *gd = folder_contents_dialog(nullptr, "Folder contents", "Test folders", "Add contents",
			[&](bool include, bool recurse)
			{
			++calls;
			folders = include;
			recursive = recurse;
			});
		GtkWidget *checkbox = gtk_widget_get_last_child(gd->vbox);
		ASSERT_TRUE(GTK_IS_CHECK_BUTTON(checkbox));
		EXPECT_FALSE(gtk_check_button_get_active(GTK_CHECK_BUTTON(checkbox)));
		gtk_check_button_set_active(GTK_CHECK_BUTTON(checkbox), response == 1);
		// Secondary buttons precede Cancel; the default Add button comes last.
		GtkWidget *button = gtk_widget_get_first_child(gd->hbox);
		if (response < 2) button = gtk_widget_get_last_child(gd->hbox);
		if (response == 3) button = gtk_widget_get_next_sibling(button);
		g_signal_emit_by_name(button, "clicked");
		EXPECT_EQ(calls, response == 3 ? 0 : 1);
		if (calls)
			{
			EXPECT_EQ(folders, response < 2);
			EXPECT_EQ(recursive, response == 1);
			}
		}
	g_application_set_default(previous_app);
}

} // namespace
