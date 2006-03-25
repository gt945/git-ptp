/*
 * Copyright (c) 2005, 2006 Rene Scharfe
 */
#include <time.h>
#include "cache.h"
#include "diff.h"
#include "commit.h"
#include "strbuf.h"
#include "tar.h"

#define RECORDSIZE	(512)
#define BLOCKSIZE	(RECORDSIZE * 20)

#define EXT_HEADER_PATH		1
#define EXT_HEADER_LINKPATH	2

static const char tar_tree_usage[] = "git-tar-tree <key> [basedir]";

static char block[BLOCKSIZE];
static unsigned long offset;

static const char *basedir;
static time_t archive_time;

struct path_prefix {
	struct path_prefix *prev;
	const char *name;
};

/* tries hard to write, either succeeds or dies in the attempt */
static void reliable_write(void *buf, unsigned long size)
{
	while (size > 0) {
		long ret = xwrite(1, buf, size);
		if (ret < 0) {
			if (errno == EPIPE)
				exit(0);
			die("git-tar-tree: %s", strerror(errno));
		} else if (!ret) {
			die("git-tar-tree: disk full?");
		}
		size -= ret;
		buf += ret;
	}
}

/* writes out the whole block, but only if it is full */
static void write_if_needed(void)
{
	if (offset == BLOCKSIZE) {
		reliable_write(block, BLOCKSIZE);
		offset = 0;
	}
}

/* acquire the next record from the buffer; user must call write_if_needed() */
static char *get_record(void)
{
	char *p = block + offset;
	memset(p, 0, RECORDSIZE);
	offset += RECORDSIZE;
	return p;
}

/*
 * The end of tar archives is marked by 1024 nul bytes and after that
 * follows the rest of the block (if any).
 */
static void write_trailer(void)
{
	get_record();
	write_if_needed();
	get_record();
	write_if_needed();
	while (offset) {
		get_record();
		write_if_needed();
	}
}

/*
 * queues up writes, so that all our write(2) calls write exactly one
 * full block; pads writes to RECORDSIZE
 */
static void write_blocked(void *buf, unsigned long size)
{
	unsigned long tail;

	if (offset) {
		unsigned long chunk = BLOCKSIZE - offset;
		if (size < chunk)
			chunk = size;
		memcpy(block + offset, buf, chunk);
		size -= chunk;
		offset += chunk;
		buf += chunk;
		write_if_needed();
	}
	while (size >= BLOCKSIZE) {
		reliable_write(buf, BLOCKSIZE);
		size -= BLOCKSIZE;
		buf += BLOCKSIZE;
	}
	if (size) {
		memcpy(block + offset, buf, size);
		buf += size;
		offset += size;
	}
	tail = offset % RECORDSIZE;
	if (tail)  {
		memset(block + offset, 0, RECORDSIZE - tail);
		offset += RECORDSIZE - tail;
	}
	write_if_needed();
}

/*
 * pax extended header records have the format "%u %s=%s\n".  %u contains
 * the size of the whole string (including the %u), the first %s is the
 * keyword, the second one is the value.  This function constructs such a
 * string and appends it to a struct strbuf.
 */
static void strbuf_append_ext_header(struct strbuf *sb, const char *keyword,
                                     const char *value, unsigned int valuelen)
{
	char *p;
	int len, total, tmp;

	/* "%u %s=%s\n" */
	len = 1 + 1 + strlen(keyword) + 1 + valuelen + 1;
	for (tmp = len; tmp > 9; tmp /= 10)
		len++;

	total = sb->len + len;
	if (total > sb->alloc) {
		sb->buf = xrealloc(sb->buf, total);
		sb->alloc = total;
	}

	p = sb->buf;
	p += sprintf(p, "%u %s=", len, keyword);
	memcpy(p, value, valuelen);
	p += valuelen;
	*p = '\n';
	sb->len = total;
}

static unsigned int ustar_header_chksum(const struct ustar_header *header)
{
	char *p = (char *)header;
	unsigned int chksum = 0;
	while (p < header->chksum)
		chksum += *p++;
	chksum += sizeof(header->chksum) * ' ';
	p += sizeof(header->chksum);
	while (p < (char *)header + sizeof(struct ustar_header))
		chksum += *p++;
	return chksum;
}

static void write_entry(const unsigned char *sha1, struct strbuf *path,
                        unsigned int mode, void *buffer, unsigned long size)
{
	struct ustar_header header;
	struct strbuf ext_header;

	memset(&header, 0, sizeof(header));
	ext_header.buf = NULL;
	ext_header.len = ext_header.alloc = 0;

	if (!sha1) {
		*header.typeflag = TYPEFLAG_GLOBAL_HEADER;
		mode = 0100666;
		strcpy(header.name, "pax_global_header");
	} else if (!path) {
		*header.typeflag = TYPEFLAG_EXT_HEADER;
		mode = 0100666;
		sprintf(header.name, "%s.paxheader", sha1_to_hex(sha1));
	} else {
		if (S_ISDIR(mode)) {
			*header.typeflag = TYPEFLAG_DIR;
			mode |= 0777;
		} else if (S_ISLNK(mode)) {
			*header.typeflag = TYPEFLAG_LNK;
			mode |= 0777;
		} else if (S_ISREG(mode)) {
			*header.typeflag = TYPEFLAG_REG;
			mode |= (mode & 0100) ? 0777 : 0666;
		} else {
			error("unsupported file mode: 0%o (SHA1: %s)",
			      mode, sha1_to_hex(sha1));
			return;
		}
		if (path->len > sizeof(header.name)) {
			sprintf(header.name, "%s.data", sha1_to_hex(sha1));
			strbuf_append_ext_header(&ext_header, "path",
				                 path->buf, path->len);
		} else
			memcpy(header.name, path->buf, path->len);
	}

	if (S_ISLNK(mode) && buffer) {
		if (size > sizeof(header.linkname)) {
			sprintf(header.linkname, "see %s.paxheader",
			        sha1_to_hex(sha1));
			strbuf_append_ext_header(&ext_header, "linkpath",
			                         buffer, size);
		} else
			memcpy(header.linkname, buffer, size);
	}

	sprintf(header.mode, "%07o", mode & 07777);
	sprintf(header.size, "%011lo", S_ISREG(mode) ? size : 0);
	sprintf(header.mtime, "%011lo", archive_time);

	/* XXX: should we provide more meaningful info here? */
	sprintf(header.uid, "%07o", 0);
	sprintf(header.gid, "%07o", 0);
	strncpy(header.uname, "git", 31);
	strncpy(header.gname, "git", 31);
	sprintf(header.devmajor, "%07o", 0);
	sprintf(header.devminor, "%07o", 0);

	memcpy(header.magic, "ustar", 6);
	memcpy(header.version, "00", 2);

	sprintf(header.chksum, "%07o", ustar_header_chksum(&header));

	if (ext_header.len > 0) {
		write_entry(sha1, NULL, 0, ext_header.buf, ext_header.len);
		free(ext_header.buf);
	}
	write_blocked(&header, sizeof(header));
	if (S_ISREG(mode) && buffer && size > 0)
		write_blocked(buffer, size);
}

static void append_string(char **p, const char *s)
{
	unsigned int len = strlen(s);
	memcpy(*p, s, len);
	*p += len;
}

static void append_char(char **p, char c)
{
	**p = c;
	*p += 1;
}

static void append_path_prefix(char **buffer, struct path_prefix *prefix)
{
	if (!prefix)
		return;
	append_path_prefix(buffer, prefix->prev);
	append_string(buffer, prefix->name);
	append_char(buffer, '/');
}

static unsigned int path_prefix_len(struct path_prefix *prefix)
{
	if (!prefix)
		return 0;
	return path_prefix_len(prefix->prev) + strlen(prefix->name) + 1;
}

static void append_path(char **p, int is_dir, const char *basepath,
                        struct path_prefix *prefix, const char *path)
{
	if (basepath) {
		append_string(p, basepath);
		append_char(p, '/');
	}
	append_path_prefix(p, prefix);
	append_string(p, path);
	if (is_dir)
		append_char(p, '/');
}

static unsigned int path_len(int is_dir, const char *basepath,
                             struct path_prefix *prefix, const char *path)
{
	unsigned int len = 0;
	if (basepath)
		len += strlen(basepath) + 1;
	len += path_prefix_len(prefix) + strlen(path);
	if (is_dir)
		len++;
	return len;
}

static void append_extended_header_prefix(char **p, unsigned int size,
                                          const char *keyword)
{
	int len = sprintf(*p, "%u %s=", size, keyword);
	*p += len;
}

static unsigned int extended_header_len(const char *keyword,
                                        unsigned int valuelen)
{
	/* "%u %s=%s\n" */
	unsigned int len = 1 + 1 + strlen(keyword) + 1 + valuelen + 1;
	if (len > 9)
		len++;
	if (len > 99)
		len++;
	return len;
}

static void append_extended_header(char **p, const char *keyword,
                                   const char *value, unsigned int len)
{
	unsigned int size = extended_header_len(keyword, len);
	append_extended_header_prefix(p, size, keyword);
	memcpy(*p, value, len);
	*p += len;
	append_char(p, '\n');
}

static void write_header(const unsigned char *, char, const char *, struct path_prefix *,
                         const char *, unsigned int, void *, unsigned long);

/* stores a pax extended header directly in the block buffer */
static void write_extended_header(const char *headerfilename, int is_dir,
                                  unsigned int flags, const char *basepath,
                                  struct path_prefix *prefix,
                                  const char *path, unsigned int namelen,
                                  void *content, unsigned int contentsize)
{
	char *buffer, *p;
	unsigned int pathlen, size, linkpathlen = 0;

	size = pathlen = extended_header_len("path", namelen);
	if (flags & EXT_HEADER_LINKPATH) {
		linkpathlen = extended_header_len("linkpath", contentsize);
		size += linkpathlen;
	}
	write_header(NULL, TYPEFLAG_EXT_HEADER, NULL, NULL, headerfilename,
	             0100600, NULL, size);

	buffer = p = malloc(size);
	if (!buffer)
		die("git-tar-tree: %s", strerror(errno));
	append_extended_header_prefix(&p, pathlen, "path");
	append_path(&p, is_dir, basepath, prefix, path);
	append_char(&p, '\n');
	if (flags & EXT_HEADER_LINKPATH)
		append_extended_header(&p, "linkpath", content, contentsize);
	write_blocked(buffer, size);
	free(buffer);
}

static void write_global_extended_header(const unsigned char *sha1)
{
	struct strbuf ext_header;
	ext_header.buf = NULL;
	ext_header.len = ext_header.alloc = 0;
	strbuf_append_ext_header(&ext_header, "comment", sha1_to_hex(sha1), 40);
	write_entry(NULL, NULL, 0, ext_header.buf, ext_header.len);
	free(ext_header.buf);
}

/* stores a ustar header directly in the block buffer */
static void write_header(const unsigned char *sha1, char typeflag, const char *basepath,
                         struct path_prefix *prefix, const char *path,
                         unsigned int mode, void *buffer, unsigned long size)
{
	unsigned int namelen; 
	char *header = NULL;
	unsigned int checksum = 0;
	int i;
	unsigned int ext_header = 0;

	if (typeflag == TYPEFLAG_AUTO) {
		if (S_ISDIR(mode))
			typeflag = TYPEFLAG_DIR;
		else if (S_ISLNK(mode))
			typeflag = TYPEFLAG_LNK;
		else
			typeflag = TYPEFLAG_REG;
	}

	namelen = path_len(S_ISDIR(mode), basepath, prefix, path);
	if (namelen > 100)
		ext_header |= EXT_HEADER_PATH;
	if (typeflag == TYPEFLAG_LNK && size > 100)
		ext_header |= EXT_HEADER_LINKPATH;

	/* the extended header must be written before the normal one */
	if (ext_header) {
		char headerfilename[51];
		sprintf(headerfilename, "%s.paxheader", sha1_to_hex(sha1));
		write_extended_header(headerfilename, S_ISDIR(mode),
		                      ext_header, basepath, prefix, path,
		                      namelen, buffer, size);
	}

	header = get_record();

	if (ext_header) {
		sprintf(header, "%s.data", sha1_to_hex(sha1));
	} else {
		char *p = header;
		append_path(&p, S_ISDIR(mode), basepath, prefix, path);
	}

	if (typeflag == TYPEFLAG_LNK) {
		if (ext_header & EXT_HEADER_LINKPATH) {
			sprintf(&header[157], "see %s.paxheader",
			        sha1_to_hex(sha1));
		} else {
			if (buffer)
				strncpy(&header[157], buffer, size);
		}
	}

	if (S_ISDIR(mode))
		mode |= 0777;
	else if (S_ISREG(mode))
		mode |= (mode & 0100) ? 0777 : 0666;
	else if (S_ISLNK(mode))
		mode |= 0777;
	sprintf(&header[100], "%07o", mode & 07777);

	/* XXX: should we provide more meaningful info here? */
	sprintf(&header[108], "%07o", 0);	/* uid */
	sprintf(&header[116], "%07o", 0);	/* gid */
	strncpy(&header[265], "git", 31);	/* uname */
	strncpy(&header[297], "git", 31);	/* gname */

	if (S_ISDIR(mode) || S_ISLNK(mode))
		size = 0;
	sprintf(&header[124], "%011lo", size);
	sprintf(&header[136], "%011lo", archive_time);

	header[156] = typeflag;

	memcpy(&header[257], "ustar", 6);
	memcpy(&header[263], "00", 2);

	sprintf(&header[329], "%07o", 0);	/* devmajor */
	sprintf(&header[337], "%07o", 0);	/* devminor */

	memset(&header[148], ' ', 8);
	for (i = 0; i < RECORDSIZE; i++)
		checksum += header[i];
	sprintf(&header[148], "%07o", checksum & 0x1fffff);

	write_if_needed();
}

static void traverse_tree(struct tree_desc *tree,
			  struct path_prefix *prefix)
{
	struct path_prefix this_prefix;
	this_prefix.prev = prefix;

	while (tree->size) {
		const char *name;
		const unsigned char *sha1;
		unsigned mode;
		void *eltbuf;
		char elttype[20];
		unsigned long eltsize;

		sha1 = tree_entry_extract(tree, &name, &mode);
		update_tree_entry(tree);

		eltbuf = read_sha1_file(sha1, elttype, &eltsize);
		if (!eltbuf)
			die("cannot read %s", sha1_to_hex(sha1));
		write_header(sha1, TYPEFLAG_AUTO, basedir, 
			     prefix, name, mode, eltbuf, eltsize);
		if (S_ISDIR(mode)) {
			struct tree_desc subtree;
			subtree.buf = eltbuf;
			subtree.size = eltsize;
			this_prefix.name = name;
			traverse_tree(&subtree, &this_prefix);
		} else if (!S_ISLNK(mode)) {
			write_blocked(eltbuf, eltsize);
		}
		free(eltbuf);
	}
}

int main(int argc, char **argv)
{
	unsigned char sha1[20], tree_sha1[20];
	struct commit *commit;
	struct tree_desc tree;

	setup_git_directory();

	switch (argc) {
	case 3:
		basedir = argv[2];
		/* FALLTHROUGH */
	case 2:
		if (get_sha1(argv[1], sha1) < 0)
			usage(tar_tree_usage);
		break;
	default:
		usage(tar_tree_usage);
	}

	commit = lookup_commit_reference_gently(sha1, 1);
	if (commit) {
		write_global_extended_header(commit->object.sha1);
		archive_time = commit->date;
	}
	tree.buf = read_object_with_reference(sha1, "tree", &tree.size,
	                                      tree_sha1);
	if (!tree.buf)
		die("not a reference to a tag, commit or tree object: %s",
		    sha1_to_hex(sha1));
	if (!archive_time)
		archive_time = time(NULL);
	if (basedir)
		write_header(tree_sha1, TYPEFLAG_DIR, NULL, NULL,
			basedir, 040777, NULL, 0);
	traverse_tree(&tree, NULL);
	write_trailer();
	return 0;
}
