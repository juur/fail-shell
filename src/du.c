#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <err.h>
#include <dirent.h>
#include <string.h>
#include <libgen.h>

static int opt_all_size = 0;
static int opt_total_size = 0;
static int opt_deref_cmdline = 0;
static int opt_units = 512;
static int opt_deref_all = 0;
static int opt_same_dev = 0;
static int opt_apparent_size = 0;

static int error_count = 0;

static void show_usage()
{
	fprintf(stderr, 
			"Usage: du [-a|-s] [-kx] [-H|-L] [file...]\n");
	exit(EXIT_FAILURE);
}

inline static ssize_t roundup(ssize_t value, ssize_t div)
{
	return (value + (div/2))/div;
}

static ssize_t perform_du(const char *restrict path, const int top, dev_t dev)
{
	struct stat sb;
	char *restrict tmp = NULL;

	/* we always want to know the size of the link, even if we are
	 * not dereferencing them */
	if (lstat(path, &sb) == -1) goto fail;

	/* deref is selected */
	if ((opt_deref_all || (top && opt_deref_cmdline)) && S_ISLNK(sb.st_mode))
	{
		tmp = malloc(sb.st_size + 1);
		if (tmp == NULL) goto fail;
		if (readlink(path, tmp, sb.st_size) == -1) goto fail;
		if (stat(tmp, &sb) == -1) goto fail;
	} 

	/* check for dev cross-over, or lookup the dev if we're
	 * looking at a command line path */
	if (dev == (dev_t)-1 && top) dev = sb.st_dev;
	else if (opt_same_dev && sb.st_dev != dev) return 0;

	const char *restrict fn = tmp ? tmp : path;
	ssize_t rc = 0;
	int is_dir = 0;

	/* handle recursion */
	if (S_ISDIR(sb.st_mode)) {
		is_dir = 1;
		struct dirent *restrict ent;
		DIR *restrict dir = opendir(fn);

		if (dir == NULL) goto fail;

		// TODO handle ent==NULL && errno
		while ((ent = readdir(dir)) != NULL)
		{
			if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;

			char *name = calloc(1, strlen(fn) + strlen(ent->d_name) + 2);
			if (name == NULL) { warn("%s", fn); continue; }

			strcpy(name, fn);
			strcat(name, "/");
			strcat(name, ent->d_name);
			rc += perform_du(name, 0, dev);
			free(name); name = NULL;
		}
		closedir(dir);
	} 

	/* add this file/directory */
	rc += opt_apparent_size ? sb.st_size : sb.st_blocks * 512;

	if (top || (is_dir && !opt_total_size) || (!is_dir && opt_all_size))
		printf("%-7ld %s\n", roundup(rc, opt_units), path);

ok:
	if (tmp) { free(tmp); tmp = NULL; }
	return rc;

fail:
	warn("%s", path);
	error_count++;
	rc = 0;
	goto ok;
}

int main(const int argc, char *argv[])
{
	{
		int opt;

		while ((opt = getopt(argc, argv, "askxHLmb")) != -1)
		{
			switch (opt)
			{
				case 'a':
					opt_all_size = 1;
					break;
				case 's':
					opt_total_size = 1;
					break;
				case 'H':
					opt_deref_cmdline = 1;
					break;
				case 'k':
					opt_units = 1024;
					break;
				case 'L':
					opt_deref_all = 1;
					break;
				case 'x':
					opt_same_dev = 1;
					break;
				case 'b':
					opt_units = 1;
					opt_apparent_size = 1;
					break;
				case 'm':
					opt_units = 1024*1024;
					break;
				default:
					show_usage();
			}
		}

		if (opt_all_size + opt_total_size > 1)
			show_usage();
	}

	ssize_t total = 0;

	if (optind >= argc)
		total = perform_du(".", 1, -1);
	else
		for (int i = optind; i < argc; i++)
			total += perform_du(argv[i], 1, -1);

	exit(error_count ? EXIT_FAILURE : EXIT_SUCCESS);
}
