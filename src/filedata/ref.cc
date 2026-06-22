/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "filedata.h"
#include "ref.h"

FileDataRef::FileDataRef(FileData *fd) : fd_(fd)
{
	if (fd_ != nullptr) fd_->file_data_ref();
}

FileDataRef::FileDataRef(FileDataRef &&other) noexcept
{
	*this = std::move(other);
}

FileDataRef &FileDataRef::operator=(FileDataRef &&other) noexcept
{
	fd_ = other.fd_;
	other.fd_ = nullptr;

	return *this;
}

FileDataRef::~FileDataRef()
{
	if(fd_ != nullptr) fd_->file_data_unref();
}

void FileDataRef::reset(FileData *new_fd)
{
	if(new_fd != nullptr) new_fd->file_data_ref();

	FileData *old_fd = fd_;
	fd_ = new_fd;

	if (old_fd != nullptr) old_fd->file_data_unref();
}

FileData *FileDataRef::release()
{
	return g_steal_pointer(&fd_);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
