/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "diagnostic-report.h"

#include <sys/utsname.h>

#include <cstring>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

#include <config.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif

#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "options.h"
#include "signal-handler.h"
#include "ui-file-chooser.h"
#include "ui-utildlg.h"
#include "view-dir.h"
#include "view-file.h"

namespace
{

void append_layout(std::ostream &out, const LayoutWindow *layout, unsigned index)
{
	out << "\n[Window " << index << "]\n"; // NOTRANSLATE: Diagnostic section.
	out << "file_view=" << (layout->options.file_view_type == FILEVIEW_LIST ? "list" : "icons") << '\n'; // NOTRANSLATE: Diagnostic enum names.
	out << "directory_view=" << (layout->options.dir_view_type == DIRVIEW_LIST ? "list" : "tree") << '\n'; // NOTRANSLATE: Diagnostic enum names.
	out << "show_thumbnails=" << static_cast<long long>(layout->options.show_thumbnails) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "show_marks=" << static_cast<long long>(layout->options.show_marks) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "show_file_filter=" << static_cast<long long>(layout->options.show_file_filter) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "show_directory_date=" << static_cast<long long>(layout->options.show_directory_date) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "split_pane_sync=" << static_cast<long long>(layout->options.split_pane_sync) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "ignore_alpha=" << static_cast<long long>(layout->options.ignore_alpha) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "animate=" << static_cast<long long>(layout->options.animate) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "tools_float=" << static_cast<long long>(layout->options.tools_float) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "tools_hidden=" << static_cast<long long>(layout->options.tools_hidden) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "bars_state.info=" << static_cast<long long>(layout->options.bars_state.info) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "bars_state.sort=" << static_cast<long long>(layout->options.bars_state.sort) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "bars_state.hidden=" << static_cast<long long>(layout->options.bars_state.hidden) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "file_view_list_sort.method=" << static_cast<long long>(layout->options.file_view_list_sort.method) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "file_view_list_sort.ascending=" << static_cast<long long>(layout->options.file_view_list_sort.ascending) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "dir_view_list_sort.method=" << static_cast<long long>(layout->options.dir_view_list_sort.method) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "dir_view_list_sort.ascending=" << static_cast<long long>(layout->options.dir_view_list_sort.ascending) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "fullscreen=" << (layout->full_screen != nullptr) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "info_sidebar_visible=" << (layout->bar && gtk_widget_get_visible(layout->bar)) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "sort_sidebar_visible=" << (layout->bar_sort && gtk_widget_get_visible(layout->bar_sort)) << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "split_mode=" << static_cast<int>(layout->split_mode) << '\n'; // NOTRANSLATE: Diagnostic field.
	if (layout->vf)
		{
		out << "file_filter.case_sensitive=" << layout->vf->file_filter.case_sensitive << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "color_label_filter=" << layout->vf->color_label_filter << '\n'; // NOTRANSLATE: Diagnostic field.
		}
}

void save_report(GFile *destination, gpointer data)
{
	std::unique_ptr<std::string> report(static_cast<std::string *>(data));
	if (!destination) return;
	g_autoptr(GError) error = nullptr;
	if (!g_file_replace_contents(destination, report->data(), report->size(), nullptr, FALSE,
	                             G_FILE_CREATE_PRIVATE, nullptr, nullptr, &error))
		warning_dialog(_("Unable to save diagnostic report"), error->message, GQ_ICON_DIALOG_ERROR, nullptr);
}

} // namespace

std::string diagnostic_report_desktop_hint(const char *value)
{
	if (!value) return "unknown"; // NOTRANSLATE: Diagnostic placeholder.
	const char *allowed[] = {"GNOME", "KDE", "XFCE", "Cinnamon", "X-Cinnamon", "MATE", "LXQt", "LXDE", "i3", "sway", "Unity", "Budgie", "COSMIC", nullptr}; // NOTRANSLATE: Desktop identifiers.
	g_auto(GStrv) tokens = g_strsplit_set(value, ":;", -1); // NOTRANSLATE: Desktop identifier separators.
	std::set<std::string> found;
	for (char **token = tokens; *token; ++token)
		for (const char *name : allowed)
			if (name && g_ascii_strcasecmp(*token, name) == 0) found.emplace(name);
	std::string result;
	for (const auto &name : found)
		{
		if (!result.empty()) result += ", "; // NOTRANSLATE: Diagnostic list separator.
		result += name;
		}
	return result.empty() ? "unknown" : result; // NOTRANSLATE: Diagnostic placeholder.
}

std::string diagnostic_report_create(const LayoutWindow *layout)
{
	std::ostringstream out;
	out << "Geeqie " << VERSION << "\nDiagnostic settings report, format 1\n"; // NOTRANSLATE: Diagnostic header.
	out << "GTK=" << gtk_get_major_version() << '.' << gtk_get_minor_version() << '.' << gtk_get_micro_version() << '\n'; // NOTRANSLATE: Library name.
	out << "GLib=" << glib_major_version << '.' << glib_minor_version << '.' << glib_micro_version << '\n'; // NOTRANSLATE: Library name.
	out << "Pango=" << pango_version_string() << '\n'; // NOTRANSLATE: Library name.
	out << "pointer_bits=" << sizeof(void *) * 8 << '\n'; // NOTRANSLATE: Diagnostic field.
	struct utsname system{};
	if (uname(&system) == 0)
		{
		// Custom kernel suffixes and hostnames can contain personal identifiers.
		std::string release;
		for (const char *p = system.release; *p && (g_ascii_isdigit(*p) || *p == '.'); ++p) release += *p;
		out << "kernel_version=" << release << '\n'; // NOTRANSLATE: Diagnostic field.
		const char *architectures[] = {"x86_64", "aarch64", "i686", "armv7l", "ppc64le", "riscv64", nullptr}; // NOTRANSLATE: Architecture allowlist.
		out << "architecture=" << (g_strv_contains(architectures, system.machine) ? system.machine : "other") << '\n'; // NOTRANSLATE: Diagnostic field.
		}
	g_autofree gchar *distribution = g_get_os_info(G_OS_INFO_KEY_ID);
	g_autofree gchar *version = g_get_os_info(G_OS_INFO_KEY_VERSION_ID);
	const char *distributions[] = {"ubuntu", "debian", "fedora", "arch", "alpine", "opensuse-leap", "opensuse-tumbleweed", "void", "gentoo", "nixos", "linuxmint", "rhel", "rocky", "almalinux", nullptr}; // NOTRANSLATE: Distribution allowlist.
	out << "distribution=" << (distribution && g_strv_contains(distributions, distribution) ? distribution : "other") << '\n'; // NOTRANSLATE: Diagnostic field.
	if (version && strspn(version, "0123456789.") == strlen(version)) // NOTRANSLATE: Version characters.
		out << "distribution_version=" << version << '\n'; // NOTRANSLATE: Diagnostic field.
#ifdef __linux__
	out << "OS=Linux\n"; // NOTRANSLATE: Operating system.
#elif defined(__FreeBSD__)
	out << "OS=FreeBSD\n"; // NOTRANSLATE: Operating system.
#elif defined(__NetBSD__)
	out << "OS=NetBSD\n"; // NOTRANSLATE: Operating system.
#elif defined(__APPLE__)
	out << "OS=macOS\n"; // NOTRANSLATE: Operating system.
#else
	out << "OS=other\n"; // NOTRANSLATE: Operating system.
#endif
	auto *display = gdk_display_get_default();
	out << "display_backend=" << (display ? G_OBJECT_TYPE_NAME(display) : "unavailable") << '\n'; // NOTRANSLATE: Diagnostic field.
	// Packaging environment variables can contain personal paths. Report only classification.
	const auto present = [](const char *name)
		{
		const char *value = g_getenv(name);
		return value && *value;
		};
	const bool flatpak = g_file_test("/.flatpak-info", G_FILE_TEST_IS_REGULAR) || present("FLATPAK_ID"); // NOTRANSLATE: Flatpak markers.
	const bool snap = present("SNAP") && present("SNAP_NAME"); // NOTRANSLATE: Snap markers.
	const bool appimage = present("APPIMAGE"); // NOTRANSLATE: AppImage marker.
	const int markers = static_cast<int>(flatpak) + static_cast<int>(snap) + static_cast<int>(appimage);
	const char *installation = markers > 1 ? "unknown-conflicting-markers" : // NOTRANSLATE: Packaging classification.
	                           flatpak ? "flatpak" : snap ? "snap" : appimage ? "appimage" : "native-or-unknown"; // NOTRANSLATE: Packaging classifications.
	out << "installation_method_hint=" << installation << '\n'; // NOTRANSLATE: Diagnostic field.
	out << "desktop_hint=" << diagnostic_report_desktop_hint(g_getenv("XDG_CURRENT_DESKTOP")) << '\n'; // NOTRANSLATE: Environment variable name and diagnostic field.
	const char *manager = nullptr;
#ifdef GDK_WINDOWING_X11
	if (display && GDK_IS_X11_DISPLAY(display))
		manager = gdk_x11_screen_get_window_manager_name(gdk_x11_display_get_screen(display));
#endif
	const char *managers[] = {"Mutter", "GNOME Shell", "KWin", "KWin_x11", "i3", "awesome", "Xfwm4", "Openbox", "Metacity", "Marco", "bspwm", "herbstluftwm", nullptr}; // NOTRANSLATE: Window manager allowlist.
	const char *known_manager = "unknown"; // NOTRANSLATE: Diagnostic placeholder.
	for (const char *name : managers)
		if (name && manager && g_ascii_strcasecmp(name, manager) == 0) known_manager = name;
	out << "window_manager_hint=" << known_manager << '\n'; // NOTRANSLATE: Diagnostic field.
#ifdef DEBUG
	out << "debug_build=1\n"; // NOTRANSLATE: Diagnostic field.
#else
	out << "debug_build=0\n"; // NOTRANSLATE: Diagnostic field.
#endif
#if HAVE_EXIV2
	out << "HAVE_EXIV2=1\n"; // NOTRANSLATE: Build feature.
#else
	out << "HAVE_EXIV2=0\n"; // NOTRANSLATE: Build feature.
#endif
#if HAVE_RAW
	out << "HAVE_RAW=1\n"; // NOTRANSLATE: Build feature.
#else
	out << "HAVE_RAW=0\n"; // NOTRANSLATE: Build feature.
#endif
#if HAVE_LCMS
	out << "HAVE_LCMS=1\n"; // NOTRANSLATE: Build feature.
#else
	out << "HAVE_LCMS=0\n"; // NOTRANSLATE: Build feature.
#endif
#if HAVE_HEIF
	out << "HAVE_HEIF=1\n"; // NOTRANSLATE: Build feature.
#else
	out << "HAVE_HEIF=0\n"; // NOTRANSLATE: Build feature.
#endif
#if HAVE_JPEG
	out << "HAVE_JPEG=1\n"; // NOTRANSLATE: Build feature.
#else
	out << "HAVE_JPEG=0\n"; // NOTRANSLATE: Build feature.
#endif
#if HAVE_TIFF
	out << "HAVE_TIFF=1\n"; // NOTRANSLATE: Build feature.
#else
	out << "HAVE_TIFF=0\n"; // NOTRANSLATE: Build feature.
#endif
#if HAVE_WEBP
	out << "HAVE_WEBP=1\n"; // NOTRANSLATE: Build feature.
#else
	out << "HAVE_WEBP=0\n"; // NOTRANSLATE: Build feature.
#endif
#if HAVE_ARCHIVE
	out << "HAVE_ARCHIVE=1\n"; // NOTRANSLATE: Build feature.
#else
	out << "HAVE_ARCHIVE=0\n"; // NOTRANSLATE: Build feature.
#endif
#if HAVE_LIBSHUMATE
	out << "HAVE_LIBSHUMATE=1\n"; // NOTRANSLATE: Build feature.
#else
	out << "HAVE_LIBSHUMATE=0\n"; // NOTRANSLATE: Build feature.
#endif
#if HAVE_EXECINFO_H
	out << "HAVE_EXECINFO_H=1\n"; // NOTRANSLATE: Build feature.
#else
	out << "HAVE_EXECINFO_H=0\n"; // NOTRANSLATE: Build feature.
#endif
	// Never serialize the options structure wholesale: string fields can identify users and files.
	if (options)
		{
		out << "\n[Preferences]\n"; // NOTRANSLATE: Diagnostic section.
		out << "hamburger_menu=" << static_cast<long long>(options->hamburger_menu) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "expand_menu_toolbar=" << static_cast<long long>(options->expand_menu_toolbar) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "color_scheme=" << static_cast<long long>(options->color_scheme) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "tree_descend_subdirs=" << static_cast<long long>(options->tree_descend_subdirs) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "view_dir_list_single_click_enter=" << static_cast<long long>(options->view_dir_list_single_click_enter) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "circular_selection_lists=" << static_cast<long long>(options->circular_selection_lists) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "auto_next_folder=" << static_cast<long long>(options->auto_next_folder) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "lazy_image_sync=" << static_cast<long long>(options->lazy_image_sync) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "update_on_time_change=" << static_cast<long long>(options->update_on_time_change) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "read_metadata_in_idle=" << static_cast<long long>(options->read_metadata_in_idle) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "hide_window_in_fullscreen=" << static_cast<long long>(options->hide_window_in_fullscreen) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "hide_osd_in_fullscreen=" << static_cast<long long>(options->hide_osd_in_fullscreen) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "sidebars_with_tools=" << static_cast<long long>(options->sidebars_with_tools) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "duplicates_match=" << static_cast<long long>(options->duplicates_match) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "duplicates_similarity_threshold=" << static_cast<long long>(options->duplicates_similarity_threshold) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "duplicates_thumbnails=" << static_cast<long long>(options->duplicates_thumbnails) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "rot_invariant_sim=" << static_cast<long long>(options->rot_invariant_sim) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "dnd_default_action=" << static_cast<long long>(options->dnd_default_action) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_sort.case_sensitive=" << static_cast<long long>(options->file_sort.case_sensitive) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_ops.enable_in_place_rename=" << static_cast<long long>(options->file_ops.enable_in_place_rename) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_ops.confirm_delete=" << static_cast<long long>(options->file_ops.confirm_delete) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_ops.confirm_move_to_trash=" << static_cast<long long>(options->file_ops.confirm_move_to_trash) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_ops.confirm_delete_dir=" << static_cast<long long>(options->file_ops.confirm_delete_dir) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_ops.confirm_move_dir_to_trash=" << static_cast<long long>(options->file_ops.confirm_move_dir_to_trash) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_ops.enable_delete_key=" << static_cast<long long>(options->file_ops.enable_delete_key) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_ops.safe_delete_enable=" << static_cast<long long>(options->file_ops.safe_delete_enable) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_ops.use_system_trash=" << static_cast<long long>(options->file_ops.use_system_trash) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_ops.no_trash=" << static_cast<long long>(options->file_ops.no_trash) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.exif_rotate_enable=" << static_cast<long long>(options->image.exif_rotate_enable) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.scroll_reset_method=" << static_cast<long long>(options->image.scroll_reset_method) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.fit_window_to_image=" << static_cast<long long>(options->image.fit_window_to_image) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.limit_window_size=" << static_cast<long long>(options->image.limit_window_size) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.limit_autofit_size=" << static_cast<long long>(options->image.limit_autofit_size) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.max_autofit_size=" << static_cast<long long>(options->image.max_autofit_size) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.max_enlargement_size=" << static_cast<long long>(options->image.max_enlargement_size) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.tile_cache_max=" << static_cast<long long>(options->image.tile_cache_max) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.image_cache_max=" << static_cast<long long>(options->image.image_cache_max) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.jpeg_decode_memory_limit=" << static_cast<long long>(options->image.jpeg_decode_memory_limit) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.enable_read_ahead=" << static_cast<long long>(options->image.enable_read_ahead) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.read_ahead_images=" << static_cast<long long>(options->image.read_ahead_images) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.zoom_mode=" << static_cast<long long>(options->image.zoom_mode) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.zoom_2pass=" << static_cast<long long>(options->image.zoom_2pass) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.zoom_to_fit_allow_expand=" << static_cast<long long>(options->image.zoom_to_fit_allow_expand) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.allow_overscroll=" << static_cast<long long>(options->image.allow_overscroll) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.nearest_above_200=" << static_cast<long long>(options->image.nearest_above_200) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.zoom_quality=" << static_cast<long long>(options->image.zoom_quality) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.zoom_increment=" << static_cast<long long>(options->image.zoom_increment) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.zoom_style=" << static_cast<long long>(options->image.zoom_style) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "image.tile_size=" << static_cast<long long>(options->image.tile_size) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "thumbnails.size.width=" << static_cast<long long>(options->thumbnails.size.width) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "thumbnails.size.height=" << static_cast<long long>(options->thumbnails.size.height) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "thumbnails.memory_cache_size=" << static_cast<long long>(options->thumbnails.memory_cache_size) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "thumbnails.enable_caching=" << static_cast<long long>(options->thumbnails.enable_caching) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "thumbnails.cache_into_dirs=" << static_cast<long long>(options->thumbnails.cache_into_dirs) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "thumbnails.spec_standard=" << static_cast<long long>(options->thumbnails.spec_standard) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "thumbnails.quality=" << static_cast<long long>(options->thumbnails.quality) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "thumbnails.use_exif=" << static_cast<long long>(options->thumbnails.use_exif) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "thumbnails.use_color_management=" << static_cast<long long>(options->thumbnails.use_color_management) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "thumbnails.use_ft_metadata=" << static_cast<long long>(options->thumbnails.use_ft_metadata) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_filter.show_hidden_files=" << static_cast<long long>(options->file_filter.show_hidden_files) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_filter.dot_prefix_hidden_files=" << static_cast<long long>(options->file_filter.dot_prefix_hidden_files) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_filter.show_parent_directory=" << static_cast<long long>(options->file_filter.show_parent_directory) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_filter.show_dot_directory=" << static_cast<long long>(options->file_filter.show_dot_directory) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_filter.disable_file_extension_checks=" << static_cast<long long>(options->file_filter.disable_file_extension_checks) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "file_filter.disable=" << static_cast<long long>(options->file_filter.disable) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "slideshow.delay=" << static_cast<long long>(options->slideshow.delay) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "slideshow.random=" << static_cast<long long>(options->slideshow.random) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "slideshow.repeat=" << static_cast<long long>(options->slideshow.repeat) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "slideshow.transition=" << static_cast<long long>(options->slideshow.transition) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "slideshow.transition_duration=" << static_cast<long long>(options->slideshow.transition_duration) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "fullscreen.screen=" << static_cast<long long>(options->fullscreen.screen) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "fullscreen.clean_flip=" << static_cast<long long>(options->fullscreen.clean_flip) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "fullscreen.disable_saver=" << static_cast<long long>(options->fullscreen.disable_saver) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "metadata.save_in_image_file=" << static_cast<long long>(options->metadata.save_in_image_file) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "metadata.save_legacy_IPTC=" << static_cast<long long>(options->metadata.save_legacy_IPTC) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "metadata.sync_grouped_files=" << static_cast<long long>(options->metadata.sync_grouped_files) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "metadata.confirm_write=" << static_cast<long long>(options->metadata.confirm_write) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "metadata.write_orientation=" << static_cast<long long>(options->metadata.write_orientation) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "metadata.sidecar_extended_name=" << static_cast<long long>(options->metadata.sidecar_extended_name) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "stereo.mode=" << static_cast<long long>(options->stereo.mode) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "stereo.fsmode=" << static_cast<long long>(options->stereo.fsmode) << '\n'; // NOTRANSLATE: Diagnostic field.
		out << "stereo.enable_fsmode=" << static_cast<long long>(options->stereo.enable_fsmode) << '\n'; // NOTRANSLATE: Diagnostic field.
		}
	if (layout)
		append_layout(out, layout, 1);
	else
		{
		unsigned index = 0;
		const auto *active = get_current_layout();
		layout_window_foreach([&](LayoutWindow *window)
			{
			append_layout(out, window, ++index);
			out << "active_window=" << (window == active) << '\n'; // NOTRANSLATE: Diagnostic field.
			});
		}
	return out.str();
}

void diagnostic_report_refresh()
{
	static std::string previous;
	auto report = diagnostic_report_create();
	if (report == previous) return;
	g_autoptr(GDateTime) now = g_date_time_new_now_utc();
	g_autofree gchar *timestamp = g_date_time_format_iso8601(now);
	const auto snapshot = std::string("Settings snapshot (UTC): ") + timestamp + "\n" + report; // NOTRANSLATE: Diagnostic timestamp.
	if (signal_handler_set_snapshot(snapshot)) previous = std::move(report);
}

void diagnostic_report_dialog_show(GtkWindow *parent)
{
	auto report = std::make_unique<std::string>(diagnostic_report_create());
	auto *dialog = generic_dialog_new(_("Diagnostic report"), "diagnostic-report", GTK_WIDGET(parent), FALSE, // NOTRANSLATE: Dialog role.
		+[](GenericDialog *gd, gpointer) { generic_dialog_close(gd); }, nullptr);
	gtk_window_set_default_size(GTK_WINDOW(dialog->dialog), 680, 520);
	gtk_window_set_modal(GTK_WINDOW(dialog->dialog), TRUE);
	auto *message = generic_dialog_add_message(dialog, GQ_ICON_DIALOG_INFO, _("Save diagnostic report"),
		_("Review the technical settings below before saving a report to attach to an issue. Filenames, folders, history, bookmarks, metadata, external commands, and environment variable contents are excluded. Nothing is uploaded automatically."), FALSE);
	gtk_label_set_max_width_chars(GTK_LABEL(gtk_widget_get_last_child(message)), 65);
	auto *view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), report->c_str(), -1);
	gtk_box_append(GTK_BOX(dialog->vbox), view);
	dialog->data = report.get();
	generic_dialog_add_button(dialog, GQ_ICON_SAVE, _("Save a copy…"), +[](GenericDialog *gd, gpointer data)
		{
		FileDialogData chooser{};
		chooser.action = FileDialogAction::SAVE;
		chooser.title = _("Save diagnostic report");
		chooser.suggested_name = "geeqie-diagnostic.txt"; // NOTRANSLATE: Suggested filename.
		chooser.parent = GTK_WINDOW(gd->dialog);
		chooser.callback = save_report;
		chooser.data = new std::string(*static_cast<std::string *>(data));
		file_dialog_show(chooser);
		}, TRUE);
	generic_dialog_add_button(dialog, nullptr, _("Close"),
		+[](GenericDialog *gd, gpointer) { generic_dialog_close(gd); }, FALSE);
	g_object_set_data_full(G_OBJECT(dialog->dialog), "diagnostic-report", report.release(), // NOTRANSLATE: Owned preview text.
		+[](gpointer data) { delete static_cast<std::string *>(data); });
	gtk_window_present(GTK_WINDOW(dialog->dialog));
}
