/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "print.h"

#include <cstring>

#include <glib/gstdio.h>

#include "gtest/gtest.h"

TEST(PrintProof, FitsCellsAndGapsWithinPrintableArea)
{
	const auto layout = print_proof_layout(540, 720, 144, 144);
	EXPECT_EQ(layout.columns, 3);
	EXPECT_EQ(layout.rows, 4);
	EXPECT_EQ(print_proof_page_count(0, layout), 0);
	EXPECT_EQ(print_proof_page_count(12, layout), 1);
	EXPECT_EQ(print_proof_page_count(13, layout), 2);
	EXPECT_EQ(print_proof_page_count(24, layout), 2);
}

TEST(PrintProof, IncludesExactBoundaryAndReservesPageText)
{
	const auto exact = print_proof_layout(293, 293, 144, 144);
	EXPECT_EQ(exact.columns, 2);
	EXPECT_EQ(exact.rows, 2);
	const auto caption = print_proof_layout(293, 273, 144, 144);
	EXPECT_EQ(caption.columns, 2);
	EXPECT_EQ(caption.rows, 1);
	EXPECT_EQ(print_proof_page_count(3, caption), 2);
}

TEST(PrintProof, OversizedProofStillFitsOneImage)
{
	const auto layout = print_proof_layout(200, 300, 720, 720);
	EXPECT_EQ(layout.columns, 1);
	EXPECT_EQ(layout.rows, 1);
	EXPECT_DOUBLE_EQ(layout.cell_width, 200);
	EXPECT_DOUBLE_EQ(layout.cell_height, 300);
	EXPECT_EQ(print_proof_page_count(3, layout), 3);
}

namespace
{

class PrintSettings : public testing::Test
{
protected:
	void SetUp() override
	{
		directory = g_dir_make_tmp("geeqie-print-settings-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		settings_path = g_build_filename(directory, "print_settings", nullptr);
		setup_path = g_build_filename(directory, "page_setup", nullptr);
	}

	void TearDown() override
	{
		if (settings_path) g_remove(settings_path);
		if (setup_path) g_remove(setup_path);
		if (directory) g_rmdir(directory);
		g_free(settings_path);
		g_free(setup_path);
		g_free(directory);
	}

	gchar *directory = nullptr;
	gchar *settings_path = nullptr;
	gchar *setup_path = nullptr;
};

TEST_F(PrintSettings, MissingFilesUseDefaultsWithoutErrors)
{
	g_autoptr(GError) error = nullptr;
	g_autoptr(GtkPrintSettings) settings = print_settings_load(settings_path, directory, &error);
	ASSERT_NE(settings, nullptr);
	EXPECT_EQ(error, nullptr);
	EXPECT_EQ(gtk_print_settings_get_n_copies(settings), 1);
	g_autoptr(GtkPageSetup) setup = print_page_setup_load(setup_path, &error);
	ASSERT_NE(setup, nullptr);
	EXPECT_EQ(error, nullptr);
	g_autoptr(GtkPageSetup) defaults = gtk_page_setup_new();
	EXPECT_EQ(gtk_page_setup_get_orientation(setup), gtk_page_setup_get_orientation(defaults));
	EXPECT_DOUBLE_EQ(gtk_page_setup_get_paper_width(setup, GTK_UNIT_MM), gtk_page_setup_get_paper_width(defaults, GTK_UNIT_MM));
}

TEST_F(PrintSettings, DefaultPdfUriEscapesSpecialCharacters)
{
	g_autofree gchar *output_directory = g_build_filename(directory, "Pictures with spaces #100% café", nullptr);
	g_autoptr(GError) error = nullptr;
	g_autoptr(GtkPrintSettings) settings = print_settings_load(settings_path, output_directory, &error);
	ASSERT_EQ(error, nullptr);
	const gchar *uri = gtk_print_settings_get(settings, GTK_PRINT_SETTINGS_OUTPUT_URI);
	ASSERT_NE(uri, nullptr);
	EXPECT_TRUE(g_str_has_prefix(uri, "file:///"));
	EXPECT_EQ(strchr(uri, ' '), nullptr);
	EXPECT_EQ(strchr(uri, '#'), nullptr);
	g_autofree gchar *filename = g_filename_from_uri(uri, nullptr, &error);
	ASSERT_EQ(error, nullptr);
	g_autofree gchar *expected = g_build_filename(output_directory, "geeqie-file.pdf", nullptr);
	EXPECT_STREQ(filename, expected);
}

TEST_F(PrintSettings, SavedSettingsAndDestinationArePreserved)
{
	g_autoptr(GtkPrintSettings) saved = gtk_print_settings_new();
	gtk_print_settings_set_printer(saved, "Test printer");
	gtk_print_settings_set_n_copies(saved, 3);
	gtk_print_settings_set(saved, GTK_PRINT_SETTINGS_OUTPUT_URI, "file:///tmp/saved%20output.pdf");
	ASSERT_TRUE(gtk_print_settings_to_file(saved, settings_path, nullptr));
	g_autoptr(GtkPageSetup) saved_setup = gtk_page_setup_new();
	gtk_page_setup_set_orientation(saved_setup, GTK_PAGE_ORIENTATION_LANDSCAPE);
	gtk_page_setup_set_left_margin(saved_setup, 12, GTK_UNIT_MM);
	ASSERT_TRUE(gtk_page_setup_to_file(saved_setup, setup_path, nullptr));
	g_autoptr(GError) error = nullptr;
	g_autoptr(GtkPrintSettings) settings = print_settings_load(settings_path, directory, &error);
	ASSERT_EQ(error, nullptr);
	EXPECT_STREQ(gtk_print_settings_get_printer(settings), "Test printer");
	EXPECT_EQ(gtk_print_settings_get_n_copies(settings), 3);
	EXPECT_STREQ(gtk_print_settings_get(settings, GTK_PRINT_SETTINGS_OUTPUT_URI), "file:///tmp/saved%20output.pdf");
	g_autoptr(GtkPageSetup) setup = print_page_setup_load(setup_path, &error);
	ASSERT_EQ(error, nullptr);
	EXPECT_EQ(gtk_page_setup_get_orientation(setup), GTK_PAGE_ORIENTATION_LANDSCAPE);
	EXPECT_DOUBLE_EQ(gtk_page_setup_get_left_margin(setup, GTK_UNIT_MM), 12);
}

TEST_F(PrintSettings, SavedSettingsWithoutDestinationGetDefaultPdfUri)
{
	g_autoptr(GtkPrintSettings) saved = gtk_print_settings_new();
	gtk_print_settings_set_n_copies(saved, 2);
	ASSERT_TRUE(gtk_print_settings_to_file(saved, settings_path, nullptr));
	g_autoptr(GError) error = nullptr;
	g_autoptr(GtkPrintSettings) settings = print_settings_load(settings_path, directory, &error);
	ASSERT_EQ(error, nullptr);
	EXPECT_EQ(gtk_print_settings_get_n_copies(settings), 2);
	g_autofree gchar *filename = g_filename_from_uri(gtk_print_settings_get(settings, GTK_PRINT_SETTINGS_OUTPUT_URI), nullptr, &error);
	ASSERT_EQ(error, nullptr);
	g_autofree gchar *expected = g_build_filename(directory, "geeqie-file.pdf", nullptr);
	EXPECT_STREQ(filename, expected);
}

TEST_F(PrintSettings, MalformedAndUnreadableFilesStillReportErrors)
{
	for (bool unreadable : {false, true})
		{
		if (unreadable)
			{
			ASSERT_EQ(g_mkdir(settings_path, 0700), 0);
			ASSERT_EQ(g_mkdir(setup_path, 0700), 0);
			}
		else
			{
			ASSERT_TRUE(g_file_set_contents(settings_path, "not a key file\n", -1, nullptr));
			ASSERT_TRUE(g_file_set_contents(setup_path, "not a key file\n", -1, nullptr));
			}
		g_autoptr(GError) error = nullptr;
		g_autoptr(GtkPrintSettings) settings = print_settings_load(settings_path, directory, &error);
		EXPECT_NE(settings, nullptr);
		ASSERT_NE(error, nullptr);
		EXPECT_FALSE(g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT));
		g_clear_error(&error);
		g_autoptr(GtkPageSetup) setup = print_page_setup_load(setup_path, &error);
		EXPECT_NE(setup, nullptr);
		ASSERT_NE(error, nullptr);
		EXPECT_FALSE(g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT));
		ASSERT_EQ(g_remove(settings_path), 0);
		ASSERT_EQ(g_remove(setup_path), 0);
		}
}

} // namespace
