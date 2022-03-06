#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <err.h>
#include <sys/statvfs.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

static bool opt_kbblk  = 0;
static bool opt_posix  = 0;
static bool opt_totals = 0;

__attribute__((noreturn))
static void show_usage(void)
{
	errx(EXIT_FAILURE, "Usage: df [-k] [-P|-t] [file...]");
}

static void do_one_disk(const char *path, bool alert)
{
	int minor;
	int major;
	struct stat sb;

	/* find out the major:minor of the path */
	if (stat(path, &sb) == -1) {
		if (alert)
			warn("stat %s", path);
		return;
	}

	minor = sb.st_dev & 0x00ff;
	major = (sb.st_dev & 0xff00) >> 8;

	struct statvfs buf;

	/* obtain the filesystem information */
	if (statvfs(path, &buf) == -1) {
		if (alert)
			warn("statvfs %s", path);
		return;
	}

	/* skip psuedo filesystems with no storage, e.g. sysfs */
	if (!buf.f_blocks)
		return;

	FILE *fh;
	
	if ((fh = fopen("/proc/self/mountinfo", "r")) == NULL) {
		warn("fopen /proc/self/mountinfo");
		return;
	}

	char *line = NULL;
	size_t len = 0;
	int f_min, f_maj;
	char *mnt_point = NULL, *mnt_dev = NULL;
	bool found = false;

	/* interate over mountinfo searching by device ID */
	while (getline(&line, &len, fh) != -1)
	{

		sscanf(line, " %*d %*d %d:%d %*s %ms %*s %*s %*s %*s %ms ",
				&f_maj, &f_min,
				&mnt_point,
				&mnt_dev);

		free(line); line = NULL;

		if (f_min == minor && f_maj == major) {
			found = true;
			break;
		}

		if (mnt_point) {
			free(mnt_point); mnt_point = NULL;
		}

		if (mnt_dev) {
			free(mnt_dev); mnt_dev = NULL;
		}
	}

	if (!found)
		return;


	/* display the findings */
	//if (opt_posix) {
	int bs = opt_kbblk ? 1024 : 512;
	size_t total_space = (buf.f_frsize * buf.f_blocks) / bs;
	size_t space_free  = (buf.f_frsize * buf.f_bfree) / bs;
	size_t space_used  = total_space - space_free;
	float pc = 0;

	if (total_space)
		pc = 100.0f * ((float)space_used / ((float)space_used + (float)space_free));

	printf("%-14s %11ld %11ld %11ld %7d%% %s\n",
			mnt_dev,
			total_space,
			space_used,
			space_free,
			(int)pc,
			mnt_point
		  );
	//}

	free(mnt_point);
	free(mnt_dev);
}

static void do_all_disks(void)
{
	FILE *fh;
	char *line = NULL;
	size_t len = 0;
	char *mnt_point;

	if ((fh = fopen("/proc/self/mountinfo", "r")) == NULL)
		err(EXIT_FAILURE, "fopen /proc/self/mountinfo");

	while (getline(&line, &len, fh) != -1)
	{
		sscanf(line, " %*d %*d %*d:%*d %*s %ms ", &mnt_point);
		free(line); line = NULL; len = 0;

		if (mnt_point) {
			if (*mnt_point) {
				do_one_disk(mnt_point, false);
			}

			free(mnt_point); mnt_point = NULL;
		}
	}
}

int main(int argc, char *argv[])
{
	{
		int opt;
		while ((opt = getopt(argc, argv, "kPt")) != -1)
		{
			switch (opt)
			{
				case 'k':
					opt_kbblk = 1;
					break;

				case 'P':
					if (opt_totals)
						show_usage();
					opt_posix = 1;
					break;

				case 't':
					if (opt_posix)
						show_usage();
					opt_totals = 1;
					break;

				default:
					show_usage();
			}
		}
	}

	printf("Filesystem     %4d-blocks        Used   Available Capacity Mounted on\n", 
			opt_kbblk ? 1024 : 512);
	if (optind == argc) {
		do_all_disks();
	} else {
		while (optind < argc)
		{
			do_one_disk(argv[optind++], true);
		}
	}
}
