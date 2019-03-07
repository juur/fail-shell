#define _XOPEN_SOURCE 700

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <ctype.h>

enum addr_en { anothing, aline, alast, apreg };
typedef struct _address {
	enum addr_en type;
	union {
	int line;
	char last;
	regex_t *preg;
	} addr;
} address_t;

typedef struct _sub {
	regex_t *preg;
	char *replacement;
	int flags;
} sub_t;

typedef struct _command {
	address_t one;
	address_t two;
	int addrs;
	char function;
	union {
		struct _command **commands;
		char *text;
		int label;
		FILE *file;
		sub_t *sub;
		char *replace[2];
	} arg;
} command_t;

typedef struct _label {
	char *name;
	command_t *target;
} label_t;

label_t **labels = NULL;

int opt_no_output = 0;

static int add_label(const char *str)
{
	label_t *ret = NULL;
	if ((ret = calloc(1, sizeof(label_t))) == NULL) {
		warn(NULL);
		return -1;
	}
	int cnt=0;
	if (labels == NULL) {
		if ((labels = calloc(2, sizeof(label_t *))) == NULL) {
			warn(NULL);
			free(ret);
			return -1;
		}
	} else {
		for(;labels[cnt];cnt++) ;
		label_t **tmp = NULL;
		if ((tmp = realloc(labels, sizeof(label_t *) * (2 + cnt))) == NULL) {
			warn(NULL);
			free(ret);
			return -1;
		}
		labels[cnt] = NULL;
		labels[cnt+1] = NULL;
		labels = tmp;
	}
	labels[cnt] = ret;
	ret->name = strdup(str);

	return 0;
}

static void free_label(label_t *l)
{
	if (!l) return;

	if (l->name) {
		free(l->name);
		l->name = NULL;
	}

	free(l);
}

static command_t *parse_command(char *str)
{
	command_t *ret;

	if ((ret = calloc(1, sizeof(command_t))) == NULL)
		goto fail;

	/* 0addr */

	if (*str == '#') 
	{
		if (!strncmp("#n", str, 2)) opt_no_output = 1;
		free(ret); ret = NULL;
		goto done;
	} 
	else if (*str == ':') 
	{
		add_label(str+1);
		goto done;
	}

	/* 1addr[,2addr] */

	int addr = 0;
	char *ptr = str;
	char *tmp = NULL;
	char *endptr = NULL;
	int val = 0;

	while(*ptr)
	{
		if (addr < 2) {
			if (*ptr == '$') 
			{
				if (addr == 0)  {
					ret->one.type = alast;
					if (*(ptr+1) == ',') {
						addr++;
					} else
						addr = 3;
				} else {
					ret->two.type = alast;
					addr = 3;
				}
				ptr++;
				ret->addrs++;
			} 
			else if (isdigit(*ptr)) 
			{
				tmp = ptr;
				while (*ptr && isdigit(*ptr)) ptr++;
				if ((tmp = strndup(str, ptr-tmp-1)) == NULL)
					goto fail;
				val = strtol(tmp, &endptr, 10);
				if (*endptr != '\0') {
					warnx("unterminated digit: %s", tmp);
					goto fail2;
				}
				free(tmp); tmp = NULL;
				if (addr == 0) {
					ret->one.type = aline;
					ret->one.addr.line = val;
					if (*ptr == ',') addr++;
					else addr = 3;
				} else {
					ret->two.type = aline;
					ret->two.addr.line = val;
					addr = 3;
				}
				ret->addrs++;
			} 
			/* BRE */
			else 
			{
				char re = *ptr++;
				tmp = ptr;
				regex_t **rc = NULL;
				val = 0;

				while (*ptr)
				{
					if (*ptr == re && *(ptr-1) != '\\') {
						endptr = ptr;
						break;
					}
					ptr++;
				}

				if (!endptr) {
					tmp = NULL;
					warnx("notermination for re:%c", re);
					goto fail2;
				}

				// repeat for optional third termination re:%c for options

				if ((tmp = strndup(tmp, endptr-tmp)) == NULL)
					goto fail;

				if (addr == 0) {
					rc = &ret->one.addr.preg;
					ret->one.type = apreg;
					if (*(ptr+1) == ',') {
						ptr++;
						addr++;
					}
					else addr = 3;
				} else {
					rc = &ret->one.addr.preg;
					ret->two.type = apreg;
					addr = 3;
				}

				if ((*rc = calloc(1, sizeof(regex_t))) == NULL)
					goto fail;

				if (regcomp(*rc, tmp, val)) {
					free(*rc);
					*rc = NULL;
					warnx("cannot compile regex: %s", tmp);
					goto fail2;
				}

				ret->addrs++;
				free(tmp); tmp= NULL;
				ptr++;
			}
		}
		if (addr == 3) {
			ret->function = *ptr;
			break;
		}
		ptr++;
	}

done:
	return ret;

fail:
	warn(NULL);
fail2:
	if (tmp) {
		free(tmp);
		tmp = NULL;
	}
	if (ret) {
		free(ret);
		ret = NULL;
	}
	goto done;
}

static void dump(command_t *c)
{
	printf("addr: %p\n", c);
	if (!c) return;
	printf("addrs: %d\n", c->addrs);
	if (c->addrs>0) {
		printf("addr[0]: type:%d\n", c->one.type);
	}
	if (c->addrs>1) {
		printf("addr[1]: type:%d\n", c->two.type);
	}
}

static void free_addr(address_t *a)
{
	switch(a->type)
	{
		case apreg:
			if (a->addr.preg) {
				regfree(a->addr.preg);
				free(a->addr.preg);
			}
			a->addr.preg = NULL;
			break;
		default:
			break;
	}
}

static void free_cmd(command_t *restrict c)
{
	if (c == NULL) return;
	if (c->addrs>0) { free_addr(&c->one); }
	if (c->addrs>1) { free_addr(&c->two); }
	switch(c->function)
	{
		case 'y':
			break;
		default:
			warnx("free_cmd: %c: unknown function", c->function);
	}
	free(c);
}

int main(int argc, char *argv[])
{
	command_t * c = NULL;
	dump(c = parse_command(argv[1]));
	free_cmd(c);
	c = NULL;

}
