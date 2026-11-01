/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef PAN_VIEW_PAN_INDEX_H
#define PAN_VIEW_PAN_INDEX_H

#include <functional>
#include <memory>

#include "filedata.h"

struct PanWindow;

/** A cancellable filesystem snapshot. Workers handle paths/stat only; FileData stays on the main thread. */
class PanIndex
{
   public:
	static std::shared_ptr<PanIndex> start(PanWindow *window, std::function<void()> ready);
	~PanIndex();
	void cancel();
	bool scanning() const;
	/** Import a bounded batch. Returns true when the snapshot is ready for layout. */
	bool step();
	GList *files(SortType method) const;
	bool directory(const gchar *path, GList **files, GList **dirs) const;

   private:
	PanIndex();
	struct Data;
	std::unique_ptr<Data> data;
};

#endif
