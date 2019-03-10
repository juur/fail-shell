#define _XOPEN_SOURCE 700
#define NDEBUG 1

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <ctype.h>
#include <assert.h>

enum addr_en { anothing, aline, alast, apreg };

typedef struct _address {
	enum addr_en type;
	union {
		int line;
		char last;
		regex_t *preg;
	} addr;
} address_t;

#define SUB_GLOBAL	(1 << 0)
#define SUB_WSTDOUT	(1 << 1)
#define SUB_WFILE	(1 << 2)
#define SUB_NTH		(1 << 3)

typedef struct _sub {
	char *wfile;
	char *replacement;
	regex_t *preg;
	int flags;
	int nth;
} sub_t;

typedef struct _command command_t;

typedef struct _label {
	char *name;
	command_t *cmd;
} label_t;

typedef struct _jump {
	label_t *to;
	char *unresolved;
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

label_t		**labels	= NULL;

command_t	 error;
command_t	 finished;

command_t	*root = NULL;

int opt_no_output = 0;

static int add_command(command_t *restrict add, command_t *restrict tail)
{
	//unsigned int cnt = 0;

	if (add == &error || add == &finished) return 0;
	if (tail)
		tail->next = add;
	//printf("adding %p[%c] to %p[%c]\n", add, add->function, tail, tail ? tail->function : ' ');
	return 0;

	/*

	if (commands == NULL) {
		if ((commands = calloc(2, sizeof(command_t *))) == NULL) {
			warn(NULL);
			return -1;
		}
	} else {
		for(;commands[cnt];cnt++) ;
		command_t **tmp = NULL;
		if ((tmp = realloc(commands, sizeof(command_t *) * (2 + cnt))) == NULL) {
			warn(NULL);
			return -1;
		}
		commands = tmp;
		commands[cnt] = NULL;
		commands[cnt+1] = NULL;
	}
	commands[cnt] = c;
	c->pos = cnt;
	return 0;
	*/
}

static label_t *add_label(const char *str)
{
	label_t *ret = NULL;
	if ((ret = calloc(1, sizeof(label_t))) == NULL) {
		warn(NULL);
		return NULL;
	}
	int cnt=0;
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

static void free_jump(jump_t *j)
{
	if (j->unresolved) {
		free(j->unresolved);
		j->unresolved = NULL;
	}
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
		for (int i = 0; labels[i]; i++)
			if (!strncmp(labels[i]->name, ptr, tmp-ptr)) {
				j->to = labels[i];
				return i;
			}

	if ((j->unresolved = strndup(ptr, tmp-ptr)) == NULL)
		warn("parse_label");

	return -1;
}

static int find_label(const char *restrict str)
{
	if (labels)
		for (int i = 0; labels[i]; i++)
			if(!strcmp(str, labels[i]->name)) return i;

	return -1;
}

static FILE *parse_file(const char *ptr)
{
	return NULL;
}

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
	int flags = 0, nth = 0;
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

static command_t *parse_command(command_t *prev, const char *, char **);

static command_t *parse_block(const char *str)
{
	char *cur = (char *)str;
	char *next = NULL;
	command_t *head = NULL;
	command_t *c = NULL;

	//printf(" parse_block: '%s'\n", str);

	while (1)
	{
		c = parse_command(c, cur, &next);

		if (c == &error) return &error;
		if (c == &finished) break;

		cur = next;
		next = NULL;
		if (head == NULL) head = c;
	}

	return head;
}

static command_t *parse_command_block(const char *ptr, char **left)
{
	char *end = (char *)ptr;
	//printf("parse_command_block: '%s'\n", ptr);
	while (*end && (*end != '}' && *(end-1) != '\\')) {
		if (*end == '{') {
			warnx("nested command blocks are not supported");
			return NULL;
		}
		end++;
	}
	//printf(" terminal found @ %s\n", end);
	if (!*end) {
		warnx("unterminated command block");
		return NULL;
	}
	*left = end + 1;
	char *tmp = strndup(ptr, end-ptr);
	//printf("new block\n");
	command_t *blk = parse_block(tmp);
	free(tmp); tmp = NULL;
	if (blk == &error) return NULL;
	//printf("new block @ %p\n", blk);
	return blk;
}

static char *parse_function(command_t *restrict cmd, const char *ptr)
{
	char *seek = (char *)ptr;
	//printf("parse_function '%c':'%s'\n", cmd->function, ptr);

	switch(cmd->function)
	{
		case '{':
			// commands
			if((cmd->arg.block = parse_command_block(ptr, &seek)) == NULL) return NULL;
			break;
		case 'a':
		case 'i':
			// text
			if ((cmd->arg.text = strdup(ptr)) == NULL) {
				warnx("%c", cmd->function);
				return NULL;
			}
			break;
		case 'b':
		case 't':
			// label
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
			// FILE
			cmd->arg.file = parse_file(ptr);
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
		default:
			warnx("%c: unknown command", cmd->function);
	}
	return seek;
}


static command_t *parse_command(command_t *prev, const char *str, char **left)
{
	command_t *restrict ret = NULL;
	char *ptr = (char *)str;
	char *tmp = NULL;

	while (ptr && *ptr && (isblank(*ptr) || *ptr == ';')) ptr++;

	if (!ptr || !*ptr) return &finished;

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
			else if (*ptr == '/')
			{
				tmp = ptr++;
				regex_t **restrict rc = NULL;
				val = 0;

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

				if (addr == 0) {
					rc = &ret->one.addr.preg;
					ret->one.type = apreg;
					if (*(ptr+1) == ',') {
						ptr++;
						addr++;
					}
					else 
						addr = 3;
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
		if (addr == 3) {
			while (*ptr && isblank(*ptr)) ptr++;
			if (!*ptr) {
				goto fail2;
			}
			ret->function = *ptr++;
			while (*ptr && isblank(*ptr)) ptr++;
			*left = parse_function(ret, ptr);
			if (*left == NULL) {
				goto fail2;
			}
			goto done;
		}
		ptr++;
	}
	*left = ptr;

done:
	if(add_command(ret, prev) == -1) return NULL;
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

static void dump(const command_t *restrict c)
{
	if (!c) 
		return;

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
					!c->arg.jmp.to ? 0 : c->arg.jmp.to->cmd,
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
		default:
			printf("%c  :\n", c->function);
	}
	printf("\n");
}

static void free_addr(address_t *restrict a)
{
	if (!a) return;

	switch(a->type)
	{
		case apreg:
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

static void free_replace(char **replace)
{
	if (replace[0]) { free(replace[0]); replace[0] = NULL; }
	if (replace[1]) { free(replace[1]); replace[1] = NULL; }
	free(replace);
}

static void free_cmd(command_t *restrict c)
{
	if (c == NULL) return;
	if (c->addrs>0) { free_addr(&c->one); }
	if (c->addrs>1) { free_addr(&c->two); }
	switch(c->function)
	{
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

static void cleanup()
{
	if (labels) {
		for(int i = 0; labels[i]; i++) {
			free_label(labels[i]);
			labels[i] = NULL;
		}
		free(labels);
		labels = NULL;
	}
	
	command_t *tmp = root;
	while(tmp)
	{
		command_t *next = tmp->next;
		free_cmd(tmp);
		tmp = next;
	}
	/*
	if (head) {
		command_t *tmp;
		for(int i = 0; commands[i]; i++) {
			free_cmd(commands[i]);
			commands[i] = NULL;
		}
		free (commands);
		commands = NULL;
	}*/
}

static label_t *label_for_cmd(command_t *c)
{
	if (labels) for (int i = 0; labels[i]; i++)
		if (labels[i]->cmd == c) 
			return labels[i];

	return NULL;
}


static void fix_block(command_t *first)
{
	//printf("fix_block: starting on %p[%c]\n", first, first->function);
	for(command_t *cur = first; cur; cur=cur->next)
	{
		//printf(" fix_block: %p[%c]\n", cur, cur->function);
		if (cur->function == '{' && cur->arg.block)
		{
			//printf("  fix_block: checking {\n");
			//printf("  fix_block: recurse\n");
			fix_block(cur->arg.block);
			//printf("  fix_block: recurse done\n");
			for (command_t *p = cur->arg.block; p; p=p->next) {
				if (p->next == NULL) {
					p->next = cur->next;
					//printf("  fix_block: setting block tail.next [%p] to %p\n", p, cur->next);
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
	//printf("fix_block: finished\n");
}

int main(const int argc, char *argv[])
{

	if (argc == 1) exit(EXIT_FAILURE);
	atexit(cleanup);

	char *cur = argv[1];
	char *next = NULL;
	command_t *c = NULL;

	while (c != &finished) 
	{
		c = parse_command(c, cur, &next);

		if (c == &error) exit(EXIT_FAILURE);
		if (c == &finished) break;

		if (root == NULL) root = c;

		cur = next;
		next = NULL;
	}

	/*
	for(int i = 0; commands[i]; i++) 
	{
		switch (commands[i]->function)
		{
			case 'b':
				if (!commands[i]->arg.jmp.to && commands[i]->arg.jmp.unresolved) {
					const int id = find_label(commands[i]->arg.jmp.unresolved);
					commands[i]->arg.jmp.to = labels[id];
					free(commands[i]->arg.jmp.unresolved);
					commands[i]->arg.jmp.unresolved = NULL;
				}
				break;
		}
	}
	*/

	/*
	if (labels) for(int i = 0; labels[i]; i++)
	{
		if (labels[i]->target)
			labels[i]->id = labels[i]->target->pos;
	}
	*/

	

	fix_block(root);
	
	const label_t *restrict l;
	for(command_t *c = root; c;) {
		if ((l = label_for_cmd(c)) != NULL)
		{
			printf(":%s\n", l->name);
		}
		
		dump(c);
		if (c->function == '{' && c->arg.block)
			c = c->arg.block;
		else
			c = c->next;
	}

	exit(EXIT_SUCCESS);
}
