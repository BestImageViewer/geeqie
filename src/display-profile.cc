/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "display-profile.h"

#include <string>
#include <utility>

namespace
{

constexpr int CALL_TIMEOUT_MS = 1000;
constexpr int LOOKUP_TIMEOUT_MS = 5000;
constexpr auto SERVICE = "org.freedesktop.ColorManager";

GVariant *get_property(GDBusConnection *connection, const char *path,
                      const char *interface, const char *property, GCancellable *cancellable)
{
	g_autoptr(GVariant) reply = g_dbus_connection_call_sync(connection, SERVICE, path,
	                                                      "org.freedesktop.DBus.Properties", "Get", // NOTRANSLATE: D-Bus protocol method.
	                                                      g_variant_new("(ss)", interface, property),
	                                                      G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE,
	                                                      CALL_TIMEOUT_MS, cancellable, nullptr);
	if (!reply) return nullptr;
	GVariant *value = nullptr;
	g_variant_get(reply, "(v)", &value);
	return value;
}

GBytes *lookup_profile(const char *connector, GCancellable *cancellable)
{
	g_autoptr(GDBusConnection) connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, cancellable, nullptr);
	return connection ? display_profile_read(connection, connector, cancellable) : nullptr;
}

} // namespace

GBytes *display_profile_read(GDBusConnection *connection, const char *connector, GCancellable *cancellable)
{
	// colord uses XRANDR_name on X11 and OutputName in newer desktop integrations.
	g_autofree gchar *device_path = nullptr;
	for (const char *property : {"OutputName", "XRANDR_name"}) // NOTRANSLATE: D-Bus protocol name.
		{
		g_autoptr(GVariant) reply = g_dbus_connection_call_sync(connection, SERVICE,
		                                                      "/org/freedesktop/ColorManager", SERVICE,
		                                                      "FindDeviceByProperty", // NOTRANSLATE: D-Bus protocol name.
		                                                      g_variant_new("(ss)", property, connector),
		                                                      G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE,
		                                                      CALL_TIMEOUT_MS, cancellable, nullptr);
		if (reply)
			{
			g_variant_get(reply, "(o)", &device_path);
			break;
			}
		if (g_cancellable_is_cancelled(cancellable)) return nullptr;
		}
	if (!device_path) return nullptr;

	g_autoptr(GVariant) profiles = get_property(connection, device_path,
	                                           "org.freedesktop.ColorManager.Device", "Profiles", cancellable); // NOTRANSLATE: D-Bus protocol name.
	if (!profiles || !g_variant_is_of_type(profiles, G_VARIANT_TYPE("ao")) ||
	    g_variant_n_children(profiles) == 0) return nullptr;
	g_autoptr(GVariant) profile_path = g_variant_get_child_value(profiles, 0);
	g_autoptr(GVariant) filename = get_property(connection, g_variant_get_string(profile_path, nullptr),
	                                           "org.freedesktop.ColorManager.Profile", "Filename", cancellable); // NOTRANSLATE: D-Bus protocol name.
	if (!filename || !g_variant_is_of_type(filename, G_VARIANT_TYPE_STRING)) return nullptr;

	g_autoptr(GFile) file = g_file_new_for_path(g_variant_get_string(filename, nullptr));
	g_autofree gchar *contents = nullptr;
	gsize length = 0;
	if (!g_file_load_contents(file, cancellable, &contents, &length, nullptr, nullptr) ||
	    length == 0 || length > G_MAXUINT) return nullptr;
	return g_bytes_new_take(g_steal_pointer(&contents), length);
}

struct DisplayProfileCache::State
{
	~State()
	{
		g_clear_pointer(&bytes, g_bytes_unref);
		g_clear_object(&pending);
		g_clear_handle_id(&timeout_id, g_source_remove);
	}

	Lookup lookup;
	std::string connector;
	std::function<void()> changed;
	GBytes *bytes = nullptr;
	GCancellable *pending = nullptr;
	guint timeout_id = 0;
	gint64 retry_at = 0;
};

DisplayProfileCache::DisplayProfileCache(Lookup lookup)
	: state(std::make_shared<State>())
{
	state->lookup = lookup ? std::move(lookup) : lookup_profile;
}

DisplayProfileCache::~DisplayProfileCache()
{
	cancel();
}

void DisplayProfileCache::cancel()
{
	if (state->pending) g_cancellable_cancel(state->pending);
	g_clear_object(&state->pending);
	g_clear_handle_id(&state->timeout_id, g_source_remove);
	state->changed = {};
	state->retry_at = 0;
}

GBytes *DisplayProfileCache::get(const char *connector, const std::function<void()> &changed)
{
	if (!connector || !*connector) return nullptr;
	if (state->connector != connector)
		{
		cancel();
		g_clear_pointer(&state->bytes, g_bytes_unref);
		state->connector = connector;
		}
	state->changed = changed;
	if (!state->pending && g_get_monotonic_time() >= state->retry_at)
		{
		state->pending = g_cancellable_new();
		state->timeout_id = g_timeout_add_full(G_PRIORITY_DEFAULT, LOOKUP_TIMEOUT_MS,
		                                     [](gpointer data) -> gboolean
			{
			auto &state = **static_cast<std::shared_ptr<State> *>(data);
			state.timeout_id = 0;
			g_cancellable_cancel(state.pending);
			return G_SOURCE_REMOVE;
			}, new std::shared_ptr<State>(state),
			[](gpointer data) { delete static_cast<std::shared_ptr<State> *>(data); });

		struct Request
		{
			std::shared_ptr<State> state;
			std::string connector;
			Lookup lookup;
		};
		auto *request = new Request{state, state->connector, state->lookup};
		g_autoptr(GTask) task = g_task_new(nullptr, state->pending,
		                                [](GObject *, GAsyncResult *result, gpointer)
			{
			auto *task = G_TASK(result);
			auto *request = static_cast<Request *>(g_task_get_task_data(task));
			auto &state = *request->state;
			g_autoptr(GBytes) bytes = static_cast<GBytes *>(g_task_propagate_pointer(task, nullptr));
			if (state.pending != g_task_get_cancellable(task)) return;
			g_clear_handle_id(&state.timeout_id, g_source_remove);
			g_clear_object(&state.pending);
			state.retry_at = g_get_monotonic_time() + ((bytes ? 60 : 30) * G_TIME_SPAN_SECOND);
			const bool different = bytes ? !state.bytes || !g_bytes_equal(bytes, state.bytes) : state.bytes != nullptr;
			g_clear_pointer(&state.bytes, g_bytes_unref);
			state.bytes = g_steal_pointer(&bytes);
			auto changed = state.changed;
			if (different && changed) changed();
			}, nullptr);
		g_task_set_task_data(task, request, [](gpointer data) { delete static_cast<Request *>(data); });
		g_task_run_in_thread(task, [](GTask *task, gpointer, gpointer data, GCancellable *cancellable)
			{
			auto *request = static_cast<Request *>(data);
			g_task_return_pointer(task, request->lookup(request->connector.c_str(), cancellable),
			                      [](gpointer bytes) { g_bytes_unref(static_cast<GBytes *>(bytes)); });
			});
		}
	return state->bytes ? g_bytes_ref(state->bytes) : nullptr;
}
