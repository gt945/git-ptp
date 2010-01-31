#include "../git-compat-util.h"

/* Adapted from libiberty's mkstemp.c. */

#undef TMP_MAX
#define TMP_MAX 16384

int gitmkstemps(char *pattern, int suffix_len)
{
	static const char letters[] =
		"abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"0123456789";
	static const int num_letters = 62;
	uint64_t value;
	struct timeval tv;
	char *template;
	size_t len;
	int fd, count;

	len = strlen(pattern);

	if (len < 6 + suffix_len) {
		errno = EINVAL;
		return -1;
	}

	if (strncmp(&pattern[len - 6 - suffix_len], "XXXXXX", 6)) {
		errno = EINVAL;
		return -1;
	}

	/*
	 * Replace pattern's XXXXXX characters with randomness.
	 * Try TMP_MAX different filenames.
	 */
	gettimeofday(&tv, NULL);
	value = ((size_t)(tv.tv_usec << 16)) ^ tv.tv_sec ^ getpid();
	template = &pattern[len - 6 - suffix_len];
	for (count = 0; count < TMP_MAX; ++count) {
		uint64_t v = value;
		/* Fill in the random bits. */
		template[0] = letters[v % num_letters]; v /= num_letters;
		template[1] = letters[v % num_letters]; v /= num_letters;
		template[2] = letters[v % num_letters]; v /= num_letters;
		template[3] = letters[v % num_letters]; v /= num_letters;
		template[4] = letters[v % num_letters]; v /= num_letters;
		template[5] = letters[v % num_letters]; v /= num_letters;

		fd = open(pattern, O_CREAT | O_EXCL | O_RDWR, 0600);
		if (fd > 0)
			return fd;
		/*
		 * Fatal error (EPERM, ENOSPC etc).
		 * It doesn't make sense to loop.
		 */
		if (errno != EEXIST)
			break;
		/*
		 * This is a random value.  It is only necessary that
		 * the next TMP_MAX values generated by adding 7777 to
		 * VALUE are different with (module 2^32).
		 */
		value += 7777;
	}
	/* We return the null string if we can't find a unique file name.  */
	pattern[0] = '\0';
	errno = EINVAL;
	return -1;
}
