#define _XOPEN_SOURCE 700

#include <getopt.h>
#include <err.h>
#include <stdlib.h>
#include <sys/resource.h>

static int opt_mode_pgid = 0;
static int opt_mode_pid  = 0;
static int opt_mode_uid  = 0;
static int opt_increment = 0;

static void show_usage(void)
{
	errx(EXIT_FAILURE, "Usage: renice [-g|-p|-u] -n increment ID...");
}

int main(int argc, char *argv[])
{
	int which;

	{
		int opt;

		while ((opt = getopt(argc, argv, "hgpun:")) != -1)
		{
			switch(opt)
			{
				case 'g':
					opt_mode_pgid = 1;
					which = PRIO_PGRP;
					break;
				case 'p':
					opt_mode_pid = 1;
					which = PRIO_PROCESS;
					break;
				case 'u':
					opt_mode_uid = 1;
					which = PRIO_USER;
					break;
				case 'n':
					opt_increment = atoi(optarg);
					break;
				default:
					show_usage();
			}
		}

		if (opt_mode_uid + opt_mode_pid + opt_mode_pgid > 1)
			show_usage();

		if (opt_mode_uid + opt_mode_pid + opt_mode_pgid == 0)
			opt_mode_pid = 1;

		if (opt_increment == 0)
			show_usage();
	}

	if (optind == argc)
		show_usage();



	int val, cur;
	while (optind < argc)
	{
		val = atoi(argv[optind++]);

		if (val == 0) {
			warnx("invalid ID '%s'", argv[optind-1]);
		} else {
			if ((cur = getpriority(which, val)) == -1) {
				warn("getpriority: %s", argv[optind-1]);
			} else if (setpriority(which, val, opt_increment) == -1) {
				warn("setpriority: %s", argv[optind-1]);
			}
		}

	}
}
