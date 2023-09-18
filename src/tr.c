#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

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

#define ALNUM (1<<0)
#define ALPHA (1<<1)
#define BLANK (1<<2)
#define CNTRL (1<<3)
#define DIGIT (1<<4)
#define GRAPH (1<<5)
#define LOWER (1<<6)
#define PRINT (1<<7)
#define PUNCT (1<<8)
#define SPACE (1<<9)
#define UPPER (1<<10)
#define XDIGIT (1<<11)

struct class_map {
	const char *name;
	int bit;
	int (*test)(int c);
	int (*conv)(int c);
};

static struct class_map class_mapping[] = {
	{"alnum",  ALNUM,  isalnum,  NULL},
	{"alpha",  ALPHA,  isalpha,  NULL},
	{"blank",  BLANK,  isblank,  NULL},
	{"cntrl",  CNTRL,  iscntrl,  NULL},
	{"digit",  DIGIT,  isdigit,  NULL},
	{"graph",  GRAPH,  isgraph,  NULL},
	{"lower",  LOWER,  islower,  toupper},
	{"print",  PRINT,  isprint,  NULL},
	{"punct",  PUNCT,  ispunct,  NULL},
	{"space",  SPACE,  isspace,  NULL},
	{"upper",  UPPER,  isupper,  tolower},
	{"xdigit", XDIGIT, isxdigit, NULL},
	{NULL,     0,      NULL,     NULL}
};

static int opt_complement_values = 0;
static int opt_complement_chars = 0;
static int opt_delete = 0;
static int opt_replace = 0;
static int string_class[2] = {0,0};

static const char *parse_list(char string[], int strnum)
{
	if (strlen(string) >= BUFSIZ-2)
		errx(EXIT_FAILURE, "string too long");

	char *ret = calloc(1, BUFSIZ);
	char *ptr = string;
	char *end = ptr + strlen(string);
	char *retptr = ret;

	if (ret == NULL)
		return NULL;

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
				*(retptr++) = start;
				continue;
			}

			start+=-1 * inc;
			do
			{
				start += inc;
				*(retptr++) = start;
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
			fprintf(stderr, "Not implemented: \\%s", numstr);

			ptr += cnt;
		}
		else if (*ptr == '\\')
		{
			char sub = 0;
			char chr = *(ptr+1);
			switch (chr)
			{
				case '\\': sub = '\\'; break;
				case 'a': sub = '\a'; break;
				case 'b': sub = '\b'; break;
				case 'f': sub = '\f'; break;
				case 'n': sub = '\n'; break;
				case 'r': sub = '\r'; break;
				case 't': sub = '\t'; break;
				case 'v': sub = '\v'; break;
				default:
					errx(EXIT_FAILURE, "invalid \\%c", chr);
			}
			*(retptr++) = sub;
			ptr+=2;
		}
		else if (*ptr == '[' && *(ptr+1) == ':')
		{
			char *start = ptr+2;
			char *end = strstr(start, ":]");
			if (!end)
				errx(EXIT_FAILURE, "[::] with unmatched :]");

			char *rng = strndup(start, end-start);

			int found = -1;
			for (int i = 0; class_mapping[i].name; i++)
			{
				if(!strcmp(class_mapping[i].name, rng)) {
					found = i;
					break;
				}
			}
			if (found == -1)
				errx(EXIT_FAILURE, "unknown character class '%s'", rng);

			string_class[strnum] |= class_mapping[found].bit;

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
			fprintf(stderr, "Unsupported: [%s]", rng);
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
				*(retptr++) = x;

			ptr++;
		}
		else
		{
			*(retptr++) = *(ptr++);
		}
	}

	return ret;
}

inline static int min(const int a, const int b)
{
	return a < b ? a : b;
}

__attribute__((nonnull))
static void perform_tr(const char *string1, const int str1cls, const char *string2, const int str2cls)
{
	//const int str2len = string2 ? strlen(string2)-1 : 0;
	const int str2len = strlen(string2)-1;

	while (1)
	{
		if (feof(stdin) || feof(stdout))
			exit(EXIT_SUCCESS);
		if (ferror(stdin) || ferror(stdout))
			exit(EXIT_FAILURE);

		char buf[BUFSIZ];
		char *line = fgets(buf, BUFSIZ, stdin);

		if (!line) 
			continue;
		if (strlen(line) == BUFSIZ)
			errx(EXIT_FAILURE, "buffer overflow");

		/* remove \n */
		line[strlen(line)-1] = '\0';

		char c;
		int op = 0;

		/* loop over every character in the line */
		while ((c = *line++) != '\0')
		{
			int mod = 0;

			/* first we check for non-character class based actions */
			for (int i = 0; string1[i]; i++)
			{
				/* NOT: a, a-a, [a*n] */
				if (c != string1[i]) 
					continue;

				mod = 1;
				op++;

				/* -d */
				if (opt_delete && !opt_replace) {
					continue;
				} 
				/* -s */
				else if (!opt_delete && opt_replace) 
				{
					while (*line && *line == c) line++;
					goto redo;
				}
				/* -sd */
				else if (opt_delete && opt_replace)
				{
					// TODO the 'c != string1[i]' only covers the first arg
					// not the second
					// should we create a second loop outside of this?
					errx(EXIT_FAILURE, "-s -d a not supported");
				}
				/* */
				else
				{
redo:
					putchar(string2[min(i, str2len)]);
				}
			}

			/* if nothing has happened, we then process character classes */
			if (!mod) {
				/* [::] */
				if (str1cls && !str2cls) 
				{
					int skip = 0;
					for (int j = 0; class_mapping[j].name; j++)
					{
						if (!(str1cls & class_mapping[j].bit)) continue;
						if (!class_mapping[j].test(c)) continue;

						mod = 1;
						op++;

						/* -d */
						if (opt_delete) {
							skip = 1;
							break;
						} 
						/* -s */
						else if (opt_replace) 
						{
							putchar(c);
							while (*line && *line == c) line++;
						}
						/* */
						else
						{
							putchar(string2[min(str2len, 0)]); // FIXME record posistion of [::] in argv instead of 1
						}
					}
					if (skip)
						continue;
				} 
				/* [::] [::] */
				else if (str1cls && str2cls && (opt_delete + opt_replace == 0)) 
				{
					for (int j = 0; class_mapping[j].name; j++)
					{
						if (!(str1cls & class_mapping[j].bit)) continue;
						if (!class_mapping[j].test(c)) continue;
						/* */
						mod = 1;
						op++;
						putchar(class_mapping[j].conv(c));
					}
				}
				/* -sd [::] [::] */
				else if (str1cls && str2cls && (opt_delete + opt_replace == 2)) 
				{
					int skip = 0;
					for (int j = 0; class_mapping[j].name; j++)
					{
						/* -d */
						if ((str1cls & class_mapping[j].bit) && class_mapping[j].test(c)) {
							mod = 1;
							skip = 1;
							break;
						}
						/* -s */
						if ((str2cls & class_mapping[j].bit) && class_mapping[j].test(c)) {
							mod = 1;
							op++;
							putchar(c);
							while (*line && *line++ != c) ;
						}
					}
					if (skip)
						continue;
				}
			} /* if (!mod) */

			/* finally, if nothing has happened, dump the original character */
			if (!mod) {
				putchar(c);
				op++;
			}
		}

		/* if we output anything, put a trailing newline */
		if (op)
			putchar('\n');

	}
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


	const char *string1 = parse_list(argv[optind++], 0);
	const char *string2;

	if (optind < argc) {
		string2 = parse_list(argv[optind], 1);
	} else
		string2 = NULL;


	if (string2 && string_class[1] & ~(UPPER|LOWER))
		show_usage("invalid characater class(es) in string2");

	if (string2 && (
		((string_class[1] & UPPER) && !(string_class[0] & LOWER)) ||
		((string_class[1] & LOWER) && !(string_class[0] & UPPER))
		))
		show_usage("character class in string2 missing from string 1");

	perform_tr(string1, string_class[0], string2, string_class[1]);
}
