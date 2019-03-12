#define _XOPEN_SOURCE 700
//#define NDEBUG 1

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <ctype.h>
#include <assert.h>
#include <stdbool.h>

/* types and defines */

enum addr_en { ANOTHING, ALINE, ALAST, APREG };

typedef struct _address {
	enum addr_en	type;
	bool			triggered;

	union {
		size_t	 line;
		char	 last;
		regex_t	*preg;
	} addr;
} address_t;

#define SUB_GLOBAL	(1 << 0)
#define SUB_WSTDOUT	(1 << 1)
#define SUB_WFILE	(1 << 2)
#define SUB_NTH		(1 << 3)

typedef struct _sub {
	char	*wfile;
	char	*replacement;
	regex_t *preg;
	int		 flags;
	size_t	 nth;
} sub_t;

typedef struct _command command_t;

typedef struct _label {
	char		*name;
	command_t	*cmd;
} label_t;

typedef struct _jump {
	label_t		*to;
	char		*unresolved;
} jump_t;

struct _command {
	command_t		*next;
	address_t		 one;
	address_t		 two;
	unsigned int	 pos;
	unsigned char	 addrs;
	char			 function;

	union {
		command_t	 *block;
		char		**replace;
		char		 *text;
		FILE		 *file;
		sub_t		 *sub;
		jump_t		  jmp;
	} arg;
};

enum script_en { SNOTHING, SSCRIPT, SSCRIPT_FILE };

typedef struct _script {
	enum script_en type;
	char *original;
	union {
		char *script;
		FILE *script_file;
	} src;
} script_t;



/* global variables */

static script_t		**scripts	= NULL;
static label_t		**labels	= NULL;
static command_t	  *root		= NULL;
static FILE			**files		= NULL;

static char			holdspace[BUFSIZ & 8192];
static command_t	error;
static command_t	finished;

static int opt_no_output	= 0;
static int current_file		= 0;
static bool s_successful	= false;



/* local (forward) function declarations */

static command_t *parse_command(command_t *, const char *, char **);



/* local functions defintions */

inline static int min(const int a, const int b)
{
	return ((a < b) ? (a) : (b));
}

/* free members of j, but not j */
static void free_jump(jump_t *j)
{
	if (j->unresolved) {
		free(j->unresolved);
		j->unresolved = NULL;
	}
	/* we don't free j */
}

#ifdef NDEBUG
/* find the (first) label that points to c */
static label_t *label_for_cmd(const command_t *c)
{
	if (labels) for (size_t i = 0; labels[i]; i++)
		if (labels[i]->cmd == c) 
			return labels[i];

	return NULL;
}
#endif

/* create a label & add to global list */
static label_t *add_label(const char *str)
{
	label_t *ret = NULL;
	if ((ret = calloc(1, sizeof(label_t))) == NULL) {
		warn(NULL);
		return NULL;
	}

	size_t cnt = 0;

	if (labels == NULL) {
		if ((labels = calloc(2, sizeof(label_t *))) == NULL) {
			warn(NULL);
			free(ret);
			return NULL;
		}
	} else {
		for(;labels[cnt];cnt++) ;

		label_t **tmp = NULL;
		
		if ((tmp = realloc(labels, sizeof(label_t *) * (2 + cnt))) == NULL) {
			warn(NULL);
			free(ret);
			return NULL;
		}
		
		labels = tmp;
		labels[cnt] = NULL;
		labels[cnt+1] = NULL;
	}
	labels[cnt] = ret;
	ret->name = strdup(str);

	return ret;
}

/* free l and members */
static void free_label(label_t *l)
{
	if (!l) 
		return;

	if (l->name) {
		free(l->name);
		l->name = NULL;
	}

	free(l);
}

static int parse_label(const char *ptr, char **left, jump_t *j)
{
	char *tmp = (char *)ptr;
	while (*tmp && isalnum(*tmp)) tmp++;
	*left = tmp;

	j->to = NULL;
	j->unresolved = NULL;

	if (tmp == ptr) {
		warnx("empty label");
		return -1;
	}

	if (labels) 
		for (size_t i = 0; labels[i]; i++)
			if (!strncmp(labels[i]->name, ptr, tmp-ptr)) {
				j->to = labels[i];
				return i;
			}

	if ((j->unresolved = strndup(ptr, tmp-ptr)) == NULL)
		warn("parse_label");

	return -1;
}

/* find label by string */
static int find_label(const char *restrict str)
{
	if (labels)
		for (size_t i = 0; labels[i]; i++)
			if(!strcmp(str, labels[i]->name)) return i;

	return -1;
}

static FILE *parse_file(const char *ptr)
{
	return NULL;
}

/* parse a BRE with arbitary delimeter into a sub_t */
static sub_t *parse_sub(const char *ptr, const command_t *restrict c, char **left)
{
	if (!ptr || !*ptr) {
		warnx("s: invalid sub");
		return NULL;
	}
	const char re = *ptr++;

	if (!isprint(re)) {
		warnx("invalid regex delimeter");
		return NULL;
	}

	char *tmp = (char *)ptr;
	char *cur = tmp;
	int flags = 0, nth = 1;
	size_t idx = 0;
	char *end = NULL, *wfile = NULL, *dup = NULL, *endptr = NULL;
	sub_t *ret = NULL;

	char *part[2] = {NULL, NULL};


	while (*tmp)
	{
		if (*tmp == ';') 
			break;

		if (idx == 3) 
			break;

		if (idx < 2 && *tmp == re && *(tmp-1) != '\\') {
			if ((part[idx] = strndup(cur, tmp-cur)) == NULL) goto dowarn;
			idx++;
			cur = tmp + 1;
		} else if (idx == 2) {
			if (isdigit(*tmp)) 
			{
				end = tmp;
				while(*end && isdigit(*end)) end++;
				if ((dup = strndup(tmp, end-tmp)) == NULL) goto dowarn;
				nth = strtol(dup, &endptr, 10);
				if (*endptr != '\0') {
					warnx("unterminated digit: '%s'", dup);
					goto fail;
				}
				free(dup); dup = NULL;
				if (nth < 1) {
					warnx("invalid nth indicator '%d'", nth);
					goto fail;
				}
				flags |= SUB_NTH;
				tmp = end;
			} 
			switch (*tmp)
			{
				case '\0':
					tmp--;
					break;
				case 'g':
					flags |= SUB_GLOBAL;
					break;
				case 'p':
					flags |= SUB_WSTDOUT;
					break;
				case 'w':
					flags |= SUB_WFILE;
					tmp++;
					if (!*tmp) goto fail;
					while(*tmp && isspace(*tmp)) tmp++;
					if (!*tmp) goto fail;
					end = strchr(tmp, ';');
					const size_t len = end ? (size_t)(end-tmp) : strlen(tmp);
					if ((wfile = strndup(tmp, len)) == NULL) goto dowarn;
					tmp = end ? end : tmp + len - 1;
					break;
				default:
					warnx("unknown option '%c'", *tmp);
					goto fail;
			}
		}
		tmp++;
	}


	if (idx < 2) {
		warnx("%c: unterminated '/'", c->function);
fail:
		if (part[0]) { free(part[0]); part[0] = NULL; }
		if (part[1]) { free(part[1]); part[1] = NULL; }
		if (wfile) { free(wfile); wfile = NULL; }
		if (ret && ret->preg) { free(ret->preg); ret->preg = NULL; }
		if (ret) { free(ret); ret = NULL; }
		if (dup) { free(dup); dup = NULL; }
		return NULL;
dowarn:
		warn("parse_sub");
		goto fail;
	}

	if ((ret = calloc(1, sizeof(sub_t))) == NULL)
		goto dowarn;

	if ((ret->preg = calloc(1, sizeof(regex_t))) == NULL)
		goto dowarn;

	// TODO cflags
	if (regcomp(ret->preg, part[0], 0) != 0) {
		warnx("unable to compile regex '%s'", part[0]);
		goto fail;
	}

	free(part[0]); part[0] = NULL;
	ret->replacement = part[1];
	ret->wfile = wfile;
	ret->flags = flags;
	ret->nth = nth;
	*left = tmp;
	return ret;
}

/* parse a replacement argument of the form /blah/blah/ */
static char **parse_replace(const char *ptr, char **seek)
{
	char **ret = NULL;
	const char *one = (char *)ptr;
	if (!one || *one++ !='/' || !*one || *one=='/') goto invalid;
	
	const char *two = strchr(one, '/');
	if (!two || !*(++two)) goto invalid;
	
	const char *three = strchr(two + 1, '/');
	if (!three) goto invalid;

	if ((ret = calloc(2, sizeof(char *))) == NULL) goto dowarn;
	if ((ret[0] = strndup(one, two-one-1)) == NULL) goto dowarn;
	if ((ret[1] = strndup(two, three-two)) == NULL) goto dowarn;
	
	*seek = (char *)(three+1);
	return ret;

invalid:
	warnx("%s: invalid replacement", ptr);
fail:
	if(ret && ret[0]) { free(ret[0]); ret[0] = NULL; }
	if(ret && ret[1]) { free(ret[1]); ret[1] = NULL; }
	if(ret) { free(ret); ret = NULL; }
	*seek = NULL;
	return NULL;

dowarn:
	warn("y");
	goto fail;
}

static size_t command_idx = 0;

/* add command add to optional existing command tail */
static int add_command(command_t *restrict add, command_t *restrict tail)
{
	if (add == NULL || add == &error)
		return -1;

	if (add == &finished) 
		return 0;

	if (tail)
		tail->next = add;

	return 0;
}

/* parse the provided block of commands - must be \0 terminated
 * should not contain {}
 **/
static command_t *parse_block(char *str, char **left)
{
	char *cur = str;
	char *next = NULL;
	command_t *head = NULL, *c = NULL;

	//printf(" strt: str='%s'\n", str);

	while (1)
	{
		c = parse_command(c, cur, &next);
		//printf(" loop: next='%s'\n", next);

		if (c == &error) return &error;

		if (left)
			*left = next;

		if (c == &finished) break;

		cur = next;
		if (head == NULL) head = c;
	}

	//printf(" end\n");

	return head;
}

static command_t *parse_command_block(char *str, char **left)
{
	char *end = str;
	command_t *temp = NULL, *tail = NULL, *first = NULL;

	//printf("strt: str='%s'\n", str);

	while (*end)
	{
		*left = end;
		//printf("loop: end='%s'\n", end);

		if (*end == '}') { 
			end++;
			goto ok;
		}
		else if (*end == '{')
			temp = parse_command(tail, end, &end);
		else
			temp = parse_block(end, &end);
		
		if (temp == &error || temp == NULL || temp == &finished)
			break;

		if (first == NULL) {
			first = temp;
		} else {
			tail->next = temp;
			while(1) if (tail->next) tail = tail->next;	else break;
		}
	}

	//*left = end;

	if (temp == &error) return NULL;

	if (!*end) {
		warnx("unterminated command block");
		return NULL;
	}
ok:
	*left = end;
	//printf("done: end='%s'\n", end);
	return first;
}

/* parse any required arguments for the function in cmd */
static char *parse_function(command_t *restrict cmd, char *ptr)
{
	char *seek = ptr;

	switch(cmd->function)
	{
		case '{':
			// commands ';' does not apply
			if((cmd->arg.block = parse_command_block(ptr, &seek)) == NULL) return NULL;
			break;
		case 'a':
		case 'c':
		case 'i':
			// text ';' does not apply
			if ((cmd->arg.text = strdup(ptr)) == NULL) {
				warnx("%c", cmd->function);
				return NULL;
			}
			seek = ptr + strlen(ptr) - 1;
			break;
		case 'b':
		case 't':
			// label ';' does not apply
			if (parse_label(ptr, &seek, &cmd->arg.jmp) == -1) {
				if (cmd->arg.jmp.unresolved == NULL) {
					if (!cmd->arg.jmp.to)
						warnx("%c: unable to resolve '%*s'", cmd->function, (int)(seek-ptr), ptr);
					return NULL;
				}
			}
			break;
		case 'd':
		case 'g':
		case 'G':
		case 'h':
		case 'H':
		case 'D':
		case 'l':
		case 'n':
		case 'N':
		case 'p':
		case 'P':
		case 'q':
		case '=':
		case 'x':
			// none
			break;
		case 'r':
		case 'w':
			// FILE ';' does not apply
			cmd->arg.file = parse_file(ptr);
			seek = ptr + strlen(ptr) - 1;
			break;
		case 's':
			// sub
			if ((cmd->arg.sub = parse_sub(ptr, cmd, &seek)) == NULL) return NULL;
			break;
		case 'y':
			// replace
			if ((cmd->arg.replace = parse_replace(ptr, &seek)) == NULL) return NULL;
			break;
		case ':':
		case '#':
			// error 0addr should be captured
			break;
		case '}':
			return seek;
		default:
			warnx("%c: unknown command", cmd->function);
			return NULL;
	}
	return seek;
}

/* the main parse routine, extracts a single command and adds it to prev
 * may invoke itself for certain types, such as : and {}
 */
static command_t *parse_command(command_t *prev, const char *str, char **left)
{
	command_t *restrict ret = NULL;
	char *ptr = (char *)str;
	char *tmp = NULL;

	// TODO ';' does not apply for {}abcirtw:#
	while (ptr && *ptr && (isblank(*ptr) || *ptr == ';')) ptr++; 

	if (!ptr || !*ptr) return &finished;

	/* others */

	if (*ptr == '}') {
		*left = ptr;
		return &finished;
	}

	/* 0addr */

	if (*ptr == '#') 
	{
		if (!strncmp("#n", str, 2)) opt_no_output = 1;
		goto done;
	} 
	else if (*ptr == ':') 
	{
		char *end = ptr+1;
		char *lab = NULL;
		while(*end && isalnum(*end)) end++;
		if ((lab = strndup(ptr+1,end-(ptr+1))) == NULL) {
			warn("label");
			return &error;
		}
		label_t *label = add_label(lab);
		free(lab); lab = NULL;
		*left = end;
		/* notice: recursion here, as we need to know the next command the label points to */
		command_t *next = parse_command(prev, end, left);
		label->cmd = next;
		return next;
	}

	if ((ret = calloc(1, sizeof(command_t))) == NULL)
		goto fail;

	/* 1addr[,2addr] */

	int addr = 0;
	char *endptr = NULL;
	int val = 0;

	while(*ptr)
	{
		/* extract an [?addr] */
		if (addr < 2) {
			/* addr of type $ */
			if (*ptr == '$') 
			{
				if (addr == 0)  {
					ret->one.type = ALAST;
					if (*(ptr+1) == ',') {
						addr++;
					} else
						addr = 3;
				} else {
					ret->two.type = ALAST;
					addr = 3;
				}
				ptr++;
				ret->addrs++;
			} 
			/* address of type [0-9]+ */
			else if (isdigit(*ptr)) 
			{
				tmp = ptr;
				while (*ptr && isdigit(*ptr)) ptr++;
				if ((tmp = strndup(str, ptr-tmp)) == NULL)
					goto fail;
				val = strtol(tmp, &endptr, 10);
				if (*endptr != '\0') {
					warnx("unterminated digit: %s", tmp);
					goto fail2;
				}
				free(tmp); tmp = NULL;
				if (addr == 0) {
					ret->one.type = ALINE;
					ret->one.addr.line = val;
					if (*ptr == ',') addr++;
					else addr = 3;
				} else {
					ret->two.type = ALINE;
					ret->two.addr.line = val;
					addr = 3;
				}
				ret->addrs++;
			} 
			/* address of type BRE, with / as delimiter */
			else if (*ptr == '/')
			{
				tmp = ++ptr;
				regex_t **restrict rc = NULL;
				val = REG_NOSUB;

				while (*ptr)
				{
					if (*ptr == '/' && *(ptr-1) != '\\') {
						endptr = ptr;
						break;
					}
					ptr++;
				}

				if (!endptr) {
					tmp = NULL;
					warnx("notermination for BRE");
					goto fail2;
				}

				// repeat for optional third termination re:%c for options

				if ((tmp = strndup(tmp, endptr-tmp)) == NULL)
					goto fail;

				ptr = endptr;

				if (addr == 0) {
					rc = &ret->one.addr.preg;
					ret->one.type = APREG;
					if (*(ptr+1) == ',') {
						//ptr++;
						addr++;
					}
					else 
						addr = 3;
				} else {
					rc = &ret->two.addr.preg;
					ret->two.type = APREG;
					addr = 3;
				}

				if ((*rc = calloc(1, sizeof(regex_t))) == NULL)
					goto fail;

				if (regcomp(*rc, tmp, val)) {
					free(*rc);
					*rc = NULL;
					warnx("unable to compile regex '%s'", tmp);
					goto fail2;
				}

				ret->addrs++;
				free(tmp); tmp= NULL;
				ptr++;
			}
			else
			{
				addr = 3;
			}
		}
		/* skip blanks, extract function character, skip blanks invoke parse_function */
		if (addr == 3) {
			while (*ptr && isblank(*ptr)) ptr++;
			if (!*ptr) {
				goto fail2;
			}
			ret->function = *ptr++;
			while (*ptr && isblank(*ptr)) ptr++;
			ret->pos = command_idx++;
			*left = parse_function(ret, ptr);
			if (*left == NULL) {
				goto fail2;
			}
			goto done;
		}
		ptr++;
	}
	/* ensure we tell the caller where we have reached so it may continue */
	*left = ptr;

done:
	/* add the command */
	if(add_command(ret, prev) == -1) 
		return NULL;
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
	return &error;
}

#ifdef NDEBUG
static void dump(const command_t *restrict c)
{
	if (!c) 
		return;

	printf(":%03d ", c->pos);

	if (c->addrs>0)
		printf("[0] t:%d  ", c->one.type);
	else
		printf("         ");

	if (c->addrs>1)
		printf("[1] t:%d  ", c->two.type);
	else
		printf("         ");

	switch (c->function)
	{
		case 'd':
			printf("rmp:");
			break;
		case 'D':
			printf("rmp: if");
			break;
		case 'g':
			printf("put: hold > pattern");
			break;
		case 'G':
			printf("put: \\n, hold >> pattern");
			break;
		case 'h':
			printf("put: pattern > hold");
			break;
		case 'H':
			printf("put: \\n, pattern > hold");
			break;
		case 'l':
			printf("prt: clean");
			break;
		case 'n':
			printf("prt: if output print, if empty quit");
			break;
		case 'N':
			printf("put: trim(in),\\n >> pattern");
			break;
		case 'p':
			printf("prt:");
			break;
		case 'P':
			printf("prt: up to \\n");
			break;
		case 'q':
			printf("end:");
			break;
		case 'x':
			printf("put: pattern <> hold");
			break;
		case '=':
			printf("prt: line number");
			break;
		case 'b':
		case 't':
			printf("%s: %s@%p[%c]\n", 
					c->function == 'b' ? "bra" : "beq",
					!c->arg.jmp.to ? c->arg.jmp.unresolved : c->arg.jmp.to->name,
					!c->arg.jmp.to ? 0 : (void *)c->arg.jmp.to->cmd,
					!c->arg.jmp.to ? ' ' : c->arg.jmp.to->cmd->function
					);
			break;
		case 'y':
			if (c->arg.replace) {
				printf("rep: fnd='%s' rep='%s'", c->arg.replace[0], c->arg.replace[1]);
			}
			break;
		case 's':
			if (c->arg.sub) {
				printf("sub: rep='%s' opts=", c->arg.sub->replacement);
				if (c->arg.sub->flags & SUB_GLOBAL) printf("GLOBAL ");
				if (c->arg.sub->flags & SUB_WSTDOUT) printf("WSTDOUT ");
				if (c->arg.sub->flags & SUB_WFILE) printf("WFILE=%s ", c->arg.sub->wfile);
				if (c->arg.sub->flags & SUB_NTH) printf("NTH[%d] ", c->arg.sub->nth);
			}
			break;
		case '{':
			printf("{   : [skip=%d]\n", c->next ? c->next->pos : 0);
			break;
		default:
			printf("%c  :\n", c->function);
	}
	printf("\n");
}
#endif

/* free members of a, but not a */
static void free_addr(address_t *restrict a)
{
	if (!a) return;

	switch(a->type)
	{
		case APREG:
			if (a->addr.preg) {
				regfree(a->addr.preg);
				free(a->addr.preg);
				a->addr.preg = NULL;
			}
			break;
		default:
			break;
	}
	/* we do not free(a) */
}

/* free s and members */
static void free_sub(sub_t *restrict s)
{
	if (!s) return;

	if (s->preg) {
		regfree(s->preg);
		free(s->preg);
		s->preg = NULL;
	}

	if (s->replacement) {
		free(s->replacement);
		s->replacement = NULL;
	}

	if (s->wfile) {
		free(s->wfile);
		s->wfile = NULL;
	}

	free(s);
}

/* free replace and members */
static void free_replace(char **replace)
{
	if (!replace) return;

	if (replace[0]) { free(replace[0]); replace[0] = NULL; }
	if (replace[1]) { free(replace[1]); replace[1] = NULL; }

	free(replace);
}

/* free c and members */
static void free_cmd(command_t *restrict c)
{
	if (c == NULL) return;
	if (c->addrs>0) { free_addr(&c->one); }
	if (c->addrs>1) { free_addr(&c->two); }

	switch(c->function)
	{
		case 'a':
		case 'c':
		case 'i':
			if (c->arg.text) {
				free(c->arg.text);
				c->arg.text = NULL;
			}
			break;
		case 'd':
		case 'g':
		case 'G':
		case 'h':
		case 'H':
		case 'D':
		case 'l':
		case 'n':
		case 'N':
		case 'p':
		case 'P':
		case 'q':
		case '=':
		case 'x':
			break;
		case 's':
			if (c->arg.sub) {
				free_sub(c->arg.sub);
				c->arg.sub = NULL;
			}
			break;
		case 'y':
			if (c->arg.replace) {
				free_replace(c->arg.replace);
				c->arg.replace = NULL;
			}
			break;
		case 'b':
			free_jump(&c->arg.jmp);
			break;
		case ':':
			break;
		case '{':
			break;
		default:
			warnx("free_cmd: %c: unknown function", c->function);
	}
	free(c);
}

/* free a block */
static void clean_block(command_t *c)
{
	if (!c)
		return;

	for(command_t *tmp = c; tmp; )
	{
		command_t *next; 
		if (tmp->function == '{' && tmp->arg.block)
			next = tmp->arg.block;
		else
			next = tmp->next;
		free_cmd(tmp);
		tmp = next;
	}
}

/* free script and members */
static void free_script(script_t *s)
{
	switch (s->type)
	{
		case SSCRIPT:
			if (s->src.script) {
				free(s->src.script);
				s->src.script = NULL;
			}
			break;
		case SSCRIPT_FILE:
			if (s->src.script_file) {
				fclose(s->src.script_file);
				s->src.script_file = NULL;
			}
			break;
		case SNOTHING:
			break;
	}

	if (s->original) {
		free(s->original);
		s->original = NULL;
	}

	free(s);
}

static void add_file(const char *restrict fn)
{
	FILE *restrict fh = NULL;

	if (!strcmp(fn, "-")) 
		fh = stdin;
	else if ((fh = fopen(fn, "r")) == NULL) {
		warn("%s", fn);
		return;
	}

	size_t cnt = 0;

	if (files == NULL) {
		if ((files = calloc(2, sizeof(FILE *))) == NULL)
			err(EXIT_FAILURE, "add_file");
	} else {
		for(;files[cnt];cnt++) ;
		FILE **tmp = NULL;
		if ((tmp = realloc(files, sizeof(FILE *) * (2+ cnt))) == NULL)
			err(EXIT_FAILURE, "add_file");
		files = tmp;
		files[cnt] = NULL;
		files[cnt+1] = NULL;
	}

	files[cnt] = fh;
	return;
}

/* add script to scripts global , value is not used and can be free'd */
static script_t *add_script(const enum script_en type, char *restrict value)
{
	script_t *restrict ret = NULL;

	if ((ret = calloc(1, sizeof(script_t))) == NULL)
		goto fail;

	ret->type = type;
	ret->original = strdup(value);

	switch (type)
	{
		case SSCRIPT:
			if ((ret->src.script = strdup(value)) == NULL)
				goto warn;
			break;
		case SSCRIPT_FILE:
			if ((ret->src.script_file = fopen(value, "r")) == NULL)
				goto warn;
			break;
		case SNOTHING:
			warnx("unknown script type");
			goto fail;
			break;
	}

	size_t cnt = 0;

	if (scripts == NULL) {
		if ((scripts = calloc(2, sizeof(script_t *))) == NULL)
			goto warn;
	} else {
		for(;scripts[cnt];cnt++) ;
		script_t **tmp = NULL;
		if ((tmp = realloc(scripts, sizeof(script_t *) * (2 + cnt))) == NULL)
			goto warn;

		scripts = tmp;
		scripts[cnt] = NULL;
		scripts[cnt+1] = NULL;
	}

	scripts[cnt] = ret;
	return ret;

warn:
	warn(NULL);
fail:
	if (ret) {
		free_script(ret);
		ret = NULL;
	}
	return NULL;
}


/* invoked by atexit() to clean up */
static void cleanup()
{
	if (labels) {
		for(size_t i = 0; labels[i]; i++) {
			free_label(labels[i]);
			labels[i] = NULL;
		}
		free(labels);
		labels = NULL;
	}
	
	if (root)
		clean_block(root);

	if (scripts) {
		for (size_t i = 0; scripts[i]; i++) {
			free_script(scripts[i]);
			scripts[i] = NULL;
		}
		free(scripts);
		scripts = NULL;
	}

	if (files) {
		for (size_t i = 0; files[i]; i++) {
			if (fileno(files[i]) > 2) {
				fclose(files[i]);
			}
			files[i] = NULL;
		}
		free(files);
		files = NULL;
	}
}

/* handle fixups for a block, such as unresolved destinations for future labels */
static void fix_block(command_t *first)
{
	for(command_t *cur = first; cur; cur=cur->next)
	{
		if (cur->function == '{' && cur->arg.block)
		{
			fix_block(cur->arg.block);
			for (command_t *p = cur->arg.block; p; p=p->next) {
				if (p->next == NULL) {
					p->next = cur->next;
					break;
				}
			}
		} 
		else if ((cur->function == 'b' || cur->function == 't') 
				&& cur->arg.jmp.to == NULL 
				&& cur->arg.jmp.unresolved) 
		{
			int label;
			if ((label = find_label(cur->arg.jmp.unresolved)) == -1) {
				warnx("cannot resolve label '%s'", cur->arg.jmp.unresolved);
			} else {
				cur->arg.jmp.to = labels[label];
				free(cur->arg.jmp.unresolved); cur->arg.jmp.unresolved = NULL;
			}
		}
	}
}

static void show_usage()
{
	fprintf(stderr,
			/* form 1 */
			"Usage: sed [-n] script [file...]\n"
			/* form 2 & 3 */
			"       sed [-n] -e script [-e script]... [-f script_file]... [file...]\n"
			"       sed [-n] [-e script]... -f script_file [-f script_file]... [file...]\n");
	exit(EXIT_FAILURE);
}

static int readline(char *buffer, size_t length)
{
	FILE *c = files[current_file];
	
	if(!c)
		return -1;

	while(1)
	{
		if (ferror(c))
			err(EXIT_FAILURE, NULL);

		if (feof(c)) {
			if ((c = files[++current_file]) == NULL)
				return 0;
			continue;
		}

		char *res = fgets(buffer, length, files[current_file]);
		size_t len = 0;
		if (res) {
			len = strlen(res);

			if (res[len-1] == '\n') res[len-1] = '\0';
			return len;
		}
	}
}

static void parse_script(const script_t *restrict s)
{
	command_t *tmp = root;
	char *script = NULL;

	switch(s->type)
	{
		case SSCRIPT_FILE:
			errx(EXIT_FAILURE, "SSCRIPT_FILE: unsupported");
			break;
		case SSCRIPT:
			script = s->src.script;
			break;
		case SNOTHING:
			errx(EXIT_FAILURE, "SNOTHING");
			break;
	}

	while((tmp = parse_command(tmp, script, &script)) != NULL) {
		if (tmp == &error) errx(EXIT_FAILURE, "bad script");
		if (tmp == &finished) break;
		if (root == NULL) root = tmp;
	}
}

const char *addrtype(const enum addr_en t)
{
	switch (t)
	{
		case APREG:
			return "APREG";
		case ALINE:
			return "ALINE";
		case ALAST:
			return "ALAST";
		case ANOTHING:
			return "ANOTHING";
	}

	return "!ERROR!";
}

static bool addr_match(const size_t line, command_t *restrict cmd, const char *buf, const bool lastline)
{
	switch(cmd->addrs)
	{
		case 0:
			return true;

		case 1:
			if (cmd->one.type == ALAST) return lastline;
			if (cmd->one.type == ALINE) return (line == cmd->one.addr.line);
			if (cmd->one.type == APREG) return (regexec(cmd->one.addr.preg, buf, 0, NULL, 0) == 0);
			break;

		case 2:
			/* we are between addr1,addr2 */
			if (cmd->one.triggered) {
				if (cmd->two.type == ALINE && line > cmd->two.addr.line) {
					cmd->one.triggered = false;
					return false;
				} else if (cmd->two.type == APREG) {
					return (cmd->one.triggered = (regexec(cmd->two.addr.preg, buf, 0, NULL, 0) != 0));
				} else if (cmd->two.type == ALAST) {
					return true;
				}
			/* we are before addr1 or after addr2 */
			} else {
				if (cmd->one.type == APREG) {
					return (cmd->one.triggered = (regexec(cmd->one.addr.preg, buf, 0, NULL, 0) == 0));
				} else if (cmd->one.type == ALINE && (line>=cmd->one.addr.line && line<=cmd->two.addr.line)) {
					return (cmd->one.triggered = true);
				}
			}
			/* no change of state */
			return cmd->one.triggered;
	}

	return false;
}

static command_t *execute(const command_t *c, char *buf, const size_t len, const int line)
{
	char *newbuf = calloc(1, len + 1);

	if (newbuf == NULL)
		err(EXIT_FAILURE, "execute");

	command_t	*ret = c->next;
	regmatch_t	 pmatch[1 + 9];

	switch(c->function)
	{
		/* misc */
		case '{':
			ret = c->arg.block;
			break;
		case '=':
			printf("%d\n", line);
			break;

		/* replacement */
		case 'y':
			{
				const char *ptr = NULL;
				for(size_t pnt = 0; buf[pnt] && pnt < len; pnt++)
				{
					if ((ptr = strchr(c->arg.replace[0], buf[pnt])) != NULL) {
						newbuf[pnt] = c->arg.replace[1][ptr-c->arg.replace[0]];
					} else {
						newbuf[pnt] = buf[pnt]; 
					}
				}
			}
			break;
		case 's':
			{
				const size_t pmatch_sz = sizeof(pmatch) / sizeof(regmatch_t);

				size_t matched = 0;
				char *next = buf;
				char *old = NULL;
				
				s_successful = false;

				/* main s/// loop */
				while(next && *next && regexec(c->arg.sub->preg, next, pmatch_sz, pmatch, 0) == 0) 
				{
					if (!old) 
					{
						/* copy any non-matched chars before the 1st match */
						strncat(newbuf, next, 
								min(len - strlen(newbuf), pmatch[0].rm_so));
					} else {
						/* copy any non-matched chars up to the start of the match from end of old */
						strncat(newbuf, old, 
								min(len - strlen(newbuf), (next + pmatch[0].rm_so) - old));
					}

					/* keep track of the nth regex matched */
					matched++;

					/* if we are not globally replacing, and this isn't the nth, skip */
					if (!(c->arg.sub->flags & SUB_GLOBAL) && matched != c->arg.sub->nth) 
					{
						/* so copy the contents of the skipped match */
						strncat(newbuf, 
								next + pmatch[0].rm_so, 
								min(len - strlen(newbuf), pmatch[0].rm_eo - pmatch[0].rm_so));
						next += pmatch[0].rm_eo;
						old = next;
						continue;
					}

					/* mark this s// as successful, for other commands */
					s_successful = true;

					/* proceed to copy the replacement, expanding as required */
					const char *restrict src = c->arg.sub->replacement;
					char *dst = newbuf + strlen(newbuf);

					while (*src && dst < (newbuf + len))
					{
						/* \\0 */
						if (*src == '\\' && *(src+1) == '\\' && isdigit(*(src+2))) {
							src++;
							*dst++ = *src++;
							*dst++ = *src++;
						} 
						/* \0 */
						else if (*src == '\\' && isdigit(*(src+1)) ) 
						{
							const int d = *(src+1) - '0';
							src += 2;

							if (pmatch[d].rm_eo != -1 && pmatch[d].rm_so != -1)
							{
								const int rlen = pmatch[d].rm_eo - pmatch[d].rm_so;
								strncat(newbuf, 
										next + pmatch[d].rm_so, 
										min(rlen, len - strlen(newbuf)));
								dst += rlen;
							}
						} 
						/* \& */
						else if (*src == '\\' && *(src+1) == '&') 
						{
							src+=2;
							*dst++ = '&';
						} 
						/* & */
						else if (*src == '&') 
						{
							src++;
							strncat(newbuf, 
									buf, 
									min(strlen(buf), len - strlen(newbuf)));
							dst = newbuf + strlen(newbuf);
						} 
						/* anything else */
						else 
						{
							*dst++ = *src++;
						}
					}
					
					/* skipped past this match to the first unchecked char */
					next += pmatch[0].rm_eo;
					old = next;
				} 

				if (s_successful) {
					/* make sure to copy from the last match to the end of the string 
					 * use next in case old hasn't been set */
					strncat(newbuf, 
							next, 
							min(len, strlen(next)));
				} else
					strcpy(newbuf, buf);
			}
			break;

			/* labels */
		case 't':
		case 'T':
			if (c->function == 't' ? s_successful : !s_successful) {
				s_successful = false;
			} else
				break;
		case 'b':
			if (c->arg.jmp.to)
				ret = c->arg.jmp.to->cmd;
			else
				ret =NULL;
			break;

			// FIXME these need to integrate with readline 
			/*
			   case 'n':
			   case 'N':
			   */

			/* holdspace/pattern space */
		case 'x':
			{
				char *tmp = strdup(holdspace);
				if (tmp == NULL) err(EXIT_FAILURE, "execute");
				strncpy(holdspace, buf, sizeof(holdspace));
				strncpy(buf, tmp, len);
				free(tmp);
				tmp = NULL;
				strcpy(newbuf, buf);
			}
			break;
		case 'h':
			strncpy(holdspace, buf, sizeof(holdspace));
			strcpy(newbuf, buf);
			break;
		case 'H':
			strncat(holdspace, buf, sizeof(holdspace));
			strcpy(newbuf, buf);
			break;
		case 'g':
			strncpy(buf, holdspace, len);
			strcpy(newbuf, buf);
			break;
		case 'G':
			strncat(buf, holdspace, len);
			strcpy(newbuf, buf);
			break;

		case 'd':
		case 'D': // FIXME handle this properly
			*newbuf = '\0';
			break;

		/* print */
		case 'P':
		case 'p': // FIXME handle 'embedded newline'
			strcpy(newbuf, buf);
			break;
		default:
			free(newbuf);
			errx(EXIT_FAILURE, "%c: unsupported", c->function);
			ret = NULL;
			break;

		case 'q': // FIXME need to signal to break the loop & print
			ret = NULL;
			break;
	}

	strcpy(buf, newbuf);
	free(newbuf);
	newbuf = NULL;
	return ret;
}

/* global functions */

int main(const int argc, char *argv[])
{
	/* process command line options */
	{
		int opt = 0;
		size_t num_scripts		= 0;
		size_t num_script_files = 0;

		while ((opt = getopt(argc, argv, "ne:f:")) != -1)
		{
			switch (opt)
			{
				case 'n':
					opt_no_output = 1;
					break;
				case 'e':
					add_script(SSCRIPT, optarg);
					num_scripts++;
					break;
				case 'f':
					add_script(SSCRIPT_FILE, optarg);
					num_script_files++;
					break;
				default:
					show_usage();
			}
		}
		if ((num_scripts + num_script_files) == 0) {
			/* form 1 */
			if (optind >= argc)
				show_usage();
			add_script(SSCRIPT, argv[optind++]);
		}

		if (optind == argc)
			add_file("-");
		else
			while (optind < argc)
				add_file(argv[optind++]);
	}
	
	memset(holdspace, 0, sizeof(holdspace));
	atexit(cleanup);

	char buf[BUFSIZ];
	char next[BUFSIZ];
	size_t line = 0;
	bool eof = false;

	for (size_t i = 0; scripts[i]; i++)
		parse_script(scripts[i]);

	if (!root) exit(EXIT_FAILURE);
	fix_block(root);

	if(readline(buf, sizeof(buf))) while(*buf)
	{
		if (!readline(next, sizeof(next))) {
			*next = '\0';
			eof = 1;
		}

		line++;
		//printf("line %04lu: '%s'\n", line, buf);
		size_t found = 0;
		command_t *cur = root;
		while(cur)
		{
			if (addr_match(line, cur, buf, eof)) 
			{
				cur = execute(cur, buf, sizeof(buf), line);
				found++;
			} else {
				cur = cur->next;
			}
		}
		if (found)
			printf("%s\n", buf);
		else if (!opt_no_output)
			printf("%s\n", buf);
		strcpy(buf, next);
	}

	exit(EXIT_SUCCESS);
}
