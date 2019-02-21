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

#define BUF_SIZE	0x4000

/* check if a NULL terminated string is a valid sequence of digits */
static int isnumber(const char *str)
{
	while(*str)
	{
		if(!isdigit(*str++)) return 0;
	}

	return 1;
}

/* check if a NULL/length terminated string is a valid POSIX 'Name' */
static int isname(const char *str, int max_len)
{
	if (!(*str == '_' || isalpha(*str))) return 0;

	int len = 0;

	while (*++str && (len++ < max_len))
	{
		if (isalnum(*str)) continue;
		else if (*str == '_') continue;
		else return 0;
	}
	return 1;
}

static inline int isnewline(const char c)
{
	return c == '\n';
}

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

/* replace the current process with another, effectively implements exec */
static int execute(int ac, char *av[])
{
	execvp(av[0], av);
	warn(av[0]);
	return EXIT_FAILURE;
}

/* execute a builtin in a seperate process. builtins that should run in the
 * shell process, should not use this */
static int builtin(int (*func)(int, char **), int ac, char **av)
{
	int status;

	if(ac == 0 || av == NULL) {
		warn("builtin: no args");
		return EXIT_FAILURE;
	}

	pid_t newpid = fork();

	if(newpid == -1) {
		warn(av[0]);
		return EXIT_FAILURE;
	} else if(newpid == 0) {
		exit(func(ac,av));
	} else {
		if(waitpid(newpid, &status, 0) == -1) {
			warn(av[0]);
			return EXIT_FAILURE;
		}
		if(WIFEXITED(status)) {
			return WEXITSTATUS(status);
		} else
			return EXIT_SUCCESS;
	}
}

static int process_args(const char *buf, int *argc, char ***argv)
{
	const char *ptr = buf;
	int ac = 0;
	char **av = NULL;
	int in_single_quote = 0;
	int in_double_quotes = 0;

	/* outer loop, each interation skips unquoted spaces & unprintable characters
	 * before processing a single argument, pushing onto *argv */
	while(*ptr != '\0')
	{
		if(isspace(*ptr)) goto next;
		if(!isprint(*ptr)) goto next;

		/* the length of this argument */
		int len = 0;
		char *tmp = (char *)ptr;

		char buf[BUFSIZ];
		char *dst = buf;

		/* inner loop, extracts a single argument. handles single & double quotes
		 * as well as backslask escapes */
		while(*tmp && isprint(*tmp)) {
			if (!in_single_quote && *tmp == '\\') {
				if (!*(tmp+1)) {
					warnx("trailing escape '\\' at end");
					goto clean_nowarn;
				}
				*(dst++) = *(tmp+1);
				memmove((void *)tmp, tmp+1, strlen(tmp));
				len++;
				tmp++;
				continue;
			} else if (!in_single_quote && !in_double_quotes && *tmp == '\'') {
				in_single_quote = 1;
				ptr++;
				len--;
				tmp++;
				continue;
			} else if (!in_single_quote && !in_double_quotes && *tmp == '"') {
				in_double_quotes = 1;
				ptr++;
				len--;
				tmp++;
				continue;
			} else if (in_single_quote && *tmp == '\'') {
				in_single_quote = 0;
				len++;
				tmp++;
				break;
			} else if (in_double_quotes && *tmp == '"') {
				in_double_quotes = 0;
				len++;
				tmp++;
				break;
			} else if (!in_single_quote && !in_double_quotes && isspace(*tmp)) {
				break;
			} 
			if (!in_single_quote && *tmp == '$' && *(tmp+1) && !isspace(*(tmp+1))) {
				char next = *(tmp+1);
				if (isalpha(next)) {
				} else if (isdigit(next)) {
				} else {
					char *end = NULL;
					char *var = NULL;

					switch (*(tmp+1))
					{
						case '{':
							end = strchr(tmp+2, '}');
							if (end == NULL) {
								warnx("unterminated ${}");
								goto clean_nowarn;
							}
							*end = '\0';
							if ((var = getenv(tmp+2)) != NULL)
							{
								int wrl = snprintf(dst, BUFSIZ - (dst - buf), "%s", var);
								dst += wrl;
								len += wrl;
							}
							tmp = end + 1;
							break;
						case '?':
							break;
						default:
							*(dst++) = *tmp;
							len++;
							tmp++;
							break;
					}
				}
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

			char **nav = realloc(av, sizeof(char *) * (ac+1));
			if (nav == NULL) goto clean_fail;
			av = nav;

			//av[ac] = strndup(ptr, len);
			av[ac] = strdup(buf);
			if (av[ac] == NULL) goto clean_fail;

			ac++;
		}
		ptr = tmp; // was before }
next:
		ptr++;
		continue;
	}
	
	/* ensure *argv is NULL terminated */
	char **nav = realloc(av, sizeof(char *) * (ac+1));
	if (nav == NULL) goto clean_fail;
	av = nav;

	av[ac] = NULL;

	*argc = ac;
	*argv = av;

	return EXIT_SUCCESS;

clean_fail:
	warn(NULL);
clean_nowarn:
	if (av) {
		for (int i = 0; i < ac; i++)
		{
			if (av[i])
				free(av[i]);
		}
		free(av);
	}
	return EXIT_FAILURE;
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

	int hyphen = !strcmp("-", argv[1]);

	if(hyphen) {
		dir = getenv("OLDPWD");
		if(dir == NULL) {
			fprintf(stderr, "%s: OLDPWD not set\n", argv[0]);
			return EXIT_FAILURE; 
		}
	}

	char oldpwd[BUFSIZ]; 
	getcwd(oldpwd, BUFSIZ);

	if(chdir(dir) == -1) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], dir, strerror(errno));
		return EXIT_FAILURE;
	}

	char pwd[BUFSIZ];
	getcwd(pwd, BUFSIZ);

	setenv("OLDPWD", oldpwd, 1);
	setenv("PWD", pwd, 1);

	if(hyphen)
		printf("%s\n", pwd);

	return EXIT_SUCCESS;
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

static void trim(char *buf)
{
	char *ptr = buf + strlen(buf) - 1;
	while (*ptr && isspace(*ptr)) *ptr-- = '\0';
}

int main(int ac, char *av[])
{
	printf("zero-shell\nNB: this is not yet a proper implementation of sh(1)\n\n");

	while(1)
	{
		if (printf("# ") < 0)
			exit(EXIT_FAILURE);

		fflush(stdout);

		char buf[BUFSIZ];
		char *line = fgets(buf, BUFSIZ, stdin);
		
		if(line == NULL) {
			if(feof(stdin))
				exit(EXIT_SUCCESS);
			exit(EXIT_FAILURE);
		}

		int argc = 0;
		char **argv = NULL;

		/* skip leading and trailing white space */
		while (*line && isspace(*line)) line++;
		trim(line);

		if (process_args(line, &argc, &argv) != EXIT_SUCCESS)
			continue;

		if(argc == 0 || argv == NULL || argv[0] == NULL)
			continue;
		
		int rc = 0;

		if(!strcmp(argv[0], "umask")) {
			rc = cmd_umask(argc, argv);
		} else if(!strcmp(argv[0], "cd")) {
			rc = cmd_cd(argc, argv);
		} else if(!strcmp(argv[0], "pwd")) {
			rc = builtin(cmd_pwd, argc, argv);
		} else if(!strcmp(argv[0], "basename")) {
			rc = builtin(cmd_basename, argc, argv);
		} else if(!strcmp(argv[0], "exit")) {
			int val = 0;
			if(ac == 2)
				val = atoi(av[1]);
			exit(val);
		} else {
			rc = builtin(execute, argc, argv);
		}

		printf("\nrc=%u\n", rc);

		for (int i = 0; i < argc; i++ )
		{
			if (argv[i])
				free(argv[i]);
		}
		free(argv);

		fflush(stdout);
		fflush(stderr);
	}
}
