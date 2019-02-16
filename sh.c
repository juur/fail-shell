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

static int isnumber(const char *str)
{
	while(*str)
	{
		if(!isdigit(*str++)) return 0;
	}

	return 1;
}

static int cmd_cat(int argc, char *argv[])
{
	FILE *in = stdin;
	int is_stdin = 1;

	int ndelay = 0;

	{
		int opt;
		while ((opt = getopt(argc, argv, "u")) != -1) {
			switch (opt) {
				case 'u':
					ndelay = 1;
					break;
				default:
					fprintf(stderr, "Usage: %s [-u] [FILES...]\n", argv[0]);
					exit(EXIT_FAILURE);
			}
		}
	}

	if(optind < argc) {
		char *file = argv[optind];
		if(file == NULL) return -1;
		in = fopen(file, "r");
		if(in == NULL) {
			fprintf(stderr, "cat: %s: %s\n", file, strerror(errno));
			return errno;
		}
		is_stdin = 0;
	}

	char buf[BUFSIZ];
	int running = 1;

	if(ndelay) {
		setvbuf(stdin, NULL, _IONBF, 0);
		setvbuf(stdin, NULL, _IONBF, 0);
	}

	while(running) {
		if(fgets(buf, BUFSIZ, in) == NULL)
			running = 0;
		else if(fputs(buf, stdout) == EOF)
			running = 0;
	}

	if(!is_stdin)
		fclose(in);
	return EXIT_SUCCESS;
}

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

static int execute(int ac, char *av[])
{
	execvp(av[0], av);
	warn(av[0]);
	return EXIT_FAILURE;
}

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

	while(*ptr != '\0')
	{
		if(isspace(*ptr)) goto next;
		if(!isprint(*ptr)) goto next;

		int len = 0;
		const char *tmp = ptr;
		
		while(!isspace(*tmp) && isprint(*tmp++)) len++;
		
		if(len>0) {
			char **nav = realloc(av, sizeof(char *) * (ac+1));
			if (nav == NULL) goto clean_fail;
			av = nav;

			av[ac] = strndup(ptr, len);
			if (av[ac] == NULL) goto clean_fail;

			ac++;
			ptr = tmp;
		}
next:
		ptr++;
		continue;
	}
	
	char **nav = realloc(av, sizeof(char *) * (ac+1));
	if (nav == NULL) goto clean_fail;
	av = nav;

	av[ac] = NULL;

	*argc = ac;
	*argv = av;

	return EXIT_SUCCESS;

clean_fail:
	if (av) {
		for (int i = 0; i < ac; i++)
		{
			if (av[i])
				free(av[i]);
		}
		free(av);
	}
	warn(NULL);
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

int main(int ac, char *av[])
{
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

		if (process_args(buf, &argc, &argv) != EXIT_SUCCESS)
			continue;

		if(argc == 0 || argv == NULL || argv[0] == NULL)
			continue;
		
		int rc = 0;

		if(!strcmp(argv[0], "cat")) {
			rc = builtin(cmd_cat, argc, argv);
		} else if(!strcmp(argv[0], "umask")) {
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
