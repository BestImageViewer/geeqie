// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef ACTIONS_H
#define ACTIONS_H

#include <gtk/gtk.h>

typedef void (*ActionCallback)(
    GSimpleAction *action,
    GVariant      *parameter,
    gpointer       user_data);

enum class ActionStateType
{
	NONE,
	BOOLEAN,
	STRING,
	INT32
};

struct ActionDef
{
	const char *action_name;
	const char *icon_name;
	const char *description;
	ActionCallback callback;

	const GVariantType *parameter_type;

	ActionStateType state_type;

	gboolean boolean_state;
	const char *string_state;
	gint32 int32_state;

	const gchar * const *targets;
};

const gchar *get_description_for_action_name(const gchar *action_name);

const ActionDef *get_collection_actions();
const ActionDef *get_dupe_main_actions();
const ActionDef *get_dupe_second_actions();
const ActionDef *get_image_actions();
const ActionDef *get_app_actions();
const ActionDef *get_main_actions();
const ActionDef *get_pan_view_actions();
const ActionDef *get_search_actions();

void register_actions_from_table(GtkApplication *app, GtkWidget *window, const ActionDef *defs, GKeyFile *accels_keyfile, gpointer user_data);
const char *get_icon_for_action_name(const char *action_name);

GStrv action_defs_to_aligned_lines(const ActionDef *actions);

#endif

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
