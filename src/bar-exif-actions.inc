// SPDX-License-Identifier: GPL-2.0-or-later

static const ActionDef bar-exif_actions[] =
{
		{
		"win.bar-exif-win-configure",
		nullptr,
		N_("Configure"),
		bar_pane_exif_conf_dialog_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.bar-exif-win-remove",
		nullptr,
		N_("Remove"),
		nullptr,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.bar-exif-win-copy",
		nullptr,
		N_("Copy"),
		bar_pane_exif_copy_entry_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.bar-exif-win-add-entry",
		nullptr,
		N_("Add entry"),
		bar_pane_exif_conf_dialog_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.bar-exif-win-show-hidden-entries",
		nullptr,
		N_("Show hidden entries"),
		bar_pane_exif_toggle_show_all_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr
		}

};
