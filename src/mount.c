#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <err.h>
#include <string.h>
#include <strings.h>
#include <sys/mount.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mntent.h>
#include <errno.h>
#include <stdbool.h>

/* local variable defintions */

static int opt_all = 0;
static int opt_verbose = 0;
static int opt_nomtab = 0;
static int opt_rdonly = 0;
static int opt_mandlock = 0;
static int opt_noatime = 0;
static int opt_nodev = 0;
static int opt_nodiratime = 0;
static int opt_noexec = 0;
static int opt_nosuid = 0;
static int opt_relatime = 0;
static int opt_silent = 0;
static int opt_strictatime = 0;
static int opt_sync = 0;
static int opt_umount = 0;
static int opt_force = 0;

/* local function declarations */

static int do_mount(const char *src, const char *target, const char *fstype, 
		unsigned long mountflags, const void *data, bool silent);

/* local function defintions */

static void show_usage()
{
	fprintf(stderr, "Usage: mount [-avVh] [-o option] [source] target\n");
	exit(EXIT_SUCCESS);
}

static void show_version()
{
	fprintf(stderr, "mount version 0.1.0\n");
	exit(EXIT_SUCCESS);
}

static void show_mounts()
{
	FILE *mtab;

	if ( (mtab = fopen("/etc/mtab", "r")) == NULL ) {
		errx(EXIT_FAILURE, "Unable to open /etc/mtab");
	}

	char *lineptr = NULL;
	size_t len = 0;
	int rc;

	while ( (rc = getline(&lineptr, &len, mtab)) > 0 )
	{
		if ( fwrite(lineptr, rc, 1, stdout) != 1 )
			errx(EXIT_FAILURE, "Error printing");

		lineptr = NULL;
		len = 0;
	}

	fclose(mtab);

	exit(EXIT_SUCCESS);
}

static void trim(char *buf)
{
	char *ptr = buf + strlen(buf) - 1;
	while (*ptr && isspace(*ptr)) *ptr-- = '\0';
}

static unsigned long parse_options(char *options, unsigned long mountflags, char *result_options)
{
	char *tok;
	tok = strtok(options, ",");

	while ( tok )
	{
		trim(tok);

		while(isspace(*tok) && *tok) 
			tok++;

		if (!*tok) goto skip;

		if ( !strcmp("defaults", tok) )
			mountflags &= ~(MS_RDONLY|MS_NOSUID|MS_NODEV|MS_NOEXEC|MS_SYNCHRONOUS);
		else if ( !strcmp("async", tok) )
			mountflags &= ~MS_SYNCHRONOUS;
		else if ( !strcmp("atime", tok) )
			mountflags &= ~MS_NOATIME;
		else if ( !strcmp("noatime", tok) )
			mountflags |= MS_NOATIME;
		else if ( !strcmp("dev", tok) )
			mountflags &= ~MS_NODEV;
		else if ( !strcmp("nodev", tok) )
			mountflags |= MS_NODEV;
		else if ( !strcmp("diratime", tok) )
			mountflags &= ~MS_NODIRATIME;
		else if ( !strcmp("nodiratime", tok) )
			mountflags |= MS_NODIRATIME;
		else if ( !strcmp("exec", tok) )
			mountflags &= ~MS_NOEXEC;
		else if ( !strcmp("noexec", tok) )
			mountflags |= MS_NOEXEC;
		else if ( !strcmp("mand", tok) )
			mountflags |= MS_MANDLOCK;
		else if ( !strcmp("nomand", tok) )
			mountflags &= ~MS_MANDLOCK;
		else if ( !strcmp("suid", tok) )
			mountflags &= ~MS_NOSUID;
		else if ( !strcmp("nosuid", tok) )
			mountflags |= MS_NOSUID;
		else if ( !strcmp("silent", tok) )
			mountflags |= MS_SILENT;
		else if ( !strcmp("loud", tok) )
			mountflags &= ~MS_SILENT;
		else if ( !strcmp("remount", tok) )
			mountflags |= MS_REMOUNT;
		else if ( !strcmp("ro", tok) )
			mountflags |= MS_RDONLY;
		else if ( !strcmp("rw", tok) )
			mountflags &= ~MS_RDONLY;
		else if ( !strcmp("sync", tok) )
			mountflags |= MS_SYNCHRONOUS;
		else if ( !strncasecmp("x-", tok, 2) )
			;
		else {
			if(*result_options)
				strcat(result_options,",");
			strcat(result_options,tok);
		}

skip:		
		tok = strtok(NULL, ",");
	}

	warnx("result_options = %s", result_options);
	return mountflags;
}

static struct mntent *search_for(char *name)
{
	struct stat sb;

	if ( strncmp("UUID=", name, 5) && strncmp("LABEL=", name, 6) )
		if ( lstat(name, &sb) )
			err(EXIT_FAILURE, "%s", name);

	FILE *fstab;

	if ( (fstab = setmntent("/etc/fstab", "r")) == NULL )
		errx(EXIT_FAILURE, "unable to open /etc/fstab");

	struct mntent *ent = NULL, *found = NULL;

	while ( (ent = getmntent(fstab)) != NULL )
	{
		warnx("%s %s", ent->mnt_fsname, ent->mnt_dir);
		if ( !strcmp(name, ent->mnt_fsname) ||
				!strcmp(name, ent->mnt_dir) ) {
			found = ent;
			break;
		}
	}


	if ( !found )
		errx(EXIT_FAILURE, "unable to find %s in /etc/fstab", name);

	warnx("fs=%s, dir=%s, type=%s, opts=%s, freq=%d, passno=%d",
			found->mnt_fsname,
			found->mnt_dir,
			found->mnt_type,
			found->mnt_opts,
			found->mnt_freq,
			found->mnt_passno);

	endmntent(fstab);
	return found;
}

static int do_auto_mount(const char *src, const char *target,
		unsigned long mountflags, const void *data)
{
	FILE *procfs;

	if ( (procfs = fopen("/proc/filesystems", "r")) == NULL )
		err(EXIT_FAILURE, "unable to open /proc/filesystems");

	for(;;)
	{
		char *lineptr = NULL;
		size_t n = 0;
		ssize_t rc;
		char *one, *two;

		if ( (rc = getline(&lineptr, &n, procfs)) == -1 )
			break;

		rc = sscanf(lineptr, " %ms %ms ", &one, &two);

		if ( rc == 0 )
			continue;
		else if ( rc == 1 )
			rc = do_mount(src, target, one, mountflags, data, true);

		if ( one ) { free(one); one = NULL; }
		if ( two ) { free(two); two = NULL; }
	}

	fclose(procfs);

	errno = ENODEV;

	return -1;
}

static int do_mount(const char *src, const char *target, const char *fstype,
		unsigned long mountflags, const void *data, bool silent)
{
	int rc;

	if ( fstype == NULL || !strcmp("auto", fstype) )
		return do_auto_mount(src, target, mountflags, data);

	if ( (rc = mount(src, target, fstype, mountflags, data)) == -1 ) {
		if (!silent)
			err(EXIT_FAILURE, "mount failed");
		return -1;
	}

	return 0;
}

/* global function defintions */

int main(int argc, char *argv[])
{
	unsigned long mountflags = 0;
	char *opt_options = NULL;
	char *opt_types = NULL;

	if ( !strcmp("umount", argv[0]) )
		opt_umount = 1;

	char opt;
	while ((opt = getopt(argc, argv, opt_umount ? ":afhnrt:vV" : ":ahno:rt:vVw")) != -1)
	{
		switch (opt)
		{
			case 'a':
				opt_all = 1;
				break;
			case 'f':
				opt_force = 1;
				break;
			case 'v':
				opt_verbose = 1;
				break;
			case 'V':
				show_version();
				break;
			case 't':
				opt_types = strdup(optarg);
				break;
			case 'o':
				opt_options = strdup(optarg);
				break;
			case 'n':
				opt_nomtab = 1;
				break;
			case 'r':
				opt_rdonly = 1;
				mountflags |= MS_RDONLY;
				break;
			case 'w':
				opt_rdonly = 0;
				mountflags &= ~MS_RDONLY;
				break;
			case ':':
				errx(EXIT_FAILURE, "missing arg for -%c", optopt);
			case '?':
			default:
				errx(EXIT_FAILURE, "unknown option '%c'", optopt);
			case 'h':
				show_usage();
		}
	}

	if ( optind == argc )
		show_mounts();

	if ( (optind >= argc) || (argc - optind < 1) || (argc - optind > 2) )
		errx(EXIT_FAILURE, "target (or source and target) args required");

	if (opt_options) {
		char *resopt;
		if ( (resopt = calloc(1, strlen(opt_options))) == NULL )
			err(EXIT_FAILURE, "calloc");
		mountflags = parse_options(opt_options, mountflags, resopt);
		free(opt_options);
		opt_options = resopt;
	}

	char *source = NULL;
	char *target = NULL;
	char *type = NULL;

	if ( (argc - optind) == 2 ) {
		source = argv[optind++];
		target = argv[optind];
		type = opt_types;
	} else {
		struct mntent *ent;
		ent = search_for(argv[optind]);
		source = ent->mnt_fsname;
		target = ent->mnt_dir;
		// TODO ent->mnt_opts
		type = ent->mnt_type;
	}

	if (type == NULL)
		type = "auto";

	if ( do_mount(source,target,type,mountflags,opt_options,false) == -1 )
		err(EXIT_FAILURE, "mount failed");

	if (opt_options)
		free(opt_options);

	if (opt_types)
		free(opt_types);
}
