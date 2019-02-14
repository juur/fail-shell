#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <libgen.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <sys/time.h>
#include <dirent.h>

static void show_usage()
{
	fprintf(stderr, "Usage: cp [-PfipRHL] source... target\n");
	exit(EXIT_FAILURE);
}

#define SL_CMDLINE_ONLY	1
#define SL_DEREF_ALL	2
#define SL_COPY_ALL		3

static int opt_force = 0;
static int opt_recurse = 0;
static int opt_mode = SL_COPY_ALL;
static int opt_confirm = 0;
static int opt_preserve = 0;
static int dest_dir = 0;
static int opt_verbose = 0;

static int rmok(char *file)
{
	if (!opt_confirm) return 1;
	char yes = 0;

	fprintf(stdout, "cp: overwrite '%s'? ", file);
	fflush(stdout);
	fscanf(stdin, "%c", &yes);

	return yes == 'y';
}

static int do_cp(char *tsrc, char *tdst, int iscmdline)
{
	int free_src = 0;
	int free_dst = 0;
	int rc = 0;
	char *src = tsrc;
	char *dst = tdst;


	struct stat src_sb;
	if (stat(src, &src_sb) == -1) {
		warn("%s", src);
		return -1;
	}

	if (!opt_recurse && S_ISDIR(src_sb.st_mode)) {
		warnx("%s: is a directory", src);
		return -1;
	} else if (S_ISDIR(src_sb.st_mode)) {
		// TODO mkdir
	
		// recurse
		DIR *dir = opendir(src);
		if (dir == NULL) {
			warn("%s", src);
			return -1;
		}
		struct dirent *ent;

		errno = 0;
		while ((ent = readdir(dir)) != NULL)
		{
			if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
				continue;

			int len = strlen(src) + 1 + strlen(ent->d_name) + 1;
			char *name = calloc(1, len);
			if (name == NULL) 
				break;

			snprintf(name, len, "%s/%s", src, ent->d_name);
			// TODO prepend folder name to dst
			do_cp(name, dst, 0);
		}

		if (errno != 0)
			warn("%s", dst);

		closedir(dir);
		return 0;
	}

	int src_flags = O_RDONLY;
	int dst_flags = O_WRONLY|O_CREAT|O_EXCL;
	
	if (opt_mode == SL_DEREF_ALL || (iscmdline && opt_mode == SL_CMDLINE_ONLY)) {
		struct stat src_lsb;
		if (lstat(src, &src_lsb) == -1) {
			warn("%s", src);
			return -1;
		}

		char *src = calloc(1, src_sb.st_size + 1);
		if (src == NULL) {
			warn(NULL);
		}

		free_src = 1;
		if (readlink(tsrc, src, src_sb.st_size) == -1) {
			warn("%s", tsrc);
			goto err_free;
		}
	}

	int src_fd = open(src, src_flags);
	if (src_fd == -1 || fstat(src_fd, &src_sb)) {
		warn("%s", src);
		goto err_free;
	}

	if (dest_dir) {
		int len = strlen(tdst) + 1 + strlen(basename(src)) + 1;
		dst = calloc(1, len);
		if (dst == NULL) {
			warn(NULL);
			goto err_free;
		}
		free_dst = 1;
		snprintf(dst, len, "%s/%s", tdst, basename(src));
	}

	struct stat dst_sb;
	if ( (stat(dst, &dst_sb) != -1) || (errno != ENOENT)) {
		if (opt_force) {
			if (!rmok(dst))
				goto err_free;
			if (unlink(dst) == -1) {
				warn("%s", dst);
				goto err_free;
			}
		} else {
			warnx("%s: file exists", dst);
			goto err_free;
		}
	}

	int dst_fd = open(dst, dst_flags);
	if (dst_fd == -1) {
		if (errno == EEXIST && opt_force) {
			if (!rmok(dst))
				goto err_free;
			if (unlink(dst) == -1) {
				warn("%s", dst);
				goto err_free;
			}
			if ((dst_fd = open(dst, dst_flags)) == -1) {
				warn("%s: deleted, but error on creating new:", dst);
				goto err_free;
			}
		} else {
			warnx("%s: file exists", dst);
			goto err_free;
		}
	}

	char buf[BUFSIZ];

	int rd = 0, wr = 0;;
	while ((rd = read(src_fd, buf, BUFSIZ)) != -1)
	{
		if (rd == 0)
			break;

		if ((wr = write(dst_fd, buf, rd)) == -1) {
			break;
		}
	}

	if (rd == -1) {
		warn("%s", src);
		goto err_free;
	}

	if (wr == -1) {
		warn("4.%s", dst);
		goto err_free;
	}

	// we don't want to delete a partially broken file after this point
	close(dst_fd);
	dst_fd = 0;

	if (opt_verbose)
		printf("%s => %s\n", src, dst);

	if (opt_preserve) {
		struct utimbuf times = {
			src_sb.st_atime,
			src_sb.st_mtime
		};

		if (utime(dst, &times) == -1)
			warn("%s: unable to set atime/mtime", dst);

		if (chown(dst, src_sb.st_uid, src_sb.st_gid) == -1)
			warn("%s: unable to set uid/gid", dst);

		if (chmod(dst, src_sb.st_mode) == -1)
			warn("%s: unable to set mode", dst);
	}

ret:
	if (dst_fd)
		close(dst_fd);
	if (src_fd)
		close(src_fd);
	if (free_src) 
		free(src);
	if (free_dst)
		free(dst);
	return rc;

err_free:
	if (dst_fd) {
		close(dst_fd);
		dst_fd = 0;

		if( unlink(dst) == -1)
			warn("%s: failed to unlink damaged file", dst);
	}
	rc = -1;
	goto ret;
}

int main(int argc, char *argv[])
{
	char opt;
	while ((opt = getopt(argc, argv, "PfivpRHL")) != -1)
	{
		switch (opt)
		{
			case 'f':
				opt_force = 1;
				break;
			case 'R':
				opt_recurse = 1;
				break;
			case 'H':
				opt_mode = SL_CMDLINE_ONLY;
				break;
			case 'i':
				opt_confirm = 1;
				break;
			case 'L':
				opt_mode = SL_DEREF_ALL;
				break;
			case 'P':
				opt_mode = SL_COPY_ALL;
				break;
			case 'p':
				opt_preserve = 1;
				break;
			case 'v':
				opt_verbose = 1;
				break;

			default:
				show_usage();
		}
	}

	if ( (optind >= argc) || (argc - optind < 2) ) {
		warnx("source and target required");
		show_usage();
	}

	struct stat sb;
	char *dest = argv[argc-1];

	if (!stat(dest, &sb))
		dest_dir = S_ISDIR(sb.st_mode);

	if (opt_recurse && !dest_dir)
		errx(EXIT_FAILURE, "%s: is not a directory", dest);

	if (argc - optind > 2 && !dest_dir)
		errx(EXIT_FAILURE, "%s: is not a directory", dest);

	int failure = 0;

	for (int i = optind; i < argc-1; i++) {
		failure += do_cp(argv[i], dest, 1);
	}

	exit(failure ? EXIT_FAILURE : EXIT_SUCCESS);
}
