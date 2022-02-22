#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>

static void show_usage()
{
	fprintf(stderr, "chmod: [-R] mode file...\n");
	exit(EXIT_FAILURE);
}

static int opt_recurse = 0;

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

static int apply_change(mode_t change, const struct stat *sb, const struct who who, const struct perm p)
{
	if (who.usr && p.r) change |= S_IRUSR;
	if (who.usr && p.w) change |= S_IWUSR;
	if (who.usr && p.x) change |= S_IXUSR;

	if (who.grp && p.r) change |= S_IRGRP;
	if (who.grp && p.w) change |= S_IWGRP;
	if (who.grp && p.x) change |= S_IXGRP;

	if (who.oth && p.r) change |= S_IROTH;
	if (who.oth && p.w) change |= S_IWOTH;
	if (who.oth && p.x) change |= S_IXOTH;

	if (p.X && (S_ISDIR(sb->st_mode) ||
				(sb->st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)))) {
		if (who.usr) change |= S_IXUSR;
		if (who.grp) change |= S_IXGRP;
		if (who.oth) change |= S_IXOTH;
	}
	return change;
}

static int do_chmod(mode_t mode, struct clause *list[], const char *file)
{
	struct clause *cur = NULL;
	int rc = 0;
	struct stat sb;

	if (stat(file, &sb) == -1) {
		warn("%s", file);
		return EXIT_FAILURE;
	}

	/* if recurising and a directory, recurse */
	if (opt_recurse && S_ISDIR(sb.st_mode)) 
	{
		DIR *dir = opendir(file);
		if (dir == NULL) {
			warn("%s", file);
			return EXIT_FAILURE;
		}

		struct dirent *ent;
		char buf[BUFSIZ];

		while ((ent = readdir(dir)) != NULL)
		{
			if (!strcmp(".", ent->d_name) || !strcmp("..", ent->d_name))
				continue;

			snprintf(buf, BUFSIZ, "%s/%s", file, ent->d_name);
			rc += do_chmod(mode, list, buf);
		}

		closedir(dir);
	}
	/* we need to do the directory itsself */

	/* handle simple case of a octal mode argument */
	if (mode) {
		if (chmod(file, mode) == -1) {
			warn("%s", file);
			rc++;
		}
		return rc;
	}

	/* handle the more complex list of symbolic modes */
	for (int i = 0; (cur = list[i]); i++)
	{
		/* as a union this checks perm & permcopy */
		if (!cur->action.permcopy) continue;


		if (stat(file, &sb) == -1) {
			warn("%s", file);
			rc++;
			break;
		}

		mode_t base;
		if (cur->op == '=') {
			base = 0;
		} else {
			base = sb.st_mode;
		}

		if (cur->who.usr + cur->who.grp + cur->who.oth == 0) {
			cur->who.usr = 1;
			cur->who.grp = 1;
			cur->who.oth = 1;
		}

		mode_t change = 0;

		if (cur->act_flag == NONE)
		{
			; // FIXME ???
		}
		else if (cur->act_flag == PERMLIST)
		{
			change = apply_change(change, &sb, cur->who, cur->action.perm);

			// TODO t and s?
			
			if (cur->op == '-')
				base &= ~change;
			else
				base |= change;

			if (chmod(file, base) == -1) {
				warn("%s", file);
				rc++;
			}
		}
		else // PERMCOPY
		{
			struct perm p;
			mode_t save = 0;

			switch (cur->action.permcopy)
			{
				case 'u':
					p.r = (sb.st_mode & S_IRUSR) ? 1 : 0;
					p.w = (sb.st_mode & S_IWUSR) ? 1 : 0;
					p.x = (sb.st_mode & S_IXUSR) ? 1 : 0;
					save = S_IRWXU;
					break;
				case 'g':
					p.r = (sb.st_mode & S_IRGRP) ? 1 : 0;
					p.w = (sb.st_mode & S_IWGRP) ? 1 : 0;
					p.x = (sb.st_mode & S_IXGRP) ? 1 : 0;
					save = S_IRWXG;
					break;
				case 'o':
					p.r = (sb.st_mode & S_IROTH) ? 1 : 0;
					p.w = (sb.st_mode & S_IWOTH) ? 1 : 0;
					p.x = (sb.st_mode & S_IXOTH) ? 1 : 0;
					save = S_IRWXO;
					break;
			}

			change = apply_change(change, &sb, cur->who, p);
			
			if (cur->op == '-')
				base &= ~change;
			else
				base |= change;

			base |= (save & sb.st_mode);

			if (chmod(file, base) == -1) {
				warn("%s", file);
				rc++;
			}
		}
		
	}

	return rc;
}

static int isnumber(const char str[])
{
	for(int i = 0; str[i]; i++)
		if(!isdigit(str[i])) 
			return 0;
	return 1;
}

int main(int argc, char *argv[])
{
	{
		int opt;
		while ((opt = getopt(argc, argv, "R")) != -1)
		{
			switch (opt)
			{
				case 'R':
					opt_recurse = 1;
					break;
				default:
					show_usage();
			}
		}
	}

	if (argc - optind < 2)
		show_usage();

	char *modestr = argv[optind++];
	char *modestrerr;

	struct clause **mode;
	
	mode_t oct_mode = 0;

	if (isnumber(modestr)) 
	{
		oct_mode = strtol(modestr, &modestrerr, 8);
		if (oct_mode == 0 && modestrerr != NULL)
			errx(EXIT_FAILURE, "'%s' is not a valid octal number: %s", 
					modestr, modestrerr);

		mode = NULL;
	} 
	else 
	{
		mode = parse_mode(modestr);
	}

	int rc = 0;
	for (int i = optind; i < argc; i++)
	{
		rc += do_chmod(oct_mode, mode, argv[i]);
	}

	exit(rc ? EXIT_FAILURE : EXIT_SUCCESS);
}
