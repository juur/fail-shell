#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <ctype.h>

static void show_usage()
{
	fprintf(stderr,
			"Usage: echo [-n] [string...]\n");
	exit(EXIT_FAILURE);
}

static int print_process(char *arg)
{
	for (char *c = arg; *c; c++)
	{
		if (*c == '\\') 
		{
			switch (*(c+1))
			{
				case 'a': fputc('\a', stdout); c++; break;
				case 'b': fputc('\b', stdout); c++; break;
				case 'c': 
						  return 1;
						  break;
				case 'f': fputc('\f', stdout); c++; break;
				case 'n': fputc('\n', stdout); c++; break;
				case 'r': fputc('\r', stdout); c++; break;
				case 't': fputc('\t', stdout); c++; break;
				case 'v': fputc('\v', stdout); c++; break;
				case '0': 
						  {
							  c++;
							  int cnt = 0;
							  while (*(c+1) && isdigit(*(c+1)))
							  {
								  cnt *= 8;
								  cnt += *c - 0x30;
								  if (cnt > 0777)
									  errx(EXIT_FAILURE, "invalid octal number");
								  c++;
							  }
							  continue;
						  }
						  break;
				default:
						  fputc(*c, stdout);
						  break;
			}
		} else
			fputc(*c, stdout);
	}

	return 0;
}

static int opt_process = 0;

int main(int argc, char *argv[])
{
	{
		int opt;

		while ((opt = getopt(argc, argv, "n")) != -1)
		{
			switch (opt)
			{
				case 'n':
					opt_process = 1;
					break;
				default:
					show_usage();
			}
		}
	}

	for (int i = optind; i < argc; i++)
	{
		if (opt_process && print_process(argv[i]))
			break;
		else if (!opt_process)
			printf("%s", argv[i]);

		if (i + 1 < argc)
			fputc(' ', stdout);
		else
			fputc('\n', stdout);
	}


}
