#include "git-compat-util.h"
#include "parse-options.h"
#include "cache.h"

#define OPT_SHORT 1
#define OPT_UNSET 2

static inline const char *get_arg(struct parse_opt_ctx_t *p)
{
	if (p->opt) {
		const char *res = p->opt;
		p->opt = NULL;
		return res;
	}
	p->argc--;
	return *++p->argv;
}

static inline const char *skip_prefix(const char *str, const char *prefix)
{
	size_t len = strlen(prefix);
	return strncmp(str, prefix, len) ? NULL : str + len;
}

static int opterror(const struct option *opt, const char *reason, int flags)
{
	if (flags & OPT_SHORT)
		return error("switch `%c' %s", opt->short_name, reason);
	if (flags & OPT_UNSET)
		return error("option `no-%s' %s", opt->long_name, reason);
	return error("option `%s' %s", opt->long_name, reason);
}

static int get_value(struct parse_opt_ctx_t *p,
                     const struct option *opt, int flags)
{
	const char *s, *arg;
	const int unset = flags & OPT_UNSET;

	if (unset && p->opt)
		return opterror(opt, "takes no value", flags);
	if (unset && (opt->flags & PARSE_OPT_NONEG))
		return opterror(opt, "isn't available", flags);

	if (!(flags & OPT_SHORT) && p->opt) {
		switch (opt->type) {
		case OPTION_CALLBACK:
			if (!(opt->flags & PARSE_OPT_NOARG))
				break;
			/* FALLTHROUGH */
		case OPTION_BOOLEAN:
		case OPTION_BIT:
		case OPTION_SET_INT:
		case OPTION_SET_PTR:
			return opterror(opt, "takes no value", flags);
		default:
			break;
		}
	}

	arg = p->opt ? p->opt : (p->argc > 1 ? p->argv[1] : NULL);
	switch (opt->type) {
	case OPTION_BIT:
		if (unset)
			*(int *)opt->value &= ~opt->defval;
		else
			*(int *)opt->value |= opt->defval;
		return 0;

	case OPTION_BOOLEAN:
		*(int *)opt->value = unset ? 0 : *(int *)opt->value + 1;
		return 0;

	case OPTION_SET_INT:
		*(int *)opt->value = unset ? 0 : opt->defval;
		return 0;

	case OPTION_SET_PTR:
		*(void **)opt->value = unset ? NULL : (void *)opt->defval;
		return 0;

	case OPTION_STRING:
		if (unset) {
			*(const char **)opt->value = NULL;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt) {
			*(const char **)opt->value = (const char *)opt->defval;
			return 0;
		}
		if (!arg)
			return opterror(opt, "requires a value", flags);
		*(const char **)opt->value = get_arg(p);
		return 0;

	case OPTION_CALLBACK:
		if (unset)
			return (*opt->callback)(opt, NULL, 1) ? (-1) : 0;
		if (opt->flags & PARSE_OPT_NOARG)
			return (*opt->callback)(opt, NULL, 0) ? (-1) : 0;
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt)
			return (*opt->callback)(opt, NULL, 0) ? (-1) : 0;
		if (!arg)
			return opterror(opt, "requires a value", flags);
		return (*opt->callback)(opt, get_arg(p), 0) ? (-1) : 0;

	case OPTION_INTEGER:
		if (unset) {
			*(int *)opt->value = 0;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt) {
			*(int *)opt->value = opt->defval;
			return 0;
		}
		if (!arg)
			return opterror(opt, "requires a value", flags);
		*(int *)opt->value = strtol(get_arg(p), (char **)&s, 10);
		if (*s)
			return opterror(opt, "expects a numerical value", flags);
		return 0;

	default:
		die("should not happen, someone must be hit on the forehead");
	}
}

static int parse_short_opt(struct parse_opt_ctx_t *p, const struct option *options)
{
	for (; options->type != OPTION_END; options++) {
		if (options->short_name == *p->opt) {
			p->opt = p->opt[1] ? p->opt + 1 : NULL;
			return get_value(p, options, OPT_SHORT);
		}
	}
	return -2;
}

static int parse_long_opt(struct parse_opt_ctx_t *p, const char *arg,
                          const struct option *options)
{
	const char *arg_end = strchr(arg, '=');
	const struct option *abbrev_option = NULL, *ambiguous_option = NULL;
	int abbrev_flags = 0, ambiguous_flags = 0;

	if (!arg_end)
		arg_end = arg + strlen(arg);

	for (; options->type != OPTION_END; options++) {
		const char *rest;
		int flags = 0;

		if (!options->long_name)
			continue;

		rest = skip_prefix(arg, options->long_name);
		if (options->type == OPTION_ARGUMENT) {
			if (!rest)
				continue;
			if (*rest == '=')
				return opterror(options, "takes no value", flags);
			if (*rest)
				continue;
			p->out[p->cpidx++] = arg - 2;
			return 0;
		}
		if (!rest) {
			/* abbreviated? */
			if (!strncmp(options->long_name, arg, arg_end - arg)) {
is_abbreviated:
				if (abbrev_option) {
					/*
					 * If this is abbreviated, it is
					 * ambiguous. So when there is no
					 * exact match later, we need to
					 * error out.
					 */
					ambiguous_option = abbrev_option;
					ambiguous_flags = abbrev_flags;
				}
				if (!(flags & OPT_UNSET) && *arg_end)
					p->opt = arg_end + 1;
				abbrev_option = options;
				abbrev_flags = flags;
				continue;
			}
			/* negated and abbreviated very much? */
			if (!prefixcmp("no-", arg)) {
				flags |= OPT_UNSET;
				goto is_abbreviated;
			}
			/* negated? */
			if (strncmp(arg, "no-", 3))
				continue;
			flags |= OPT_UNSET;
			rest = skip_prefix(arg + 3, options->long_name);
			/* abbreviated and negated? */
			if (!rest && !prefixcmp(options->long_name, arg + 3))
				goto is_abbreviated;
			if (!rest)
				continue;
		}
		if (*rest) {
			if (*rest != '=')
				continue;
			p->opt = rest + 1;
		}
		return get_value(p, options, flags);
	}

	if (ambiguous_option)
		return error("Ambiguous option: %s "
			"(could be --%s%s or --%s%s)",
			arg,
			(ambiguous_flags & OPT_UNSET) ?  "no-" : "",
			ambiguous_option->long_name,
			(abbrev_flags & OPT_UNSET) ?  "no-" : "",
			abbrev_option->long_name);
	if (abbrev_option)
		return get_value(p, abbrev_option, abbrev_flags);
	return -2;
}

void check_typos(const char *arg, const struct option *options)
{
	if (strlen(arg) < 3)
		return;

	if (!prefixcmp(arg, "no-")) {
		error ("did you mean `--%s` (with two dashes ?)", arg);
		exit(129);
	}

	for (; options->type != OPTION_END; options++) {
		if (!options->long_name)
			continue;
		if (!prefixcmp(options->long_name, arg)) {
			error ("did you mean `--%s` (with two dashes ?)", arg);
			exit(129);
		}
	}
}

void parse_options_start(struct parse_opt_ctx_t *ctx,
			 int argc, const char **argv, int flags)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->argc = argc - 1;
	ctx->argv = argv + 1;
	ctx->out  = argv;
	ctx->cpidx = ((flags & PARSE_OPT_KEEP_ARGV0) != 0);
	ctx->flags = flags;
}

static int usage_with_options_internal(const char * const *,
				       const struct option *, int);

int parse_options_step(struct parse_opt_ctx_t *ctx,
		       const struct option *options,
		       const char * const usagestr[])
{
	/* we must reset ->opt, unknown short option leave it dangling */
	ctx->opt = NULL;

	for (; ctx->argc; ctx->argc--, ctx->argv++) {
		const char *arg = ctx->argv[0];

		if (*arg != '-' || !arg[1]) {
			if (ctx->flags & PARSE_OPT_STOP_AT_NON_OPTION)
				break;
			ctx->out[ctx->cpidx++] = ctx->argv[0];
			continue;
		}

		if (arg[1] != '-') {
			ctx->opt = arg + 1;
			if (*ctx->opt == 'h')
				return parse_options_usage(usagestr, options);
			switch (parse_short_opt(ctx, options)) {
			case -1:
				return parse_options_usage(usagestr, options);
			case -2:
				return PARSE_OPT_UNKNOWN;
			}
			if (ctx->opt)
				check_typos(arg + 1, options);
			while (ctx->opt) {
				if (*ctx->opt == 'h')
					return parse_options_usage(usagestr, options);
				switch (parse_short_opt(ctx, options)) {
				case -1:
					return parse_options_usage(usagestr, options);
				case -2:
					/* fake a short option thing to hide the fact that we may have
					 * started to parse aggregated stuff
					 *
					 * This is leaky, too bad.
					 */
					ctx->argv[0] = xstrdup(ctx->opt - 1);
					*(char *)ctx->argv[0] = '-';
					return PARSE_OPT_UNKNOWN;
				}
			}
			continue;
		}

		if (!arg[2]) { /* "--" */
			if (!(ctx->flags & PARSE_OPT_KEEP_DASHDASH)) {
				ctx->argc--;
				ctx->argv++;
			}
			break;
		}

		if (!strcmp(arg + 2, "help-all"))
			return usage_with_options_internal(usagestr, options, 1);
		if (!strcmp(arg + 2, "help"))
			return parse_options_usage(usagestr, options);
		switch (parse_long_opt(ctx, arg + 2, options)) {
		case -1:
			return parse_options_usage(usagestr, options);
		case -2:
			return PARSE_OPT_UNKNOWN;
		}
	}
	return PARSE_OPT_DONE;
}

int parse_options_end(struct parse_opt_ctx_t *ctx)
{
	memmove(ctx->out + ctx->cpidx, ctx->argv, ctx->argc * sizeof(*ctx->out));
	ctx->out[ctx->cpidx + ctx->argc] = NULL;
	return ctx->cpidx + ctx->argc;
}

int parse_options(int argc, const char **argv, const struct option *options,
		  const char * const usagestr[], int flags)
{
	struct parse_opt_ctx_t ctx;

	parse_options_start(&ctx, argc, argv, flags);
	switch (parse_options_step(&ctx, options, usagestr)) {
	case PARSE_OPT_HELP:
		exit(129);
	case PARSE_OPT_DONE:
		break;
	default: /* PARSE_OPT_UNKNOWN */
		if (ctx.argv[0][1] == '-') {
			error("unknown option `%s'", ctx.argv[0] + 2);
		} else {
			error("unknown switch `%c'", *ctx.opt);
		}
		usage_with_options(usagestr, options);
	}

	return parse_options_end(&ctx);
}

#define USAGE_OPTS_WIDTH 24
#define USAGE_GAP         2

int usage_with_options_internal(const char * const *usagestr,
				const struct option *opts, int full)
{
	fprintf(stderr, "usage: %s\n", *usagestr++);
	while (*usagestr && **usagestr)
		fprintf(stderr, "   or: %s\n", *usagestr++);
	while (*usagestr) {
		fprintf(stderr, "%s%s\n",
				**usagestr ? "    " : "",
				*usagestr);
		usagestr++;
	}

	if (opts->type != OPTION_GROUP)
		fputc('\n', stderr);

	for (; opts->type != OPTION_END; opts++) {
		size_t pos;
		int pad;

		if (opts->type == OPTION_GROUP) {
			fputc('\n', stderr);
			if (*opts->help)
				fprintf(stderr, "%s\n", opts->help);
			continue;
		}
		if (!full && (opts->flags & PARSE_OPT_HIDDEN))
			continue;

		pos = fprintf(stderr, "    ");
		if (opts->short_name)
			pos += fprintf(stderr, "-%c", opts->short_name);
		if (opts->long_name && opts->short_name)
			pos += fprintf(stderr, ", ");
		if (opts->long_name)
			pos += fprintf(stderr, "--%s", opts->long_name);

		switch (opts->type) {
		case OPTION_ARGUMENT:
			break;
		case OPTION_INTEGER:
			if (opts->flags & PARSE_OPT_OPTARG)
				if (opts->long_name)
					pos += fprintf(stderr, "[=<n>]");
				else
					pos += fprintf(stderr, "[<n>]");
			else
				pos += fprintf(stderr, " <n>");
			break;
		case OPTION_CALLBACK:
			if (opts->flags & PARSE_OPT_NOARG)
				break;
			/* FALLTHROUGH */
		case OPTION_STRING:
			if (opts->argh) {
				if (opts->flags & PARSE_OPT_OPTARG)
					if (opts->long_name)
						pos += fprintf(stderr, "[=<%s>]", opts->argh);
					else
						pos += fprintf(stderr, "[<%s>]", opts->argh);
				else
					pos += fprintf(stderr, " <%s>", opts->argh);
			} else {
				if (opts->flags & PARSE_OPT_OPTARG)
					if (opts->long_name)
						pos += fprintf(stderr, "[=...]");
					else
						pos += fprintf(stderr, "[...]");
				else
					pos += fprintf(stderr, " ...");
			}
			break;
		default: /* OPTION_{BIT,BOOLEAN,SET_INT,SET_PTR} */
			break;
		}

		if (pos <= USAGE_OPTS_WIDTH)
			pad = USAGE_OPTS_WIDTH - pos;
		else {
			fputc('\n', stderr);
			pad = USAGE_OPTS_WIDTH;
		}
		fprintf(stderr, "%*s%s\n", pad + USAGE_GAP, "", opts->help);
	}
	fputc('\n', stderr);

	return PARSE_OPT_HELP;
}

void usage_with_options(const char * const *usagestr,
			const struct option *opts)
{
	usage_with_options_internal(usagestr, opts, 0);
	exit(129);
}

int parse_options_usage(const char * const *usagestr,
			const struct option *opts)
{
	return usage_with_options_internal(usagestr, opts, 0);
}


/*----- some often used options -----*/
#include "cache.h"

int parse_opt_abbrev_cb(const struct option *opt, const char *arg, int unset)
{
	int v;

	if (!arg) {
		v = unset ? 0 : DEFAULT_ABBREV;
	} else {
		v = strtol(arg, (char **)&arg, 10);
		if (*arg)
			return opterror(opt, "expects a numerical value", 0);
		if (v && v < MINIMUM_ABBREV)
			v = MINIMUM_ABBREV;
		else if (v > 40)
			v = 40;
	}
	*(int *)(opt->value) = v;
	return 0;
}

int parse_opt_approxidate_cb(const struct option *opt, const char *arg,
			     int unset)
{
	*(unsigned long *)(opt->value) = approxidate(arg);
	return 0;
}
