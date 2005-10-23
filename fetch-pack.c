#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include "commit.h"
#include "tag.h"
#include <time.h>
#include <sys/wait.h>

static int quiet;
static int verbose;
static const char fetch_pack_usage[] =
"git-fetch-pack [-q] [-v] [--exec=upload-pack] [host:]directory <refs>...";
static const char *exec = "git-upload-pack";

#define COMPLETE	(1U << 0)
#define COMMON		(1U << 1)
#define COMMON_REF	(1U << 2 | COMMON)
#define SEEN		(1U << 3)
#define POPPED		(1U << 4)

static struct commit_list *rev_list = NULL;
static struct commit_list *rev_list_end = NULL;
static unsigned long non_common_revs = 0;

static void rev_list_append(struct commit *commit, int mark)
{
	if (!(commit->object.flags & mark)) {
		commit->object.flags |= mark;

		if (rev_list == NULL) {
			commit_list_insert(commit, &rev_list);
			rev_list_end = rev_list;
		} else {
			commit_list_insert(commit, &(rev_list_end->next));
			rev_list_end = rev_list_end->next;
		}

		if (!(commit->object.flags & COMMON))
			non_common_revs++;
	}
}

static int rev_list_append_sha1(const char *path, const unsigned char *sha1)
{
	struct object *o = deref_tag(parse_object(sha1));

	if (o->type == commit_type)
		rev_list_append((struct commit *)o, SEEN);

	return 0;
}

static void mark_common(struct commit *commit)
{
	if (commit != NULL && !(commit->object.flags & COMMON)) {
		struct object *o = (struct object *)commit;
		o->flags |= COMMON;
		if (!(o->flags & SEEN))
			rev_list_append(commit, SEEN);
		else {
			struct commit_list *parents;

			if (!(o->flags & POPPED))
				non_common_revs--;
			if (!o->parsed)
				parse_commit(commit);
			for (parents = commit->parents;
					parents;
					parents = parents->next)
				mark_common(parents->item);
		}
	}
}

/*
  Get the next rev to send, ignoring the common.
*/

static const unsigned char* get_rev()
{
	struct commit *commit = NULL;

	while (commit == NULL) {
		unsigned int mark;
		struct commit_list* parents;

		if (rev_list == NULL || non_common_revs == 0)
			return NULL;

		commit = rev_list->item;
		if (!(commit->object.parsed))
			parse_commit(commit);
		commit->object.flags |= POPPED;
		if (!(commit->object.flags & COMMON))
			non_common_revs--;
	
		parents = commit->parents;

		if (commit->object.flags & COMMON) {
			/* do not send "have", and ignore ancestors */
			commit = NULL;
			mark = COMMON | SEEN;
		} else if (commit->object.flags & COMMON_REF)
			/* send "have", and ignore ancestors */
			mark = COMMON | SEEN;
		else
			/* send "have", also for its ancestors */
			mark = SEEN;

		while (parents) {
			if (mark & COMMON)
				mark_common(parents->item);
			else
				rev_list_append(parents->item, mark);
			parents = parents->next;
		}

		rev_list = rev_list->next;
	}

	return commit->object.sha1;
}

static int find_common(int fd[2], unsigned char *result_sha1,
		       struct ref *refs)
{
	int fetching;
	int count = 0, flushes = 0, multi_ack = 0, retval;
	const unsigned char *sha1;

	for_each_ref(rev_list_append_sha1);

	fetching = 0;
	for ( ; refs ; refs = refs->next) {
		unsigned char *remote = refs->old_sha1;
		struct object *o;

		/*
		 * If that object is complete (i.e. it is an ancestor of a
		 * local ref), we tell them we have it but do not have to
		 * tell them about its ancestors, which they already know
		 * about.
		 *
		 * We use lookup_object here because we are only
		 * interested in the case we *know* the object is
		 * reachable and we have already scanned it.
		 */
		if (((o = lookup_object(remote)) != NULL) &&
		    (o->flags & COMPLETE)) {
			o = deref_tag(o);

			if (o->type == commit_type)
				rev_list_append((struct commit *)o,
						COMMON_REF | SEEN);

			continue;
		}

		packet_write(fd[1], "want %s multi_ack\n", sha1_to_hex(remote));
		fetching++;
	}
	packet_flush(fd[1]);
	if (!fetching)
		return 1;

	flushes = 0;
	retval = -1;
	while ((sha1 = get_rev())) {
		packet_write(fd[1], "have %s\n", sha1_to_hex(sha1));
		if (verbose)
			fprintf(stderr, "have %s\n", sha1_to_hex(sha1));
		if (!(31 & ++count)) {
			int ack;

			packet_flush(fd[1]);
			flushes++;

			/*
			 * We keep one window "ahead" of the other side, and
			 * will wait for an ACK only on the next one
			 */
			if (count == 32)
				continue;

			do {
				ack = get_ack(fd[0], result_sha1);
				if (verbose && ack)
					fprintf(stderr, "got ack %d %s\n", ack,
							sha1_to_hex(result_sha1));
				if (ack == 1) {
					if (!multi_ack)
						flushes = 0;
					retval = 0;
					goto done;
				} else if (ack == 2) {
					multi_ack = 1;
					mark_common((struct commit *)
							lookup_object(result_sha1));
					retval = 0;
				}
			} while(ack);
			flushes--;
		}
	}
done:
	if (multi_ack) {
		packet_flush(fd[1]);
		flushes++;
	}
	packet_write(fd[1], "done\n");
	if (verbose)
		fprintf(stderr, "done\n");
	if (retval != 0)
		flushes++;
	while (flushes) {
		if (get_ack(fd[0], result_sha1)) {
			if (verbose)
				fprintf(stderr, "got ack %s\n",
					sha1_to_hex(result_sha1));
			if (!multi_ack)
				return 0;
			retval = 0;
			continue;
		}
		flushes--;
	}
	return retval;
}

static struct commit_list *complete = NULL;

static int mark_complete(const char *path, const unsigned char *sha1)
{
	struct object *o = parse_object(sha1);

	while (o && o->type == tag_type) {
		struct tag *t = (struct tag *) o;
		if (!t->tagged)
			break; /* broken repository */
		o->flags |= COMPLETE;
		o = parse_object(t->tagged->sha1);
	}
	if (o && o->type == commit_type) {
		struct commit *commit = (struct commit *)o;
		commit->object.flags |= COMPLETE;
		insert_by_date(commit, &complete);
	}
	return 0;
}

static void mark_recent_complete_commits(unsigned long cutoff)
{
	while (complete && cutoff <= complete->item->date) {
		if (verbose)
			fprintf(stderr, "Marking %s as complete\n",
				sha1_to_hex(complete->item->object.sha1));
		pop_most_recent_commit(&complete, COMPLETE);
	}
}

static int everything_local(struct ref *refs)
{
	struct ref *ref;
	int retval;
	unsigned long cutoff = 0;

	track_object_refs = 0;
	save_commit_buffer = 0;

	for (ref = refs; ref; ref = ref->next) {
		struct object *o;

		o = parse_object(ref->old_sha1);
		if (!o)
			continue;

		/* We already have it -- which may mean that we were
		 * in sync with the other side at some time after
		 * that (it is OK if we guess wrong here).
		 */
		if (o->type == commit_type) {
			struct commit *commit = (struct commit *)o;
			if (!cutoff || cutoff < commit->date)
				cutoff = commit->date;
		}
	}

	for_each_ref(mark_complete);
	if (cutoff)
		mark_recent_complete_commits(cutoff);

	for (retval = 1; refs ; refs = refs->next) {
		const unsigned char *remote = refs->old_sha1;
		unsigned char local[20];
		struct object *o;

		o = parse_object(remote);
		if (!o || !(o->flags & COMPLETE)) {
			retval = 0;
			if (!verbose)
				continue;
			fprintf(stderr,
				"want %s (%s)\n", sha1_to_hex(remote),
				refs->name);
			continue;
		}

		memcpy(refs->new_sha1, local, 20);
		if (!verbose)
			continue;
		fprintf(stderr,
			"already have %s (%s)\n", sha1_to_hex(remote),
			refs->name);
	}
	return retval;
}

static int fetch_pack(int fd[2], int nr_match, char **match)
{
	struct ref *ref;
	unsigned char sha1[20];
	int status;
	pid_t pid;

	get_remote_heads(fd[0], &ref, nr_match, match, 1);
	if (!ref) {
		packet_flush(fd[1]);
		die("no matching remote head");
	}
	if (everything_local(ref)) {
		packet_flush(fd[1]);
		goto all_done;
	}
	if (find_common(fd, sha1, ref) < 0)
		fprintf(stderr, "warning: no common commits\n");
	pid = fork();
	if (pid < 0)
		die("git-fetch-pack: unable to fork off git-unpack-objects");
	if (!pid) {
		dup2(fd[0], 0);
		close(fd[0]);
		close(fd[1]);
		execlp("git-unpack-objects", "git-unpack-objects",
		       quiet ? "-q" : NULL, NULL);
		die("git-unpack-objects exec failed");
	}
	close(fd[0]);
	close(fd[1]);
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			die("waiting for git-unpack-objects: %s", strerror(errno));
	}
	if (WIFEXITED(status)) {
		int code = WEXITSTATUS(status);
		if (code)
			die("git-unpack-objects died with error code %d", code);
all_done:
		while (ref) {
			printf("%s %s\n",
			       sha1_to_hex(ref->old_sha1), ref->name);
			ref = ref->next;
		}
		return 0;
	}
	if (WIFSIGNALED(status)) {
		int sig = WTERMSIG(status);
		die("git-unpack-objects died of signal %d", sig);
	}
	die("Sherlock Holmes! git-unpack-objects died of unnatural causes %d!", status);
}

int main(int argc, char **argv)
{
	int i, ret, nr_heads;
	char *dest = NULL, **heads;
	int fd[2];
	pid_t pid;

	nr_heads = 0;
	heads = NULL;
	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (*arg == '-') {
			if (!strncmp("--exec=", arg, 7)) {
				exec = arg + 7;
				continue;
			}
			if (!strcmp("-q", arg)) {
				quiet = 1;
				continue;
			}
			if (!strcmp("-v", arg)) {
				verbose = 1;
				continue;
			}
			usage(fetch_pack_usage);
		}
		dest = arg;
		heads = argv + i + 1;
		nr_heads = argc - i - 1;
		break;
	}
	if (!dest)
		usage(fetch_pack_usage);
	pid = git_connect(fd, dest, exec);
	if (pid < 0)
		return 1;
	ret = fetch_pack(fd, nr_heads, heads);
	close(fd[0]);
	close(fd[1]);
	finish_connect(pid);
	return ret;
}
