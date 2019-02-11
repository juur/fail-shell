#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <err.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <libgen.h>

#define BUF_SIZE	0x4000

static int ls(int argc, char *argv[])
{
	DIR *dir = opendir(".");
	if( dir == NULL ) {
		fprintf(stderr, "%s: cannot access %s: %s", argv[0], ".", strerror(errno));
		return errno;
	}
	struct dirent *ent = NULL;
	do {
		errno = 0;
		ent = readdir(dir);
		
		if(ent == NULL && errno == 0) {
			continue;
		} else if(ent == NULL) {
			fprintf(stderr, "%s: cannot read entry: %s", argv[0], strerror(errno));
			continue;
		} else if(ent->d_name[0] == '\0') {
			continue;
		} else {
			if (ent->d_name[0] == '.')
				continue;
			printf("%s ", ent->d_name);
		}

		fflush(stdout);
	} while(ent != NULL);
	closedir(dir);
	printf("\n");
	return 0;
}

static int cat(int argc, char *argv[])
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
	return 0;
}

static int execute(int ac, char *av[])
{
	execvp(av[0], av);
	warn(av[0]);
	exit(errno);
}

static int builtin(int (*func)(int, char **), int ac, char **av)
{
	int status;

	if(ac == 0 || av == NULL)
		errx(EXIT_FAILURE, "builtin: no args");

	pid_t newpid = fork();

	if(newpid == -1) {
		warn(av[0]);
		return errno;
	} else if(newpid == 0) {
		exit(func(ac,av));
	} else {
		if(waitpid(newpid, &status, 0) == -1) {
			warn(av[0]);
			return errno;
		}
		if(WIFEXITED(status)) {
			return WEXITSTATUS(status);
		} else
			return -1;
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
			av = realloc(av, sizeof(char *) * (ac+1));
			av[ac] = strndup(ptr, len);
			ac++;
			ptr = tmp;
		}
next:
		ptr++;
		continue;
	}

	*argc = ac;
	*argv = av;

	return 0;
}

static int pwd(int argc, char *argv[])
{
	char pwd[BUFSIZ];
	getcwd(pwd, BUFSIZ);
	printf("%s\n", pwd);
	exit(EXIT_SUCCESS);
}

static int cd(int argc, char *argv[])
{
	char *dir = NULL;

	if(argc == 1) {
		dir = getenv("HOME");
	} else {
		dir = argv[1];
	}

	if(dir == NULL) {
		fprintf(stderr, "%s: HOME not set\n", argv[0]);
		return -1;
	}

	int hyphen = !strcmp("-", argv[1]);

	if(hyphen) {
		dir = getenv("OLDPWD");
		if(dir == NULL) {
			fprintf(stderr, "%s: OLDPWD not set\n", argv[0]);
			return -1; 
		}
	}

	char oldpwd[BUFSIZ]; 
	getcwd(oldpwd, BUFSIZ);

	if(chdir(dir) == -1) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], dir, strerror(errno));
		return -1;
	}

	char pwd[BUFSIZ];
	getcwd(pwd, BUFSIZ);

	setenv("OLDPWD", oldpwd, 1);
	setenv("PWD", pwd, 1);

	if(hyphen)
		printf("%s\n", pwd);
}

static int basename_f(int argc, char *argv[])
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
		char buf[BUFSIZ];
		printf("# ");
		fflush(stdout);
		char *line = fgets(buf, BUFSIZ, stdin);
		
		if(line == NULL) {
			if(feof(stdin))
				exit(EXIT_SUCCESS);
			exit(EXIT_FAILURE);
		}

		int argc;
		char **argv;
		process_args(buf, &argc, &argv);

		if(argc == 0 || argv == NULL || argv[0] == NULL)
			continue;

		if(!strcmp(argv[0], "ls")) {
			builtin(ls, argc, argv);
		} else if(!strcmp(argv[0], "cat")) {
			builtin(cat, argc, argv);
		} else if(!strcmp(argv[0], "cd")) {
			cd(argc, argv);
		} else if(!strcmp(argv[0], "pwd")) {
			builtin(pwd, argc, argv);
		} else if(!strcmp(argv[0], "basename")) {
			builtin(basename_f, argc, argv);
		} else if(!strcmp(argv[0], "exit")) {
			int val = 0;
			if(ac == 2)
				val = atoi(av[1]);
			exit(val);
		} else {
			builtin(execute, argc, argv);
		}

		fflush(stdout);
		fflush(stderr);
	}
}
