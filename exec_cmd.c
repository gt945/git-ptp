#include "cache.h"
#include "exec_cmd.h"
#define MAX_ARGS	32

extern char **environ;
static const char *builtin_exec_path = GIT_EXEC_PATH;
static const char *current_exec_path = NULL;

void git_set_exec_path(const char *exec_path)
{
	current_exec_path = exec_path;
}


/* Returns the highest-priority, location to look for git programs. */
const char *git_exec_path(void)
{
	const char *env;

	if (current_exec_path)
		return current_exec_path;

	env = getenv("GIT_EXEC_PATH");
	if (env) {
		return env;
	}

	return builtin_exec_path;
}


int execv_git_cmd(char **argv)
{
	char git_command[PATH_MAX + 1];
	char *tmp;
	int len, err, i;
	const char *paths[] = { current_exec_path,
				getenv("GIT_EXEC_PATH"),
				builtin_exec_path };

	for (i = 0; i < sizeof(paths)/sizeof(paths[0]); ++i) {
		const char *exec_dir = paths[i];
		if (!exec_dir) continue;

		if (*exec_dir != '/') {
			if (!getcwd(git_command, sizeof(git_command))) {
				fprintf(stderr, "git: cannot determine "
					"current directory\n");
				exit(1);
			}
			len = strlen(git_command);

			/* Trivial cleanup */
			while (!strncmp(exec_dir, "./", 2)) {
				exec_dir += 2;
				while (*exec_dir == '/')
					exec_dir++;
			}
			snprintf(git_command + len, sizeof(git_command) - len,
				 "/%s", exec_dir);
		} else {
			strcpy(git_command, exec_dir);
		}

		len = strlen(git_command);
		len += snprintf(git_command + len, sizeof(git_command) - len,
				"/git-%s", argv[0]);

		if (sizeof(git_command) <= len) {
			fprintf(stderr,
				"git: command name given is too long.\n");
			break;
		}

		/* argv[0] must be the git command, but the argv array
		 * belongs to the caller, and my be reused in
		 * subsequent loop iterations. Save argv[0] and
		 * restore it on error.
		 */

		tmp = argv[0];
		argv[0] = git_command;

		/* execve() can only ever return if it fails */
		execve(git_command, argv, environ);

		err = errno;

		argv[0] = tmp;
	}
	return -1;

}


int execl_git_cmd(char *cmd,...)
{
	int argc;
	char *argv[MAX_ARGS + 1];
	char *arg;
	va_list param;

	va_start(param, cmd);
	argv[0] = cmd;
	argc = 1;
	while (argc < MAX_ARGS) {
		arg = argv[argc++] = va_arg(param, char *);
		if (!arg)
			break;
	}
	va_end(param);
	if (MAX_ARGS <= argc)
		return error("too many args to run %s", cmd);

	argv[argc] = NULL;
	return execv_git_cmd(argv);
}
