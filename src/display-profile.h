/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef DISPLAY_PROFILE_H
#define DISPLAY_PROFILE_H

#include <functional>
#include <memory>

#include <gio/gio.h>

/** Read owned ICC bytes from colord using bounded calls. Run only on a worker thread. */
GBytes *display_profile_read(GDBusConnection *connection, const char *connector, GCancellable *cancellable);

/** Asynchronous display-profile cache. All methods and notifications run on the main thread. */
class DisplayProfileCache
{
public:
	using Lookup = std::function<GBytes *(const char *, GCancellable *)>;

	explicit DisplayProfileCache(Lookup lookup = {});
	~DisplayProfileCache();
	DisplayProfileCache(const DisplayProfileCache &) = delete;
	DisplayProfileCache &operator=(const DisplayProfileCache &) = delete;

	/** Return an owned reference to cached ICC bytes, or nullptr while unavailable.
	 * The worker lookup returns owned bytes; changed runs when the cached bytes change.
	 * Subsequent access retries successful lookups after 60 seconds and failures after 30.
	 */
	GBytes *get(const char *connector, const std::function<void()> &changed);
	void cancel();

private:
	struct State;
	std::shared_ptr<State> state;
};

#endif
