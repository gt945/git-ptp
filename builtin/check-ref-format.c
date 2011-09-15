/*
 * GIT - The information manager from hell
 */

#include "cache.h"
#include "refs.h"
#include "builtin.h"
#include "strbuf.h"

static const char builtin_check_ref_format_usage[] =
"git check-ref-format [--print] [options] <refname>\n"
"   or: git check-ref-format --branch <branchname-shorthand>";

/*
 * Return a copy of refname but with leading slashes removed and runs
 * of adjacent slashes replaced with single slashes.
 *
 * This function is similar to normalize_path_copy(), but stripped down
 * to meet check_ref_format's simpler needs.
 */
static char *collapse_slashes(const char *refname)
{
	char *ret = xmalloc(strlen(refname) + 1);
	char ch;
	char prev = '/';
	char *cp = ret;

	while ((ch = *refname++) != '\0') {
		if (prev == '/' && ch == prev)
			continue;

		*cp++ = ch;
		prev = ch;
	}
	*cp = '\0';
	return ret;
}

static int check_ref_format_branch(const char *arg)
{
	struct strbuf sb = STRBUF_INIT;
	int nongit;

	setup_git_directory_gently(&nongit);
	if (strbuf_check_branch_ref(&sb, arg))
		die("'%s' is not a valid branch name", arg);
	printf("%s\n", sb.buf + 11);
	return 0;
}

static void refname_format_print(const char *arg)
{
	char *refname = collapse_slashes(arg);
	printf("%s\n", refname);
}

int cmd_check_ref_format(int argc, const char **argv, const char *prefix)
{
	int i;
	int print = 0;
	int flags = 0;

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage(builtin_check_ref_format_usage);

	if (argc == 3 && !strcmp(argv[1], "--branch"))
		return check_ref_format_branch(argv[2]);

	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "--print"))
			print = 1;
		else if (!strcmp(argv[i], "--allow-onelevel"))
			flags |= REFNAME_ALLOW_ONELEVEL;
		else if (!strcmp(argv[i], "--no-allow-onelevel"))
			flags &= ~REFNAME_ALLOW_ONELEVEL;
		else if (!strcmp(argv[i], "--refspec-pattern"))
			flags |= REFNAME_REFSPEC_PATTERN;
		else
			usage(builtin_check_ref_format_usage);
	}
	if (! (i == argc - 1))
		usage(builtin_check_ref_format_usage);

	if (check_refname_format(argv[i], flags))
		return 1;

	if (print)
		refname_format_print(argv[i]);

	return 0;
}
