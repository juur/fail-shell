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

/* deference a file. always returns a new string that should be free()'d */
static char *deref(char *txt, int iscmdline)
{
	if (opt_mode == SL_DEREF_ALL || (iscmdline && opt_mode == SL_CMDLINE_ONLY)) {
		struct stat lsb;
		if (lstat(txt, &lsb) == -1) {
			warn("lstat: %s", txt);
			return NULL;
		}

		char *ret = calloc(1, lsb.st_size + 1);
		if (ret == NULL) {
			warn(NULL);
			return NULL;
		}

		if (readlink(txt, ret, lsb.st_size) == -1) {
			warn("readlink: %s", txt);
			return NULL;
		}
	} else {
		return(strdup(txt));
	}
}

/* main copy function */
static int do_cp(char *tsrc, char *tdst, int iscmdline)
{
	int free_dst = 0;
	int free_src = 0;
	int rc = 0;

	/* deference the source as required */
	tsrc = deref(tsrc, iscmdline);
	
	char *src = tsrc;
	char *dst = tdst;

	struct stat src_sb;
	if (stat(src, &src_sb) == -1) {
		warn("stat: %s", src);
		return -1;
	}

	if (!opt_recurse && S_ISDIR(src_sb.st_mode)) {
		warnx("%s: is a directory", src);
		return -1;
	} else if (S_ISDIR(src_sb.st_mode)) {
		DIR *src_dir = opendir(src);
		if (src_dir == NULL) {
			warn("opendir: %s", src);
			return -1;
		}

		/* loop of each directory entry, process as appropriate */
		struct dirent *ent;
		while ((ent = readdir(src_dir)) != NULL)
		{
			errno = 0;
			char *dn = ent->d_name;
			char *full_dn;
			int free_dn = 0;
			int free_full_dn = 0;

			/* skip . and .. */
			if (!strcmp(dn, ".") || !strcmp(dn, ".."))
				continue;


			/* build the full destination name */
			{
				int len = strlen(src) + 1 + strlen(dn) + 1;

				full_dn = calloc(1, len);
				if (full_dn == NULL)
					continue;
				free_full_dn = 1;

				snprintf(full_dn, len, "%s/%s", src, dn);
			}

			struct stat ent_sb;
			if (stat(full_dn, &ent_sb) == -1) {
				warn("stat: %s", full_dn);
				goto skip;
			}

			/* dereference the source if required */
			if (S_ISLNK(ent_sb.st_mode) && 
					(opt_mode == SL_DEREF_ALL || (iscmdline && opt_mode == SL_CMDLINE_ONLY))) {
				struct stat ent_lsb;
				if (lstat(dn, &ent_lsb) == -1) {
					warn("lstat: %s", dn);
					goto skip;
				}

				dn = calloc(1, ent_sb.st_size);
				if (dn == NULL) {
					warn(NULL);
					goto err_free;
				}
				free_dn = 1;
				if (readlink(ent->d_name, dn, ent_sb.st_size) == -1) {
					warn("readlink: %s", ent->d_name);
					goto skip;
				}

				if (stat(dn, &ent_sb) == -1) {
					warn("stat: %s", dn);
					goto skip;
				}
			}

			int is_dir = 0;
			char *newdst = NULL;

			/* if our source is a directory, adjust the destination filename prefix &
			 * create the destination folder if it doesn't exist */
			if (S_ISDIR(ent_sb.st_mode)) {
				is_dir = 1;
				int dirdstlen = strlen(dst) + 1 + strlen(basename(full_dn)) + 1;
				newdst = calloc(1, dirdstlen);
				snprintf(newdst, dirdstlen, "%s/%s", dst, basename(full_dn));

				struct stat sb_dst_dir;
				int res = stat(newdst, &sb_dst_dir);

				if( res && errno == ENOENT ) {
					if (mkdir(newdst, ent_sb.st_mode) == -1) {
						warn("mkdir: %s", newdst);
						goto skip;
					}
				} else if( res || (res == 0 && !S_ISDIR(sb_dst_dir.st_mode)) ) {
					if( res )
						warn("stat: %s", newdst);
					else
						warnx("stat: %s: is not a directory", newdst);
					goto skip;
				}
			}

			/* recurse down - use either target file or target directory + file */
			do_cp(full_dn, is_dir ? newdst : dst, 0);
			if (newdst)
				free(newdst);
			errno = 0;
skip:
			if (free_full_dn)
				free(full_dn);
			if (free_dn) 
				free(dn);
		}

		if (errno != 0)
			warn("read_loop: %s", dst);

		closedir(src_dir);
		return 0;
	}

	/* set-up the open(2) flags ready */
	int src_flags = O_RDONLY;
	int dst_flags = O_WRONLY|O_CREAT|O_EXCL;
	
	/* dereference the source file, if required */
	if (opt_mode == SL_DEREF_ALL || (iscmdline && opt_mode == SL_CMDLINE_ONLY)) {
		struct stat src_lsb;
		if (lstat(src, &src_lsb) == -1) {
			warn("lstat: %s", src);
			return -1;
		}

		free(src);
		src = calloc(1, src_sb.st_size + 1);
		if (src == NULL) {
			warn(NULL);
			goto err_free;
		}

		free_src = 1;
		if (readlink(tsrc, src, src_sb.st_size) == -1) {
			warn("readlink: %s", tsrc);
			goto err_free;
		}
	}


	/* if our destination is a folder, append the filename to create a 
	 * destination file */
	if (dest_dir) {
		int len = strlen(tdst) + 1 + strlen(basename(src)) + 1;
		if (dst != tdst)
			free(dst);
		dst = calloc(1, len);
		if (dst == NULL) {
			warn(NULL);
			goto err_free;
		}
		free_dst = 1;
		snprintf(dst, len, "%s/%s", tdst, basename(src));
	}

	/* FIXME this is a duplication of the open command below? */
	struct stat dst_sb;
	if ( (stat(dst, &dst_sb) != -1) || (errno != ENOENT)) {
		if (opt_force) {
			if (!rmok(dst))
				goto err_free;
			if (unlink(dst) == -1) {
				warn("unlink: %s", dst);
				goto err_free;
			}
		} else {
			warnx("%s: file exists", dst);
			goto err_free;
		}
	}

	/* ensure we can open the source file */
	int src_fd = open(src, src_flags);
	if (src_fd == -1 || fstat(src_fd, &src_sb)) {
		warn("open: %s", src);
		goto err_free;
	}

	/* create the target file, if the destination already exists, handle forced
	 * overwritting. This is done last to reduce left over files */
	int dst_fd = open(dst, dst_flags, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
	if (dst_fd == -1) {
		if (errno == EEXIST && opt_force) {
			if (!rmok(dst))
				goto err_free;
			if (unlink(dst) == -1) {
				warn("unlink: %s", dst);
				goto err_free;
			}
			if ((dst_fd = open(dst, dst_flags)) == -1) {
				warn("%s: deleted, but error on creating new:", dst);
				goto err_free;
			}
		} else {
			warn("open: %s", dst);
			goto err_free;
		}
	}

	/* perform the actual data copy */
	{
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
			warn("%s", dst);
			goto err_free;
		}
	}

	/* we don't want to delete a partially broken file after this point */
	close(dst_fd);
	dst_fd = -1;

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
	if (dst_fd != -1)
		close(dst_fd);
	if (src_fd != -1)
		close(src_fd);

	if (free_src) 
		free(src);
	if (free_dst)
		free(dst);
	return rc;

err_free:
	if (dst_fd != -1) {
		close(dst_fd);
		dst_fd = -1;

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

	if (argc - optind == 2 && !dest_dir)
	{
		char *src = deref(argv[optind], 1);
		struct stat src_sb;
		if (stat(src, &src_sb) == -1)
			err(EXIT_FAILURE, "stat: %s", src);
	} 

	for (int i = optind; i < argc-1; i++) {
		failure += do_cp(argv[i], dest, 1);
	}

	exit(failure ? EXIT_FAILURE : EXIT_SUCCESS);
}
