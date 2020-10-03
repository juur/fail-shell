#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <err.h>
#include <string.h>
#include <sys/mount.h>
#include <ctype.h>

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


static void show_usage()
{
	fprintf(stderr, "Usage: mount [-avVh] [-o option] [source] target\n");
	exit(EXIT_FAILURE);
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

static unsigned long parse_options(char *options, unsigned long mountflags)
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
		else
			warnx("unknown option '%s'", tok);

skip:		
		tok = strtok(NULL, ",");
	}

	return mountflags;
}

int main(int argc, char *argv[])
{
	unsigned long mountflags = 0;
	char *opt_options = NULL;

	char opt;
	while ((opt = getopt(argc, argv, "ahno:rvVw")) != -1)
	{
		switch (opt)
		{
			case 'a':
				opt_all = 1;
				break;
			case 'v':
				opt_verbose = 1;
				break;
			case 'V':
				show_version();
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
			case 'h':
			default:
				show_usage();
		}
	}

	if ( optind == argc )
		show_mounts();

	if ( (optind >= argc) || (argc - optind < 1) || (argc - optind > 2) ) {
		warnx("target or source and target required [ac:%d opt:%d]", argc, optind);
		show_usage();
	}

	if (opt_options)
		mountflags = parse_options(opt_options, mountflags);
}
