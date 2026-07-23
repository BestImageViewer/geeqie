/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "spell.h"

#include <libspelling.h>

#include "ui-misc.h"

void spell_text_view_enable(GtkTextView *text_view)
{
	static gsize initialized = 0;

	if (g_once_init_enter(&initialized))
		{
		spelling_init();
		g_once_init_leave(&initialized, 1);
		}

	GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
	if (!GTK_SOURCE_IS_BUFFER(buffer))
		{
		GtkSourceBuffer *source_buffer = gtk_source_buffer_new(nullptr);

		g_autofree gchar *text = text_buffer_get_text(buffer, TRUE);
		gtk_text_buffer_set_text(GTK_TEXT_BUFFER(source_buffer), text, -1);
		gtk_text_view_set_buffer(text_view, GTK_TEXT_BUFFER(source_buffer));
		g_object_unref(source_buffer);

		buffer = gtk_text_view_get_buffer(text_view);
		}

	SpellingChecker *checker = spelling_checker_get_default();
	SpellingTextBufferAdapter *adapter = spelling_text_buffer_adapter_new(GTK_SOURCE_BUFFER(buffer), checker);

	spelling_text_buffer_adapter_set_enabled(adapter, TRUE);
	gtk_widget_insert_action_group(GTK_WIDGET(text_view), "spelling", G_ACTION_GROUP(adapter));
	gtk_text_view_set_extra_menu(text_view, spelling_text_buffer_adapter_get_menu_model(adapter));

	g_object_set_data_full(G_OBJECT(buffer), "geeqie-spelling-adapter", adapter, g_object_unref);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
