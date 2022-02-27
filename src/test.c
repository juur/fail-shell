#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#undef	EXIT_FAILURE
#define EXIT_FAILURE	2

static struct stat sb;

static int is_block(const char *f) {
	if (stat(f, &sb) == -1) return 1; else return !(S_ISBLK(sb.st_mode));
}

static int is_char(const char *f) {
	if (stat(f, &sb) == -1) return 1; else return !(S_ISCHR(sb.st_mode));
}

static int is_dir(const char *f) {
	if (stat(f, &sb) == -1) return 1; else return !(S_ISDIR(sb.st_mode));
}

static int is_existing(const char *f) {
	if (stat(f, &sb) == -1) return 1; else return 0;
}

static int is_file(const char *f) {
	if (stat(f, &sb) == -1) return 1; else return !(S_ISREG(sb.st_mode));
}

static int is_setgid(const char *f) {
	if (stat(f, &sb) == -1) return 1; else return !((sb.st_mode & S_ISGID) == S_ISGID);
}

static int is_link(const char *f) {
	if (stat(f, &sb) == -1) return 1; else return !(S_ISLNK(sb.st_mode));
}

static int is_fifo(const char *f) {
	if (stat(f, &sb) == -1) return 1; else return !(S_ISFIFO(sb.st_mode));
}

static int is_socket(const char *f) {
	if (stat(f, &sb) == -1) return 1; else return !(S_ISSOCK(sb.st_mode));
}

static int is_setuid(const char *f) {
	if (stat(f, &sb) == -1) return 1; else return !((sb.st_mode & S_ISUID) == S_ISUID);
}

static int is_not_empty(const char *f) {
	if (stat(f, &sb) == -1) return 1; else return !(sb.st_size > 0);
}

static int is_readable(const char *f) {
	if (access(f, R_OK)) return 1; else return 0;
}

static int is_writeable(const char *f) {
	if (access(f, W_OK)) return 1; else return 0;
}

static int is_exec(const char *f) {
	if (access(f, X_OK)) return 1; else return 0;
}

static int is_file_desc(const char *f) {
	warnx("is_file_desc not implemented");
	return 0;
}

static int binaryintop(const char *op, long n1, long n2)
{
	op++;

	if (!strcmp(op, "eq"))      { return n1 == n2; }
	else if (!strcmp(op, "ne")) { return n1 != n2; }
	else if (!strcmp(op, "gt")) { return n1 >  n2; }
	else if (!strcmp(op, "ge")) { return n1 >= n2; }
	else if (!strcmp(op, "lt")) { return n1 <  n2; }
	else if (!strcmp(op, "le")) { return n1 <= n2; }
	else {
		errx(EXIT_FAILURE, "unknown operator %s", op);
	}
	return 0;
}

static int binaryop(const char *op, const char *arg0, const char *arg1)
{
	if (*op == '-' ) {
		char *endptr = NULL;

		errno = 0;
		long n1 = strtol(arg0, &endptr, 10);

		if (errno)
			err(EXIT_FAILURE, "invalid integer: %s", arg0);
		if (endptr != NULL)
			errx(EXIT_FAILURE, "invalid integer: %s", arg0);
			
		errno = 0;
		endptr = NULL;
		long n2 = strtol(arg1, &endptr, 10);

		if (errno)
			err(EXIT_FAILURE, "invalid integer: %s", arg0);
		if (endptr != NULL)
			errx(EXIT_FAILURE, "invalid integer: %s", arg0);

		return binaryintop(op, n1, n2);
	} else if (!strcmp(op, "=")) {
		return strcmp(arg0, arg1) ? 1 : 0;
	} else if (!strcmp(op, "!=")) {
		return strcmp(arg0, arg1) ? 0 : 1;
	} else {
		errx(EXIT_FAILURE, "unknown operator %s", op);
	}

	return 0;
}

static int unaryop(const char *op, const char *arg)
{
	int rc = 0;

	switch(*(op+1))
	{
		case 'b':
			rc = is_block(arg);
			break;
		case 'c':
			rc = is_char(arg);
			break;
		case 'd':
			rc = is_dir(arg);
			break;
		case 'e':
			rc = is_existing(arg);
			break;
		case 'f':
			rc = is_file(arg);
			break;
		case 'g':
			rc = is_setgid(arg);
			break;
		case 'h':
		case 'L':
			rc = is_link(arg);
			break;
		case 'n':
			rc = !strlen(arg);
			break;
		case 'p':
			rc = is_fifo(arg);
			break;
		case 'r':
			rc = is_readable(arg);
			break;
		case 'S':
			rc = is_socket(arg);
			break;
		case 's':
			rc = is_not_empty(arg);
			break;
		case 't':
			rc = is_file_desc(arg);
			break;
		case 'u':
			rc = is_setuid(arg);
			break;
		case 'w':
			rc = is_writeable(arg);
			break;
		case 'x':
			rc = is_exec(arg);
			break;
		case 'z':
			rc = strlen(arg);
			break;
		default:
			errx(EXIT_FAILURE, "unknown test '%s'", op);
	}

	return rc;
}

int main(int argc, char *argv[])
{
	int rc = -1;

	if (!strcmp(argv[0], "[")) {
		if(!strcmp(argv[argc-1], "]")) {
			argc--;
		} else {
			errx(EXIT_FAILURE, "']' required as last argument");
		}
	}

	switch (argc) {
		/* test */
		case 1:
			rc = 1;
			break;
		
		/* test arg */
		case 2:
			rc = strlen(argv[1]) ? 1 : 0;
			break;

		/* test ?? ?? */
		case 3:			
			if (!strcmp(argv[1], "!")) {
				/* ! arg */
				rc = strlen(argv[1]) ? 0 : 1;
			} else {
				/* op arg */
				rc = unaryop(argv[1], argv[2]);
			}
			break;

		/* test ?? op arg */
		case 4:
			if (!strcmp(argv[1], "!")) {
				/* test ! op arg */
				rc = !unaryop(argv[2], argv[3]);
			} else {
				/* test arg op arg */
				rc = binaryop(argv[2], argv[1], argv[3]);
			}
			break;

		case 5:
			if (strcmp(argv[1], "!"))
				break;
			rc = !binaryop(argv[3], argv[2], argv[4]);
			break;

		default:
			break;

	}


	if (rc == -1)
		err(EXIT_FAILURE, "Unsupported test");

	exit(rc ? 1 : 0);
}
