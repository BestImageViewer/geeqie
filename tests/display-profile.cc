/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include "display-profile.h"

namespace
{

bool wait_for(const std::function<bool()> &ready, gint64 timeout = 2 * G_TIME_SPAN_SECOND)
{
	const auto deadline = g_get_monotonic_time() + timeout;
	while (!ready() && g_get_monotonic_time() < deadline)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		g_usleep(1000);
		}
	return ready();
}

struct LookupControl
{
	std::atomic<int> calls{0};
	std::atomic<bool> release{false};
	std::atomic<bool> cancelled{false};
	std::atomic<bool> finished{false};

	GBytes *lookup(const char *connector, GCancellable *cancellable)
	{
		++calls;
		while (!release && !g_cancellable_is_cancelled(cancellable)) g_usleep(1000);
		cancelled = g_cancellable_is_cancelled(cancellable);
		finished = true;
		return cancelled ? nullptr : g_bytes_new(connector, strlen(connector));
	}
};

TEST(DisplayProfile, SlowLookupKeepsMainLoopResponsiveAndSharesCachedBytes)
{
	auto control = std::make_shared<LookupControl>();
	DisplayProfileCache cache([control](const char *connector, GCancellable *cancellable)
		{
		return control->lookup(connector, cancellable);
		});
	int changes = 0;
	bool callback_on_main_thread = false;
	auto *main_thread = g_thread_self();
	const auto changed = [&]()
		{
		++changes;
		callback_on_main_thread = g_thread_self() == main_thread;
		};
	EXPECT_EQ(cache.get("DP-1", changed), nullptr);
	ASSERT_TRUE(wait_for([&]() { return control->calls == 1; }));
	for (int i = 0; i < 10; ++i) EXPECT_EQ(cache.get("DP-1", changed), nullptr);
	bool idle_ran = false;
	g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, [](gpointer data) -> gboolean
		{
		*static_cast<bool *>(data) = true;
		return G_SOURCE_REMOVE;
		}, &idle_ran, nullptr);
	EXPECT_TRUE(wait_for([&]() { return idle_ran; }));
	EXPECT_EQ(changes, 0);
	EXPECT_EQ(control->calls, 1);
	control->release = true;
	ASSERT_TRUE(wait_for([&]() { return changes == 1; }));
	EXPECT_TRUE(callback_on_main_thread);
	for (int i = 0; i < 10; ++i)
		{
		g_autoptr(GBytes) bytes = cache.get("DP-1", changed);
		ASSERT_NE(bytes, nullptr);
		g_autoptr(GBytes) expected = g_bytes_new_static("DP-1", 4);
		EXPECT_TRUE(g_bytes_equal(bytes, expected));
		}
	EXPECT_EQ(control->calls, 1);
}

TEST(DisplayProfile, FailedLookupIsCachedWithoutRepeatedRequests)
{
	auto control = std::make_shared<LookupControl>();
	DisplayProfileCache cache([control](const char *, GCancellable *) -> GBytes *
		{
		++control->calls;
		return nullptr;
		});
	int changes = 0;
	EXPECT_EQ(cache.get("DP-1", [&]() { ++changes; }), nullptr);
	ASSERT_TRUE(wait_for([&]() { return control->calls == 1; }));
	const auto deadline = g_get_monotonic_time() + (100 * G_TIME_SPAN_MILLISECOND);
	while (g_get_monotonic_time() < deadline)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		EXPECT_EQ(cache.get("DP-1", [&]() { ++changes; }), nullptr);
		g_usleep(1000);
		}
	EXPECT_EQ(control->calls, 1);
	EXPECT_EQ(changes, 0);
}

TEST(DisplayProfile, DestroyingCacheCancelsLookupWithoutCallingItsOwner)
{
	auto control = std::make_shared<LookupControl>();
	int changes = 0;
	{
	DisplayProfileCache cache([control](const char *connector, GCancellable *cancellable)
		{
		return control->lookup(connector, cancellable);
		});
	EXPECT_EQ(cache.get("DP-1", [&]() { ++changes; }), nullptr);
	ASSERT_TRUE(wait_for([&]() { return control->calls == 1; }));
	}
	ASSERT_TRUE(wait_for([&]() { return control->finished.load(); }));
	while (g_main_context_iteration(nullptr, FALSE)) {}
	EXPECT_TRUE(control->cancelled);
	EXPECT_EQ(changes, 0);
}

TEST(DisplayProfile, ConnectorChangeDiscardsTheCancelledResult)
{
	auto control = std::make_shared<LookupControl>();
	DisplayProfileCache cache([control](const char *connector, GCancellable *cancellable)
		{
		if (std::string(connector) == "DP-1") return control->lookup(connector, cancellable);
		return g_bytes_new_static("second", 6);
		});
	int changes = 0;
	EXPECT_EQ(cache.get("DP-1", [&]() { ++changes; }), nullptr);
	ASSERT_TRUE(wait_for([&]() { return control->calls == 1; }));
	EXPECT_EQ(cache.get("DP-2", [&]() { ++changes; }), nullptr);
	ASSERT_TRUE(wait_for([&]() { return changes == 1 && control->finished; }));
	g_autoptr(GBytes) bytes = cache.get("DP-2", [&]() { ++changes; });
	g_autoptr(GBytes) expected = g_bytes_new_static("second", 6);
	ASSERT_NE(bytes, nullptr);
	EXPECT_TRUE(g_bytes_equal(bytes, expected));
	EXPECT_TRUE(control->cancelled);
	EXPECT_EQ(changes, 1);
}

TEST(DisplayProfile, LookupDeadlineCancelsAnUnresponsiveWorker)
{
	auto control = std::make_shared<LookupControl>();
	DisplayProfileCache cache([control](const char *connector, GCancellable *cancellable)
		{
		return control->lookup(connector, cancellable);
		});
	int changes = 0;
	EXPECT_EQ(cache.get("DP-1", [&]() { ++changes; }), nullptr);
	ASSERT_TRUE(wait_for([&]() { return control->finished.load(); }, 7 * G_TIME_SPAN_SECOND));
	EXPECT_TRUE(control->cancelled);
	EXPECT_EQ(changes, 0);
}

class DisplayProfileService : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!g_getenv("DBUS_SESSION_BUS_ADDRESS")) GTEST_SKIP() << "Requires an isolated session bus";
		connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
		ASSERT_NE(connection, nullptr);
		g_autoptr(GVariant) reply = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus",
		                                                      "/org/freedesktop/DBus", "org.freedesktop.DBus",
		                                                      "RequestName", g_variant_new("(su)", "org.freedesktop.ColorManager", 4U),
		                                                      G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
		ASSERT_NE(reply, nullptr);
		guint result = 0;
		g_variant_get(reply, "(u)", &result);
		ASSERT_EQ(result, 1U);
		const char xml[] =
			"<node><interface name='org.freedesktop.ColorManager'>"
			"<method name='FindDeviceByProperty'><arg type='s' direction='in'/>"
			"<arg type='s' direction='in'/><arg type='o' direction='out'/></method></interface>"
			"<interface name='org.freedesktop.ColorManager.Device'><property name='Profiles' type='ao' access='read'/></interface>"
			"<interface name='org.freedesktop.ColorManager.Profile'><property name='Filename' type='s' access='read'/></interface></node>";
		g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(xml, nullptr);
		static const GDBusInterfaceVTable vtable{
			[](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
			   GVariant *parameters, GDBusMethodInvocation *invocation, gpointer data)
				{
				auto *self = static_cast<DisplayProfileService *>(data);
				++self->requests;
				const gchar *property;
				const gchar *connector;
				g_variant_get(parameters, "(&s&s)", &property, &connector);
				EXPECT_STREQ(connector, "DP-1");
				if (self->stall)
					self->invocations = g_list_prepend(self->invocations, g_object_ref(invocation));
				else if (g_strcmp0(property, "OutputName") == 0)
					g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.ColorManager.NotFound", "Try XRANDR_name");
				else
					g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", "/device"));
				},
			[](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *property, GError **, gpointer data) -> GVariant *
				{
				auto *self = static_cast<DisplayProfileService *>(data);
				++self->properties;
				if (g_strcmp0(property, "Profiles") == 0)
					{
					const char *paths[] = {"/profile"};
					return g_variant_new_objv(paths, 1);
					}
				return g_variant_new_string(self->filename);
				}, nullptr, {nullptr}};
		const char *paths[] = {"/org/freedesktop/ColorManager", "/device", "/profile"};
		for (int i = 0; i < 3; ++i)
			{
			registrations[i] = g_dbus_connection_register_object(connection, paths[i], info->interfaces[i], &vtable, this, nullptr, nullptr);
			ASSERT_NE(registrations[i], 0U);
			}
		const int fd = g_file_open_tmp("geeqie-display-profile-XXXXXX", &filename, nullptr);
		ASSERT_GE(fd, 0);
		g_close(fd, nullptr);
		ASSERT_TRUE(g_file_set_contents(filename, "profile bytes", -1, nullptr));
	}

	void TearDown() override
	{
		for (GList *work = invocations; work; work = work->next)
			g_dbus_method_invocation_return_dbus_error(G_DBUS_METHOD_INVOCATION(work->data), "org.freedesktop.ColorManager.Cancelled", "Test finished");
		g_list_free_full(invocations, g_object_unref);
		if (connection)
			{
			for (auto registration : registrations)
				if (registration) g_dbus_connection_unregister_object(connection, registration);
			g_autoptr(GVariant) reply = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus",
			                                                      "/org/freedesktop/DBus", "org.freedesktop.DBus", "ReleaseName",
			                                                      g_variant_new("(s)", "org.freedesktop.ColorManager"),
			                                                      nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
			g_object_unref(connection);
			}
		if (filename) g_remove(filename);
		g_free(filename);
	}

	GDBusConnection *connection = nullptr;
	guint registrations[3]{};
	gchar *filename = nullptr;
	int requests = 0;
	int properties = 0;
	bool stall = false;
	GList *invocations = nullptr;
};

TEST_F(DisplayProfileService, ReadsProfileWithConnectorFallbackAndCachesItsContents)
{
	DisplayProfileCache cache([this](const char *connector, GCancellable *cancellable)
		{
		return display_profile_read(connection, connector, cancellable);
		});
	bool changed = false;
	EXPECT_EQ(cache.get("DP-1", [&]() { changed = true; }), nullptr);
	ASSERT_TRUE(wait_for([&]() { return changed; }));
	EXPECT_EQ(requests, 2);
	EXPECT_EQ(properties, 2);
	g_autoptr(GBytes) bytes = cache.get("DP-1", []() {});
	g_autoptr(GBytes) expected = g_bytes_new_static("profile bytes", 13);
	ASSERT_NE(bytes, nullptr);
	EXPECT_TRUE(g_bytes_equal(bytes, expected));
	EXPECT_EQ(requests, 2);
}

TEST_F(DisplayProfileService, UnresponsiveServiceCanBeCancelled)
{
	stall = true;
	auto finished = std::make_shared<std::atomic<bool>>(false);
	auto owned_connection = std::shared_ptr<GDBusConnection>(G_DBUS_CONNECTION(g_object_ref(connection)), g_object_unref);
	DisplayProfileCache cache([owned_connection, finished](const char *connector, GCancellable *cancellable)
		{
		auto *bytes = display_profile_read(owned_connection.get(), connector, cancellable);
		*finished = true;
		return bytes;
		});
	int changes = 0;
	EXPECT_EQ(cache.get("DP-1", [&]() { ++changes; }), nullptr);
	ASSERT_TRUE(wait_for([&]() { return requests == 1; }));
	cache.cancel();
	ASSERT_TRUE(wait_for([&]() { return finished->load(); }));
	EXPECT_EQ(changes, 0);
	EXPECT_EQ(requests, 1);
}

} // namespace
