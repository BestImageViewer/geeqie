/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <memory>
#include <utility>

#include <config.h>
#include <glib/gstdio.h>

#include "color-label.h"
#include "exif.h"
#include "filedata.h"
#include "metadata.h"
#include "options.h"
#include "osd.h"

namespace
{

class ColorLabel : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		saved = options->color_labels;
		saved_timeout = options->metadata.confirm_after_timeout;
		options->metadata.confirm_after_timeout = FALSE;
		options->color_labels = {TRUE, g_strdup("Red"), g_strdup("Yellow"), g_strdup("Green"), g_strdup("Blue"), g_strdup("Purple")};
		directory = g_dir_make_tmp("geeqie-color-label-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "image.jpg", nullptr);
		g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 16, 16);
		gdk_pixbuf_fill(pixels, 0x123456ff);
		ASSERT_TRUE(gdk_pixbuf_save(pixels, path, "jpeg", nullptr, nullptr));
		image = FileData::new_simple(path);
	}

	void TearDown() override
	{
		if (image)
			{
			if (image->modified_xmp) metadata_write_queue_remove(image);
			file_data_free_ci(image);
			}
		image.reset(nullptr);
		g_free(options->color_labels.red);
		g_free(options->color_labels.yellow);
		g_free(options->color_labels.green);
		g_free(options->color_labels.blue);
		g_free(options->color_labels.purple);
		options->color_labels = saved;
		options->metadata.confirm_after_timeout = saved_timeout;
		if (directory)
			{
			for (const gchar *name : {"image.jpg", "image.xmp"})
				{
				g_autofree gchar *file = g_build_filename(directory, name, nullptr);
				g_remove(file);
				}
			g_rmdir(directory);
			}
		g_free(path);
		g_free(directory);
	}

	void save(const gchar *destination)
	{
		ASSERT_TRUE(file_data_add_ci_write_metadata(image));
		g_free(image->change->dest);
		image->change->dest = g_strdup(destination);
		ASSERT_TRUE(metadata_write_perform(image));
		ASSERT_TRUE(metadata_write_queue_remove(image));
		file_data_free_ci(image);
	}

	decltype(options->color_labels) saved{};
	gboolean saved_timeout = FALSE;
	gchar *directory = nullptr;
	gchar *path = nullptr;
	FileDataRef image{nullptr};
};

TEST_F(ColorLabel, MapsConfiguredTextWithoutChangingUnknownLabels)
{
	EXPECT_EQ(color_label_index(nullptr), 0);
	EXPECT_EQ(color_label_index(""), 0);
	EXPECT_EQ(color_label_index("red"), 1);
	EXPECT_EQ(color_label_index("Purple"), 5);
	EXPECT_EQ(color_label_index("Approved"), COLOR_LABEL_OTHER);
	g_free(options->color_labels.green);
	options->color_labels.green = g_strdup("Approved");
	EXPECT_EQ(color_label_index("approved"), 3);
	EXPECT_EQ(color_label_index("Green"), COLOR_LABEL_OTHER);
	ASSERT_TRUE(metadata_write_string(image, COLOR_LABEL_KEY, "Review <later>"));
	EXPECT_STREQ(color_label_read(image), "Review <later>");
	EXPECT_EQ(color_label_index(color_label_read(image)), COLOR_LABEL_OTHER);
}

TEST_F(ColorLabel, OsdFormattedLabelUsesPaletteAndEscapesUnknownText)
{
	const OsdTemplate vars;
	for (int index = 1; index <= COLOR_LABEL_COUNT; ++index)
		{
		ASSERT_TRUE(metadata_write_string(image, COLOR_LABEL_KEY, color_label_name(index)));
		g_autofree gchar *output = image_osd_mkinfo("%formatted.Label%", image, vars);
		g_autofree gchar *expected = g_strdup_printf("<span foreground=\"%s\">●</span>", color_label_color(index));
		EXPECT_STREQ(output, expected);
		g_autofree gchar *plain = nullptr;
		PangoAttrList *attributes = nullptr;
		ASSERT_TRUE(pango_parse_markup(output, -1, 0, &attributes, &plain, nullptr, nullptr));
		EXPECT_STREQ(plain, "●");
		pango_attr_list_unref(attributes);
		}
	ASSERT_TRUE(metadata_write_string(image, COLOR_LABEL_KEY, "rEd"));
	g_autofree gchar *decorated = image_osd_mkinfo("%formatted.Label:1:<b>*</b>%", image, vars);
	g_autofree gchar *red = g_strdup_printf("<b><span foreground=\"%s\">●</span></b>", color_label_color(1));
	EXPECT_STREQ(decorated, red);
	g_free(options->color_labels.green);
	options->color_labels.green = g_strdup("Approved");
	ASSERT_TRUE(metadata_write_string(image, COLOR_LABEL_KEY, "approved"));
	g_autofree gchar *custom = image_osd_mkinfo("%formatted.Label%", image, vars);
	g_autofree gchar *green = g_strdup_printf("<span foreground=\"%s\">●</span>", color_label_color(3));
	EXPECT_STREQ(custom, green);
	ASSERT_TRUE(metadata_write_string(image, COLOR_LABEL_KEY, "Review <later> & <span foreground='red'>text</span>"));
	g_autofree gchar *unknown = image_osd_mkinfo("%formatted.Label%", image, vars);
	EXPECT_STREQ(unknown, "Review &lt;later&gt; &amp; &lt;span foreground=&apos;red&apos;&gt;text&lt;/span&gt;");
	ASSERT_TRUE(metadata_write_string(image, COLOR_LABEL_KEY, ""));
	g_autofree gchar *empty = image_osd_mkinfo("%formatted.Label%", image, vars);
	EXPECT_STREQ(empty, "");
}

TEST_F(ColorLabel, AssignmentClearAndRevertInvalidateCachedText)
{
#if !HAVE_EXIV2
	EXPECT_FALSE(color_label_assign(image, 1));
	return;
#endif
	EXPECT_STREQ(color_label_read(image), "");
	ASSERT_TRUE(color_label_assign(image, 1));
	EXPECT_STREQ(color_label_read(image), "Red");
	ASSERT_TRUE(color_label_assign(image, 4));
	EXPECT_STREQ(color_label_read(image), "Blue");
	ASSERT_TRUE(color_label_assign(image, 0));
	EXPECT_STREQ(color_label_read(image), "");
	ASSERT_TRUE(color_label_assign(image, 3));
	ASSERT_TRUE(metadata_write_revert(image, COLOR_LABEL_KEY));
	EXPECT_STREQ(color_label_read(image), "");
	EXPECT_FALSE(color_label_assign(image, -1));
	EXPECT_FALSE(color_label_assign(image, COLOR_LABEL_OTHER));
}

TEST_F(ColorLabel, LegacyDestinationDoesNotDiscardLabelEdits)
{
	ASSERT_TRUE(metadata_write_string(image, COLOR_LABEL_KEY, "Approved"));
	g_autofree gchar *destination = g_build_filename(directory, "image.meta", nullptr);
	ASSERT_TRUE(file_data_add_ci_write_metadata(image));
	g_free(image->change->dest);
	image->change->dest = g_strdup(destination);
	EXPECT_FALSE(metadata_write_perform(image));
	EXPECT_NE(image->modified_xmp, nullptr);
	EXPECT_STREQ(color_label_read(image), "Approved");
	EXPECT_FALSE(g_file_test(destination, G_FILE_TEST_EXISTS));
}

TEST_F(ColorLabel, FilteringMatchesUnlabeledKnownAndUnknownText)
{
	for (const auto &[text, index] : {std::pair<const char *, int>{"", 0}, {"Red", 1}, {"Custom", COLOR_LABEL_OTHER}})
		{
		ASSERT_TRUE(metadata_write_string(image, COLOR_LABEL_KEY, text));
		g_autoptr(FileDataList) matching = g_list_append(nullptr, file_data_ref(image));
		matching = color_label_filter(matching, 1U << index);
		ASSERT_EQ(g_list_length(matching), 1U);
		g_autoptr(FileDataList) excluded = g_list_append(nullptr, file_data_ref(image));
		excluded = color_label_filter(excluded, 1U << ((index + 1) % 7));
		EXPECT_EQ(excluded, nullptr);
		}
}

TEST_F(ColorLabel, EmbeddedAndSidecarWritesPreserveOtherMetadata)
{
#if HAVE_EXIV2
	for (const bool sidecar : {false, true})
		{
		g_autofree gchar *sidecar_path = sidecar ? g_build_filename(directory, "image.xmp", nullptr) : nullptr;
		ASSERT_TRUE(metadata_write_string(image, COMMENT_KEY, "Keep description"));
		ASSERT_TRUE(metadata_write_string(image, "Xmp.photoshop.LabelColor", "yellow"));
		ASSERT_TRUE(color_label_assign(image, 4));
		save(sidecar_path);
		std::unique_ptr<ExifData, decltype(&exif_free)> metadata(exif_read(path, sidecar_path, nullptr), exif_free);
		ASSERT_NE(metadata, nullptr);
		g_autoptr(GList) values = exif_get_metadata(metadata.get(), COLOR_LABEL_KEY, METADATA_PLAIN);
		ASSERT_NE(values, nullptr);
		EXPECT_STREQ(static_cast<const gchar *>(values->data), "Blue");
		g_list_free_full(g_steal_pointer(&values), g_free);
		values = exif_get_metadata(metadata.get(), "Xmp.photoshop.LabelColor", METADATA_PLAIN);
		ASSERT_NE(values, nullptr);
		EXPECT_STREQ(static_cast<const gchar *>(values->data), "yellow");
		g_list_free_full(g_steal_pointer(&values), g_free);
		values = exif_get_metadata(metadata.get(), COMMENT_KEY, METADATA_PLAIN);
		ASSERT_NE(values, nullptr);
		EXPECT_STREQ(static_cast<const gchar *>(values->data), "Keep description");
		g_list_free_full(g_steal_pointer(&values), g_free);
		ASSERT_TRUE(color_label_assign(image, 0));
		save(sidecar_path);
		EXPECT_STREQ(color_label_read(image), "");
		}
#else
	GTEST_SKIP() << "Requires Exiv2";
#endif
}

} // namespace
