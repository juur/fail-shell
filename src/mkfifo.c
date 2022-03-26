#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <err.h>
#include <stdlib.h>
#include <ctype.h>

static void show_usage(void)
{
	errx(EXIT_FAILURE, "Usage: mkfifo [-m mode] file...");
}

struct who {
	unsigned usr:1;
	unsigned grp:1;
	unsigned oth:1;
	unsigned umask:1;
};

struct perm {
	unsigned r:1;
	unsigned w:1;
	unsigned x:1;
	unsigned X:1;
	unsigned t:1;
};

struct clause {
	struct who who;
	int op;

	union {
		struct perm perm;
		int permcopy;
	} action;

	/* 0 = none, 1 = permlist, 2 = permcopy */
	int act_flag;
};

#define NONE		0
#define PERMLIST	1
#define PERMCOPY	2

static struct clause **parse_mode(char arg[])
{
	struct clause **ret;
	struct clause *current;
	int pos = 0;
	int entry = 0;
	char *str = arg;

	if ((ret = calloc(1, sizeof(struct clause *))) == NULL)
		err(EXIT_FAILURE, NULL);

	if ((current = calloc(1, sizeof(struct clause))) == NULL)
		err(EXIT_FAILURE, NULL);

	ret[entry] = current;

	while (*str)
	{
		if (*str == ',') 
		{
			str++;
			entry++;
			if ((ret = realloc(ret, sizeof(struct clause *) 
							* (entry + 1))) == NULL)
				err(EXIT_FAILURE, NULL);
			if ((current = calloc(1, sizeof(struct clause))) == NULL)
				err(EXIT_FAILURE, NULL);
			ret[entry] = current;
			pos = 0;
		} 
		else if (isalpha(*str)) 
		{
			if (pos == 1) 
				pos = 2;

			if (!pos)
			{
				switch (*str)
				{
					case 'u': current->who.usr = 1; break;
					case 'g': current->who.grp = 1; break;
					case 'o': current->who.oth = 1; break;
					case 'a': 
							  current->who.umask = 1;
							  current->who.usr = 1; 
							  current->who.grp = 1;
							  current->who.oth = 1;
							  break;
					default:
							  errx(EXIT_FAILURE, "Unknown who '%c'", *str);
			
				}
			}
			else 
			{
				if (*str == 'u' || *str == 'g' || *str == 'o') 
				{
					if (current->act_flag == PERMLIST)
						errx(EXIT_FAILURE, 
								"Can't mix permcopy with permlist '%c'", *str);
					current->act_flag = PERMCOPY;
				}
				else if (*str == 'r' || *str == 'w' || *str =='x' 
						|| *str == 'X' || *str == 't')
				{
					if (current->act_flag == PERMCOPY)
						errx(EXIT_FAILURE, 
								"Can't mix permlist with permcopy '%c'", *str);
					current->act_flag = PERMLIST;
				}
				
				switch (*str)
				{
					case 'r': current->action.perm.r = 1; break;
					case 'w': current->action.perm.w = 1; break;
					case 'x': current->action.perm.x = 1; break;
					case 'X': current->action.perm.X = 1; break;
					case 't': current->action.perm.t = 1; break; // not POSIX 

					case 'u': 
					case 'g': 
					case 'o': 
							  current->action.permcopy = *str; break;
					default:
							  errx(EXIT_FAILURE, 
									  "Unknown permlist/copy '%c'", *str);
				}
			}
			str++;
		} 
		else if (*str == '-' || *str == '+' || *str == '=') 
		{
			if (pos == 2)
			{
				errx(EXIT_FAILURE, 
						"multiple operators not supported at '%c'", *str);
			}

			pos = 1;
			switch (*str)
			{
				case '-': 
				case '+': 
				case '=': 
					current->op = *str;
					break;
			}
			str++;
		} 
		else
		{
			errx(EXIT_FAILURE, "invalid clause at '%c'", *str);
		}
	}

	if (pos == 0)
		errx(EXIT_FAILURE, "empty clause");

	entry++;
	if ((ret = realloc(ret, sizeof(struct clause *) * (entry + 1))) == NULL)
		err(EXIT_FAILURE, NULL);
	ret[entry] = NULL;

	return ret;
}


static int isnumber(const char str[])
{
    for(int i = 0; str[i]; i++)
        if(!isdigit(str[i]))
            return 0;
    return 1;
}

static int do_mkfifo(mode_t mode, struct clause *list[], const char *file)
{
}

int main(int argc, char *argv[])
{
	char *modestr = NULL;

	{
		int opt;
		while ((opt = getopt(argc, argv, "m:")) != -1)
		{
			switch (opt)
			{
				case 'm':
					modestr = optarg;
					break;
				default:
					show_usage();
			}
		}
	}

	if (argc - optind < 1)
		show_usage();

	char *modestrerr;
	struct clause **mode;
	mode_t oct_mode = 0;

	if (modestr) {
		if (isnumber(modestr)) {
			oct_mode = strtol(modestr, &modestrerr, 8);
			if (oct_mode == 0 && modestrerr != NULL)
				errx(EXIT_FAILURE, "'%s' is not a valid octal mode: %s",
						modestr, modestrerr);

			mode = NULL;
		} else {
			mode = parse_mode(modestr);
		}
	}

	while (optind < argc)
		do_mkfifo(oct_mode, mode, argv[optind++]);
}
