#ifndef BUILTIN_H
#define BUILTIN_H

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

extern const char git_version_string[];

void cmd_usage(int show_all, const char *exec_path, const char *fmt, ...)
#ifdef __GNUC__
	__attribute__((__format__(__printf__, 3, 4), __noreturn__))
#endif
	;

extern int cmd_help(int argc, const char **argv, char **envp);
extern int cmd_version(int argc, const char **argv, char **envp);

extern int cmd_whatchanged(int argc, const char **argv, char **envp);
extern int cmd_show(int argc, const char **argv, char **envp);
extern int cmd_log(int argc, const char **argv, char **envp);

#endif
