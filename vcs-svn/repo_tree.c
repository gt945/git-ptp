/*
 * Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "git-compat-util.h"
#include "strbuf.h"
#include "repo_tree.h"
#include "fast_export.h"

const char *repo_read_path(const char *path)
{
	int err;
	uint32_t dummy;
	static struct strbuf buf = STRBUF_INIT;

	strbuf_reset(&buf);
	err = fast_export_ls(path, &dummy, &buf);
	if (err) {
		if (errno != ENOENT)
			die_errno("BUG: unexpected fast_export_ls error");
		return NULL;
	}
	return buf.buf;
}

uint32_t repo_read_mode(const char *path)
{
	int err;
	uint32_t result;
	static struct strbuf dummy = STRBUF_INIT;

	strbuf_reset(&dummy);
	err = fast_export_ls(path, &result, &dummy);
	if (err) {
		if (errno != ENOENT)
			die_errno("BUG: unexpected fast_export_ls error");
		/* Treat missing paths as directories. */
		return REPO_MODE_DIR;
	}
	return result;
}

void repo_copy(uint32_t revision, const char *src, const char *dst)
{
	int err;
	uint32_t mode;
	static struct strbuf data = STRBUF_INIT;

	strbuf_reset(&data);
	err = fast_export_ls_rev(revision, src, &mode, &data);
	if (err) {
		if (errno != ENOENT)
			die_errno("BUG: unexpected fast_export_ls_rev error");
		fast_export_delete(dst);
		return;
	}
	fast_export_modify(dst, mode, data.buf);
}

void repo_delete(const char *path)
{
	fast_export_delete(path);
}
