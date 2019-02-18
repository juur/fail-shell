#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>

static void show_usage(const char *message)
{
	if (message)
		warnx("%s", message);

	fprintf(stderr,
			"tr [-c|-C] [-s] string1 string2\n"
			"tr -s [-c|-C] string1\n"
			"tr -d [-c|-C] string1\n"
			"tr -ds [-c|-C] string1 string2\n");
	exit(EXIT_FAILURE);
}

static int opt_complement_values = 0;
static int opt_complement_chars = 0;
static int opt_delete = 0;
static int opt_replace = 0;

static const char *parse_list(char string[])
{
	if (strlen(string) >= BUFSIZ-2)
		errx(EXIT_FAILURE, "string too long");

	char *ret = calloc(1, BUFSIZ);
	char *ptr = string;
	char *end = ptr + strlen(string);

	while (*ptr)
	{
		int left = end - ptr - 1;

		if (left > 1 && *(ptr+1) == '-') 
		{
			char start = *ptr;
			if (left < 2)
				errx(EXIT_FAILURE, "incomplete ?-? range");
			char end = *(ptr+2);
			char inc = start < end ? 1 : -1;
			ptr += 3;

			if (start == end) {
				putchar(start);
				continue;
			}

			start+=-1 * inc;
			do
			{
				start += inc;
				putchar(start);
			} 
			while (start != end);

		} 
		else if (*ptr == '\\' && isdigit(*(ptr+1))) 
		{
			int cnt = 1;

			while( *(ptr+cnt+1) && isdigit(*(ptr+cnt+1)) && cnt<=4 )
				cnt++;

			if (cnt == 4)
				errx(EXIT_FAILURE, 
						"octal values can onlybe 1,2 or 3 digits long");
			
			char *numstr = strndup(ptr+1, cnt);
			printf("\\%s", numstr);

			ptr += cnt;
		}
		else if (*ptr == '\\')
		{
			char sub = 0;
			char chr = *(ptr+1);
			switch (chr)
			{
				case '\\': sub = '\\'; break;
				case '\a': sub = '\a'; break;
				case '\b': sub = '\b'; break;
				case '\f': sub = '\f'; break;
				case '\n': sub = '\n'; break;
				case '\r': sub = '\r'; break;
				case '\t': sub = '\t'; break;
				case '\v': sub = '\v'; break;
				default:
					errx(EXIT_FAILURE, "invalid \\%c", chr);
			}
			printf("\\%c", chr);
			ptr+=2;
		}
		else if (*ptr == '[' && *(ptr+1) == ':')
		{
			char *start = ptr+2;
			char *end = strstr(start, ":]");
			if (!end)
				errx(EXIT_FAILURE, "[::] with unmatched :]");

			char *rng = strndup(start, end-start);
			printf("[%s]", rng);
			free(rng);

			ptr = end + 2;
		}
		else if (*ptr == '[' && *(ptr+1) == '=')
		{
			char *start = ptr+2;
			char *end = strstr(start, "=]");
			if (!end)
				errx(EXIT_FAILURE, "[==] with unmatched =]");

			char *rng = strndup(start, end-start);
			printf("[%s]", rng);
			free(rng);

			ptr = end + 2;
		}
		else if (*ptr == '[' && *(ptr+2) == '*')
		{
			char x = *(++ptr);
			ptr++;
			char *from = ++ptr;
			while (*ptr && *ptr != ']' && isdigit(*ptr))
				ptr++;

			if (*ptr != ']')
				errx(EXIT_FAILURE, "invalid digit (%c)/unmatched ]", *ptr);

			char *numstr = strndup(from, ptr-from);
			int num = atoi(numstr);
			if (num <= 0)
				errx(EXIT_FAILURE, "invalid number '%s'", numstr);
			free(numstr);

			while(num-- > 0)
				putchar(x);

			ptr++;
		}
		else
		{
			putchar(*ptr);
			ptr++;
		}
	}

	return ret;
}

int main(int argc, char *argv[])
{

	{
		int opt;

		while ((opt = getopt(argc, argv, "cCsd")) != -1)
		{
			switch (opt)
			{
				case 'c':
					opt_complement_values = 1;
					break;
				case 'C':
					opt_complement_chars = 1;
					break;
				case 'd':
					opt_delete = 1;
					break;
				case 's':
					opt_replace = 1;
					break;
			}
		}

		if (opt_complement_values + opt_complement_chars > 1)
			show_usage("only one of -c and -C can be specified");

		if (optind >= argc || argc - optind > 2)
			show_usage("missing string argument");

		if ((argc - optind == 1) && (opt_delete + opt_replace != 1))
			show_usage(NULL);
	}

	const char *string1 = parse_list(argv[optind++]);
	const char *string2;

	if (optind >= argc)
		string2 = parse_list(argv[optind]);
	else
		string2 = NULL;
}
