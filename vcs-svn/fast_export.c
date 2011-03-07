/*
 * Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "git-compat-util.h"
#include "fast_export.h"
#include "line_buffer.h"
#include "repo_tree.h"
#include "string_pool.h"
#include "strbuf.h"

#define MAX_GITSVN_LINE_LEN 4096

static uint32_t first_commit_done;
static struct line_buffer report_buffer = LINE_BUFFER_INIT;

void fast_export_init(int fd)
{
	if (buffer_fdinit(&report_buffer, fd))
		die_errno("cannot read from file descriptor %d", fd);
}

void fast_export_deinit(void)
{
	if (buffer_deinit(&report_buffer))
		die_errno("error closing fast-import feedback stream");
}

void fast_export_reset(void)
{
	buffer_reset(&report_buffer);
}

void fast_export_delete(uint32_t depth, const uint32_t *path)
{
	printf("D \"");
	pool_print_seq_q(depth, path, '/', stdout);
	printf("\"\n");
}

static void fast_export_truncate(uint32_t depth, const uint32_t *path, uint32_t mode)
{
	fast_export_modify(depth, path, mode, "inline");
	printf("data 0\n\n");
}

void fast_export_modify(uint32_t depth, const uint32_t *path, uint32_t mode,
			const char *dataref)
{
	/* Mode must be 100644, 100755, 120000, or 160000. */
	if (!dataref) {
		fast_export_truncate(depth, path, mode);
		return;
	}
	printf("M %06"PRIo32" %s \"", mode, dataref);
	pool_print_seq_q(depth, path, '/', stdout);
	printf("\"\n");
}

static char gitsvnline[MAX_GITSVN_LINE_LEN];
void fast_export_begin_commit(uint32_t revision, uint32_t author, char *log,
			uint32_t uuid, uint32_t url,
			unsigned long timestamp)
{
	if (!log)
		log = "";
	if (~uuid && ~url) {
		snprintf(gitsvnline, MAX_GITSVN_LINE_LEN,
				"\n\ngit-svn-id: %s@%"PRIu32" %s\n",
				 pool_fetch(url), revision, pool_fetch(uuid));
	} else {
		*gitsvnline = '\0';
	}
	printf("commit refs/heads/master\n");
	printf("mark :%"PRIu32"\n", revision);
	printf("committer %s <%s@%s> %ld +0000\n",
		   ~author ? pool_fetch(author) : "nobody",
		   ~author ? pool_fetch(author) : "nobody",
		   ~uuid ? pool_fetch(uuid) : "local", timestamp);
	printf("data %"PRIu32"\n%s%s\n",
		   (uint32_t) (strlen(log) + strlen(gitsvnline)),
		   log, gitsvnline);
	if (!first_commit_done) {
		if (revision > 1)
			printf("from refs/heads/master^0\n");
		first_commit_done = 1;
	}
}

void fast_export_end_commit(uint32_t revision)
{
	printf("progress Imported commit %"PRIu32".\n\n", revision);
}

static void ls_from_rev(uint32_t rev, uint32_t depth, const uint32_t *path)
{
	/* ls :5 path/to/old/file */
	printf("ls :%"PRIu32" \"", rev);
	pool_print_seq_q(depth, path, '/', stdout);
	printf("\"\n");
	fflush(stdout);
}

static void ls_from_active_commit(uint32_t depth, const uint32_t *path)
{
	/* ls "path/to/file" */
	printf("ls \"");
	pool_print_seq_q(depth, path, '/', stdout);
	printf("\"\n");
	fflush(stdout);
}

static const char *get_response_line(void)
{
	const char *line = buffer_read_line(&report_buffer);
	if (line)
		return line;
	if (buffer_ferror(&report_buffer))
		die_errno("error reading from fast-import");
	die("unexpected end of fast-import feedback");
}

void fast_export_data(uint32_t mode, uint32_t len, struct line_buffer *input)
{
	if (mode == REPO_MODE_LNK) {
		/* svn symlink blobs start with "link " */
		buffer_skip_bytes(input, 5);
		len -= 5;
	}
	printf("data %"PRIu32"\n", len);
	buffer_copy_bytes(input, len);
	fputc('\n', stdout);
}

static int parse_ls_response(const char *response, uint32_t *mode,
					struct strbuf *dataref)
{
	const char *tab;
	const char *response_end;

	assert(response);
	response_end = response + strlen(response);

	if (*response == 'm') {	/* Missing. */
		errno = ENOENT;
		return -1;
	}

	/* Mode. */
	if (response_end - response < strlen("100644") ||
	    response[strlen("100644")] != ' ')
		die("invalid ls response: missing mode: %s", response);
	*mode = 0;
	for (; *response != ' '; response++) {
		char ch = *response;
		if (ch < '0' || ch > '7')
			die("invalid ls response: mode is not octal: %s", response);
		*mode *= 8;
		*mode += ch - '0';
	}

	/* ' blob ' or ' tree ' */
	if (response_end - response < strlen(" blob ") ||
	    (response[1] != 'b' && response[1] != 't'))
		die("unexpected ls response: not a tree or blob: %s", response);
	response += strlen(" blob ");

	/* Dataref. */
	tab = memchr(response, '\t', response_end - response);
	if (!tab)
		die("invalid ls response: missing tab: %s", response);
	strbuf_add(dataref, response, tab - response);
	return 0;
}

int fast_export_ls_rev(uint32_t rev, uint32_t depth, const uint32_t *path,
				uint32_t *mode, struct strbuf *dataref)
{
	ls_from_rev(rev, depth, path);
	return parse_ls_response(get_response_line(), mode, dataref);
}

int fast_export_ls(uint32_t depth, const uint32_t *path,
				uint32_t *mode, struct strbuf *dataref)
{
	ls_from_active_commit(depth, path);
	return parse_ls_response(get_response_line(), mode, dataref);
}
