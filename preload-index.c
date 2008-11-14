/*
 * Copyright (C) 2008 Linus Torvalds
 */
#include "cache.h"
#include <pthread.h>

/*
 * Mostly randomly chosen maximum thread counts: we
 * cap the parallelism to 20 threads, and we want
 * to have at least 500 lstat's per thread for it to
 * be worth starting a thread.
 */
#define MAX_PARALLEL (20)
#define THREAD_COST (500)

struct thread_data {
	pthread_t pthread;
	struct index_state *index;
	const char **pathspec;
	int offset, nr;
};

static void *preload_thread(void *_data)
{
	int nr;
	struct thread_data *p = _data;
	struct index_state *index = p->index;
	struct cache_entry **cep = index->cache + p->offset;

	nr = p->nr;
	if (nr + p->offset > index->cache_nr)
		nr = index->cache_nr - p->offset;

	do {
		struct cache_entry *ce = *cep++;
		struct stat st;

		if (ce_stage(ce))
			continue;
		if (ce_uptodate(ce))
			continue;
		if (!ce_path_match(ce, p->pathspec))
			continue;
		if (lstat(ce->name, &st))
			continue;
		if (ie_match_stat(index, ce, &st, 0))
			continue;
		ce_mark_uptodate(ce);
	} while (--nr > 0);
	return NULL;
}

static void preload_index(struct index_state *index, const char **pathspec)
{
	int threads, i, work, offset;
	struct thread_data data[MAX_PARALLEL];

	if (!core_preload_index)
		return;

	threads = index->cache_nr / THREAD_COST;
	if (threads < 2)
		return;
	if (threads > MAX_PARALLEL)
		threads = MAX_PARALLEL;
	offset = 0;
	work = (index->cache_nr + threads - 1) / threads;
	for (i = 0; i < threads; i++) {
		struct thread_data *p = data+i;
		p->index = index;
		p->pathspec = pathspec;
		p->offset = offset;
		p->nr = work;
		offset += work;
		if (pthread_create(&p->pthread, NULL, preload_thread, p))
			die("unable to create threaded lstat");
	}
	for (i = 0; i < threads; i++) {
		struct thread_data *p = data+i;
		if (pthread_join(p->pthread, NULL))
			die("unable to join threaded lstat");
	}
}

int read_index_preload(struct index_state *index, const char **pathspec)
{
	int retval = read_index(index);

	preload_index(index, pathspec);
	return retval;
}
