#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <strings.h>

static void show_usage()
{
	fprintf(stderr,
			"Usage: grep [-E|-F] [-c|-l|-q] [-insvx] -e pattern_list [-e pattern_file]... \n"
			"              [-f pattern_file]... [file...]\n"
			"       grep [-E|-F] [-c|-l|-q] [-insvx] [-e pattern_list]...\n"
			"              -f pattern_file... [-f pattern_file]... [file...]\n"
			"       grep [-E|-F] [-c|-l|-q] [-insvx] pattern_list [file...]\n"
		   );
	exit(EXIT_FAILURE);
}

static int opt_ere = 0;
static int opt_strings = 0;
static int opt_write_count = 0;
static int opt_case_insensitive = 0;
static int opt_write_filenames = 0;
static int opt_write_lineno = 0;
static int opt_quiet = 0;
static int opt_supress_enoent = 0;
static int opt_not_matching = 0;
static int opt_match_entire_line = 0;

static int total_files = 1;

static int do_grep(char **patterns, char *file)
{
	FILE *fp;
	int rc = 0;
	int lineno = 0;
	int fn_written = 0;
	int total_match = 0;

	if (file == NULL) {
		fp = stdin;
	} else if ((fp = fopen(file, "r")) == NULL) {
		if (!opt_supress_enoent)
			warn("%s", file);
		return EXIT_FAILURE;
	}

	while (1)
	{
		if (feof(fp)) break;
		if (ferror(fp)) { rc = EXIT_FAILURE; break; }

		char buf[BUFSIZ];
		char *line;

		if ((line = fgets(buf, BUFSIZ, fp)) == NULL)
			continue;

		lineno++;
		int match = 0;
		char *pattern;

		for (int i = 0; patterns[i]; i++)
		{
			pattern = patterns[i];
			if (opt_strings) {
				if (opt_match_entire_line && !opt_case_insensitive) match = !strcasecmp(line, pattern);
				else if (opt_match_entire_line && opt_case_insensitive) match = !strcmp(line, pattern);
				else if (!opt_match_entire_line && !opt_case_insensitive) match = (strstr(line, pattern) != NULL);
				else if (!opt_match_entire_line && opt_case_insensitive) errx(EXIT_FAILURE, "no strcasestr");
			} else {
				errx(EXIT_FAILURE, "no RE or ERE");
			}
		}

		if ( (!match && opt_not_matching) || (match && !opt_not_matching) ) {
			total_match++;
			if (!opt_quiet) {
				if (opt_write_filenames && !fn_written) {
					fn_written = 1;
					puts(file);
				} else if (opt_write_count) {
				} else {
					if (opt_write_lineno) printf("%d:", lineno);
					puts(line);
				}
			}
		}
	}

	if (!opt_quiet) {
		if (!opt_write_filenames && opt_write_count) {
			if (total_files > 1)
				printf("%s:", file);
			printf("%d\n", total_match);
		}
	}

	return rc;
}

inline static char **add_to_list(char **list, int *cnt, char *string)
{
	char **ret;

	if ((ret = realloc(list, sizeof(char *) * (*cnt + 1))) == NULL)
		err(EXIT_FAILURE, NULL);
	
	if (string) {
		if ((ret[*cnt++] = strdup(string)) == NULL) {
			err(EXIT_FAILURE, NULL);
		}
	} else
		ret[*cnt] = NULL;

	return ret;
}

int main(int argc, char *argv[])
{
	char **pattern_list = NULL;
	char **pattern_file = NULL;
	int num_lists = 0;
	int num_files = 0;

	/* get options */
	{
		int opt;

		while ((opt = getopt(argc, argv, "EFclqinsvxe:f:")) != -1)
		{
			switch (opt)
			{
				case 'E':
					opt_ere = 1;
					break;
				case 'F':
					opt_strings = 1;
					break;
				case 'c':
					opt_write_count = 1;
					break;
				case 'e':
					pattern_list = add_to_list(pattern_list, &num_lists, optarg);
					// TODO add_to_list(&pattern_list, &num_lists, optarg);
					if ((pattern_list = realloc(pattern_list, sizeof(char *) * (num_lists + 1))) == NULL)
						err(EXIT_FAILURE, NULL);
					if ((pattern_list[num_lists++] = strdup(optarg)) == NULL)
						err(EXIT_FAILURE, NULL);
					break;
				case 'f':
					if ((pattern_file = realloc(pattern_file, sizeof(char *) * (num_files + 1))) == NULL)
						err(EXIT_FAILURE, NULL);
					if ((pattern_file[num_files++] = strdup(optarg)) == NULL)
						err(EXIT_FAILURE, NULL);
					break;
					break;
				case 'i':
					opt_case_insensitive = 1;
					break;
				case 'l':
					opt_write_filenames = 1;
					break;
				case 'n':
					opt_write_lineno = 1;
					break;
				case 'q':
					opt_quiet = 1;
					break;
				case 's':
					opt_supress_enoent = 1;
					break;
				case 'v':
					opt_not_matching = 1;
					break;
				case 'x':
					opt_match_entire_line = 1;
					break;
				default:
					show_usage();
			}
		}
	}

	if ((pattern_list = realloc(pattern_list, sizeof(char *) * (num_lists + 1))) == NULL)
		err(EXIT_FAILURE, NULL);
	pattern_list[num_lists] = NULL;

	if ((pattern_file = realloc(pattern_file, sizeof(char *) * (num_files + 1))) == NULL)
		err(EXIT_FAILURE, NULL);
	pattern_file[num_files] = NULL;

	if (opt_ere + opt_strings > 1)
		show_usage();

	if (num_files + num_lists == 0) {
		if (optind >= argc)
			show_usage();
		else {
			if ((pattern_list = realloc(pattern_list, sizeof(char *) * (num_lists + 1))) == NULL)
				err(EXIT_FAILURE, NULL);
			if ((pattern_list[num_lists++] = strdup(argv[optind++])) == NULL)
				err(EXIT_FAILURE, NULL);
		}
	}

	// TODO: read all pattern_file into pattern_list

	int rc = 0;

	if (argc - optind > 0) {
		total_files = (argc - optind);
		for (int i = optind; i < argc; i++) {
			rc += do_grep(pattern_list, argv[i]);
		}
	} else {
		rc += do_grep(pattern_list, NULL);
	}

	exit(rc);
}
