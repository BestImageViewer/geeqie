// SPDX-License-Identifier: GPL-2.0-or-later

static const ActionDef advanced_exif_actions[] =
{
		{
		"win.advanced-exif-win-close",
		nullptr,
		N_("Close"),
		advanced_exif_close_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.advanced-exif-win-context-menu",
		nullptr,
		N_("Context help"),
		exif_window_context_help_cb,
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
