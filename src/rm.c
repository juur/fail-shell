#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <err.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <dirent.h>

static void show_usage()
{
	fprintf(stderr, "Usage: rm [-fiRr] file...\n");
	exit(EXIT_FAILURE);
}

static int opt_force = 0;
static int opt_interactive = 0;
static int opt_recursive = 0;

static int perform_rm(const char *name, mode_t mode) 
{
	if (S_ISDIR(mode)) {
		printf("rmdir(%s)\n", name);
		/*
		if (rmdir(name)) {
			warn("rmdir <%s>:", name);
			return 1;
		}
		*/
	} else {
		printf("unlink(%s)\n", name);
		/*
		if (unlink(name)) {
			warn("unlink <%s>:", name);
			return 1;
		}
		*/
	}

	return 0;
}

static bool is_ok(const char *name)
{
	printf("Okay to delete <%s> ? (y/n): ", name);
	fflush(stdout);
    fflush(stdin);

	char *resp = NULL;

	if (scanf(" %ms[\n]", &resp) != 1)
		return false;

	if (!strcmp("y", resp)) {
		free(resp);
		return true;
	}

	free(resp);
	return false;
}

static int do_rm(const char *name)
{
	if (!strcmp(".", name) || !strcmp("..", name)) {
		warnx("refusing to remove <%s>", name);
		return 1;
	}

    int rc = 0;
	struct stat sb;

	if ( (stat(name, &sb) == -1) && (errno == ENOENT) ) {
		if (opt_force == 0)
			warnx("file does not exist: %s", name);
		return 1;
	}

	if (!S_ISDIR(sb.st_mode)) {
do_dir:
		if (!opt_force && ((access(name, W_OK) && isatty(STDIN_FILENO))
					|| opt_interactive)) {
			if (is_ok(name))
				return rc + perform_rm(name, sb.st_mode);
		} else {
			return rc + perform_rm(name, sb.st_mode);
		}
	} else {
		if (!opt_recursive) {
			warnx("%s is a directory", name);
			return 1;
		}

		DIR *dir;

		if ((dir = opendir(name)) == NULL) {
			warn("opendir: %s", name);
			return 1;
		}

		const struct dirent *dirp;

		char buf[BUFSIZ];

		while(1)
		{
			if ((dirp = readdir(dir)) == NULL)
				break;

			if (!strcmp(".", dirp->d_name) || !strcmp("..", dirp->d_name))
				continue;
			
			if ((size_t)snprintf(buf, 0, "%s/%s", name, dirp->d_name) > sizeof(buf)) {
				rc++;
				break;
			}

			snprintf(buf, sizeof(buf), "%s/%s", name, dirp->d_name);

			do_rm(buf);
		}

		closedir(dir);

        goto do_dir;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	char opt;
	while ((opt = getopt(argc, argv, "fiRr")) != -1)
	{
		switch (opt)
		{
			case 'f':
				opt_force = 1;
				opt_interactive = 0;
				break;

			case 'i':
				opt_force = 0;
				opt_interactive = 1;
				break;

			case 'r':
			case 'R':
				opt_recursive = 1;
				break;

			default:
				show_usage();
		}
	}

	if ( (optind >= argc) || (argc - optind < 1) ) {
		warnx("At least one file required.");
		show_usage();
	}

	int failure = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);

	for (int i = optind; i < argc; i++)
		failure += do_rm(argv[i]);

	exit(failure ? EXIT_FAILURE : EXIT_SUCCESS);
}
