/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <glib-object.h>
#include <glib.h>

#include "filedata.h"
#include "image-load.h"

namespace
{

TEST(ImageLoader, CancelPendingStart)
{
	auto *fd = file_data_new_simple("/nonexistent/geeqie-pending-image.jpg");
	auto *il = image_loader_new(fd);
	file_data_unref(fd);
	ASSERT_TRUE(image_loader_start_deferred(il));
	EXPECT_FALSE(il->thread);
	EXPECT_EQ(il->mapped_file, nullptr);
	const guint source_id = il->idle_id;
	EXPECT_NE(g_main_context_find_source_by_id(nullptr, source_id), nullptr);
	EXPECT_FALSE(image_loader_start_deferred(il));
	image_loader_free(il);
	EXPECT_EQ(g_main_context_find_source_by_id(nullptr, source_id), nullptr);
}

TEST(ImageLoader, DeferredStartReportsFailure)
{
	auto *fd = file_data_new_simple("/nonexistent/geeqie-pending-image.jpg");
	auto *il = image_loader_new(fd);
	file_data_unref(fd);
	gboolean failed = FALSE;
	g_signal_connect(il, "error", G_CALLBACK(+[](ImageLoader *, gpointer data)
		{
		*static_cast<gboolean *>(data) = TRUE;
		}), &failed);
	ASSERT_TRUE(image_loader_start_deferred(il));
	const gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
	while (!failed && g_get_monotonic_time() < deadline)
		{
		g_main_context_iteration(nullptr, FALSE);
		}
	EXPECT_TRUE(failed);
	EXPECT_EQ(il->idle_id, 0U);
	image_loader_free(il);
}

} // namespace
