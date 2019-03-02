#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <err.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <libgen.h>
#include <sys/stat.h>
#include <termios.h>
#include <fcntl.h>

/* function prototypes */
static int invoke_shell(char *, int, int);



/* preprocessor defines */

/* for shenv_t */
#define	MAX_TRAP	15
#define	MAX_OPTS	10
#define NUM_FDS		10

/* for list_t */
#define LIST_AND	0
#define	LIST_OR		1



/* types, structures & unions */
typedef int (*builtin_t)(int, char *[]);

struct builtin {
	const char *name;
	const builtin_t func;
	const int special;
	const int fork;
};

typedef struct {
	char	**argv;
	int		argc;
	int		type;
	int		pipe[2];
} list_t;

typedef struct {
	char		*name;
	char		*val;
	int			 exported;
	int			 readonly;
	int			 freed;
} env_t;

typedef struct sh_exec_env {
	struct sh_exec_env *parent;
	
	char	 *name;
	int		  fds		[NUM_FDS];		/* fds from the parent that will be dup'd to the child */
	mode_t	  umask;
	void	 *traps		[MAX_TRAP + 1];
	int		  options	[MAX_OPTS + 1];
	void	 *functions;
	pid_t	**last_cmds;
	void	 *aliases;
	env_t	**private_envs;
	list_t	**sh_list;
	char	**argv;
	int		argc;
} shenv_t;

/* local variables */
static int fork_mode = 0;
static int pipe_mode = 0;
static int pipe_fd[2] = {-1,-1};

static shenv_t *cur_sh_env = NULL;

static int opt_command_string = 0;
static int opt_interactive = 0;
static int opt_read_stdin = 0;



/* enviromental ones */
static int opt_allexport = 0;
static int opt_notify = 0;
static int opt_noclobber = 0;
static int opt_errexit = 0;
static int opt_noglob = 0;
static int opt_monitor = 0;
static int opt_noexec = 0;
static int opt_nounset = 0;
static int opt_verbose = 0;
static int opt_xtrace = 0;



/* local function defintions */

static env_t *getshenv(shenv_t *sh, char *name)
{
	errno = 0;
	if (!sh->private_envs) 
		return NULL;

	env_t *ret = NULL;
	for (int i = 0; (ret = sh->private_envs[i]); i++)
		if (!strcmp(name, ret->name)) 
			break;

	return ret;
}

static void exportenv(env_t *env)
{
	if (env->exported) return;
	env->exported = 1;
	setenv(env->name, env->val, 1);
}

static env_t *setshenv(shenv_t *sh, char *name, char *value)
{
	errno = 0;
	int cnt;

	env_t *ret = getshenv(sh, name);

	if (!ret)
	{
		if ((ret = calloc(1, sizeof(env_t))) == NULL)
			return NULL;

		for (cnt = 0; sh->private_envs[cnt]; cnt++) ;
		env_t **tmp = realloc(sh->private_envs, sizeof(env_t *) * (cnt+2));
		if (tmp == NULL) {
			free(ret);
			ret = NULL;
			return NULL;
		}

		if ((ret->name = strdup(name)) == NULL) {
			free(tmp);
			free(ret);
			tmp = NULL;
			ret = NULL;
			return NULL;
		}
		//printf("setshenv e->name to %p\n", ret->name);

		sh->private_envs = tmp;
		sh->private_envs[cnt++] = ret;
		sh->private_envs[cnt] = NULL;
	}

	if (ret->readonly) {
		warnx("%s: readonly variable", ret->name);
		return ret;
	}

	if (ret->val) {
		free(ret->val);
		ret->val = NULL;
	}
	ret->val = strdup(value);

	if (ret->exported)
		setenv(ret->name, ret->val, 1);

	return ret;
}

static void dump_envs(shenv_t *sh)
{
	env_t *e;

	for (int i = 0; (e = sh->private_envs[i]); i++)
	{
		printf("[%2d] %s = %s", i, e->name, e->val);
		if (e->readonly) printf(" (ro)");
		if (e->exported) printf(" (expt)");
		fputc('\n', stdout);
	}
}


/* check if a NULL terminated string is a valid sequence of digits */
static int isnumber(const char *str)
{
	while(*str)
	{
		if(!isdigit(*str++)) return 0;
	}

	return 1;
}

/* check if a NULL/length terminated string is a valid POSIX 'Pathname' from 
 * the portable file name character set, plus <space>
 */
#if 0
static int ispathname(const char *str, int max_len)
{
	int len = 0;
	char c;

	while ((c = str[len]) && (len++ < max_len))
	{
		if (isalnum(c)) continue;
		else if (c == '.' || c == '_' || c == '-' || c == '/' || c == ' ') continue;
		else return 0;
	}

	return 1;
}
#endif

/* check if a NULL/length terminated string is a valid POSIX 'Name' */
static int isname(const char *str, int max_len)
{
	if (!(*str == '_' || isalpha(*str))) return 0;

	int len = 1;

	while (*++str && (len++ < max_len))
	{
		if (isalnum(*str)) continue;
		else if (*str == '_') continue;
		else return 0;
	}
	return 1;
}

/* grow an array (char **), append the (char *) entry, and ensure the last
 * entry is NULL. increase the (int *) cnt to match useable entries */
static int array_add(char ***array, int *cnt, char *entry)
{
	// TODO handle cases where there is no cnt, and solely NULL terminated
	// TODO migrate other instances to this

	char **new;

	if ((new = realloc(*array, (*cnt+2) * sizeof(char *))) == NULL)
		return -1;

	*array = new;
	new[*cnt++] = entry;
	new[*cnt] = NULL;

	return 0;
}

#if 0
static void free_list(list_t **l)
{
	for (int i = 0; l[i]; i++)
	{
		list_t *lst = l[i];

		if (lst->argv)
		{
			for (int j = 0; lst->argv[j]; j++)
				free(lst->argv[j]);
			free(lst->argv);
			lst->argv = NULL;
		}

		lst->argc = 0;
		lst->type = -1;

		free(lst);
		l[i] = NULL;
	}

	free(l);
}

static int add_list(list_t ***lst, char **argv, int argc, int type)
{
	int cnt = 0;

	if (lst)
		for (cnt = 0; *lst[cnt]; cnt++) ;

	if ((*lst = realloc(*lst, (cnt+2) * sizeof(list_t *))) == NULL)
		return -1;

	list_t *new;
	if ((new = calloc(1, sizeof(list_t))) == NULL)
		return -1;

	*lst[cnt] = new;
	*lst[cnt+1] = NULL;

	return EXIT_SUCCESS;
}
#endif

static void free_env(env_t *e)
{
	if (e->freed) printf("error: double free\n");

	//printf("  free_env @ %p name:%p val:%p\n", e, e->name, e->val);

	if (e->name) {
		free(e->name);
		e->name = NULL;
		//printf("done name\n");
	}
	if (e->val) {
		free(e->val);
		e->val = NULL;
	}

	//printf("  done\n");
	e->freed = 1;
	free(e);
}

static void free_ex_env(shenv_t *c)
{
	//printf("\nfree_ex_env @ %p [%s]\n", c, c->name);
	if (c->parent) {
		free_ex_env(c->parent);
		c->parent = NULL;
	}

	if (c->private_envs) {
		env_t *e;
		for (int i = 0; (e = c->private_envs[i]) != NULL; i++)
		{
			free_env(c->private_envs[i]);
			c->private_envs[i] = NULL;
		}
		free(c->private_envs);
		c->private_envs = NULL;
	}

	if(c->argv) {
		for (int i = 0; i < c->argc; i++)
		{
			if (c->argv[i]) {
				free(c->argv[i]);
				c->argv[i] = NULL;
			}
		}
		free(c->argv);
		c->argv = NULL;
		c->argc = 0;
	}

	/*
	if(c->fds) {
		for (int i = 0; i < c->max_fd; i++) {
			if (c->fds[i]) {
				if(fclose(c->fds[i]) == -1)
					warn("fclose");
				c->fds[i] = NULL;
			}
		}
		free(c->fds);
		c->max_fd = -1;
		c->fds = NULL;
	}
	*/

	free(c);
}

static shenv_t *clone_env(shenv_t *cur, const char *name)
{
	shenv_t *ret;
	if ((ret = calloc(1, sizeof(shenv_t))) == NULL)
		return NULL;

	/*
	   if (cur->cwd)
	   if ((ret->cwd = strdup(cur->cwd)) == NULL) goto free_ret;
	   */
	ret->umask = cur->umask;

	if (cur->private_envs)
	{
		int env_cnt = 0;
		while (cur->private_envs[env_cnt]) env_cnt++;

		if (env_cnt) {
			if ((ret->private_envs = calloc(1, sizeof(env_t *) * (env_cnt+1))) == NULL) goto free_ret;
			for (int i = 0; i < env_cnt; i++)
			{
				if ((ret->private_envs[i] = calloc(1, sizeof(env_t))) == NULL)
					goto free_envs;

				memcpy(ret->private_envs[i], cur->private_envs[i], sizeof(env_t));
				ret->private_envs[i]->name = strdup(cur->private_envs[i]->name);
				ret->private_envs[i]->val = strdup(cur->private_envs[i]->val);

			}
		}
	}

	ret->argc = cur->argc;
	if (cur->argv)
	{
		if ((ret->argv = calloc(1, sizeof(char *) * ret->argc)) == NULL) goto free_envs;
		for (int i = 0; i < ret->argc; i++) {
			if ((ret->argv[i] = strdup(cur->argv[i])) == NULL)
				goto free_argv;
		}
	}


	memcpy(ret->fds, cur->fds, sizeof(ret->fds));

	/*
	if ((ret->fds = calloc(1, sizeof(FILE *) * (cur->max_fd+1))) == NULL)
		goto free_argv;

	ret->max_fd = cur->max_fd;

	if (cur->fds)
		for (int i = 0; i < ret->max_fd; i++)
		{
			if (cur->fds[i]) {
				//printf("cloning fd[%d]\n", i);
				int mode = fcntl(i, F_GETFL);
				char *opmode = "r";
				if (mode & O_RDONLY) opmode = "r";
				else if (mode & O_WRONLY) opmode = "w";
				else if (mode & O_RDWR) opmode = "r+";
				
				if ((ret->fds[i] = fdopen(i, opmode)) == NULL)
					warn("%d: unable to clone", i);
			}
		}
		*/

	ret->parent = cur;
	if ((ret->name = strdup(name)) == NULL)
		warnx(NULL);

	return ret;

free_argv:
	if (ret->argv) {
		for (int i = 0; i < ret->argc; i++)
			if(ret->argv[i]) {
				free(ret->argv[i]);
				ret->argv[i] = NULL;
			}
		free(ret->argv);
		ret->argv = NULL;
	}
free_envs:
	if (ret->private_envs) {
		for (int i = 0; ret->private_envs[i]; i++) {
			free(ret->private_envs[i]);
			ret->private_envs[i] = NULL;
		}
		free(ret->private_envs);
		ret->private_envs = NULL;
	}
	/*	
free_cwd:
if (ret->cwd)
free(ret->cwd);
*/
free_ret:
	free(ret);
	ret = NULL;
	return ret;
}

/*
static int add_fds(struct shenv_t *e, int fd, FILE *f)
{
	if (fd >= MAX_FDS) {
		errno = EBADF;
		return -1;
	}

	if (fd >= e->max_fd) {
		FILE **tmp;
		if ((tmp = realloc(e->fds, (fd+2) * sizeof(FILE *))) == NULL)
			return -1;
		e->fds = tmp;
		//printf("e->fds: %p. fd=%d ", e->fds, fd);
		//void *from = &e->fds[e->max_fd];
		//int len = (sizeof(FILE *) * ((fd+1) - e->max_fd));
		//printf("from = %p len = %d\n", from, len);
		memset(&e->fds[e->max_fd], 0, sizeof(FILE *) * ((fd+1) - e->max_fd));
		e->max_fd = fd + 1;
	}

	e->fds[fd] = f;
	//printf("adding e->fds[%d]=%p max_fd=%d\n", fd, e->fds[fd], e->max_fd);
	// close existing ones?
	return EXIT_SUCCESS;
}
*/

/* umask builtin */
static int cmd_umask(int argc, char *argv[])
{
	const char *usage = "Usage: umask [-S] [mask]";

	int opt_symbolic = 0;
	int mask = -1;
	{
		int opt;
		while ((opt = getopt(argc, argv, "S")) != -1)
		{
			switch (opt)
			{
				case 'S':
					opt_symbolic = 1;
					break;
				default:
					warnx(usage);
					return EXIT_FAILURE;
			}
		}
		if (argc - optind > 1) {
			warnx(usage);
			return EXIT_FAILURE;
		}
		if (argc - optind == 1) {
			if (isnumber(argv[optind])) {
				mask = atoi(argv[optind]);
			} else {
				warnx("mask (%s) is not numerical", argv[optind]);
				return EXIT_FAILURE;
			}
		}
	}

	if (mask == -1) {
		mode_t um = umask(0);
		umask(um);
		if (opt_symbolic)
			printf("symbolic not supported\n");
		printf("%04u\n", um);
	} else
		umask(mask);

	return EXIT_SUCCESS;
}

static int cmd_pwd(int argc, char *argv[])
{
	char pwd[BUFSIZ];
	if (getcwd(pwd, BUFSIZ) == NULL)
		err(EXIT_FAILURE, NULL);
	printf("%s\n", pwd);
	exit(EXIT_SUCCESS);
}

static int cmd_cd(int argc, char *argv[])
{
	char *dir = NULL;

	if(argc == 1) {
		dir = getenv("HOME");
	} else {
		dir = argv[1];
	}

	if(dir == NULL) {
		fprintf(stderr, "%s: HOME not set\n", argv[0]);
		return EXIT_FAILURE;
	}

	int hyphen = 0;
	if (argc > 1) {
		if((hyphen = !strcmp("-", argv[1]))) {
			dir = getenv("OLDPWD");
			if(dir == NULL) {
				fprintf(stderr, "%s: OLDPWD not set\n", argv[0]);
				return EXIT_FAILURE; 
			}
		}
	}

	char oldpwd[BUFSIZ]; 
	if (getcwd(oldpwd, BUFSIZ) == NULL) {
		warnx(NULL);
		return EXIT_FAILURE;
	}

	if(chdir(dir) == -1) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], dir, strerror(errno));
		return EXIT_FAILURE;
	}

	char pwd[BUFSIZ];
	if (getcwd(pwd, BUFSIZ) == NULL) {
		warnx(NULL);
		return EXIT_FAILURE;
	}

	setenv("OLDPWD", oldpwd, 1);
	setenv("PWD", pwd, 1);

	if(hyphen)
		printf("%s\n", pwd);

	return EXIT_SUCCESS;
}

inline static int isifsspace(const char c, const char *restrict IFS)
{
	if (!isspace(c)) 
		return 0;
	if (strchr(IFS, c)) 
		return 1;
	return 0;
}

static int cmd_read(int argc, char *argv[])
{
	int opt_raw = 0;

	{
		int opt = 0;

		while ((opt = getopt(argc, argv, "r")) != -1)
		{
			switch (opt)
			{
				case 'r':
					opt_raw = 1;
					break;
				default:
					fprintf(stderr,
							"Usage: read [-r] var...\n");
					return(EXIT_FAILURE);
			}
		}
	}

	const char *IFS = getenv("IFS");

	if (IFS == NULL) 
		IFS=" \r\n"; // FIXME correct? or strlen==0 then set IFS?

	char buf[BUFSIZ];
	char *restrict line = NULL;
	int len = 0;

	line = fgets(buf, BUFSIZ, stdin);

	if (line == NULL || !strlen(line) )
		return EXIT_SUCCESS;

	len = strlen(line);

	if (!opt_raw && len >= 2 && buf[len-2] == '\\' && buf[len-1] == '\n') 
	{
		while (buf[len-2] == '\\' && buf[len-1] == '\n' && len < BUFSIZ)
		{
			buf[len-2] = IFS[0];
			line = &buf[len-1];
			line = fgets(line, BUFSIZ-len, stdin);
			len = strlen(buf);

			if (line == NULL)
				break;
		}
	}

	int pos = 0;
	int start = 0;
	int arg = 0;
	char *last = NULL;
	char delim = 0;
	char *str = NULL;
	const int numargs = argc - optind;

	while(pos <= len)
	{
		if (pos == len)
			break;

		while(pos <= len && isifsspace(buf[pos], IFS)) 
			pos++;

		if (pos == len) 
			break;

		start = pos;
		delim = (pos > 0) ? ' ' : buf[pos-1];

		while(pos <= len && !isifsspace(buf[pos], IFS)) 
			pos++;

		if ((str = strndup(buf+start, pos-start)) == NULL) {
			warn(NULL);

			if (last) {
				free(last);
				last = NULL;
			}
			
			return(EXIT_FAILURE);
		}

		if (numargs && arg < numargs-1) 
		{
			if (setenv(argv[optind+arg], str, 1) == -1) {
				warn(NULL);

				if (str) {
					free(str);
					str = NULL;
				}

				if (last) {
					free(last);
					last = NULL;
				}
				
				return(EXIT_FAILURE);
			}
		} 
		else if (numargs) 
		{
			const int oldlen = (last != NULL) ? strlen(last) : 0;
			const int newlen = oldlen + strlen(str);

			if (last) 
			{
				char *tmp = realloc(last, newlen+2);

				if (tmp == NULL) {
					warn(NULL);

					free(last);
					last = NULL;

					if (str) {
						free(str);
						str = NULL;
					}

					return(EXIT_FAILURE);
				}

				last = tmp;
				last[oldlen] = delim;
				last[oldlen+1] = '\0';
			} 
			else 
			{
				last = calloc(1, newlen + 2);

				if (last == NULL) {
					warn(NULL);
					if (str) {
						free(str);
						str = NULL;
					}
					return(EXIT_FAILURE);
				}
				strcat(last, str);
			}
		}

		if (str) {
			free(str);
			str = NULL;
		}

		arg++;
	}

	if (last && numargs) {
		if (setenv(argv[numargs], last, 1) == -1) {
			warn(NULL);
		}
		free(last);
		last = NULL;
	}

	if (ferror(stdin) || feof(stdin))
		return(EXIT_FAILURE);

	return(EXIT_SUCCESS);
}

static int cmd_basename(int argc, char *argv[])
{
	//char *suffix = NULL;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage: %s string [suffix]\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	//if (argc == 3)
	//	suffix = argv[2];

	char *bname = basename(argv[1]);

	// TODO implement suffix here

	printf("%s\n", bname);
	exit(EXIT_SUCCESS);
}

static int cmd_exit(const int ac, char *av[])
{
	int val = 0;
	if (ac == 2)
		val = atoi(av[1]);
	exit(val);
}

/* replace the current process with another, effectively implements exec */
static int cmd_exec(const int ac, char *av[])
{
	warnx("exec: not implemented");
	return EXIT_FAILURE;

	execvp(av[1], av);
	warn(av[1]);
	return EXIT_FAILURE;
}

static int cmd_external(const int ac, char *av[])
{
	//	for (int i = 0; i <= 2; i++) {
	//		int cur = fcntl(1, F_GETFD);
	//		fcntl(1, F_SETFD, cur|FD_CLOEXEC);
	//	}
	execvp(av[0], av);
	warn(av[0]);
	return EXIT_FAILURE;
}

static int parse_set(char mod, char opt)
{
	int add = mod == '+' ? 1 : 0;

	switch (opt)
	{
		case 'a':
			opt_allexport = add;
			break;
		case 'b':
			opt_notify = add;
			break;
		case 'C':
			opt_noclobber = add;
			break;
		case 'e':
			opt_errexit = add;
			break;
		case 'f':
			opt_noglob = add;
			break;
		case 'm':
			opt_monitor = add;
			break;
		case 'n':
			opt_noexec = add;
			break;
		case 'u':
			opt_nounset = add;
			break;
		case 'v':
			opt_verbose = add;
			break;
		case 'x':
			opt_xtrace = add;
			break;
		default:
			warnx("%c: unknown set option", opt);
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

typedef struct option_map {
	const char *name;
	const char opt;
} optmap_t;

static optmap_t optmap[] = {
	{"allexport",	'a'},
	{"notify",		'b'},
	{"noclobber",	'C'},
	{"errexit",		'e'},
	{"noglob",		'f'},
	{"monitor",		'm'},
	{"noexec",		'n'},
	{"nounset",		'u'},
	{"verbose",		'v'},
	{"xtrace",		'x'},

	{NULL, 0}
};

static int parse_set_option(char *opt)
{
	return EXIT_SUCCESS;
}

static int cmd_set(const int ac, char *av[])
{
	int opt_show_vars = (ac == 1);
	int opt_show_options = 0;

	{
		int opt;
		while ((opt = getopt(ac, av, "abCefhmnuvxo")) != -1)
		{
			switch (opt)
			{
				case 'a':
				case 'b':
				case 'C':
				case 'e':
				case 'f':
				case 'm':
				case 'n':
				case 'u':
				case 'v':
				case 'x':
					if (parse_set('-',opt))
						return EXIT_FAILURE;
					break;
				case 'o':
					if (optind+1 == ac || *av[optind+1] == '-')
					{
						opt_show_options = 1;
						break;
					}

					if (parse_set_option(av[optind++]))
						return EXIT_FAILURE;
					break;
			}
		}
	}

	if (opt_show_vars) {
		dump_envs(cur_sh_env);
		return EXIT_SUCCESS;
	}

	if (opt_show_options) {
		;
	}

	return EXIT_SUCCESS;
}

static const struct builtin builtins[] = {

	{"basename",	cmd_basename,	0, 1},
	{"cd",			cmd_cd,			0, 0},
	{"exec",		cmd_exec,		1, 0},
	{"exit",		cmd_exit,		1, 0},
	{"pwd",			cmd_pwd,		0, 1},
	{"umask",		cmd_umask,		0, 0},
	{"read",		cmd_read,		0, 0},
	{"set",			cmd_set,		0, 1},

	{NULL, NULL, 0, 0}
};

/* execute a builtin in a seperate process. builtins that should run in the
 * shell process, should not use this */
static int builtin(builtin_t func, const int ac, char *av[], const int async)
{
	int status;

	if(ac == 0 || av == NULL) {
		warn("builtin: no args");
		return EXIT_FAILURE;
	}

	fork_mode = 0;
	pid_t newpid = fork();

	if(newpid == -1) {
		warn(av[0]);
		return EXIT_FAILURE;
	} else if(newpid == 0) {
		//for (int i = 0; i < NUM_FDS; i++)
		//	close(i);

		for (int i = 0; i < NUM_FDS; i++)
		{
			int fd;
			if (cur_sh_env->fds[i] != -1 && cur_sh_env->fds[i] != i) {
				fprintf(stderr, "dup2(%d,%d)", cur_sh_env->fds[i], i);
				if ((fd = dup2(cur_sh_env->fds[i], i)) == -1)
					warn("dup2");
			}
		}

		exit(func(ac,av));
	} else {
		if(!async) {
			if(waitpid(newpid, &status, 0) == -1) {
				warn(av[0]);
				return EXIT_FAILURE;
			}
			if(WIFEXITED(status)) {
				return WEXITSTATUS(status);
			}
		} else {
			printf("[?] %d\n", newpid);
		}
	}

	return EXIT_SUCCESS;
}

static void sig_chld(int sig)
{
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) != 0)
	{
		if (pid == -1) {
			if (errno != ECHILD)
				warn("sig_chld: %d", errno);
			return;
		}
		if (status)
			printf("[%d]+ Exit %-5d %s\n", 0,status, "(tbc)");
		else
			printf("[%d]+ Done       %s\n", 0, "(tbc)");
	}
}

static int is_redirect(char *str)
{
	while (*str && isspace(*str)) str++;
	char *ptr = str;
	char *tmp;

	if ((ptr = strstr(str, "|>")) != NULL) 
	{
		if (ptr == str) return 1;
	} 
	else if ((ptr = strchr(str, '>')) != NULL)
	{
		if (ptr == str) return 1;
		tmp = ptr-1;
		while (tmp >= str) if (!isdigit(*tmp--)) return 0;
		return 1;
	} 
	else if ((ptr = strchr(str, '<')) != NULL)
	{
		if (ptr == str) return 1;
		tmp = ptr-1;
		while (tmp >= str) if (!isdigit(*tmp--)) return 0;
	}

	return 0;
}

/**
 * tmp - first char to process
 * close - char to look for for close = one of )}`
 * type - { or ( or `
 * dst - output buffer (for expansion)
 * buf - buffer for snprintf n calc
 * rc - current rc
 *
 * $( ) or `` expand a new sub-shell replacing the command with the output
 *
 * ( ) run a new sub-shell
 * { } run a set of commands in the same env
 *
 */
static int process_sub_shell(char **tmp, char close, char type, char **dst, const char *buf, const int rc)
{
	char *start = *tmp;
//	char *end = NULL;
	char *ptr = start;

	int dbl_quot = 0, sing_quot = 0;

	while (*ptr)
	{
		if (!dbl_quot && !sing_quot && *ptr == close) {
//			end = ptr - 1;
			break;
		}

		if (!sing_quot && *ptr == '\\') ptr++;
		else if (!sing_quot && !dbl_quot && *ptr == '\'') sing_quot = 1;
		else if (!sing_quot && !dbl_quot && *ptr == '"') dbl_quot = 1;
		else if (sing_quot && *ptr == '\'') sing_quot = 0;
		else if (!sing_quot && dbl_quot && *ptr == '"') dbl_quot = 0;

		ptr++;
	}

	return 0;
}

static int process_expansion(char **dst, const char *buf, char **tmp, const int rc, int dep)
{
	/* the first char being processed */
	char ch = **tmp;
	/* lookup the next, could be \0 */
	char nextch = *((*tmp)+1);

	char *end = NULL;
	char *var = NULL;
	char *tmpstr = NULL;

	int wrl = 0;

	/* return value */
	int len = 0;

	/* $ENV */
	if (ch == '$' && isalpha(nextch)) 
	{
		end = (*tmp) + 2;
		wrl = 1;
		while (*end && isalnum(*end++)) wrl++;

		if ((tmpstr = strndup((*tmp) + 1, wrl)) == NULL) {
			warn(NULL);
			goto ex_fail;
		}

		env_t *env;
		if ((env = getshenv(cur_sh_env, tmpstr)) != NULL)
		{
			wrl = snprintf(*dst, BUFSIZ - (*dst - buf), "%s", env->val);
			*dst += wrl;
			len += wrl;
		}
		free(tmpstr);
		tmpstr = NULL;
		*tmp = end;
	} 
	/* $0 - $9 */
	else if (ch == '$' && isdigit(nextch)) 
	{
		wrl = snprintf(*dst, BUFSIZ - (*dst - buf), "ARGV[%d]", nextch - 0x30);
		len += wrl;
		*dst += wrl;
		*tmp+=2;
	}
	/* $?... */
	else if (ch == '$' && nextch == '{')
	{
		end = strchr((*tmp) + 2, '}');
		if (end == NULL) {
			warnx("unterminated ${}");
			goto ex_fail;
		}
		*end = '\0';
		env_t *env;
		if ((env = getshenv(cur_sh_env, (*tmp)+2)) != NULL)
		{
			wrl = snprintf(*dst, BUFSIZ - (*dst - buf), "%s", env->val);
			*dst += wrl;
			len += wrl;
		} 
		*tmp = end + 1;
	} else if (ch == '$' && nextch == '(' && *((*tmp)+2) == '(' ) {
		var = strstr((*tmp)+3, "))");
		if (var == NULL) {
			warnx("unterminated $(())");
			goto ex_fail;
		}
		*var = '\0';
		warnx("unprocessed arithmetic expansion: $((%s))\n", (*tmp)+3);
		*tmp = var + 3;
	} else if (ch == '`' || nextch == '(') {
		int open = 1;
		int inner_double = 0, inner_single = 0;
		var = (ch == '`') ? (*tmp)+1 : (*tmp)+2;
		while (*var)
		{
			if (!inner_single && *var == '\\') {
				if (!*(var+1)) {
					warnx("trailing escape '\\' at end");
					goto ex_fail;
				}
				var++;
			} else if (!inner_single && !inner_double && *var == '\'') {
				inner_single = 1;
			} else if (!inner_single && !inner_double && *var == '"') {
				inner_double = 1;
			} else if (inner_single && *var == '\'') {
				inner_single = 0;
			} else if (inner_double && *var == '"') {
				inner_double = 0;
			} else if ((inner_single && *var != '\'') || 
					(inner_double && *var != '"')) {
				;
			} else if (ch == '`' && *var == '`') {
				open--;
				break;
			} else {
				if (*var=='(') open++;
				if (*var==')') open--;
				if (ch != '`' && !open) break;
			}
			var++;
		}
		if (open || inner_double || inner_single) {
			warnx("unterminated )");
			goto ex_fail;
		}
		*var = '\0';
		invoke_shell((ch == '`') ? (*tmp)+1 : (*tmp)+2, rc, dep+1); // FIXME where is the return?
		*tmp = var + 1;
	} else if (ch == '$' && nextch == '$') {
		wrl = snprintf(*dst, BUFSIZ - (*dst - buf), "%d", getpid());
		*dst += wrl;
		len += wrl;
		*tmp += 2;
	} else if (ch == '$' && nextch == '?') {
		wrl = snprintf(*dst, BUFSIZ - (*dst - buf), "%d", rc);
		*dst += wrl;
		len += wrl;
		*tmp += 2;
	}
	//printf("returning %d\n", len);
	return len;
ex_fail:
	return -1;
}

static const char *pad = "";

/**
 * process_args
 *
 * splits a string into arguments (argc, argv). 
 *
 * expands expansions, single quote and double quotes, as well as back-tick
 * and backslash escapes
 *
 * should any sh(1) terminating
 * string be found (e.g. | or ;) will stop processing and fill next with the
 * unprocessed buf location
 *
 * extracts redirect strings (unprocessed) into redirects
 *
 * returns the resultant return code
 *
 * buf - buffer to process
 * argc - return of argument count processed
 * argv - return of array of pointers to arguments (NULL terminated)
 * next - return of start of unprocessed input (or NULL)
 * rc - current value of $?
 * redirects - return of array of pointers to redirects (NULL terminated, unprocessed)
 */
static int process_args(const char *buf, int *argc, char ***argv, char **next, int rc, char ***redirects, int *newenv, int dep)
{
	const char *ptr = buf;
	const char *end = buf + strlen(buf);
	int ac = 0, rdc = 0;
	char **av = NULL, **rd = NULL;
	int in_single_quote = 0;
	int in_double_quotes = 0;
	*next = NULL;
	int in_redirects = 0;

	/* outer loop, each interation skips unquoted spaces & unprintable characters
	 * before processing a single argument, pushing onto *argv */
	while(ptr <= end && *ptr != '\0')
	{
		/* skip <blank> and garbage */
		if(isspace(*ptr)) goto next;
		else if(!isprint(*ptr)) goto next;

		/* used to look ahead of outer loop pointer */
		char *tmp = (char *)ptr;


		//printf("%*s processing_args(\"%s\",%d,\"%s\")\n", dep, pad, ptr, *argc, next ? *next : NULL);

		/* handle controls strings with no <blank> to the right */
		if(*ptr == ';') {
			*tmp++ = '\0';
			*next = tmp;
			break;
		} else if(*ptr == '|' && *(ptr+1) != '|') {
			// setup_pipe
			if (!pipe_mode) pipe_mode = 1;
			else if (pipe_mode == 1) pipe_mode = 3;
			else if (pipe_mode == 2) pipe_mode = 3;
			*newenv = 1;
			*next = tmp+1;
			*tmp++ = '\0';
			printf("%*spipe1\n", dep, pad);
			break;
		} else if(*ptr == '&' && *(ptr+1) != '&') {
			// setup_fork
			//printf("fork3\n");
			fork_mode = 1;
			pipe_mode = 0;
			*tmp++ = '\0';
			*next = tmp;
			break;
		} else if(*ptr == '|' && (*ptr+1) == '|') {
			// setup_list_or
			pipe_mode = 0;
			*tmp++ = '\0';
			*tmp++ = '\0';
			*next = tmp;
			break;
		} else if(*ptr == '&' && (*ptr+1) == '&') {
			// setup_list_and
			pipe_mode = 0;
			*tmp++ = '\0';
			*tmp++ = '\0';
			*next = tmp;
			break;
		}

		/* the length of this argument */
		int len = 0;

		/* temporary buffer & pointer therein */
		char buf[BUFSIZ+1];
		char *dst = buf;
		memset(dst, 0, BUFSIZ+1);

		/* inner loop, extracts a single argument. handles single & double quotes
		 * as well as backslask escapes and shell expansions */
		while(*tmp && isprint(*tmp)) {
			if (!in_single_quote && *tmp == '\\') {
				if (!*(tmp+1)) {
					warnx("trailing escape '\\' at end");
					goto clean_nowarn;
				}
				*(dst++) = *(tmp+1);
				memmove((void *)tmp, tmp+1, strlen(tmp));

				len++; tmp++;
				continue;
			} else if (!in_single_quote && !in_double_quotes && *tmp == '\'') {
				in_single_quote = 1;
				ptr++; len--; tmp++;
				continue;
			} else if (!in_single_quote && !in_double_quotes && *tmp == '"') {
				in_double_quotes = 1;

				ptr++; len--; tmp++;
				continue;
			} else if (in_single_quote && *tmp == '\'') {
				in_single_quote = 0;

				len++; tmp++;
				break;
			} else if (in_double_quotes && *tmp == '"') {
				in_double_quotes = 0;

				len++; tmp++;
				break;
			} else if (!in_single_quote && !in_double_quotes && isspace(*tmp)) {
				break;
			} else if (!in_single_quote && !in_double_quotes && *tmp == '&' && *(tmp+1) != '&' ) {
				fork_mode = 1;
				*tmp++ = '\0';
				*next = tmp;
				break;
			} else if (!in_single_quote && !in_double_quotes && *tmp == '|' && *(tmp+1) != '|' ) {
				if (!pipe_mode) pipe_mode = 1;	
				else if (pipe_mode == 1) pipe_mode = 2;
				*tmp++ = '\0';
				*next = tmp;
				printf("%*s  pipe2\n", dep, pad);
				break;
			} else if (!in_single_quote && !in_double_quotes && *tmp == '&' && *(tmp+1) == '&' ) {
				*tmp++ = '\0';
				*tmp++ = '\0';
				*next = tmp;
				break;
			} else if (!in_single_quote && !in_double_quotes && *tmp == '|' && *(tmp+1) == '|' ) {
				*tmp++ = '\0';
				*tmp++ = '\0';
				*next = tmp;
				break;
			}
			else if (!in_single_quote && !in_double_quotes && (*tmp == '{' || *tmp == '('))
			{
				*tmp+=1;
				int ex_rc = process_sub_shell(&tmp, *tmp == '{' ? '}' : ')', *tmp, &dst, buf, rc);
				if (ex_rc == -1)
					goto clean_nowarn;
				len += ex_rc;
			}
			/* handle $ followed by something other than whitespace */
			else if (!in_single_quote && (*tmp == '$' || *tmp == '`') && *(tmp+1) && !isspace(*(tmp+1))) 
			{
				/* refactor this into a function, so that it can be called
				 * recursively, e.g. inside $(()) 
				 *
				 * buf is needed for snprintf() protection
				 *
				 * rc for child invoke_shell() - FIXME how to handle rc return?
				 *
				 * len += expand_dollar(&dst, buf, &tmp, rc);
				 *
				 * treat -1 as 'clean_nowarn'
				 */
				//printf("was: dst=%s, buf=%s, tmp=%s len=%d\n", dst, buf, tmp, len);
				int ex_rc = process_expansion(&dst, buf, &tmp, rc, dep);
				if (ex_rc == -1)
					goto clean_nowarn;
				len += ex_rc;
				//printf(" is: dst=%s, buf=%s, tmp=%s len=%d\n", dst, buf, tmp, len);
			} else {
				*(dst++) = *tmp;
				len++;
				tmp++;
			}
		}

		if (in_single_quote) {
			warnx("unterminated single quote");
			goto clean_nowarn;
		}

		if (in_double_quotes) {
			warnx("unterminated double quotes");
			goto clean_nowarn;
		}

		/* if we have an argument, grow *argv and append a pointer */
		if(len>0) {
			*dst = '\0';

			/* handle <, >, >>, |> */
			if (is_redirect(buf)) {
				in_redirects = 1;

				char **nrd = realloc(rd, sizeof(char *) * (rdc+2));
				if (nrd == NULL) goto clean_fail;
				rd = nrd;

				if ((rd[rdc] = strdup(buf)) == NULL) goto clean_fail;
				rdc++;
				rd[rdc] = NULL;

				ptr = tmp;
				goto next;
			} else if (in_redirects) {
				in_redirects = 0;
			}

			char **nav = realloc(av, sizeof(char *) * (ac+2));
			if (nav == NULL) goto clean_fail;
			av = nav;

			if ((av[ac] = strdup(buf)) == NULL) goto clean_fail;
			ac++;
			av[ac] = NULL;
			//printf("%*s  adding arg '%s'\n", dep, pad, buf);

			len = strlen(buf) - 1;
		}

		ptr = tmp; // was before }
next:

		if (*next) break;
		ptr++; // FIXME reading ptr now causes a valgrind issue
		continue;
	}


	/* ensure *argv is NULL terminated */
	char **nav = realloc(av, sizeof(char *) * (ac+1));
	if (nav == NULL) goto clean_fail;
	av = nav;

	av[ac] = NULL;

	*argc = ac;
	*argv = av;

	//printf("%*s process_args resulted in %d args\n", dep, pad, *argc);

	return EXIT_SUCCESS;

clean_fail:
	warn(NULL);
clean_nowarn:
	if (av) {
		for (int i = 0; i < ac; i++)
		{
			if (av[i]) {
				free(av[i]);
				av[i] = NULL;
			}
		}
		free(av);
		av = NULL;
	}
	return EXIT_FAILURE;
}

static void trim(char *buf)
{
	char *ptr = buf + strlen(buf) - 1;
	while (*ptr && isspace(*ptr)) *ptr-- = '\0';
}

static int inside_if = 0;
static int inside_then = 0;
static int inside_else = 0;

static int invoke_shell(char *line, int old_rc, int dep)
{
	int argc = 0;
	char **argv = NULL;
	char **redirects = NULL;
	char *next;
	int newenv = 0;

	/* skip leading and trailing white space */
	while (*line && isspace(*line)) line++;
	trim(line);
	printf("%*sinvoke_shell=%s\n", dep, pad, line);

	if (process_args(line, &argc, &argv, &next, old_rc, &redirects, &newenv, dep) != EXIT_SUCCESS)
		return EXIT_FAILURE;
	printf("%*s        rest=%s\n", dep, pad, next);

	if(argc == 0 || argv == NULL || argv[0] == NULL)
		return EXIT_SUCCESS;

	int rc = old_rc;

	/* line us now useless, as expansions will have happened */
	int arglen = 0;
	char *allargs = calloc(1,200);
	int bufsiz = 200;
	//printf("%*s setting allargs to %p\n", dep, pad, allargs);
	for (int i = 1; i < argc && argv[i]; i++)
	{
		//printf("%*s processing argc=%d\n", dep, pad, i);
		//printf("%*s processing argv[%d]=%s\n", dep, pad, i, argv[i]);
		arglen += strlen(argv[i]);
		if(arglen == 0) continue;
		if (arglen > bufsiz)
		{

			while (bufsiz < (bufsiz+arglen+3)) 
				bufsiz += 200;

			//printf("%*s realloc: %p, %d\n", dep, pad, allargs, bufsiz);
			char *tmpall = realloc(allargs, bufsiz);
			//printf("%*s done\n", dep, pad);
			if (tmpall == NULL) {
				warnx("invoke_shell");
				return -1;
			}
			allargs = tmpall;
		}
		strcat(allargs, argv[i]);
		strcat(allargs, " ");
		//printf("%*s done\n", dep, pad);
	}

	if (newenv)
	{
		// setup pipe for LHS
	}

	char *tmp = NULL;
	if ((tmp = strchr(argv[0], '=')) != NULL && isname(argv[0], tmp - argv[0])) {
		*tmp = '\0';
		setshenv(cur_sh_env, argv[0], tmp+1);
		rc = invoke_shell(allargs, old_rc, dep+1);
	} else if (!strcmp(argv[0], "if")) {
		if (inside_if && !inside_then) goto unexpected;
		inside_if++;
		//printf(" invoke from if\n");
		rc = invoke_shell(allargs, old_rc, dep+1);
	} else if (!strcmp(argv[0], "then")) {
		if (!inside_if || inside_then) goto unexpected;
		inside_then++;
		//printf(" invoke from then");
		if (!rc) rc = invoke_shell(allargs, rc, dep+1);
	} else if (!strcmp(argv[0], "else")) {
		if (!inside_if || !inside_then) goto unexpected;
		if (inside_then) inside_then--;
		inside_else++;
		//printf(" invoke from else");
		if (rc) rc = invoke_shell(allargs, rc, dep+1);
	} else if (!strcmp(argv[0], "fi")) {
		if (!inside_if && !(inside_then || inside_else)) goto unexpected;
		if (inside_then) inside_then--;
		if (inside_else) inside_else--;
		inside_if--;
	} else {
		//printf("%*s running %s with %s\n", dep, pad, argv[0], allargs);
		printf("\n%*sfork_mode=%d, pipe_mode=%d, cmd=%s, args='%s'\n", dep, pad,
				fork_mode, pipe_mode, argv[0], allargs);

		if (pipe_mode == 1) {
			if (pipe(pipe_fd) == -1)
				warn("%s", argv[0]);
			else
				printf("%*s pipe[%d,%d]\n", dep, pad, pipe_fd[0], pipe_fd[1]);
			
			cur_sh_env->fds[STDOUT_FILENO] = pipe_fd[1];
			pipe_fd[1] = -1;
			cur_sh_env->fds[STDIN_FILENO] = STDIN_FILENO;
			pipe_mode = 2;
		} 
		else if (pipe_mode == 2) 
		{
			cur_sh_env->fds[STDOUT_FILENO] = STDOUT_FILENO;
			cur_sh_env->fds[STDIN_FILENO] = pipe_fd[0];
			pipe_fd[0] = -1;

			if (pipe(pipe_fd) == -1)
				warn("%s", argv[0]);
			else
				printf("%*s pipe[%d,%d]\n", dep, pad, pipe_fd[0], pipe_fd[1]);
		} 
		else if (pipe_mode == 3) 
		{
			cur_sh_env->fds[STDIN_FILENO] = pipe_fd[0];
			pipe_fd[0] = -1;
			
			if (pipe(pipe_fd) == -1)
				warn("%s", argv[0]);
			else
				printf("%*s pipe[%d,%d]\n", dep, pad, pipe_fd[0], pipe_fd[1]);

			cur_sh_env->fds[STDOUT_FILENO] = pipe_fd[1];
			pipe_fd[1] = -1;
			pipe_mode--;
		}
		else if (pipe_mode == 0)
		{
			cur_sh_env->fds[STDOUT_FILENO] = STDOUT_FILENO;
			cur_sh_env->fds[STDIN_FILENO] = STDIN_FILENO;
		}

		const struct builtin *bi = NULL;
		for (int i = 0; (bi = &builtins[i])->name; i++)
			if (!strcmp(argv[0], bi->name))
				break;

		if (bi->name && bi->fork) {
			rc = builtin(bi->func, argc, argv, fork_mode);
		} else if (bi->name) {
			rc = bi->func(argc, argv);
		} else {
			rc = builtin(cmd_external, argc, argv, fork_mode);
		}
	}

done:
	if (allargs) {
		free(allargs);
		allargs = NULL;
	}

	for (int i = 0; i < argc; i++ )
	{
		if (argv[i]) {
			free(argv[i]);
			argv[i] = NULL;
		}
	}
	free(argv);
	argv = NULL;

	if (next) {
		if (newenv) {
			cur_sh_env = clone_env(cur_sh_env, "tbc");
			newenv = 0;
			// set-up pipe for RHS
		}
		//fprintf(stdout, " recurse: if=%d, then=%d, else=%d, next=%s\n",
		//		inside_if, inside_then, inside_else, next);
		rc = invoke_shell(next, rc, dep+1);
	}

	return rc;
unexpected:
	warnx("unexpected %s", argv[0]);
	/*
	   warnx("unexpected %s (if=%d,then=%d,else=%d)", argv[0],
	   inside_if, inside_then, inside_else);
	   */
	inside_if = inside_then = inside_else = 0;
	goto done;
}

static void reset_terminal()
{
	/*
	   struct termios tios;
	   if (tcgetattr(STDIN_FILENO, &tios) == -1)
	   err(EXIT_FAILURE, "unable to reset terminal");

	   tios.c_lflag |= (ICANON|ECHO);

	   if (tcsetattr(STDIN_FILENO, TCSANOW, &tios) == -1)
	   err(EXIT_FAILURE, "unable to reset terminal");
	   */
}

static void clean_env()
{
	if (cur_sh_env != NULL) {
		free_ex_env(cur_sh_env);
		cur_sh_env = NULL;
	}
}

/* exported function defintions */

int main(int ac, char *av[])
{
	char *command_string = NULL;
	char *command_name = NULL; 
	char *command_file = NULL;

	{
		int opt = 0;
		while ((opt = getopt(ac, av, "abCefhimnuvxo:cs")) != -1)
		{
			switch (opt)
			{
				case 'a':
				case 'b':
				case 'C':
				case 'e':
				case 'f':
				case 'm':
				case 'n':
				case 'u':
				case 'v':
				case 'x':
					if (parse_set('-',opt))
						exit(EXIT_FAILURE);
					break;
				case 'o':
					if (parse_set_option(optarg))
						exit(EXIT_FAILURE);
					break;

				case 'c':
					opt_command_string = 1;
					break;
				case 'i':
					opt_interactive = 1;
					break;
				case 's':
					opt_read_stdin = 1;
					break;
			}
		}

		if (optind < ac && (!strcmp(av[optind], "-") || !strcmp(av[optind], "--")))
			optind++;

		if (optind >= ac) {
			if (opt_command_string)
				errx(EXIT_FAILURE, "missing command string");
			opt_read_stdin = 1;
			goto opt_skip;
		}


		if (opt_command_string) {
			command_string = av[optind++];
		}

		if (optind >= ac) goto opt_skip;

		if (opt_command_string)
			command_name = av[optind++];
		else if (!opt_read_stdin)
			command_file = av[optind++];
opt_skip:	
		;

		if (!command_string && !command_name && !command_file && isatty(STDIN_FILENO))
			opt_interactive = 1;
	}

	if (opt_interactive)
		printf("zero-shell\nNB: this is not yet a proper implementation of sh(1)\n\n");

	/*
	   if (opt_command_string ) {
	   int rc = EXIT_SUCCESS;
	   exit(rc);
	   }

	   if (!opt_read_stdin && command_string) {
	   int rc = EXIT_SUCCESS;
	   exit(rc);
	   }
	   */

	if (opt_interactive && isatty(STDIN_FILENO))
	{
		/*
		   struct termios tios;
		   if (tcgetattr(STDIN_FILENO, &tios) == -1)
		   err(EXIT_FAILURE, NULL);

		   tios.c_lflag &= ~(ICANON|ECHO);
		   */
		// FORK ISSUE atexit(reset_terminal);
		/*
		   if (tcsetattr(STDIN_FILENO, TCSANOW, &tios) == -1)
		   err(EXIT_FAILURE, NULL);
		   */
	}

	if ((cur_sh_env = calloc(1, sizeof(shenv_t))) == NULL)
		err(EXIT_FAILURE, NULL);

	// FORK ISSUE atexit(clean_env);

	if ((cur_sh_env->private_envs = calloc(1, sizeof(env_t *))) == NULL)
		err(EXIT_FAILURE, NULL);

	/*
	add_fds(cur_sh_env, 0, stdin);
	add_fds(cur_sh_env, 1, stdout);
	add_fds(cur_sh_env, 2, stderr);
	*/

	cur_sh_env->fds[0] = 0;
	cur_sh_env->fds[1] = 1;
	cur_sh_env->fds[2] = 2;

	for (int i = 3; i < NUM_FDS; i++) cur_sh_env->fds[i] = -1;

	cur_sh_env->name = strdup("master");

	umask((cur_sh_env->umask = umask(0)));


	if (signal(SIGCHLD, sig_chld) == SIG_ERR)
		err(EXIT_FAILURE, "signal");

	if (command_name == NULL)
		command_name = av[0];

	if (array_add(&cur_sh_env->argv, &cur_sh_env->argc, command_name) == -1)
		err(EXIT_FAILURE, NULL);

	/* used all over */
	char buf[BUFSIZ] = {0};

	char *shlvl_str = getenv("SHLVL");
	int shlvl = 1;
	if (shlvl_str)
		shlvl = atoi(shlvl_str) + 1;
	snprintf(buf, 10, "%u", shlvl);

	env_t *env = NULL;
	if ((env = setshenv(cur_sh_env, "SHLVL", buf)) == NULL) {
		if (errno)
			err(EXIT_FAILURE, NULL);
		else
			err(EXIT_FAILURE, "Unable to set SHLVL");
	}

	exportenv(env);

	//dump_envs(cur_sh_env);

	/* -i and/or -s */
	char *line = NULL;
	if (!opt_command_string) {
		line = buf;
		*line = '\0';
	}
	//int c;
	//int newline = 1;
	int rc = 0;

	setvbuf(stdout, NULL, _IONBF, 0);

	while(1)
	{
		/*
		   if (opt_interactive)
		   {
		   if (line < buf) line = buf;
		   else if (line >= buf+sizeof(buf)) line = buf + sizeof(buf) - 1;

		   if (newline) {*/
		if (opt_interactive)
			if (printf("# ") < 0)
				exit(EXIT_FAILURE);


		//fflush(stdout);
		/*
						  newline = 0;
						  }

						  if ((c = fgetc(stdin)) == EOF) {
						  exit(feof(stdin) ? EXIT_SUCCESS : EXIT_FAILURE);
						  }

						  if (c == '\n' ) {
						*line = '\0';
						fputc(c, stdout);
						rc = invoke_shell(buf, rc);
						memset(buf, 0, strlen(buf)+1);
						line = buf;
						newline = 1;
						} else {
						*line++ = c;
						fputc(c, stdout);
						}
						}

						if (!opt_interactive)
						{*/

		if (opt_command_string && command_string && *command_string) {
			sscanf(command_string, "%m[^\n]", &line);
			if (line == NULL)
				exit(EXIT_FAILURE);
			command_string = NULL;
		} else if (!opt_command_string) {
			line = fgets(buf, BUFSIZ, stdin);

			if(line == NULL) {
				if(feof(stdin))
					break;
				exit(EXIT_FAILURE);
			}
		} else
			break;

		rc = invoke_shell(line, rc, 1);
		if (opt_command_string) 
		{
			if (line) {
				free(line);
			line = NULL;
			}
		}
		/*}

		  if (opt_interactive) {*/
		fflush(stdout);
		fflush(stderr);
		//}
	}

	clean_env();
	exit(EXIT_SUCCESS);
}
