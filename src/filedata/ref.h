/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef FILEDATA_REF_H
#define FILEDATA_REF_H

class FileData;

/**
 * @class FileDataRef
 * @brief RAII object that holds a ref to the specified FileData.
 */
class FileDataRef
{
    public:
	explicit FileDataRef(FileData *fd);
	// Not copyable or copy-assignable.
	FileDataRef(FileDataRef &) = delete;
	FileDataRef &operator=(const FileDataRef &) = delete;

	// Allow move and move-assignment.
	FileDataRef(FileDataRef &&) noexcept;
	FileDataRef &operator=(FileDataRef &&) noexcept;

	~FileDataRef();

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

	void reset(FileData *new_fd);
	FileData *release();
	FileData* operator*() const { return fd_; }
	FileData* operator->() const { return fd_; }

    private:
	FileData *fd_ = nullptr;
};

#endif  // FILEDATA_REF_H

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
