#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <ctype.h>

static void show_usage(const char *str)
{
	if (str)
		fprintf(stderr, "%s\n", str);
	fprintf(stderr,
			"Usage: tail [-f] [-c number|-n number] [file]\n");
	exit(EXIT_FAILURE);
}

static int opt_follow = 0;
static int opt_bytes = 0;
static int opt_lines = 0;
static int num_lines = -1;

int main(int argc, char *argv[])
{
	{
		int opt;
		char *cnt_str = NULL;

		while ((opt = getopt(argc, argv, "fc:n:")) != -1)
		{
			switch (opt)
			{
				case 'f':
					opt_follow = 1;
					break;
				case 'c':
					if (opt_bytes) show_usage("only one -n");
					opt_bytes = 1;
					cnt_str = strdup(optarg);
					break;
				case 'n':
					if (opt_lines) show_usage("only one -n");
					opt_lines = 1;
					cnt_str = strdup(optarg);
					break;
				default:
					show_usage(NULL);
			}
		}

		if (opt_lines + opt_bytes > 1)
			show_usage("-c or -n only");

		if (opt_lines + opt_bytes == 0) {
			num_lines = 1;
		}

		if (cnt_str) {
			char *err;
			int flip = 0;
			if (*cnt_str == '+') 
				cnt_str++;
			else if (isdigit(*cnt_str))
				flip = 1;

			num_lines = strtol(cnt_str, &err, 10);
			if (!(*cnt_str != '\0' && *err == '\0'))
				errx(EXIT_FAILURE, "%s: not a valid number", cnt_str);
			
			if (flip)
				num_lines = -num_lines;
		} else
			num_lines = -10;
	}

	printf("num_lines = %d\n", num_lines);
}

