#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <stdlib.h>
#include <err.h>
#include <stdio.h>

static void show_usage()
{
	fprintf(stderr,
			"Usage: head [-n number] [file...]\n");
	exit(EXIT_FAILURE);
}

static int opt_number_lines = 10;

static int head_file(const char *file)
{
	int rc = 0;
	FILE *fp;

	if (!file)
		fp = stdin;
	else if ((fp = fopen(file, "r")) == NULL) {
		warnx("%s", file);
		return 1;
	}

	char buf[BUFSIZ];
	int lines = 0;

	while (1)
	{
		if (feof(fp))
			break;

		if (ferror(fp)) {
			rc = 1;
			break;
		}
		if (lines == opt_number_lines)
			break;

		if (fgets(buf, BUFSIZ, fp)) {
			printf("%s", buf);
			lines++;
		}
	}

	if (file)
		fclose(fp);
	return rc;
}

int main(int argc, char *argv[])
{
	{
		int opt;
		char *err;
		while ((opt = getopt(argc, argv, "n:")) != -1)
		{
			switch (opt)
			{
				case 'n':
					opt_number_lines = strtol(optarg, &err, 10);
					if(!*optarg || *err != '\0')
						errx(EXIT_FAILURE, "%s: not a number", optarg);
					break;
				default:
					show_usage();
			}
		}

		if (opt_number_lines <= 0)
			show_usage();
	}

	int rc = 0;

	if (optind >= argc)
		exit(head_file(NULL));

	int num_files = argc - optind;

	for (int i = optind; i < argc; i++)
	{
		if (num_files > 1 && i == optind)
			printf("==> %s <==\n", argv[i]);
		else if (num_files > 1)
			printf("\n==> %s <==\n", argv[i]);

		rc += head_file(argv[i]);
	}

	exit(rc);
}
