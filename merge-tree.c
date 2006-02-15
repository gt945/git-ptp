#include "cache.h"
#include "diff.h"

static const char merge_tree_usage[] = "git-merge-tree <base-tree> <branch1> <branch2>";
static int resolve_directories = 1;

static void merge_trees(struct tree_desc t[3], const char *base);

static void *fill_tree_descriptor(struct tree_desc *desc, const unsigned char *sha1)
{
	unsigned long size = 0;
	void *buf = NULL;

	if (sha1) {
		buf = read_object_with_reference(sha1, "tree", &size, NULL);
		if (!buf)
			die("unable to read tree %s", sha1_to_hex(sha1));
	}
	desc->size = size;
	desc->buf = buf;
	return buf;
}

struct name_entry {
	const unsigned char *sha1;
	const char *path;
	unsigned int mode;
	int pathlen;
};

static void entry_clear(struct name_entry *a)
{
	memset(a, 0, sizeof(*a));
}

static int entry_compare(struct name_entry *a, struct name_entry *b)
{
	return base_name_compare(
			a->path, a->pathlen, a->mode,
			b->path, b->pathlen, b->mode);
}

static void entry_extract(struct tree_desc *t, struct name_entry *a)
{
	a->sha1 = tree_entry_extract(t, &a->path, &a->mode);
	a->pathlen = strlen(a->path);
}

/* An empty entry never compares same, not even to another empty entry */
static int same_entry(struct name_entry *a, struct name_entry *b)
{
	return	a->sha1 &&
		b->sha1 &&
		!memcmp(a->sha1, b->sha1, 20) &&
		a->mode == b->mode;
}

static void resolve(const char *base, struct name_entry *result)
{
	printf("0 %06o %s %s%s\n", result->mode, sha1_to_hex(result->sha1), base, result->path);
}

static int unresolved_directory(const char *base, struct name_entry n[3])
{
	int baselen;
	char *newbase;
	struct name_entry *p;
	struct tree_desc t[3];
	void *buf0, *buf1, *buf2;

	if (!resolve_directories)
		return 0;
	p = n;
	if (!p->mode) {
		p++;
		if (!p->mode)
			p++;
	}
	if (!S_ISDIR(p->mode))
		return 0;
	baselen = strlen(base);
	newbase = xmalloc(baselen + p->pathlen + 2);
	memcpy(newbase, base, baselen);
	memcpy(newbase + baselen, p->path, p->pathlen);
	memcpy(newbase + baselen + p->pathlen, "/", 2);

	buf0 = fill_tree_descriptor(t+0, n[0].sha1);
	buf1 = fill_tree_descriptor(t+1, n[1].sha1);
	buf2 = fill_tree_descriptor(t+2, n[2].sha1);
	merge_trees(t, newbase);

	free(buf0);
	free(buf1);
	free(buf2);
	free(newbase);
	return 1;
}

static void unresolved(const char *base, struct name_entry n[3])
{
	if (unresolved_directory(base, n))
		return;
	printf("1 %06o %s %s%s\n", n[0].mode, sha1_to_hex(n[0].sha1), base, n[0].path);
	printf("2 %06o %s %s%s\n", n[1].mode, sha1_to_hex(n[1].sha1), base, n[1].path);
	printf("3 %06o %s %s%s\n", n[2].mode, sha1_to_hex(n[2].sha1), base, n[2].path);
}

/*
 * Merge two trees together (t[1] and t[2]), using a common base (t[0])
 * as the origin.
 *
 * This walks the (sorted) trees in lock-step, checking every possible
 * name. Note that directories automatically sort differently from other
 * files (see "base_name_compare"), so you'll never see file/directory
 * conflicts, because they won't ever compare the same.
 *
 * IOW, if a directory changes to a filename, it will automatically be
 * seen as the directory going away, and the filename being created.
 *
 * Think of this as a three-way diff.
 *
 * The output will be either:
 *  - successful merge
 *	 "0 mode sha1 filename"
 *    NOTE NOTE NOTE! FIXME! We really really need to walk the index
 *    in parallel with this too!
 * 
 *  - conflict:
 *	"1 mode sha1 filename"
 *	"2 mode sha1 filename"
 *	"3 mode sha1 filename"
 *    where not all of the 1/2/3 lines may exist, of course.
 *
 * The successful merge rules are the same as for the three-way merge
 * in git-read-tree.
 */
static void merge_trees(struct tree_desc t[3], const char *base)
{
	for (;;) {
		struct name_entry entry[3];
		unsigned int mask = 0;
		int i, last;

		last = -1;
		for (i = 0; i < 3; i++) {
			if (!t[i].size)
				continue;
			entry_extract(t+i, entry+i);
			if (last >= 0) {
				int cmp = entry_compare(entry+i, entry+last);

				/*
				 * Is the new name bigger than the old one?
				 * Ignore it
				 */
				if (cmp > 0)
					continue;
				/*
				 * Is the new name smaller than the old one?
				 * Ignore all old ones
				 */
				if (cmp < 0)
					mask = 0;
			}
			mask |= 1u << i;
			last = i;
		}
		if (!mask)
			break;

		/*
		 * Update the tree entries we've walked, and clear
		 * all the unused name-entries.
		 */
		for (i = 0; i < 3; i++) {
			if (mask & (1u << i)) {
				update_tree_entry(t+i);
				continue;
			}
			entry_clear(entry + i);
		}

		/* Same in both? */
		if (same_entry(entry+1, entry+2)) {
			if (entry[0].sha1) {
				resolve(base, entry+1);
				continue;
			}
		}

		if (same_entry(entry+0, entry+1)) {
			if (entry[2].sha1) {
				resolve(base, entry+2);
				continue;
			}
		}

		if (same_entry(entry+0, entry+2)) {
			if (entry[1].sha1) {
				resolve(base, entry+1);
				continue;
			}
		}

		unresolved(base, entry);
	}
}

static void *get_tree_descriptor(struct tree_desc *desc, const char *rev)
{
	unsigned char sha1[20];
	void *buf;

	if (get_sha1(rev, sha1) < 0)
		die("unknown rev %s", rev);
	buf = fill_tree_descriptor(desc, sha1);
	if (!buf)
		die("%s is not a tree", rev);
	return buf;
}

int main(int argc, char **argv)
{
	struct tree_desc t[3];
	void *buf1, *buf2, *buf3;

	if (argc < 4)
		usage(merge_tree_usage);

	buf1 = get_tree_descriptor(t+0, argv[1]);
	buf2 = get_tree_descriptor(t+1, argv[2]);
	buf3 = get_tree_descriptor(t+2, argv[3]);
	merge_trees(t, "");
	free(buf1);
	free(buf2);
	free(buf3);
	return 0;
}
