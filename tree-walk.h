#ifndef TREE_WALK_H
#define TREE_WALK_H

struct tree_desc {
	void *buf;
	unsigned long size;
};

struct name_entry {
	const unsigned char *sha1;
	const char *path;
	unsigned int mode;
	int pathlen;
};

void update_tree_entry(struct tree_desc *);
const unsigned char *tree_entry_extract(struct tree_desc *, const char **, unsigned int *);

void *fill_tree_descriptor(struct tree_desc *desc, const unsigned char *sha1);

typedef void (*traverse_callback_t)(int n, unsigned long mask, struct name_entry *entry, const char *base);

void traverse_trees(int n, struct tree_desc *t, const char *base, traverse_callback_t callback);

int get_tree_entry(const unsigned char *, const char *, unsigned char *, unsigned *);

#endif
