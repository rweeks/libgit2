/*
 * Copyright (C) 2009-2012 the libgit2 contributors
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include "git2/common.h"
#include "git2/object.h"
#include "git2/repository.h"

#include "common.h"
#include "blob.h"

const void *git_blob_rawcontent(git_blob *blob)
{
	assert(blob);
	return blob->odb_object->raw.data;
}

size_t git_blob_rawsize(git_blob *blob)
{
	assert(blob);
	return blob->odb_object->raw.len;
}

void git_blob__free(git_blob *blob)
{
	git_odb_object_free(blob->odb_object);
	git__free(blob);
}

int git_blob__parse(git_blob *blob, git_odb_object *odb_obj)
{
	assert(blob);
	git_cached_obj_incref((git_cached_obj *)odb_obj);
	blob->odb_object = odb_obj;
	return GIT_SUCCESS;
}

int git_blob_create_frombuffer(git_oid *oid, git_repository *repo, const void *buffer, size_t len)
{
	int error;
	git_odb *odb;
	git_odb_stream *stream;

	error = git_repository_odb__weakptr(&odb, repo);
	if (error < GIT_SUCCESS)
		return error;

	if ((error = git_odb_open_wstream(&stream, odb, len, GIT_OBJ_BLOB)) < GIT_SUCCESS)
		return git__rethrow(error, "Failed to create blob");

	if ((error = stream->write(stream, buffer, len)) < GIT_SUCCESS) {
		stream->free(stream);
		return error;
	}

	error = stream->finalize_write(oid, stream);
	stream->free(stream);

	if (error < GIT_SUCCESS)
		return git__rethrow(error, "Failed to create blob");

	return GIT_SUCCESS;
}

int git_blob_create_fromfile(git_oid *oid, git_repository *repo, const char *path)
{
	int error = GIT_SUCCESS;
	git_buf full_path = GIT_BUF_INIT;
	git_off_t size;
	git_odb_stream *stream = NULL;
	struct stat st;
	const char *workdir;
	git_odb *odb;

	workdir = git_repository_workdir(repo);
	if (workdir == NULL)
		return git__throw(GIT_ENOTFOUND, "Failed to create blob. (No working directory found)");

	error = git_buf_joinpath(&full_path, workdir, path);
	if (error < GIT_SUCCESS)
		return error;

	error = p_lstat(full_path.ptr, &st);
	if (error < 0) {
		error = git__throw(GIT_EOSERR, "Failed to stat blob. %s", strerror(errno));
		goto cleanup;
	}

	size = st.st_size;

	error = git_repository_odb__weakptr(&odb, repo);
	if (error < GIT_SUCCESS)
		goto cleanup;

	if ((error = git_odb_open_wstream(&stream, odb, (size_t)size, GIT_OBJ_BLOB)) < GIT_SUCCESS)
		goto cleanup;

	if (S_ISLNK(st.st_mode)) {
		char *link_data;
		ssize_t read_len;

		link_data = git__malloc((size_t)size);
		if (!link_data) {
			error = GIT_ENOMEM;
			goto cleanup;
		}

		read_len = p_readlink(full_path.ptr, link_data, (size_t)size);

		if (read_len != (ssize_t)size) {
			error = git__throw(GIT_EOSERR, "Failed to create blob. Can't read symlink");
			free(link_data);
			goto cleanup;
		}

		stream->write(stream, link_data, (size_t)size);
		free(link_data);

	} else {
		int fd;
		char buffer[2048];

		if ((fd = p_open(full_path.ptr, O_RDONLY)) < 0) {
			error = git__throw(GIT_ENOTFOUND, "Failed to create blob. Could not open '%s'", full_path.ptr);
			goto cleanup;
		}

		while (size > 0) {
			ssize_t read_len = p_read(fd, buffer, sizeof(buffer));

			if (read_len < 0) {
				error = git__throw(GIT_EOSERR, "Failed to create blob. Can't read full file");
				p_close(fd);
				goto cleanup;
			}

			stream->write(stream, buffer, read_len);
			size -= read_len;
		}

		p_close(fd);
	}

	error = stream->finalize_write(oid, stream);

cleanup:
	if (stream)
		stream->free(stream);

	git_buf_free(&full_path);

	return error;
}

