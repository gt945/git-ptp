#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "pkt-line.h"
#include "utf8.h"
#include "diff.h"
#include "revision.h"

int save_commit_buffer = 1;

struct sort_node
{
	/*
	 * the number of children of the associated commit
	 * that also occur in the list being sorted.
	 */
	unsigned int indegree;

	/*
	 * reference to original list item that we will re-use
	 * on output.
	 */
	struct commit_list * list_item;

};

const char *commit_type = "commit";

static struct commit *check_commit(struct object *obj,
				   const unsigned char *sha1,
				   int quiet)
{
	if (obj->type != OBJ_COMMIT) {
		if (!quiet)
			error("Object %s is a %s, not a commit",
			      sha1_to_hex(sha1), typename(obj->type));
		return NULL;
	}
	return (struct commit *) obj;
}

struct commit *lookup_commit_reference_gently(const unsigned char *sha1,
					      int quiet)
{
	struct object *obj = deref_tag(parse_object(sha1), NULL, 0);

	if (!obj)
		return NULL;
	return check_commit(obj, sha1, quiet);
}

struct commit *lookup_commit_reference(const unsigned char *sha1)
{
	return lookup_commit_reference_gently(sha1, 0);
}

struct commit *lookup_commit(const unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj)
		return create_object(sha1, OBJ_COMMIT, alloc_commit_node());
	if (!obj->type)
		obj->type = OBJ_COMMIT;
	return check_commit(obj, sha1, 0);
}

static unsigned long parse_commit_date(const char *buf)
{
	unsigned long date;

	if (memcmp(buf, "author", 6))
		return 0;
	while (*buf++ != '\n')
		/* nada */;
	if (memcmp(buf, "committer", 9))
		return 0;
	while (*buf++ != '>')
		/* nada */;
	date = strtoul(buf, NULL, 10);
	if (date == ULONG_MAX)
		date = 0;
	return date;
}

static struct commit_graft **commit_graft;
static int commit_graft_alloc, commit_graft_nr;

static int commit_graft_pos(const unsigned char *sha1)
{
	int lo, hi;
	lo = 0;
	hi = commit_graft_nr;
	while (lo < hi) {
		int mi = (lo + hi) / 2;
		struct commit_graft *graft = commit_graft[mi];
		int cmp = hashcmp(sha1, graft->sha1);
		if (!cmp)
			return mi;
		if (cmp < 0)
			hi = mi;
		else
			lo = mi + 1;
	}
	return -lo - 1;
}

int register_commit_graft(struct commit_graft *graft, int ignore_dups)
{
	int pos = commit_graft_pos(graft->sha1);

	if (0 <= pos) {
		if (ignore_dups)
			free(graft);
		else {
			free(commit_graft[pos]);
			commit_graft[pos] = graft;
		}
		return 1;
	}
	pos = -pos - 1;
	if (commit_graft_alloc <= ++commit_graft_nr) {
		commit_graft_alloc = alloc_nr(commit_graft_alloc);
		commit_graft = xrealloc(commit_graft,
					sizeof(*commit_graft) *
					commit_graft_alloc);
	}
	if (pos < commit_graft_nr)
		memmove(commit_graft + pos + 1,
			commit_graft + pos,
			(commit_graft_nr - pos - 1) *
			sizeof(*commit_graft));
	commit_graft[pos] = graft;
	return 0;
}

struct commit_graft *read_graft_line(char *buf, int len)
{
	/* The format is just "Commit Parent1 Parent2 ...\n" */
	int i;
	struct commit_graft *graft = NULL;

	if (buf[len-1] == '\n')
		buf[--len] = 0;
	if (buf[0] == '#' || buf[0] == '\0')
		return NULL;
	if ((len + 1) % 41) {
	bad_graft_data:
		error("bad graft data: %s", buf);
		free(graft);
		return NULL;
	}
	i = (len + 1) / 41 - 1;
	graft = xmalloc(sizeof(*graft) + 20 * i);
	graft->nr_parent = i;
	if (get_sha1_hex(buf, graft->sha1))
		goto bad_graft_data;
	for (i = 40; i < len; i += 41) {
		if (buf[i] != ' ')
			goto bad_graft_data;
		if (get_sha1_hex(buf + i + 1, graft->parent[i/41]))
			goto bad_graft_data;
	}
	return graft;
}

int read_graft_file(const char *graft_file)
{
	FILE *fp = fopen(graft_file, "r");
	char buf[1024];
	if (!fp)
		return -1;
	while (fgets(buf, sizeof(buf), fp)) {
		/* The format is just "Commit Parent1 Parent2 ...\n" */
		int len = strlen(buf);
		struct commit_graft *graft = read_graft_line(buf, len);
		if (!graft)
			continue;
		if (register_commit_graft(graft, 1))
			error("duplicate graft data: %s", buf);
	}
	fclose(fp);
	return 0;
}

static void prepare_commit_graft(void)
{
	static int commit_graft_prepared;
	char *graft_file;

	if (commit_graft_prepared)
		return;
	graft_file = get_graft_file();
	read_graft_file(graft_file);
	/* make sure shallows are read */
	is_repository_shallow();
	commit_graft_prepared = 1;
}

static struct commit_graft *lookup_commit_graft(const unsigned char *sha1)
{
	int pos;
	prepare_commit_graft();
	pos = commit_graft_pos(sha1);
	if (pos < 0)
		return NULL;
	return commit_graft[pos];
}

int write_shallow_commits(int fd, int use_pack_protocol)
{
	int i, count = 0;
	for (i = 0; i < commit_graft_nr; i++)
		if (commit_graft[i]->nr_parent < 0) {
			const char *hex =
				sha1_to_hex(commit_graft[i]->sha1);
			count++;
			if (use_pack_protocol)
				packet_write(fd, "shallow %s", hex);
			else {
				if (write_in_full(fd, hex,  40) != 40)
					break;
				if (write_in_full(fd, "\n", 1) != 1)
					break;
			}
		}
	return count;
}

int unregister_shallow(const unsigned char *sha1)
{
	int pos = commit_graft_pos(sha1);
	if (pos < 0)
		return -1;
	if (pos + 1 < commit_graft_nr)
		memcpy(commit_graft + pos, commit_graft + pos + 1,
				sizeof(struct commit_graft *)
				* (commit_graft_nr - pos - 1));
	commit_graft_nr--;
	return 0;
}

int parse_commit_buffer(struct commit *item, void *buffer, unsigned long size)
{
	char *tail = buffer;
	char *bufptr = buffer;
	unsigned char parent[20];
	struct commit_list **pptr;
	struct commit_graft *graft;
	unsigned n_refs = 0;

	if (item->object.parsed)
		return 0;
	item->object.parsed = 1;
	tail += size;
	if (tail <= bufptr + 5 || memcmp(bufptr, "tree ", 5))
		return error("bogus commit object %s", sha1_to_hex(item->object.sha1));
	if (tail <= bufptr + 45 || get_sha1_hex(bufptr + 5, parent) < 0)
		return error("bad tree pointer in commit %s",
			     sha1_to_hex(item->object.sha1));
	item->tree = lookup_tree(parent);
	if (item->tree)
		n_refs++;
	bufptr += 46; /* "tree " + "hex sha1" + "\n" */
	pptr = &item->parents;

	graft = lookup_commit_graft(item->object.sha1);
	while (bufptr + 48 < tail && !memcmp(bufptr, "parent ", 7)) {
		struct commit *new_parent;

		if (tail <= bufptr + 48 ||
		    get_sha1_hex(bufptr + 7, parent) ||
		    bufptr[47] != '\n')
			return error("bad parents in commit %s", sha1_to_hex(item->object.sha1));
		bufptr += 48;
		if (graft)
			continue;
		new_parent = lookup_commit(parent);
		if (new_parent) {
			pptr = &commit_list_insert(new_parent, pptr)->next;
			n_refs++;
		}
	}
	if (graft) {
		int i;
		struct commit *new_parent;
		for (i = 0; i < graft->nr_parent; i++) {
			new_parent = lookup_commit(graft->parent[i]);
			if (!new_parent)
				continue;
			pptr = &commit_list_insert(new_parent, pptr)->next;
			n_refs++;
		}
	}
	item->date = parse_commit_date(bufptr);

	if (track_object_refs) {
		unsigned i = 0;
		struct commit_list *p;
		struct object_refs *refs = alloc_object_refs(n_refs);
		if (item->tree)
			refs->ref[i++] = &item->tree->object;
		for (p = item->parents; p; p = p->next)
			refs->ref[i++] = &p->item->object;
		set_object_refs(&item->object, refs);
	}

	return 0;
}

int parse_commit(struct commit *item)
{
	enum object_type type;
	void *buffer;
	unsigned long size;
	int ret;

	if (item->object.parsed)
		return 0;
	buffer = read_sha1_file(item->object.sha1, &type, &size);
	if (!buffer)
		return error("Could not read %s",
			     sha1_to_hex(item->object.sha1));
	if (type != OBJ_COMMIT) {
		free(buffer);
		return error("Object %s not a commit",
			     sha1_to_hex(item->object.sha1));
	}
	ret = parse_commit_buffer(item, buffer, size);
	if (save_commit_buffer && !ret) {
		item->buffer = buffer;
		return 0;
	}
	free(buffer);
	return ret;
}

struct commit_list *commit_list_insert(struct commit *item, struct commit_list **list_p)
{
	struct commit_list *new_list = xmalloc(sizeof(struct commit_list));
	new_list->item = item;
	new_list->next = *list_p;
	*list_p = new_list;
	return new_list;
}

void free_commit_list(struct commit_list *list)
{
	while (list) {
		struct commit_list *temp = list;
		list = temp->next;
		free(temp);
	}
}

struct commit_list * insert_by_date(struct commit *item, struct commit_list **list)
{
	struct commit_list **pp = list;
	struct commit_list *p;
	while ((p = *pp) != NULL) {
		if (p->item->date < item->date) {
			break;
		}
		pp = &p->next;
	}
	return commit_list_insert(item, pp);
}


void sort_by_date(struct commit_list **list)
{
	struct commit_list *ret = NULL;
	while (*list) {
		insert_by_date((*list)->item, &ret);
		*list = (*list)->next;
	}
	*list = ret;
}

struct commit *pop_most_recent_commit(struct commit_list **list,
				      unsigned int mark)
{
	struct commit *ret = (*list)->item;
	struct commit_list *parents = ret->parents;
	struct commit_list *old = *list;

	*list = (*list)->next;
	free(old);

	while (parents) {
		struct commit *commit = parents->item;
		parse_commit(commit);
		if (!(commit->object.flags & mark)) {
			commit->object.flags |= mark;
			insert_by_date(commit, list);
		}
		parents = parents->next;
	}
	return ret;
}

void clear_commit_marks(struct commit *commit, unsigned int mark)
{
	while (commit) {
		struct commit_list *parents;

		if (!(mark & commit->object.flags))
			return;

		commit->object.flags &= ~mark;

		parents = commit->parents;
		if (!parents)
			return;

		while ((parents = parents->next))
			clear_commit_marks(parents->item, mark);

		commit = commit->parents->item;
	}
}

struct commit *pop_commit(struct commit_list **stack)
{
	struct commit_list *top = *stack;
	struct commit *item = top ? top->item : NULL;

	if (top) {
		*stack = top->next;
		free(top);
	}
	return item;
}

void topo_sort_default_setter(struct commit *c, void *data)
{
	c->util = data;
}

void *topo_sort_default_getter(struct commit *c)
{
	return c->util;
}

/*
 * Performs an in-place topological sort on the list supplied.
 */
void sort_in_topological_order(struct commit_list ** list, int lifo)
{
	sort_in_topological_order_fn(list, lifo, topo_sort_default_setter,
				     topo_sort_default_getter);
}

void sort_in_topological_order_fn(struct commit_list ** list, int lifo,
				  topo_sort_set_fn_t setter,
				  topo_sort_get_fn_t getter)
{
	struct commit_list * next = *list;
	struct commit_list * work = NULL, **insert;
	struct commit_list ** pptr = list;
	struct sort_node * nodes;
	struct sort_node * next_nodes;
	int count = 0;

	/* determine the size of the list */
	while (next) {
		next = next->next;
		count++;
	}

	if (!count)
		return;
	/* allocate an array to help sort the list */
	nodes = xcalloc(count, sizeof(*nodes));
	/* link the list to the array */
	next_nodes = nodes;
	next=*list;
	while (next) {
		next_nodes->list_item = next;
		setter(next->item, next_nodes);
		next_nodes++;
		next = next->next;
	}
	/* update the indegree */
	next=*list;
	while (next) {
		struct commit_list * parents = next->item->parents;
		while (parents) {
			struct commit * parent=parents->item;
			struct sort_node * pn = (struct sort_node *) getter(parent);

			if (pn)
				pn->indegree++;
			parents=parents->next;
		}
		next=next->next;
	}
	/*
	 * find the tips
	 *
	 * tips are nodes not reachable from any other node in the list
	 *
	 * the tips serve as a starting set for the work queue.
	 */
	next=*list;
	insert = &work;
	while (next) {
		struct sort_node * node = (struct sort_node *) getter(next->item);

		if (node->indegree == 0) {
			insert = &commit_list_insert(next->item, insert)->next;
		}
		next=next->next;
	}

	/* process the list in topological order */
	if (!lifo)
		sort_by_date(&work);
	while (work) {
		struct commit * work_item = pop_commit(&work);
		struct sort_node * work_node = (struct sort_node *) getter(work_item);
		struct commit_list * parents = work_item->parents;

		while (parents) {
			struct commit * parent=parents->item;
			struct sort_node * pn = (struct sort_node *) getter(parent);

			if (pn) {
				/*
				 * parents are only enqueued for emission
				 * when all their children have been emitted thereby
				 * guaranteeing topological order.
				 */
				pn->indegree--;
				if (!pn->indegree) {
					if (!lifo)
						insert_by_date(parent, &work);
					else
						commit_list_insert(parent, &work);
				}
			}
			parents=parents->next;
		}
		/*
		 * work_item is a commit all of whose children
		 * have already been emitted. we can emit it now.
		 */
		*pptr = work_node->list_item;
		pptr = &(*pptr)->next;
		*pptr = NULL;
		setter(work_item, NULL);
	}
	free(nodes);
}

/* merge-base stuff */

/* bits #0..15 in revision.h */
#define PARENT1		(1u<<16)
#define PARENT2		(1u<<17)
#define STALE		(1u<<18)
#define RESULT		(1u<<19)

static const unsigned all_flags = (PARENT1 | PARENT2 | STALE | RESULT);

static struct commit *interesting(struct commit_list *list)
{
	while (list) {
		struct commit *commit = list->item;
		list = list->next;
		if (commit->object.flags & STALE)
			continue;
		return commit;
	}
	return NULL;
}

static struct commit_list *merge_bases(struct commit *one, struct commit *two)
{
	struct commit_list *list = NULL;
	struct commit_list *result = NULL;

	if (one == two)
		/* We do not mark this even with RESULT so we do not
		 * have to clean it up.
		 */
		return commit_list_insert(one, &result);

	parse_commit(one);
	parse_commit(two);

	one->object.flags |= PARENT1;
	two->object.flags |= PARENT2;
	insert_by_date(one, &list);
	insert_by_date(two, &list);

	while (interesting(list)) {
		struct commit *commit;
		struct commit_list *parents;
		struct commit_list *n;
		int flags;

		commit = list->item;
		n = list->next;
		free(list);
		list = n;

		flags = commit->object.flags & (PARENT1 | PARENT2 | STALE);
		if (flags == (PARENT1 | PARENT2)) {
			if (!(commit->object.flags & RESULT)) {
				commit->object.flags |= RESULT;
				insert_by_date(commit, &result);
			}
			/* Mark parents of a found merge stale */
			flags |= STALE;
		}
		parents = commit->parents;
		while (parents) {
			struct commit *p = parents->item;
			parents = parents->next;
			if ((p->object.flags & flags) == flags)
				continue;
			parse_commit(p);
			p->object.flags |= flags;
			insert_by_date(p, &list);
		}
	}

	/* Clean up the result to remove stale ones */
	free_commit_list(list);
	list = result; result = NULL;
	while (list) {
		struct commit_list *n = list->next;
		if (!(list->item->object.flags & STALE))
			insert_by_date(list->item, &result);
		free(list);
		list = n;
	}
	return result;
}

struct commit_list *get_merge_bases(struct commit *one,
					struct commit *two, int cleanup)
{
	struct commit_list *list;
	struct commit **rslt;
	struct commit_list *result;
	int cnt, i, j;

	result = merge_bases(one, two);
	if (one == two)
		return result;
	if (!result || !result->next) {
		if (cleanup) {
			clear_commit_marks(one, all_flags);
			clear_commit_marks(two, all_flags);
		}
		return result;
	}

	/* There are more than one */
	cnt = 0;
	list = result;
	while (list) {
		list = list->next;
		cnt++;
	}
	rslt = xcalloc(cnt, sizeof(*rslt));
	for (list = result, i = 0; list; list = list->next)
		rslt[i++] = list->item;
	free_commit_list(result);

	clear_commit_marks(one, all_flags);
	clear_commit_marks(two, all_flags);
	for (i = 0; i < cnt - 1; i++) {
		for (j = i+1; j < cnt; j++) {
			if (!rslt[i] || !rslt[j])
				continue;
			result = merge_bases(rslt[i], rslt[j]);
			clear_commit_marks(rslt[i], all_flags);
			clear_commit_marks(rslt[j], all_flags);
			for (list = result; list; list = list->next) {
				if (rslt[i] == list->item)
					rslt[i] = NULL;
				if (rslt[j] == list->item)
					rslt[j] = NULL;
			}
		}
	}

	/* Surviving ones in rslt[] are the independent results */
	result = NULL;
	for (i = 0; i < cnt; i++) {
		if (rslt[i])
			insert_by_date(rslt[i], &result);
	}
	free(rslt);
	return result;
}

int in_merge_bases(struct commit *commit, struct commit **reference, int num)
{
	struct commit_list *bases, *b;
	int ret = 0;

	if (num == 1)
		bases = get_merge_bases(commit, *reference, 1);
	else
		die("not yet");
	for (b = bases; b; b = b->next) {
		if (!hashcmp(commit->object.sha1, b->item->object.sha1)) {
			ret = 1;
			break;
		}
	}

	free_commit_list(bases);
	return ret;
}
