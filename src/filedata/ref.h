/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef FILEDATA_REF_H
#define FILEDATA_REF_H

class FileData;

/**
 * @class FileDataRef
 * @brief Move-only owner of one FileData reference
 *
 * Construction and reset() add a reference; they do not adopt a reference
 * returned by a file_data_new_*() function. Destruction releases the held
 * reference. Pointer access is borrowed; release() transfers the reference.
 * Keeping a FileData alive does not keep its associated view or window alive,
 * and does not make its mutable fields safe for concurrent access.
 * @see ownership_and_async
 */
class FileDataRef
{
    public:
	/** @brief Add a reference to @p fd; nullptr creates an empty owner.
	 * @param fd Borrowed FileData pointer
	 */
	explicit FileDataRef(FileData *fd);
	// Not copyable or copy-assignable.
	FileDataRef(FileDataRef &) = delete;
	FileDataRef &operator=(const FileDataRef &) = delete;

	// Allow move and move-assignment.
	FileDataRef(FileDataRef &&) noexcept;
	/** Transfer the source reference without adding a reference.
	 * The current implementation does not release an existing destination
	 * reference: call reset(nullptr) first if the destination is nonempty.
	 */
	FileDataRef &operator=(FileDataRef &&) noexcept;

	~FileDataRef();

	// Pointer conversion and dereference return borrowed pointers without adding a reference.
	// Allow implicit conversion directly to FileData*.
	// This allows a FileDataRef to be used anywhere a FileData* is expected.
	operator FileData*() const { return fd_; }

	// In a boolean context, return whether or not we're holding nullptr.
	operator bool() const { return fd_ != nullptr; }

	// FileDataRef equality means equality of the contained FileData*.
	// This is aligned with the behavior of `FileDataRef == FileData*`, which is implicitly
	// converted to `FileData* == FileData*`.
	bool operator==(const FileDataRef &other) const { return fd_ == other.fd_; }
	bool operator!=(const FileDataRef &other) const { return !(*this == other); }

	/** @brief Reference the replacement before releasing the previous reference.
	 * @param new_fd Borrowed replacement, possibly nullptr or the current pointer
	 */
	void reset(FileData *new_fd);
	/** @brief Empty this owner and transfer its reference to the caller.
	 * @return Owned reference, or nullptr; the recipient must eventually unref it
	 */
	FileData *release();
	FileData* operator*() const { return fd_; }
	FileData* operator->() const { return fd_; }

    private:
	FileData *fd_ = nullptr;
};

#endif  // FILEDATA_REF_H

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
