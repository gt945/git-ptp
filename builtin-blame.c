/*
 * Pickaxe
 *
 * Copyright (c) 2006, Junio C Hamano
 */

#include "cache.h"
#include "builtin.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree-walk.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"
#include "xdiff-interface.h"

#include <time.h>
#include <sys/time.h>
#include <regex.h>

static char blame_usage[] =
"git-blame [-c] [-l] [-t] [-f] [-n] [-p] [-L n,m] [-S <revs-file>] [-M] [-C] [-C] [commit] [--] file\n"
"  -c, --compatibility Use the same output mode as git-annotate (Default: off)\n"
"  -l, --long          Show long commit SHA1 (Default: off)\n"
"  -t, --time          Show raw timestamp (Default: off)\n"
"  -f, --show-name     Show original filename (Default: auto)\n"
"  -n, --show-number   Show original linenumber (Default: off)\n"
"  -p, --porcelain     Show in a format designed for machine consumption\n"
"  -L n,m              Process only line range n,m, counting from 1\n"
"  -M, -C              Find line movements within and across files\n"
"  -S revs-file        Use revisions from revs-file instead of calling git-rev-list\n";

static int longest_file;
static int longest_author;
static int max_orig_digits;
static int max_digits;
static int max_score_digits;

#ifndef DEBUG
#define DEBUG 0
#endif

/* stats */
static int num_read_blob;
static int num_get_patch;
static int num_commits;

#define PICKAXE_BLAME_MOVE		01
#define PICKAXE_BLAME_COPY		02
#define PICKAXE_BLAME_COPY_HARDER	04

/*
 * blame for a blame_entry with score lower than these thresholds
 * is not passed to the parent using move/copy logic.
 */
static unsigned blame_move_score;
static unsigned blame_copy_score;
#define BLAME_DEFAULT_MOVE_SCORE	20
#define BLAME_DEFAULT_COPY_SCORE	40

/* bits #0..7 in revision.h, #8..11 used for merge_bases() in commit.c */
#define METAINFO_SHOWN		(1u<<12)
#define MORE_THAN_ONE_PATH	(1u<<13)

/*
 * One blob in a commit that is being suspected
 */
struct origin {
	int refcnt;
	struct commit *commit;
	mmfile_t file;
	unsigned char blob_sha1[20];
	char path[FLEX_ARRAY];
};

static char *fill_origin_blob(struct origin *o, mmfile_t *file)
{
	if (!o->file.ptr) {
		char type[10];
		num_read_blob++;
		file->ptr = read_sha1_file(o->blob_sha1, type,
					   (unsigned long *)(&(file->size)));
		o->file = *file;
	}
	else
		*file = o->file;
	return file->ptr;
}

static inline struct origin *origin_incref(struct origin *o)
{
	if (o)
		o->refcnt++;
	return o;
}

static void origin_decref(struct origin *o)
{
	if (o && --o->refcnt <= 0) {
		if (o->file.ptr)
			free(o->file.ptr);
		memset(o, 0, sizeof(*o));
		free(o);
	}
}

struct blame_entry {
	struct blame_entry *prev;
	struct blame_entry *next;

	/* the first line of this group in the final image;
	 * internally all line numbers are 0 based.
	 */
	int lno;

	/* how many lines this group has */
	int num_lines;

	/* the commit that introduced this group into the final image */
	struct origin *suspect;

	/* true if the suspect is truly guilty; false while we have not
	 * checked if the group came from one of its parents.
	 */
	char guilty;

	/* the line number of the first line of this group in the
	 * suspect's file; internally all line numbers are 0 based.
	 */
	int s_lno;

	/* how significant this entry is -- cached to avoid
	 * scanning the lines over and over
	 */
	unsigned score;
};

struct scoreboard {
	/* the final commit (i.e. where we started digging from) */
	struct commit *final;

	const char *path;

	/* the contents in the final; pointed into by buf pointers of
	 * blame_entries
	 */
	const char *final_buf;
	unsigned long final_buf_size;

	/* linked list of blames */
	struct blame_entry *ent;

	/* look-up a line in the final buffer */
	int num_lines;
	int *lineno;
};

static int cmp_suspect(struct origin *a, struct origin *b)
{
	int cmp = hashcmp(a->commit->object.sha1, b->commit->object.sha1);
	if (cmp)
		return cmp;
	return strcmp(a->path, b->path);
}

#define cmp_suspect(a, b) ( ((a)==(b)) ? 0 : cmp_suspect(a,b) )

static void sanity_check_refcnt(struct scoreboard *);

static void coalesce(struct scoreboard *sb)
{
	struct blame_entry *ent, *next;

	for (ent = sb->ent; ent && (next = ent->next); ent = next) {
		if (!cmp_suspect(ent->suspect, next->suspect) &&
		    ent->guilty == next->guilty &&
		    ent->s_lno + ent->num_lines == next->s_lno) {
			ent->num_lines += next->num_lines;
			ent->next = next->next;
			if (ent->next)
				ent->next->prev = ent;
			origin_decref(next->suspect);
			free(next);
			ent->score = 0;
			next = ent; /* again */
		}
	}

	if (DEBUG) /* sanity */
		sanity_check_refcnt(sb);
}

static struct origin *make_origin(struct commit *commit, const char *path)
{
	struct origin *o;
	o = xcalloc(1, sizeof(*o) + strlen(path) + 1);
	o->commit = commit;
	o->refcnt = 1;
	strcpy(o->path, path);
	return o;
}

static struct origin *get_origin(struct scoreboard *sb,
				 struct commit *commit,
				 const char *path)
{
	struct blame_entry *e;

	for (e = sb->ent; e; e = e->next) {
		if (e->suspect->commit == commit &&
		    !strcmp(e->suspect->path, path))
			return origin_incref(e->suspect);
	}
	return make_origin(commit, path);
}

static int fill_blob_sha1(struct origin *origin)
{
	unsigned mode;
	char type[10];

	if (!is_null_sha1(origin->blob_sha1))
		return 0;
	if (get_tree_entry(origin->commit->object.sha1,
			   origin->path,
			   origin->blob_sha1, &mode))
		goto error_out;
	if (sha1_object_info(origin->blob_sha1, type, NULL) ||
	    strcmp(type, blob_type))
		goto error_out;
	return 0;
 error_out:
	hashclr(origin->blob_sha1);
	return -1;
}

static struct origin *find_origin(struct scoreboard *sb,
				  struct commit *parent,
				  struct origin *origin)
{
	struct origin *porigin = NULL;
	struct diff_options diff_opts;
	const char *paths[2];

	if (parent->util) {
		/* This is a freestanding copy of origin and not
		 * refcounted.
		 */
		struct origin *cached = parent->util;
		if (!strcmp(cached->path, origin->path)) {
			porigin = get_origin(sb, parent, cached->path);
			if (porigin->refcnt == 1)
				hashcpy(porigin->blob_sha1, cached->blob_sha1);
			return porigin;
		}
		/* otherwise it was not very useful; free it */
		free(parent->util);
		parent->util = NULL;
	}

	/* See if the origin->path is different between parent
	 * and origin first.  Most of the time they are the
	 * same and diff-tree is fairly efficient about this.
	 */
	diff_setup(&diff_opts);
	diff_opts.recursive = 1;
	diff_opts.detect_rename = 0;
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	paths[0] = origin->path;
	paths[1] = NULL;

	diff_tree_setup_paths(paths, &diff_opts);
	if (diff_setup_done(&diff_opts) < 0)
		die("diff-setup");
	diff_tree_sha1(parent->tree->object.sha1,
		       origin->commit->tree->object.sha1,
		       "", &diff_opts);
	diffcore_std(&diff_opts);

	/* It is either one entry that says "modified", or "created",
	 * or nothing.
	 */
	if (!diff_queued_diff.nr) {
		/* The path is the same as parent */
		porigin = get_origin(sb, parent, origin->path);
		hashcpy(porigin->blob_sha1, origin->blob_sha1);
	}
	else if (diff_queued_diff.nr != 1)
		die("internal error in blame::find_origin");
	else {
		struct diff_filepair *p = diff_queued_diff.queue[0];
		switch (p->status) {
		default:
			die("internal error in blame::find_origin (%c)",
			    p->status);
		case 'M':
			porigin = get_origin(sb, parent, origin->path);
			hashcpy(porigin->blob_sha1, p->one->sha1);
			break;
		case 'A':
		case 'T':
			/* Did not exist in parent, or type changed */
			break;
		}
	}
	diff_flush(&diff_opts);
	if (porigin) {
		struct origin *cached;
		cached = make_origin(porigin->commit, porigin->path);
		hashcpy(cached->blob_sha1, porigin->blob_sha1);
		parent->util = cached;
	}
	return porigin;
}

static struct origin *find_rename(struct scoreboard *sb,
				  struct commit *parent,
				  struct origin *origin)
{
	struct origin *porigin = NULL;
	struct diff_options diff_opts;
	int i;
	const char *paths[2];

	diff_setup(&diff_opts);
	diff_opts.recursive = 1;
	diff_opts.detect_rename = DIFF_DETECT_RENAME;
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_opts.single_follow = origin->path;
	paths[0] = NULL;
	diff_tree_setup_paths(paths, &diff_opts);
	if (diff_setup_done(&diff_opts) < 0)
		die("diff-setup");
	diff_tree_sha1(parent->tree->object.sha1,
		       origin->commit->tree->object.sha1,
		       "", &diff_opts);
	diffcore_std(&diff_opts);

	for (i = 0; i < diff_queued_diff.nr; i++) {
		struct diff_filepair *p = diff_queued_diff.queue[i];
		if ((p->status == 'R' || p->status == 'C') &&
		    !strcmp(p->two->path, origin->path)) {
			porigin = get_origin(sb, parent, p->one->path);
			hashcpy(porigin->blob_sha1, p->one->sha1);
			break;
		}
	}
	diff_flush(&diff_opts);
	return porigin;
}

struct chunk {
	/* line number in postimage; up to but not including this
	 * line is the same as preimage
	 */
	int same;

	/* preimage line number after this chunk */
	int p_next;

	/* postimage line number after this chunk */
	int t_next;
};

struct patch {
	struct chunk *chunks;
	int num;
};

struct blame_diff_state {
	struct xdiff_emit_state xm;
	struct patch *ret;
	unsigned hunk_post_context;
	unsigned hunk_in_pre_context : 1;
};

static void process_u_diff(void *state_, char *line, unsigned long len)
{
	struct blame_diff_state *state = state_;
	struct chunk *chunk;
	int off1, off2, len1, len2, num;

	num = state->ret->num;
	if (len < 4 || line[0] != '@' || line[1] != '@') {
		if (state->hunk_in_pre_context && line[0] == ' ')
			state->ret->chunks[num - 1].same++;
		else {
			state->hunk_in_pre_context = 0;
			if (line[0] == ' ')
				state->hunk_post_context++;
			else
				state->hunk_post_context = 0;
		}
		return;
	}

	if (num && state->hunk_post_context) {
		chunk = &state->ret->chunks[num - 1];
		chunk->p_next -= state->hunk_post_context;
		chunk->t_next -= state->hunk_post_context;
	}
	state->ret->num = ++num;
	state->ret->chunks = xrealloc(state->ret->chunks,
				      sizeof(struct chunk) * num);
	chunk = &state->ret->chunks[num - 1];
	if (parse_hunk_header(line, len, &off1, &len1, &off2, &len2)) {
		state->ret->num--;
		return;
	}

	/* Line numbers in patch output are one based. */
	off1--;
	off2--;

	chunk->same = len2 ? off2 : (off2 + 1);

	chunk->p_next = off1 + (len1 ? len1 : 1);
	chunk->t_next = chunk->same + len2;
	state->hunk_in_pre_context = 1;
	state->hunk_post_context = 0;
}

static struct patch *compare_buffer(mmfile_t *file_p, mmfile_t *file_o,
				    int context)
{
	struct blame_diff_state state;
	xpparam_t xpp;
	xdemitconf_t xecfg;
	xdemitcb_t ecb;

	xpp.flags = XDF_NEED_MINIMAL;
	xecfg.ctxlen = context;
	xecfg.flags = 0;
	ecb.outf = xdiff_outf;
	ecb.priv = &state;
	memset(&state, 0, sizeof(state));
	state.xm.consume = process_u_diff;
	state.ret = xmalloc(sizeof(struct patch));
	state.ret->chunks = NULL;
	state.ret->num = 0;

	xdl_diff(file_p, file_o, &xpp, &xecfg, &ecb);

	if (state.ret->num) {
		struct chunk *chunk;
		chunk = &state.ret->chunks[state.ret->num - 1];
		chunk->p_next -= state.hunk_post_context;
		chunk->t_next -= state.hunk_post_context;
	}
	return state.ret;
}

static struct patch *get_patch(struct origin *parent, struct origin *origin)
{
	mmfile_t file_p, file_o;
	struct patch *patch;

	fill_origin_blob(parent, &file_p);
	fill_origin_blob(origin, &file_o);
	if (!file_p.ptr || !file_o.ptr)
		return NULL;
	patch = compare_buffer(&file_p, &file_o, 0);
	num_get_patch++;
	return patch;
}

static void free_patch(struct patch *p)
{
	free(p->chunks);
	free(p);
}

static void add_blame_entry(struct scoreboard *sb, struct blame_entry *e)
{
	struct blame_entry *ent, *prev = NULL;

	origin_incref(e->suspect);

	for (ent = sb->ent; ent && ent->lno < e->lno; ent = ent->next)
		prev = ent;

	/* prev, if not NULL, is the last one that is below e */
	e->prev = prev;
	if (prev) {
		e->next = prev->next;
		prev->next = e;
	}
	else {
		e->next = sb->ent;
		sb->ent = e;
	}
	if (e->next)
		e->next->prev = e;
}

static void dup_entry(struct blame_entry *dst, struct blame_entry *src)
{
	struct blame_entry *p, *n;

	p = dst->prev;
	n = dst->next;
	origin_incref(src->suspect);
	origin_decref(dst->suspect);
	memcpy(dst, src, sizeof(*src));
	dst->prev = p;
	dst->next = n;
	dst->score = 0;
}

static const char *nth_line(struct scoreboard *sb, int lno)
{
	return sb->final_buf + sb->lineno[lno];
}

static void split_overlap(struct blame_entry *split,
			  struct blame_entry *e,
			  int tlno, int plno, int same,
			  struct origin *parent)
{
	/* it is known that lines between tlno to same came from
	 * parent, and e has an overlap with that range.  it also is
	 * known that parent's line plno corresponds to e's line tlno.
	 *
	 *                <---- e ----->
	 *                   <------>
	 *                   <------------>
	 *             <------------>
	 *             <------------------>
	 *
	 * Potentially we need to split e into three parts; before
	 * this chunk, the chunk to be blamed for parent, and after
	 * that portion.
	 */
	int chunk_end_lno;
	memset(split, 0, sizeof(struct blame_entry [3]));

	if (e->s_lno < tlno) {
		/* there is a pre-chunk part not blamed on parent */
		split[0].suspect = origin_incref(e->suspect);
		split[0].lno = e->lno;
		split[0].s_lno = e->s_lno;
		split[0].num_lines = tlno - e->s_lno;
		split[1].lno = e->lno + tlno - e->s_lno;
		split[1].s_lno = plno;
	}
	else {
		split[1].lno = e->lno;
		split[1].s_lno = plno + (e->s_lno - tlno);
	}

	if (same < e->s_lno + e->num_lines) {
		/* there is a post-chunk part not blamed on parent */
		split[2].suspect = origin_incref(e->suspect);
		split[2].lno = e->lno + (same - e->s_lno);
		split[2].s_lno = e->s_lno + (same - e->s_lno);
		split[2].num_lines = e->s_lno + e->num_lines - same;
		chunk_end_lno = split[2].lno;
	}
	else
		chunk_end_lno = e->lno + e->num_lines;
	split[1].num_lines = chunk_end_lno - split[1].lno;

	if (split[1].num_lines < 1)
		return;
	split[1].suspect = origin_incref(parent);
}

static void split_blame(struct scoreboard *sb,
			struct blame_entry *split,
			struct blame_entry *e)
{
	struct blame_entry *new_entry;

	if (split[0].suspect && split[2].suspect) {
		/* we need to split e into two and add another for parent */
		dup_entry(e, &split[0]);

		new_entry = xmalloc(sizeof(*new_entry));
		memcpy(new_entry, &(split[2]), sizeof(struct blame_entry));
		add_blame_entry(sb, new_entry);

		new_entry = xmalloc(sizeof(*new_entry));
		memcpy(new_entry, &(split[1]), sizeof(struct blame_entry));
		add_blame_entry(sb, new_entry);
	}
	else if (!split[0].suspect && !split[2].suspect)
		/* parent covers the entire area */
		dup_entry(e, &split[1]);
	else if (split[0].suspect) {
		dup_entry(e, &split[0]);

		new_entry = xmalloc(sizeof(*new_entry));
		memcpy(new_entry, &(split[1]), sizeof(struct blame_entry));
		add_blame_entry(sb, new_entry);
	}
	else {
		dup_entry(e, &split[1]);

		new_entry = xmalloc(sizeof(*new_entry));
		memcpy(new_entry, &(split[2]), sizeof(struct blame_entry));
		add_blame_entry(sb, new_entry);
	}

	if (DEBUG) { /* sanity */
		struct blame_entry *ent;
		int lno = sb->ent->lno, corrupt = 0;

		for (ent = sb->ent; ent; ent = ent->next) {
			if (lno != ent->lno)
				corrupt = 1;
			if (ent->s_lno < 0)
				corrupt = 1;
			lno += ent->num_lines;
		}
		if (corrupt) {
			lno = sb->ent->lno;
			for (ent = sb->ent; ent; ent = ent->next) {
				printf("L %8d l %8d n %8d\n",
				       lno, ent->lno, ent->num_lines);
				lno = ent->lno + ent->num_lines;
			}
			die("oops");
		}
	}
}

static void decref_split(struct blame_entry *split)
{
	int i;

	for (i = 0; i < 3; i++)
		origin_decref(split[i].suspect);
}

static void blame_overlap(struct scoreboard *sb, struct blame_entry *e,
			  int tlno, int plno, int same,
			  struct origin *parent)
{
	struct blame_entry split[3];

	split_overlap(split, e, tlno, plno, same, parent);
	if (split[1].suspect)
		split_blame(sb, split, e);
	decref_split(split);
}

static int find_last_in_target(struct scoreboard *sb, struct origin *target)
{
	struct blame_entry *e;
	int last_in_target = -1;

	for (e = sb->ent; e; e = e->next) {
		if (e->guilty || cmp_suspect(e->suspect, target))
			continue;
		if (last_in_target < e->s_lno + e->num_lines)
			last_in_target = e->s_lno + e->num_lines;
	}
	return last_in_target;
}

static void blame_chunk(struct scoreboard *sb,
			int tlno, int plno, int same,
			struct origin *target, struct origin *parent)
{
	struct blame_entry *e;

	for (e = sb->ent; e; e = e->next) {
		if (e->guilty || cmp_suspect(e->suspect, target))
			continue;
		if (same <= e->s_lno)
			continue;
		if (tlno < e->s_lno + e->num_lines)
			blame_overlap(sb, e, tlno, plno, same, parent);
	}
}

static int pass_blame_to_parent(struct scoreboard *sb,
				struct origin *target,
				struct origin *parent)
{
	int i, last_in_target, plno, tlno;
	struct patch *patch;

	last_in_target = find_last_in_target(sb, target);
	if (last_in_target < 0)
		return 1; /* nothing remains for this target */

	patch = get_patch(parent, target);
	plno = tlno = 0;
	for (i = 0; i < patch->num; i++) {
		struct chunk *chunk = &patch->chunks[i];

		blame_chunk(sb, tlno, plno, chunk->same, target, parent);
		plno = chunk->p_next;
		tlno = chunk->t_next;
	}
	/* rest (i.e. anything above tlno) are the same as parent */
	blame_chunk(sb, tlno, plno, last_in_target, target, parent);

	free_patch(patch);
	return 0;
}

static unsigned ent_score(struct scoreboard *sb, struct blame_entry *e)
{
	unsigned score;
	const char *cp, *ep;

	if (e->score)
		return e->score;

	score = 1;
	cp = nth_line(sb, e->lno);
	ep = nth_line(sb, e->lno + e->num_lines);
	while (cp < ep) {
		unsigned ch = *((unsigned char *)cp);
		if (isalnum(ch))
			score++;
		cp++;
	}
	e->score = score;
	return score;
}

static void copy_split_if_better(struct scoreboard *sb,
				 struct blame_entry *best_so_far,
				 struct blame_entry *this)
{
	int i;

	if (!this[1].suspect)
		return;
	if (best_so_far[1].suspect) {
		if (ent_score(sb, &this[1]) < ent_score(sb, &best_so_far[1]))
			return;
	}

	for (i = 0; i < 3; i++)
		origin_incref(this[i].suspect);
	decref_split(best_so_far);
	memcpy(best_so_far, this, sizeof(struct blame_entry [3]));
}

static void find_copy_in_blob(struct scoreboard *sb,
			      struct blame_entry *ent,
			      struct origin *parent,
			      struct blame_entry *split,
			      mmfile_t *file_p)
{
	const char *cp;
	int cnt;
	mmfile_t file_o;
	struct patch *patch;
	int i, plno, tlno;

	cp = nth_line(sb, ent->lno);
	file_o.ptr = (char*) cp;
	cnt = ent->num_lines;

	while (cnt && cp < sb->final_buf + sb->final_buf_size) {
		if (*cp++ == '\n')
			cnt--;
	}
	file_o.size = cp - file_o.ptr;

	patch = compare_buffer(file_p, &file_o, 1);

	memset(split, 0, sizeof(struct blame_entry [3]));
	plno = tlno = 0;
	for (i = 0; i < patch->num; i++) {
		struct chunk *chunk = &patch->chunks[i];

		/* tlno to chunk->same are the same as ent */
		if (ent->num_lines <= tlno)
			break;
		if (tlno < chunk->same) {
			struct blame_entry this[3];
			split_overlap(this, ent,
				      tlno + ent->s_lno, plno,
				      chunk->same + ent->s_lno,
				      parent);
			copy_split_if_better(sb, split, this);
			decref_split(this);
		}
		plno = chunk->p_next;
		tlno = chunk->t_next;
	}
	free_patch(patch);
}

static int find_move_in_parent(struct scoreboard *sb,
			       struct origin *target,
			       struct origin *parent)
{
	int last_in_target, made_progress;
	struct blame_entry *e, split[3];
	mmfile_t file_p;

	last_in_target = find_last_in_target(sb, target);
	if (last_in_target < 0)
		return 1; /* nothing remains for this target */

	fill_origin_blob(parent, &file_p);
	if (!file_p.ptr)
		return 0;

	made_progress = 1;
	while (made_progress) {
		made_progress = 0;
		for (e = sb->ent; e; e = e->next) {
			if (e->guilty || cmp_suspect(e->suspect, target))
				continue;
			find_copy_in_blob(sb, e, parent, split, &file_p);
			if (split[1].suspect &&
			    blame_move_score < ent_score(sb, &split[1])) {
				split_blame(sb, split, e);
				made_progress = 1;
			}
			decref_split(split);
		}
	}
	return 0;
}


struct blame_list {
	struct blame_entry *ent;
	struct blame_entry split[3];
};

static struct blame_list *setup_blame_list(struct scoreboard *sb,
					   struct origin *target,
					   int *num_ents_p)
{
	struct blame_entry *e;
	int num_ents, i;
	struct blame_list *blame_list = NULL;

	/* Count the number of entries the target is suspected for,
	 * and prepare a list of entry and the best split.
	 */
	for (e = sb->ent, num_ents = 0; e; e = e->next)
		if (!e->guilty && !cmp_suspect(e->suspect, target))
			num_ents++;
	if (num_ents) {
		blame_list = xcalloc(num_ents, sizeof(struct blame_list));
		for (e = sb->ent, i = 0; e; e = e->next)
			if (!e->guilty && !cmp_suspect(e->suspect, target))
				blame_list[i++].ent = e;
	}
	*num_ents_p = num_ents;
	return blame_list;
}

static int find_copy_in_parent(struct scoreboard *sb,
			       struct origin *target,
			       struct commit *parent,
			       struct origin *porigin,
			       int opt)
{
	struct diff_options diff_opts;
	const char *paths[1];
	int i, j;
	int retval;
	struct blame_list *blame_list;
	int num_ents;

	blame_list = setup_blame_list(sb, target, &num_ents);
	if (!blame_list)
		return 1; /* nothing remains for this target */

	diff_setup(&diff_opts);
	diff_opts.recursive = 1;
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;

	paths[0] = NULL;
	diff_tree_setup_paths(paths, &diff_opts);
	if (diff_setup_done(&diff_opts) < 0)
		die("diff-setup");

	/* Try "find copies harder" on new path if requested;
	 * we do not want to use diffcore_rename() actually to
	 * match things up; find_copies_harder is set only to
	 * force diff_tree_sha1() to feed all filepairs to diff_queue,
	 * and this code needs to be after diff_setup_done(), which
	 * usually makes find-copies-harder imply copy detection.
	 */
	if ((opt & PICKAXE_BLAME_COPY_HARDER) &&
	    (!porigin || strcmp(target->path, porigin->path)))
		diff_opts.find_copies_harder = 1;

	diff_tree_sha1(parent->tree->object.sha1,
		       target->commit->tree->object.sha1,
		       "", &diff_opts);

	if (!diff_opts.find_copies_harder)
		diffcore_std(&diff_opts);

	retval = 0;
	while (1) {
		int made_progress = 0;

		for (i = 0; i < diff_queued_diff.nr; i++) {
			struct diff_filepair *p = diff_queued_diff.queue[i];
			struct origin *norigin;
			mmfile_t file_p;
			struct blame_entry this[3];

			if (!DIFF_FILE_VALID(p->one))
				continue; /* does not exist in parent */
			if (porigin && !strcmp(p->one->path, porigin->path))
				/* find_move already dealt with this path */
				continue;

			norigin = get_origin(sb, parent, p->one->path);
			hashcpy(norigin->blob_sha1, p->one->sha1);
			fill_origin_blob(norigin, &file_p);
			if (!file_p.ptr)
				continue;

			for (j = 0; j < num_ents; j++) {
				find_copy_in_blob(sb, blame_list[j].ent,
						  norigin, this, &file_p);
				copy_split_if_better(sb, blame_list[j].split,
						     this);
				decref_split(this);
			}
			origin_decref(norigin);
		}

		for (j = 0; j < num_ents; j++) {
			struct blame_entry *split = blame_list[j].split;
			if (split[1].suspect &&
			    blame_copy_score < ent_score(sb, &split[1])) {
				split_blame(sb, split, blame_list[j].ent);
				made_progress = 1;
			}
			decref_split(split);
		}
		free(blame_list);

		if (!made_progress)
			break;
		blame_list = setup_blame_list(sb, target, &num_ents);
		if (!blame_list) {
			retval = 1;
			break;
		}
	}
	diff_flush(&diff_opts);

	return retval;
}

/* The blobs of origin and porigin exactly match, so everything
 * origin is suspected for can be blamed on the parent.
 */
static void pass_whole_blame(struct scoreboard *sb,
			     struct origin *origin, struct origin *porigin)
{
	struct blame_entry *e;

	if (!porigin->file.ptr && origin->file.ptr) {
		/* Steal its file */
		porigin->file = origin->file;
		origin->file.ptr = NULL;
	}
	for (e = sb->ent; e; e = e->next) {
		if (cmp_suspect(e->suspect, origin))
			continue;
		origin_incref(porigin);
		origin_decref(e->suspect);
		e->suspect = porigin;
	}
}

#define MAXPARENT 16

static void pass_blame(struct scoreboard *sb, struct origin *origin, int opt)
{
	int i, pass;
	struct commit *commit = origin->commit;
	struct commit_list *parent;
	struct origin *parent_origin[MAXPARENT], *porigin;

	memset(parent_origin, 0, sizeof(parent_origin));

	/* The first pass looks for unrenamed path to optimize for
	 * common cases, then we look for renames in the second pass.
	 */
	for (pass = 0; pass < 2; pass++) {
		struct origin *(*find)(struct scoreboard *,
				       struct commit *, struct origin *);
		find = pass ? find_rename : find_origin;

		for (i = 0, parent = commit->parents;
		     i < MAXPARENT && parent;
		     parent = parent->next, i++) {
			struct commit *p = parent->item;
			int j, same;

			if (parent_origin[i])
				continue;
			if (parse_commit(p))
				continue;
			porigin = find(sb, p, origin);
			if (!porigin)
				continue;
			if (!hashcmp(porigin->blob_sha1, origin->blob_sha1)) {
				pass_whole_blame(sb, origin, porigin);
				origin_decref(porigin);
				goto finish;
			}
			for (j = same = 0; j < i; j++)
				if (parent_origin[j] &&
				    !hashcmp(parent_origin[j]->blob_sha1,
					     porigin->blob_sha1)) {
					same = 1;
					break;
				}
			if (!same)
				parent_origin[i] = porigin;
			else
				origin_decref(porigin);
		}
	}

	num_commits++;
	for (i = 0, parent = commit->parents;
	     i < MAXPARENT && parent;
	     parent = parent->next, i++) {
		struct origin *porigin = parent_origin[i];
		if (!porigin)
			continue;
		if (pass_blame_to_parent(sb, origin, porigin))
			goto finish;
	}

	/*
	 * Optionally run "miff" to find moves in parents' files here.
	 */
	if (opt & PICKAXE_BLAME_MOVE)
		for (i = 0, parent = commit->parents;
		     i < MAXPARENT && parent;
		     parent = parent->next, i++) {
			struct origin *porigin = parent_origin[i];
			if (!porigin)
				continue;
			if (find_move_in_parent(sb, origin, porigin))
				goto finish;
		}

	/*
	 * Optionally run "ciff" to find copies from parents' files here.
	 */
	if (opt & PICKAXE_BLAME_COPY)
		for (i = 0, parent = commit->parents;
		     i < MAXPARENT && parent;
		     parent = parent->next, i++) {
			struct origin *porigin = parent_origin[i];
			if (find_copy_in_parent(sb, origin, parent->item,
						porigin, opt))
				goto finish;
		}

 finish:
	for (i = 0; i < MAXPARENT; i++)
		origin_decref(parent_origin[i]);
}

static void assign_blame(struct scoreboard *sb, struct rev_info *revs, int opt)
{
	while (1) {
		struct blame_entry *ent;
		struct commit *commit;
		struct origin *suspect = NULL;

		/* find one suspect to break down */
		for (ent = sb->ent; !suspect && ent; ent = ent->next)
			if (!ent->guilty)
				suspect = ent->suspect;
		if (!suspect)
			return; /* all done */

		origin_incref(suspect);
		commit = suspect->commit;
		if (!commit->object.parsed)
			parse_commit(commit);
		if (!(commit->object.flags & UNINTERESTING) &&
		    !(revs->max_age != -1 && commit->date  < revs->max_age))
			pass_blame(sb, suspect, opt);

		/* Take responsibility for the remaining entries */
		for (ent = sb->ent; ent; ent = ent->next)
			if (!cmp_suspect(ent->suspect, suspect))
				ent->guilty = 1;
		origin_decref(suspect);

		if (DEBUG) /* sanity */
			sanity_check_refcnt(sb);
	}
}

static const char *format_time(unsigned long time, const char *tz_str,
			       int show_raw_time)
{
	static char time_buf[128];
	time_t t = time;
	int minutes, tz;
	struct tm *tm;

	if (show_raw_time) {
		sprintf(time_buf, "%lu %s", time, tz_str);
		return time_buf;
	}

	tz = atoi(tz_str);
	minutes = tz < 0 ? -tz : tz;
	minutes = (minutes / 100)*60 + (minutes % 100);
	minutes = tz < 0 ? -minutes : minutes;
	t = time + minutes * 60;
	tm = gmtime(&t);

	strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S ", tm);
	strcat(time_buf, tz_str);
	return time_buf;
}

struct commit_info
{
	char *author;
	char *author_mail;
	unsigned long author_time;
	char *author_tz;

	/* filled only when asked for details */
	char *committer;
	char *committer_mail;
	unsigned long committer_time;
	char *committer_tz;

	char *summary;
};

static void get_ac_line(const char *inbuf, const char *what,
			int bufsz, char *person, char **mail,
			unsigned long *time, char **tz)
{
	int len;
	char *tmp, *endp;

	tmp = strstr(inbuf, what);
	if (!tmp)
		goto error_out;
	tmp += strlen(what);
	endp = strchr(tmp, '\n');
	if (!endp)
		len = strlen(tmp);
	else
		len = endp - tmp;
	if (bufsz <= len) {
	error_out:
		/* Ugh */
		person = *mail = *tz = "(unknown)";
		*time = 0;
		return;
	}
	memcpy(person, tmp, len);

	tmp = person;
	tmp += len;
	*tmp = 0;
	while (*tmp != ' ')
		tmp--;
	*tz = tmp+1;

	*tmp = 0;
	while (*tmp != ' ')
		tmp--;
	*time = strtoul(tmp, NULL, 10);

	*tmp = 0;
	while (*tmp != ' ')
		tmp--;
	*mail = tmp + 1;
	*tmp = 0;
}

static void get_commit_info(struct commit *commit,
			    struct commit_info *ret,
			    int detailed)
{
	int len;
	char *tmp, *endp;
	static char author_buf[1024];
	static char committer_buf[1024];
	static char summary_buf[1024];

	/* We've operated without save_commit_buffer, so
	 * we now need to populate them for output.
	 */
	if (!commit->buffer) {
		char type[20];
		unsigned long size;
		commit->buffer =
			read_sha1_file(commit->object.sha1, type, &size);
	}
	ret->author = author_buf;
	get_ac_line(commit->buffer, "\nauthor ",
		    sizeof(author_buf), author_buf, &ret->author_mail,
		    &ret->author_time, &ret->author_tz);

	if (!detailed)
		return;

	ret->committer = committer_buf;
	get_ac_line(commit->buffer, "\ncommitter ",
		    sizeof(committer_buf), committer_buf, &ret->committer_mail,
		    &ret->committer_time, &ret->committer_tz);

	ret->summary = summary_buf;
	tmp = strstr(commit->buffer, "\n\n");
	if (!tmp) {
	error_out:
		sprintf(summary_buf, "(%s)", sha1_to_hex(commit->object.sha1));
		return;
	}
	tmp += 2;
	endp = strchr(tmp, '\n');
	if (!endp)
		goto error_out;
	len = endp - tmp;
	if (len >= sizeof(summary_buf))
		goto error_out;
	memcpy(summary_buf, tmp, len);
	summary_buf[len] = 0;
}

#define OUTPUT_ANNOTATE_COMPAT	001
#define OUTPUT_LONG_OBJECT_NAME	002
#define OUTPUT_RAW_TIMESTAMP	004
#define OUTPUT_PORCELAIN	010
#define OUTPUT_SHOW_NAME	020
#define OUTPUT_SHOW_NUMBER	040
#define OUTPUT_SHOW_SCORE      0100

static void emit_porcelain(struct scoreboard *sb, struct blame_entry *ent)
{
	int cnt;
	const char *cp;
	struct origin *suspect = ent->suspect;
	char hex[41];

	strcpy(hex, sha1_to_hex(suspect->commit->object.sha1));
	printf("%s%c%d %d %d\n",
	       hex,
	       ent->guilty ? ' ' : '*', // purely for debugging
	       ent->s_lno + 1,
	       ent->lno + 1,
	       ent->num_lines);
	if (!(suspect->commit->object.flags & METAINFO_SHOWN)) {
		struct commit_info ci;
		suspect->commit->object.flags |= METAINFO_SHOWN;
		get_commit_info(suspect->commit, &ci, 1);
		printf("author %s\n", ci.author);
		printf("author-mail %s\n", ci.author_mail);
		printf("author-time %lu\n", ci.author_time);
		printf("author-tz %s\n", ci.author_tz);
		printf("committer %s\n", ci.committer);
		printf("committer-mail %s\n", ci.committer_mail);
		printf("committer-time %lu\n", ci.committer_time);
		printf("committer-tz %s\n", ci.committer_tz);
		printf("filename %s\n", suspect->path);
		printf("summary %s\n", ci.summary);
	}
	else if (suspect->commit->object.flags & MORE_THAN_ONE_PATH)
		printf("filename %s\n", suspect->path);

	cp = nth_line(sb, ent->lno);
	for (cnt = 0; cnt < ent->num_lines; cnt++) {
		char ch;
		if (cnt)
			printf("%s %d %d\n", hex,
			       ent->s_lno + 1 + cnt,
			       ent->lno + 1 + cnt);
		putchar('\t');
		do {
			ch = *cp++;
			putchar(ch);
		} while (ch != '\n' &&
			 cp < sb->final_buf + sb->final_buf_size);
	}
}

static void emit_other(struct scoreboard *sb, struct blame_entry *ent, int opt)
{
	int cnt;
	const char *cp;
	struct origin *suspect = ent->suspect;
	struct commit_info ci;
	char hex[41];
	int show_raw_time = !!(opt & OUTPUT_RAW_TIMESTAMP);

	get_commit_info(suspect->commit, &ci, 1);
	strcpy(hex, sha1_to_hex(suspect->commit->object.sha1));

	cp = nth_line(sb, ent->lno);
	for (cnt = 0; cnt < ent->num_lines; cnt++) {
		char ch;

		printf("%.*s", (opt & OUTPUT_LONG_OBJECT_NAME) ? 40 : 8, hex);
		if (opt & OUTPUT_ANNOTATE_COMPAT)
			printf("\t(%10s\t%10s\t%d)", ci.author,
			       format_time(ci.author_time, ci.author_tz,
					   show_raw_time),
			       ent->lno + 1 + cnt);
		else {
			if (opt & OUTPUT_SHOW_SCORE)
				printf(" %*d %02d",
				       max_score_digits, ent->score,
				       ent->suspect->refcnt);
			if (opt & OUTPUT_SHOW_NAME)
				printf(" %-*.*s", longest_file, longest_file,
				       suspect->path);
			if (opt & OUTPUT_SHOW_NUMBER)
				printf(" %*d", max_orig_digits,
				       ent->s_lno + 1 + cnt);
			printf(" (%-*.*s %10s %*d) ",
			       longest_author, longest_author, ci.author,
			       format_time(ci.author_time, ci.author_tz,
					   show_raw_time),
			       max_digits, ent->lno + 1 + cnt);
		}
		do {
			ch = *cp++;
			putchar(ch);
		} while (ch != '\n' &&
			 cp < sb->final_buf + sb->final_buf_size);
	}
}

static void output(struct scoreboard *sb, int option)
{
	struct blame_entry *ent;

	if (option & OUTPUT_PORCELAIN) {
		for (ent = sb->ent; ent; ent = ent->next) {
			struct blame_entry *oth;
			struct origin *suspect = ent->suspect;
			struct commit *commit = suspect->commit;
			if (commit->object.flags & MORE_THAN_ONE_PATH)
				continue;
			for (oth = ent->next; oth; oth = oth->next) {
				if ((oth->suspect->commit != commit) ||
				    !strcmp(oth->suspect->path, suspect->path))
					continue;
				commit->object.flags |= MORE_THAN_ONE_PATH;
				break;
			}
		}
	}

	for (ent = sb->ent; ent; ent = ent->next) {
		if (option & OUTPUT_PORCELAIN)
			emit_porcelain(sb, ent);
		else {
			emit_other(sb, ent, option);
		}
	}
}

static int prepare_lines(struct scoreboard *sb)
{
	const char *buf = sb->final_buf;
	unsigned long len = sb->final_buf_size;
	int num = 0, incomplete = 0, bol = 1;

	if (len && buf[len-1] != '\n')
		incomplete++; /* incomplete line at the end */
	while (len--) {
		if (bol) {
			sb->lineno = xrealloc(sb->lineno,
					      sizeof(int* ) * (num + 1));
			sb->lineno[num] = buf - sb->final_buf;
			bol = 0;
		}
		if (*buf++ == '\n') {
			num++;
			bol = 1;
		}
	}
	sb->lineno = xrealloc(sb->lineno,
			      sizeof(int* ) * (num + incomplete + 1));
	sb->lineno[num + incomplete] = buf - sb->final_buf;
	sb->num_lines = num + incomplete;
	return sb->num_lines;
}

static int read_ancestry(const char *graft_file)
{
	FILE *fp = fopen(graft_file, "r");
	char buf[1024];
	if (!fp)
		return -1;
	while (fgets(buf, sizeof(buf), fp)) {
		/* The format is just "Commit Parent1 Parent2 ...\n" */
		int len = strlen(buf);
		struct commit_graft *graft = read_graft_line(buf, len);
		if (graft)
			register_commit_graft(graft, 0);
	}
	fclose(fp);
	return 0;
}

static int lineno_width(int lines)
{
        int i, width;

        for (width = 1, i = 10; i <= lines + 1; width++)
                i *= 10;
        return width;
}

static void find_alignment(struct scoreboard *sb, int *option)
{
	int longest_src_lines = 0;
	int longest_dst_lines = 0;
	unsigned largest_score = 0;
	struct blame_entry *e;

	for (e = sb->ent; e; e = e->next) {
		struct origin *suspect = e->suspect;
		struct commit_info ci;
		int num;

		if (!(suspect->commit->object.flags & METAINFO_SHOWN)) {
			suspect->commit->object.flags |= METAINFO_SHOWN;
			get_commit_info(suspect->commit, &ci, 1);
			if (strcmp(suspect->path, sb->path))
				*option |= OUTPUT_SHOW_NAME;
			num = strlen(suspect->path);
			if (longest_file < num)
				longest_file = num;
			num = strlen(ci.author);
			if (longest_author < num)
				longest_author = num;
		}
		num = e->s_lno + e->num_lines;
		if (longest_src_lines < num)
			longest_src_lines = num;
		num = e->lno + e->num_lines;
		if (longest_dst_lines < num)
			longest_dst_lines = num;
		if (largest_score < ent_score(sb, e))
			largest_score = ent_score(sb, e);
	}
	max_orig_digits = lineno_width(longest_src_lines);
	max_digits = lineno_width(longest_dst_lines);
	max_score_digits = lineno_width(largest_score);
}

static void sanity_check_refcnt(struct scoreboard *sb)
{
	int baa = 0;
	struct blame_entry *ent;

	for (ent = sb->ent; ent; ent = ent->next) {
		/* Nobody should have zero or negative refcnt */
		if (ent->suspect->refcnt <= 0) {
			fprintf(stderr, "%s in %s has negative refcnt %d\n",
				ent->suspect->path,
				sha1_to_hex(ent->suspect->commit->object.sha1),
				ent->suspect->refcnt);
			baa = 1;
		}
	}
	for (ent = sb->ent; ent; ent = ent->next) {
		/* Mark the ones that haven't been checked */
		if (0 < ent->suspect->refcnt)
			ent->suspect->refcnt = -ent->suspect->refcnt;
	}
	for (ent = sb->ent; ent; ent = ent->next) {
		/* then pick each and see if they have the the correct
		 * refcnt.
		 */
		int found;
		struct blame_entry *e;
		struct origin *suspect = ent->suspect;

		if (0 < suspect->refcnt)
			continue;
		suspect->refcnt = -suspect->refcnt; /* Unmark */
		for (found = 0, e = sb->ent; e; e = e->next) {
			if (e->suspect != suspect)
				continue;
			found++;
		}
		if (suspect->refcnt != found) {
			fprintf(stderr, "%s in %s has refcnt %d, not %d\n",
				ent->suspect->path,
				sha1_to_hex(ent->suspect->commit->object.sha1),
				ent->suspect->refcnt, found);
			baa = 2;
		}
	}
	if (baa) {
		int opt = 0160;
		find_alignment(sb, &opt);
		output(sb, opt);
		die("Baa %d!", baa);
	}
}

static int has_path_in_work_tree(const char *path)
{
	struct stat st;
	return !lstat(path, &st);
}

static unsigned parse_score(const char *arg)
{
	char *end;
	unsigned long score = strtoul(arg, &end, 10);
	if (*end)
		return 0;
	return score;
}

static const char *add_prefix(const char *prefix, const char *path)
{
	if (!prefix || !prefix[0])
		return path;
	return prefix_path(prefix, strlen(prefix), path);
}

static const char *parse_loc(const char *spec,
			     struct scoreboard *sb, long lno,
			     long begin, long *ret)
{
	char *term;
	const char *line;
	long num;
	int reg_error;
	regex_t regexp;
	regmatch_t match[1];

	/* Allow "-L <something>,+20" to mean starting at <something>
	 * for 20 lines, or "-L <something>,-5" for 5 lines ending at
	 * <something>.
	 */
	if (1 < begin && (spec[0] == '+' || spec[0] == '-')) {
		num = strtol(spec + 1, &term, 10);
		if (term != spec + 1) {
			if (spec[0] == '-')
				num = 0 - num;
			if (0 < num)
				*ret = begin + num - 2;
			else if (!num)
				*ret = begin;
			else
				*ret = begin + num;
			return term;
		}
		return spec;
	}
	num = strtol(spec, &term, 10);
	if (term != spec) {
		*ret = num;
		return term;
	}
	if (spec[0] != '/')
		return spec;

	/* it could be a regexp of form /.../ */
	for (term = (char*) spec + 1; *term && *term != '/'; term++) {
		if (*term == '\\')
			term++;
	}
	if (*term != '/')
		return spec;

	/* try [spec+1 .. term-1] as regexp */
	*term = 0;
	begin--; /* input is in human terms */
	line = nth_line(sb, begin);

	if (!(reg_error = regcomp(&regexp, spec + 1, REG_NEWLINE)) &&
	    !(reg_error = regexec(&regexp, line, 1, match, 0))) {
		const char *cp = line + match[0].rm_so;
		const char *nline;

		while (begin++ < lno) {
			nline = nth_line(sb, begin);
			if (line <= cp && cp < nline)
				break;
			line = nline;
		}
		*ret = begin;
		regfree(&regexp);
		*term++ = '/';
		return term;
	}
	else {
		char errbuf[1024];
		regerror(reg_error, &regexp, errbuf, 1024);
		die("-L parameter '%s': %s", spec + 1, errbuf);
	}
}

static void prepare_blame_range(struct scoreboard *sb,
				const char *bottomtop,
				long lno,
				long *bottom, long *top)
{
	const char *term;

	term = parse_loc(bottomtop, sb, lno, 1, bottom);
	if (*term == ',') {
		term = parse_loc(term + 1, sb, lno, *bottom + 1, top);
		if (*term)
			usage(blame_usage);
	}
	if (*term)
		usage(blame_usage);
}

int cmd_blame(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	const char *path;
	struct scoreboard sb;
	struct origin *o;
	struct blame_entry *ent;
	int i, seen_dashdash, unk, opt;
	long bottom, top, lno;
	int output_option = 0;
	const char *revs_file = NULL;
	const char *final_commit_name = NULL;
	char type[10];
	const char *bottomtop = NULL;

	save_commit_buffer = 0;

	opt = 0;
	seen_dashdash = 0;
	for (unk = i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (*arg != '-')
			break;
		else if (!strcmp("-c", arg))
			output_option |= OUTPUT_ANNOTATE_COMPAT;
		else if (!strcmp("-t", arg))
			output_option |= OUTPUT_RAW_TIMESTAMP;
		else if (!strcmp("-l", arg))
			output_option |= OUTPUT_LONG_OBJECT_NAME;
		else if (!strcmp("-S", arg) && ++i < argc)
			revs_file = argv[i];
		else if (!strncmp("-M", arg, 2)) {
			opt |= PICKAXE_BLAME_MOVE;
			blame_move_score = parse_score(arg+2);
		}
		else if (!strncmp("-C", arg, 2)) {
			if (opt & PICKAXE_BLAME_COPY)
				opt |= PICKAXE_BLAME_COPY_HARDER;
			opt |= PICKAXE_BLAME_COPY | PICKAXE_BLAME_MOVE;
			blame_copy_score = parse_score(arg+2);
		}
		else if (!strncmp("-L", arg, 2)) {
			if (!arg[2]) {
				if (++i >= argc)
					usage(blame_usage);
				arg = argv[i];
			}
			else
				arg += 2;
			if (bottomtop)
				die("More than one '-L n,m' option given");
			bottomtop = arg;
		}
		else if (!strcmp("--score-debug", arg))
			output_option |= OUTPUT_SHOW_SCORE;
		else if (!strcmp("-f", arg) ||
			 !strcmp("--show-name", arg))
			output_option |= OUTPUT_SHOW_NAME;
		else if (!strcmp("-n", arg) ||
			 !strcmp("--show-number", arg))
			output_option |= OUTPUT_SHOW_NUMBER;
		else if (!strcmp("-p", arg) ||
			 !strcmp("--porcelain", arg))
			output_option |= OUTPUT_PORCELAIN;
		else if (!strcmp("--", arg)) {
			seen_dashdash = 1;
			i++;
			break;
		}
		else
			argv[unk++] = arg;
	}

	if (!blame_move_score)
		blame_move_score = BLAME_DEFAULT_MOVE_SCORE;
	if (!blame_copy_score)
		blame_copy_score = BLAME_DEFAULT_COPY_SCORE;

	/* We have collected options unknown to us in argv[1..unk]
	 * which are to be passed to revision machinery if we are
	 * going to do the "bottom" procesing.
	 *
	 * The remaining are:
	 *
	 * (1) if seen_dashdash, its either
	 *     "-options -- <path>" or
	 *     "-options -- <path> <rev>".
	 *     but the latter is allowed only if there is no
	 *     options that we passed to revision machinery.
	 *
	 * (2) otherwise, we may have "--" somewhere later and
	 *     might be looking at the first one of multiple 'rev'
	 *     parameters (e.g. " master ^next ^maint -- path").
	 *     See if there is a dashdash first, and give the
	 *     arguments before that to revision machinery.
	 *     After that there must be one 'path'.
	 *
	 * (3) otherwise, its one of the three:
	 *     "-options <path> <rev>"
	 *     "-options <rev> <path>"
	 *     "-options <path>"
	 *     but again the first one is allowed only if
	 *     there is no options that we passed to revision
	 *     machinery.
	 */

	if (seen_dashdash) {
		/* (1) */
		if (argc <= i)
			usage(blame_usage);
		path = add_prefix(prefix, argv[i]);
		if (i + 1 == argc - 1) {
			if (unk != 1)
				usage(blame_usage);
			argv[unk++] = argv[i + 1];
		}
		else if (i + 1 != argc)
			/* garbage at end */
			usage(blame_usage);
	}
	else {
		int j;
		for (j = i; !seen_dashdash && j < argc; j++)
			if (!strcmp(argv[j], "--"))
				seen_dashdash = j;
		if (seen_dashdash) {
			if (seen_dashdash + 1 != argc - 1)
				usage(blame_usage);
			path = add_prefix(prefix, argv[seen_dashdash + 1]);
			for (j = i; j < seen_dashdash; j++)
				argv[unk++] = argv[j];
		}
		else {
			/* (3) */
			path = add_prefix(prefix, argv[i]);
			if (i + 1 == argc - 1) {
				final_commit_name = argv[i + 1];

				/* if (unk == 1) we could be getting
				 * old-style
				 */
				if (unk == 1 && !has_path_in_work_tree(path)) {
					path = add_prefix(prefix, argv[i + 1]);
					final_commit_name = argv[i];
				}
			}
			else if (i != argc - 1)
				usage(blame_usage); /* garbage at end */

			if (!has_path_in_work_tree(path))
				die("cannot stat path %s: %s",
				    path, strerror(errno));
		}
	}

	if (final_commit_name)
		argv[unk++] = final_commit_name;

	/* Now we got rev and path.  We do not want the path pruning
	 * but we may want "bottom" processing.
	 */
	argv[unk] = NULL;

	init_revisions(&revs, NULL);
	setup_revisions(unk, argv, &revs, "HEAD");
	memset(&sb, 0, sizeof(sb));

	/* There must be one and only one positive commit in the
	 * revs->pending array.
	 */
	for (i = 0; i < revs.pending.nr; i++) {
		struct object *obj = revs.pending.objects[i].item;
		if (obj->flags & UNINTERESTING)
			continue;
		while (obj->type == OBJ_TAG)
			obj = deref_tag(obj, NULL, 0);
		if (obj->type != OBJ_COMMIT)
			die("Non commit %s?",
			    revs.pending.objects[i].name);
		if (sb.final)
			die("More than one commit to dig from %s and %s?",
			    revs.pending.objects[i].name,
			    final_commit_name);
		sb.final = (struct commit *) obj;
		final_commit_name = revs.pending.objects[i].name;
	}

	if (!sb.final) {
		/* "--not A B -- path" without anything positive */
		unsigned char head_sha1[20];

		final_commit_name = "HEAD";
		if (get_sha1(final_commit_name, head_sha1))
			die("No such ref: HEAD");
		sb.final = lookup_commit_reference(head_sha1);
		add_pending_object(&revs, &(sb.final->object), "HEAD");
	}

	/* If we have bottom, this will mark the ancestors of the
	 * bottom commits we would reach while traversing as
	 * uninteresting.
	 */
	prepare_revision_walk(&revs);

	o = get_origin(&sb, sb.final, path);
	if (fill_blob_sha1(o))
		die("no such path %s in %s", path, final_commit_name);

	sb.final_buf = read_sha1_file(o->blob_sha1, type, &sb.final_buf_size);
	num_read_blob++;
	lno = prepare_lines(&sb);

	bottom = top = 0;
	if (bottomtop)
		prepare_blame_range(&sb, bottomtop, lno, &bottom, &top);
	if (bottom && top && top < bottom) {
		long tmp;
		tmp = top; top = bottom; bottom = tmp;
	}
	if (bottom < 1)
		bottom = 1;
	if (top < 1)
		top = lno;
	bottom--;
	if (lno < top)
		die("file %s has only %lu lines", path, lno);

	ent = xcalloc(1, sizeof(*ent));
	ent->lno = bottom;
	ent->num_lines = top - bottom;
	ent->suspect = o;
	ent->s_lno = bottom;

	sb.ent = ent;
	sb.path = path;

	if (revs_file && read_ancestry(revs_file))
		die("reading graft file %s failed: %s",
		    revs_file, strerror(errno));

	assign_blame(&sb, &revs, opt);

	coalesce(&sb);

	if (!(output_option & OUTPUT_PORCELAIN))
		find_alignment(&sb, &output_option);

	output(&sb, output_option);
	free((void *)sb.final_buf);
	for (ent = sb.ent; ent; ) {
		struct blame_entry *e = ent->next;
		free(ent);
		ent = e;
	}

	if (DEBUG) {
		printf("num read blob: %d\n", num_read_blob);
		printf("num get patch: %d\n", num_get_patch);
		printf("num commits: %d\n", num_commits);
	}
	return 0;
}
