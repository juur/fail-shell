#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

static void show_usage(const char *message)
{
	if (message)
		warnx("%s", message);

	fprintf(stderr,
			"Usage: ln: [-fs] [-L|-P] source_file target_file\n"
			"           [-fs] [-L|-P] source_file... target_dir\n");
	exit(EXIT_FAILURE);
}

static int opt_force = 0;
static int opt_deref_source = 0;
static int opt_symlink = 0;

static int linknode(const char *to, const char *from)
{
	struct stat sb;
	if (stat(from, &sb) != -1 || errno != ENOENT) {
		if (opt_force) {
			if (unlink(from) == -1) {
				warn("%s", from);
				return EXIT_FAILURE;
			}
		} else {
			warnx("%s: file exists", from);
			return EXIT_FAILURE;
		}
	}

	if (opt_symlink)
	{
		if (symlink(to, from) == -1) {
			warn("%s", from);
			return EXIT_FAILURE;
		}
	}
	else
	{
		if (link(to, from) == -1) {
			warn("%s", from);
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}

int main(const int argc, char *argv[])
{
	{
		int opt;
		while ((opt = getopt(argc, argv, "fsLP")) != -1)
		{
			switch (opt)
			{
				case 'f':
					opt_force = 1;
					break;
				case 's':
					opt_symlink = 1;
					break;
				case 'L':
					opt_deref_source = 1;
					break;
				case 'P':
					opt_deref_source = 0;
					break;
				default:
					show_usage(NULL);
					break;
			}

			if (opt_symlink)
				opt_deref_source = 0;
		}

		if (argc - optind < 2)
			show_usage(NULL);
	}

	const char *dest_name = argv[argc-1];
	struct stat sb;
	int rc = 0, is_dir = 0;

	if ((rc = stat(dest_name, &sb)) == -1 && errno != ENOENT)
		err(EXIT_FAILURE, "%s", dest_name);
	else if (rc == 0)
		is_dir = S_ISDIR(sb.st_mode);

	if ((argc - optind > 2) && !is_dir)
		show_usage("target is not a directory");

	int exitcode = 0;
	for (int i = optind; i < argc-1; i++)
	{
		char buf[BUFSIZ];
		char *src = argv[i];
		int free_src = 0;

		if (opt_deref_source)
		{
			if (lstat(src, &sb) == -1) {
				warn("%s", src);
				continue;
			}

			if (S_ISLNK(sb.st_mode))
			{
				if (lstat(src, &sb) == -1) {
					warn("%s", src);
					continue;
				}

				char *path = src;
				if ((src = calloc(1, sb.st_size + 1)) == NULL) {
					warn("%s", path);
					continue;
				}
				free_src = 1;

				if (readlink(path, src, sb.st_size) == -1) {
					warn("%s", path);
					goto next;
				}
			}
		}

		if (is_dir) {
			snprintf(buf, BUFSIZ, "%s/%s", dest_name, basename(src));
			exitcode += linknode(src, buf);
		} else {
			exitcode += linknode(src, dest_name);
		}
next:
		if (free_src)
			free(src);
	}

	exit(exitcode ? EXIT_FAILURE : EXIT_SUCCESS);
}
