#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <err.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <libgen.h>

static void show_usage()
{
	fprintf(stderr, "Usage: ls [-CFRacdilqrtu1] [file...]\n");
	exit(EXIT_FAILURE);
}

static int opt_multi_text_col = 0;
static int opt_append_slash = 0;
static int opt_append_slash_dirs = 0;
static int opt_recurse = 0;
static int opt_sort_status_lastmod = 0;
static int opt_dirs_are_files = 0;
static int opt_show_file_serial = 0;
static int opt_show_long = 0;
static int opt_show_one = 0;
static int opt_hide_non_print = 0;
static int opt_reverse = 0;
static int opt_sort_lastmod = 0;
static int opt_sort_lastacc = 0;
static int opt_show_all = 0;
static int opt_multi_header = 0;
static int opt_supress_names = 0;

static char *file_mode(mode_t mode)
{
	char *ret = calloc(1, 16);

	char c;

	if( S_ISREG(mode) ) c = '-';
	else if( S_ISBLK(mode) ) c = 'b';
	else if( S_ISDIR(mode) ) c = 'd';
	else if( S_ISCHR(mode) ) c = 'c';
	else if( S_ISFIFO(mode) ) c = 'p';
	else c = '?';

	snprintf(ret, 16, "%c%c%c%c%c%c%c%c%c%c%c", 
			c,

			(mode & S_IRUSR) ? 'r' : '-',
			(mode & S_IWUSR) ? 'w' : '-',
			(mode & S_IXUSR) ? (mode & S_ISUID ? 's': 'x') : (mode & S_ISUID ? 'S' : '-'),

			(mode & S_IRGRP) ? 'r' : '-',
			(mode & S_IWGRP) ? 'w' : '-',
			(mode & S_IXGRP) ? (mode & S_ISGID ? 's': 'x') : (mode & S_ISGID ? 'S' : '-'),

			(mode & S_IROTH) ? 'r' : '-',
			(mode & S_IWOTH) ? 'w' : '-',
			(mode & S_IXOTH) ? 'x' : '-',

			(mode & S_ISVTX) ? 't' : '-'
			);
	return ret;
}

#define TIME_RECENT "%b %e %H:%M"
#define TIME_OLD	"%b %e  %Y"
#define SIX_MONTHS (60*60*24*(365/2))

static int print_single_entry(char *name, struct stat *sb, struct stat *lsb)
{
	if( opt_show_file_serial )
		printf("%lu ", sb->st_ino);

	if( opt_show_long ) {
		char ubuf[32], gbuf[32];
		struct passwd *uid_nam = getpwuid(sb->st_uid);
		struct group *gid_nam = getgrgid(sb->st_gid);
		snprintf(ubuf, 32, "%u", sb->st_uid);
		snprintf(gbuf, 32, "%u", sb->st_gid);

		char tbuf[100];

		time_t point = sb->st_mtime;
		time_t age = time(NULL) - point;

		strftime(tbuf, 100, age > SIX_MONTHS ? TIME_OLD : TIME_RECENT, localtime(&point));

		char *fm = file_mode(sb->st_mode);

		printf("%s %3lu %8s %8s %8lu %s %s",
				fm,
				sb->st_nlink,
				uid_nam ? uid_nam->pw_name : ubuf,
				gid_nam ? gid_nam->gr_name : gbuf,
				sb->st_size,
				tbuf,
				basename(name)
			  );


		char append = 0;
		if( opt_append_slash ) {
			if( (lsb->st_mode & S_IFLNK) == S_IFLNK ) append = '@';
			else if( S_ISDIR(sb->st_mode) ) append = '/';
			else if( sb->st_mode & (S_IXUSR|S_IXGRP|S_IXOTH) ) append = '*';
			else if( S_ISFIFO(sb->st_mode) ) append = '|';
		} else if( opt_append_slash_dirs ) {
			if( S_ISDIR(sb->st_mode) ) append = '/';
		}
		if( append )
			printf("%c", append);

		free(fm);

	} else {
		printf("%s", basename(name));
		// TODO opt_append_slash[_dirs]
	}
	
	if( opt_show_one || opt_show_long )
		printf("\n");
	else
		printf(" ");

	return 0;
}

static int do_one_path(const char *tpath)
{
	int failure = 0;
	char *path = strdup(tpath);
	int pathlen = strlen(path);

	if( path[pathlen-1] == '/' ) {
		path[pathlen-1] = '\0';
		pathlen--;
	}

	DIR *dir = opendir(path);
	if( dir == NULL ) {
		errx(EXIT_FAILURE, "cannot access %s: %s", path, strerror(errno));
		free(path);
		return errno;
	}

	if( opt_multi_header )
		printf("\n%s:\n", path);
	printf("total %u\n", 0);

	struct dirent *ent = NULL;
	do {
		errno = 0;
		ent = readdir(dir);
		
		if(ent == NULL && errno == 0) {
			continue;
		} else if(ent == NULL) {
			warn("cannot read entry");
			failure = 1;
			continue;
		} else if(ent->d_name[0] == '\0') {
			continue;
		} else {
			if (!opt_show_all && ent->d_name[0] == '.')
				continue;
			struct stat buf, lbuf;
			int namelen = pathlen + 1 + strlen(ent->d_name) + 1;
			char *name = calloc(1, namelen);
			if (name == NULL) {
				warn("%s", ent->d_name);
				failure = 1;
			} else {
				snprintf(name, namelen, "%s/%s", path, ent->d_name);
				if (stat(name, &buf) == -1) {
					warn("%s", name);
					failure = 1;
				} else if (lstat(name, &lbuf) == -1) {
					warn("%s", name);
					failure = 1;
				} else {
					if( print_single_entry(name, &buf, &lbuf) )
						failure = 1;
					if (opt_recurse && S_ISDIR(buf.st_mode)) {
						do_one_path(name);
					}
				}
			}
			if( name != NULL ) 
				free(name);
		}

		fflush(stdout);
	} while(ent != NULL);

	if(dir != NULL) 
		closedir(dir);
	if(path != NULL) 
		free(path);

	if( !opt_show_one && !opt_show_long )
		printf("\n");
	return failure;
}


int main(int argc, char *argv[])
{
	if( !isatty(STDOUT_FILENO) )
		opt_show_one = 1;

	int opt;
	while( (opt = getopt(argc, argv, "CFRacdinplqrtu1")) != -1 )
	{
		switch( opt ) {
			case 'C':
				opt_multi_text_col = 1;
				break;
			case 'p':
				opt_append_slash_dirs = 1;
				break;
			case 'F':
				opt_append_slash = 1;
				break;
			case 'R':
				opt_recurse = 1;
				break;
			case 'a':
				opt_show_all = 1;
				break;
			case 'c':
				opt_sort_status_lastmod = 1;
				break;
			case 'd':
				opt_dirs_are_files = 1;
				break;
			case 'i':
				opt_show_file_serial = 1;
				break;
			case 'n':
				opt_supress_names = 1;
				// fall through
			case 'l':
				opt_show_long = 1;
				// fall through
			case '1':
				opt_show_one = 1;
				break;
			case 'q':
				opt_hide_non_print = 1;
				break;
			case 'r':
				opt_reverse = 1;
				break;
			case 't':
				opt_sort_lastmod = 1;
				break;
			case 'u':
				opt_sort_lastacc = 1;
				break;
			default:
				warnx("Unknown option '%c'\n", opt);
				show_usage();
		}
	}

	if( (opt_multi_text_col + opt_show_one > 1)
			|| (opt_multi_text_col + opt_show_long > 1) 
			|| (opt_sort_status_lastmod + opt_sort_lastacc > 1) ) {
		warnx("Conflicting options\n");
		show_usage();
	}

	if( optind >= argc )
		exit(do_one_path("."));

	if( (argc - optind > 1) || opt_recurse )
		opt_multi_header = 1;

	int failure = EXIT_SUCCESS;
	for( int i = optind; i<argc; i++ ) {
		if( do_one_path(argv[i]) )
			failure = EXIT_FAILURE;
	}
	
	exit(failure);
}


