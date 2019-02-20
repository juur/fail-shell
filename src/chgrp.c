#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <err.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

static int opt_recurse = 0;
static int opt_deref = 0;
static int opt_traverse_cmd = 0;
static int opt_traverse_all = 0;
static int opt_traverse_none = 0;
static int mode_chgrp = 0;

static void show_usage()
{
	if (mode_chgrp)
		fprintf(stderr,
				"Usage: chgrp [-h] group file...\n"
				"       chgrp -R [-H|-L|-P]\n");
	else
		fprintf(stderr,
				"Usage: chown [-h] owner[:group] file...\n"
				"       chown -R [-H|-L|-P]\n");
	exit(EXIT_FAILURE);
}

static int do_chgrp(const uid_t uid, const gid_t gid, const char *file, const int cmdline)
{
	int rc = EXIT_SUCCESS, err;
	struct stat sb, lsb;
	
	if (lstat(file, &lsb) == -1 || stat(file, &sb) == -1) {
		warn("%s", file);
		return EXIT_FAILURE;
	}

	/* we only recurse if recurse is on & this file is a directory
	 * however, if it is a link to a directory, we have to check
	 * the opt_traverse_* options
	 */
	if (opt_recurse && S_ISDIR(sb.st_mode) && 
			(!S_ISLNK(lsb.st_mode) || 
			 (((opt_traverse_cmd && cmdline) || 
			   (opt_traverse_all)) &&
			  !opt_traverse_none)))
	{
		DIR *dir = opendir(file);
		if (dir == NULL) {
			warn("%s", file);
			return EXIT_FAILURE;
		}

		char buf[BUFSIZ];
		struct dirent *ent;
		while ((ent = readdir(dir)) != NULL)
		{
			if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
				continue;

			snprintf(buf, BUFSIZ, "%s/%s", file, ent->d_name);
			rc += do_chgrp(uid, gid, buf, 0);
			errno = 0;
		}

		/* this should catch errors on readdir() */
		if (errno) {
			warnx("%s", file);
			rc++;
		}

		closedir(dir);
		/* pass through so we then chgrp the directory itsself */
	}
	

	if (opt_deref)
		err = lchown(file, uid, gid);
	else
		err = chown(file, uid, gid);

	if (err == -1 && ++rc)
		warn("%s", file);

	return rc;
}

int main(int argc, char *argv[])
{
	if (!strcmp(argv[0], "chgrp"))
		mode_chgrp = 1;

	{
		int opt;
		while ((opt = getopt(argc, argv, "hRHLP")) != -1)
		{
			switch (opt)
			{
				case 'h':
					opt_deref = 1;
					break;
				case 'R':
					opt_recurse = 1;
					break;
				case 'H':
					opt_traverse_cmd = 1;
					break;
				case 'L':
					opt_traverse_all = 1;
					break;
				case 'P':
					opt_traverse_none = 1;
					break;
				default:
					show_usage();
			}

			if (opt_recurse && 
					(opt_traverse_cmd + 
					 opt_traverse_none + 
					 opt_traverse_all == 0))
				opt_traverse_none = 1;

			if (argc - optind < 2)
				show_usage();

			if (opt_recurse + opt_deref > 1)
				show_usage();
		}
	}

	/* the next arg is either the group name (chgrp) or user[:group] (chown) */
	const char *grpnam = mode_chgrp ? argv[optind] : strchr(argv[optind], ':');
	const struct group *grp = grpnam ? getgrnam(argv[optind]) : NULL;

	gid_t gid = -1;
	uid_t uid = -1;

	/* if we were passed a group name but couldn't find it in the db */
	if (grp == NULL && grpnam) {
		char *err = NULL;
		gid = strtol(argv[optind], &err, 10);

		if (err != NULL && *err)
			errx(EXIT_FAILURE, "%s: invalid name/gid", argv[optind]);
	} else if (grp) {
		gid = grp->gr_gid;
	}

	/* for chown, repeat for the username/uid */
	if (!mode_chgrp) {
		/* user */
		char *tmp = argv[optind];
		/* user:group */
		if (grpnam)
			tmp = strndup(argv[optind], grpnam - argv[optind] - 1);

		const struct passwd *usr = getpwnam(tmp);
		if (usr == NULL) {
			char *err = NULL;
			uid = strtol(tmp, &err, 10);

			if (err != NULL && *err)
				errx(EXIT_FAILURE, "%s: invalid name/uid", tmp);
		} else {
			uid = usr->pw_uid;
		}
	}

	/* process each remaining argument as a target */
	int rc = 0;
	for (int i = ++optind; i < argc; i++)
	{
		rc += do_chgrp(uid, gid, argv[i], 1);
	}

	exit(rc ? EXIT_FAILURE : EXIT_SUCCESS);
}
