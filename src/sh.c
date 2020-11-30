#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <err.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

#include "sh.h"
#include "sh.y.tab.h"

/* preprocessor defines */

/* for shenv_t */
#define	MAX_TRAP	15
#define	MAX_OPTS	10
#define NUM_FDS		10

/* for list_t */
#define LIST_AND	0
#define	LIST_OR		1

#define QUOTE_BASE		(1<<0)
#define QUOTE_SPECIAL	(1<<1)
#define QUOTE_DOUBLE	(1<<2)

/* types, structures & unions */
typedef int (*builtin_t)(int, char *[]);

struct builtin {
	const char *name;
	const builtin_t func;
	const int special;
	const int fork;
};

typedef struct {
	char	**argv;
	int		argc;
	int		type;
	int		pipe[2];
} list_t;

typedef struct {
	char		*name;
	char		*val;
	int			 exported;
	int			 readonly;
	int			 freed;
} env_t;

typedef struct sh_exec_env {
	struct sh_exec_env *parent;
	
	char	 *name;
	int		  fds		[NUM_FDS];		/* fds from the parent that will be dup'd to the child */
	mode_t	  umask;
	void	 *traps		[MAX_TRAP + 1];
	int		  options	[MAX_OPTS + 1];
	void	 *functions;
	pid_t	**last_cmds;
	void	 *aliases;
	env_t	**private_envs;
	list_t	**sh_list;
	char	**argv;
	int		argc;
} shenv_t;


typedef struct {
	const char  *str;
	const int    tok;
} map_t;

static const map_t lookup[] = {
    {"&&",      AND_IF},
    {"||",      OR_IF},
    {";;",      DSEMI},
    {"<<-",     DLESSDASH},
    {">|",      CLOBBER},
    {"<<",      DLESS},
    {">>",      DGREAT},
    {"<&",      LESSAND},
    {">&",      GREATAND},
    {"<>",      LESSGREAT},
    {"<",       '<'},
    {">",       '>'},

    {"if",      If},
    {"then",    Then},
    {"else",    Else},
    {"elif",    Elif},
    {"fi",      Fi},
    {"do",      Do},
    {"done",    Done},
    {"case",    Case},
    {"esac",    Esac},
    {"while",   While},
    {"until",   Until},
    {"for",     For},
    {"in",      In},

    {"{",       Lbrace},
    {"}",       Rbrace},

    {"&",       '&'},
    {"(",       '('},
    {")",       ')'},
    {";",       ';'},
    {"|",       '|'},
    {"\n",      NEWLINE},

    {NULL,      0}

    //{"!",     Bang},
};

/* forward declarations */

static int cmd_umask(int, char *[]);
static int cmd_basename(int, char *[]);
static int cmd_cd(int, char *[]);
static int cmd_exec(int, char *[]);
static int cmd_umask(int, char *[]);
static int cmd_read(int, char *[]);
static int cmd_set(int, char *[]);
static int cmd_pwd(int, char *[]);
static int cmd_exit(int, char *[]);
static bool get_next_parser_string(int);

/* constants */

static const struct builtin builtins[] = {

	{"basename",	cmd_basename,	0, 1},
	{"cd",			cmd_cd,			0, 0},
	{"exec",		cmd_exec,		1, 0},
	{"exit",		cmd_exit,		1, 0},
	{"pwd",			cmd_pwd,		0, 1},
	{"umask",		cmd_umask,		0, 0},
	{"read",		cmd_read,		0, 0},
	{"set",			cmd_set,		0, 1},

	{NULL, NULL, 0, 0}
};

static const map_t word				= { "WORD",				WORD };
static const map_t io_number		= { "IO_NUMBER",		IO_NUMBER };
static const map_t assignment_word	= { "ASSIGNMENT_WORD",	ASSIGNMENT_WORD };
static const map_t name				= { "NAME",				NAME };

static const char *reg_assignment_str	= "^[a-zA-Z]([a-zA-Z0-9_]+)?=[^ \t\n]+";    // FIXME doesn't handle A="a a"
static const char *reg_name_str			= "^[a-zA-Z]([a-zA-Z0-9_]+)?$";
static const char *pad_str				= "";

/* local variables */

static regex_t reg_assignment;
static regex_t reg_name;

static const map_t *prev	= NULL;
static char *left			= NULL;
static int yyline			= 0;
static int yyrow			= 0;

static char *here_doc_delim		= NULL;
static char *here_doc_word		= NULL;
static char *here_doc_remaining = NULL;
static char *free_me			= NULL;
static bool here_doc			= false;
static bool free_left			= false;
static bool in_case				= false;
static bool in_case_in			= false;
static int	in_do				= 0;
static bool in_for_in			= false;
static int	in_for				= 0;
static bool in_brace			= false;
static bool in_func				= false;
static int	func_brace			= 0;
static int	in_if				= 0;

static shenv_t *cur_sh_env = NULL;
static char *parser_string = NULL;

/* enviromental ones */
static int opt_allexport = 0;
static int opt_notify = 0;
static int opt_noclobber = 0;
static int opt_errexit = 0;
static int opt_noglob = 0;
static int opt_monitor = 0;
static int opt_noexec = 0;
static int opt_nounset = 0;
static int opt_verbose = 0;
static int opt_xtrace = 0;



/* external variables */

extern YYSTYPE yylval;
extern char **environ;


/* local function defintions */

/* check if a NULL terminated string is a valid sequence of digits */
inline static int isnumber(const char *str)
{
	while(*str)
	{
		if(!isdigit(*str++)) return 0;
	}

	return 1;
}

static char **push(char *** list, char * ptr)
{
	size_t cnt = 0;
	//printf("push with %p[%p], %s\n", list, *list, ptr);

	if (*list == NULL) {
		*list = malloc(2 * sizeof(char *));
		//printf("cnt is %lu\n", cnt);
	} else {
		for (;(*list)[cnt];cnt++) ;
		//printf("cnt is %lu\n", cnt);
		char **tmp = realloc(*list, (cnt + 2) * sizeof(char *));
		if (!tmp) { 
			warn("push");
			return NULL;
		}
		*list = tmp;
	}
	(*list)[cnt] = ptr;
	(*list)[cnt+1] = NULL;

	return *list;
}

/*
static void farray(char **list)
{
	for(size_t i = 0; list && list[i]; i++)
	{
		free(list[i]);
		list[i] = NULL;
	}
	free(list);
}
*/

static env_t *getshenv(shenv_t *sh, char *name)
{
	errno = 0;
	if (!sh->private_envs) 
		return NULL;

	env_t *ret = NULL;
	for (int i = 0; (ret = sh->private_envs[i]); i++)
		if (!strcmp(name, ret->name)) 
			break;

	return ret;
}

static void exportenv(env_t *env)
{
	if (env->exported) return;
	env->exported = 1;
	setenv(env->name, env->val, 1);
}

static env_t *setshenv(shenv_t *restrict sh, char *restrict name, const char *restrict value)
{
	errno = 0;
	int cnt = 0;

	env_t *ret = getshenv(sh, name);

	if (!ret)
	{
		if ((ret = calloc(1, sizeof(env_t))) == NULL)
			return NULL;

		for (cnt = 0; sh->private_envs[cnt]; cnt++) ;
		env_t **tmp = realloc(sh->private_envs, sizeof(env_t *) * (cnt+2));
		if (tmp == NULL) {
			free(ret);
			ret = NULL;
			return NULL;
		}

		if ((ret->name = strdup(name)) == NULL) {
			free(tmp);
			free(ret);
			tmp = NULL;
			ret = NULL;
			return NULL;
		}

		sh->private_envs = tmp;
		sh->private_envs[cnt++] = ret;
		sh->private_envs[cnt] = NULL;
	}

	if (ret->readonly) {
		warnx("%s: readonly variable", ret->name);
		return ret;
	}

	if (ret->val) {
		free(ret->val);
		ret->val = NULL;
	}
	ret->val = value ? strdup(value) : NULL;

	if (ret->exported) {
		if (ret->val) {
			setenv(ret->name, ret->val, 1);
		} else {
			unsetenv(ret->name);
		}
	}

	return ret;
}


static const char *node_type(const enum node_en type)
{
	switch(type)
	{
		case N_NONE             : return "!!N_NONE!!";
		case N_SUBSHELL         : return "n_subshell";
		case N_OP               : return "n_op";
		case N_IF               : return "n_if";
		case N_ASSIGN           : return "n_assign";
		case N_SIMPLE           : return "n_simple";
		case N_IOREDIRECT       : return "n_ioredirect";
		case N_STRING           : return "n_string";
		case N_FUNC             : return "n_func";
		case N_CASE             : return "n_case";
		case N_CASEITEM         : return "n_caseitem";
		case N_WHILE            : return "n_while";
		case N_COMPOUND_COMMAND : return "n_compound";
		case N_FOR              : return "n_for";
		case N_UNTIL            : return "n_until";
		case N_PATTERN          : return "n_pattern";
	}
	return "!!UNKNOWN!!";
}

inline static int min(const int a, const int b)
{
	return (a < b) ? (a) : (b);
}

static const char *token(int t)
{
	static char tokbuf[2];

	if (t < 256) {
		snprintf(tokbuf, 2, "%c", isprint(t) ? t : '_');
		return tokbuf;
	}

	for (int i = 0; lookup[i].str; i++)
	{
		if (lookup[i].tok == t) return lookup[i].str;
	}

	return "!!UNKNOWN!!";
}

/* umask builtin */
static int cmd_umask(int argc, char *argv[])
{
	const char *usage = "Usage: umask [-S] [mask]";

	int opt_symbolic = 0;
	int mask = -1;
	{
		int opt;
		while ((opt = getopt(argc, argv, "S")) != -1)
		{
			switch (opt)
			{
				case 'S':
					opt_symbolic = 1;
					break;
				default:
					warnx(usage);
					return EXIT_FAILURE;
			}
		}
		if (argc - optind > 1) {
			warnx(usage);
			return EXIT_FAILURE;
		}
		if (argc - optind == 1) {
			if (isnumber(argv[optind])) {
				mask = atoi(argv[optind]);
			} else {
				warnx("mask (%s) is not numerical", argv[optind]);
				return EXIT_FAILURE;
			}
		}
	}

	if (mask == -1) {
		mode_t um = umask(0);
		umask(um);
		if (opt_symbolic)
			printf("symbolic not supported\n");
		printf("%04u\n", um);
	} else
		umask(mask);

	return EXIT_SUCCESS;
}

static int cmd_pwd(int argc, char *argv[])
{
	char pwd[BUFSIZ];
	if (getcwd(pwd, BUFSIZ) == NULL)
		err(EXIT_FAILURE, NULL);
	printf("%s\n", pwd);
	exit(EXIT_SUCCESS);
}

static int cmd_cd(int argc, char *argv[])
{
	char *dir = NULL;

	if(argc == 1) {
		dir = getenv("HOME");
	} else {
		dir = argv[1];
	}

	if(dir == NULL) {
		fprintf(stderr, "%s: HOME not set\n", argv[0]);
		return EXIT_FAILURE;
	}

	int hyphen = 0;
	if (argc > 1) {
		if((hyphen = !strcmp("-", argv[1]))) {
			dir = getenv("OLDPWD");
			if(dir == NULL) {
				fprintf(stderr, "%s: OLDPWD not set\n", argv[0]);
				return EXIT_FAILURE; 
			}
		}
	}

	char oldpwd[BUFSIZ]; 
	if (getcwd(oldpwd, BUFSIZ) == NULL) {
		warnx(NULL);
		return EXIT_FAILURE;
	}

	if(chdir(dir) == -1) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], dir, strerror(errno));
		return EXIT_FAILURE;
	}

	char pwd[BUFSIZ];
	if (getcwd(pwd, BUFSIZ) == NULL) {
		warnx(NULL);
		return EXIT_FAILURE;
	}

	setenv("OLDPWD", oldpwd, 1);
	setenv("PWD", pwd, 1);

	if(hyphen)
		printf("%s\n", pwd);

	return EXIT_SUCCESS;
}

inline static int isifsspace(const char c, const char *restrict IFS)
{
	if (!isspace(c)) 
		return 0;
	if (strchr(IFS, c)) 
		return 1;
	return 0;
}

static int cmd_read(int argc, char *argv[])
{
	int opt_raw = 0;

	{
		int opt = 0;

		while ((opt = getopt(argc, argv, "r")) != -1)
		{
			switch (opt)
			{
				case 'r':
					opt_raw = 1;
					break;
				default:
					fprintf(stderr,
							"Usage: read [-r] var...\n");
					return(EXIT_FAILURE);
			}
		}
	}

	const char *IFS = getenv("IFS");

	if (IFS == NULL) 
		IFS=" \r\n"; // FIXME correct? or strlen==0 then set IFS?

	char buf[BUFSIZ];
	char *restrict line = NULL;
	int len = 0;

	line = fgets(buf, BUFSIZ, stdin);

	if (line == NULL || !strlen(line) )
		return EXIT_SUCCESS;

	len = strlen(line);

	if (!opt_raw && len >= 2 && buf[len-2] == '\\' && buf[len-1] == '\n') 
	{
		while (buf[len-2] == '\\' && buf[len-1] == '\n' && len < BUFSIZ)
		{
			buf[len-2] = IFS[0];
			line = &buf[len-1];
			line = fgets(line, BUFSIZ-len, stdin);
			len = strlen(buf);

			if (line == NULL)
				break;
		}
	}

	int pos = 0;
	int start = 0;
	int arg = 0;
	char *last = NULL;
	char delim = 0;
	char *str = NULL;
	const int numargs = argc - optind;

	while(pos <= len)
	{
		if (pos == len)
			break;

		while(pos <= len && isifsspace(buf[pos], IFS)) 
			pos++;

		if (pos == len) 
			break;

		start = pos;
		delim = (pos > 0) ? ' ' : buf[pos-1];

		while(pos <= len && !isifsspace(buf[pos], IFS)) 
			pos++;

		if ((str = strndup(buf+start, pos-start)) == NULL) {
			warn(NULL);

			if (last) {
				free(last);
				last = NULL;
			}
			
			return(EXIT_FAILURE);
		}

		if (numargs && arg < numargs-1) 
		{
			if (setenv(argv[optind+arg], str, 1) == -1) {
				warn(NULL);

				if (str) {
					free(str);
					str = NULL;
				}

				if (last) {
					free(last);
					last = NULL;
				}
				
				return(EXIT_FAILURE);
			}
		} 
		else if (numargs) 
		{
			const int oldlen = (last != NULL) ? strlen(last) : 0;
			const int newlen = oldlen + strlen(str);

			if (last) 
			{
				char *tmp = realloc(last, newlen+2);

				if (tmp == NULL) {
					warn(NULL);

					free(last);
					last = NULL;

					if (str) {
						free(str);
						str = NULL;
					}

					return(EXIT_FAILURE);
				}

				last = tmp;
				last[oldlen] = delim;
				last[oldlen+1] = '\0';
			} 
			else 
			{
				last = calloc(1, newlen + 2);

				if (last == NULL) {
					warn(NULL);
					if (str) {
						free(str);
						str = NULL;
					}
					return(EXIT_FAILURE);
				}
				strcat(last, str);
			}
		}

		if (str) {
			free(str);
			str = NULL;
		}

		arg++;
	}

	if (last && numargs) {
		if (setenv(argv[numargs], last, 1) == -1) {
			warn(NULL);
		}
		free(last);
		last = NULL;
	}

	if (ferror(stdin) || feof(stdin))
		return(EXIT_FAILURE);

	return(EXIT_SUCCESS);
}

static int cmd_basename(int argc, char *argv[])
{
	//char *suffix = NULL;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage: %s string [suffix]\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	//if (argc == 3)
	//	suffix = argv[2];

	char *bname = basename(argv[1]);

	// TODO implement suffix here

	printf("%s\n", bname);
	exit(EXIT_SUCCESS);
}

static int cmd_exit(const int ac, char *av[])
{
	int val = 0;
	if (ac == 2)
		val = atoi(av[1]);
	exit(val);
}

/* replace the current process with another, effectively implements exec */
static int cmd_exec(const int ac, char *av[])
{
	warnx("exec: not implemented");
	return EXIT_FAILURE;

	execvp(av[1], av);
	warn(av[1]);
	return EXIT_FAILURE;
}

static int parse_set(char mod, char opt)
{
	int add = mod == '+' ? 1 : 0;

	switch (opt)
	{
		case 'a':
			opt_allexport = add;
			break;
		case 'b':
			opt_notify = add;
			break;
		case 'C':
			opt_noclobber = add;
			break;
		case 'e':
			opt_errexit = add;
			break;
		case 'f':
			opt_noglob = add;
			break;
		case 'm':
			opt_monitor = add;
			break;
		case 'n':
			opt_noexec = add;
			break;
		case 'u':
			opt_nounset = add;
			break;
		case 'v':
			opt_verbose = add;
			break;
		case 'x':
			opt_xtrace = add;
			break;
		default:
			warnx("%c: unknown set option", opt);
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

typedef struct option_map {
	const char *name;
	const char opt;
} optmap_t;

static const optmap_t optmap[] = {
	{"allexport",	'a'},
	{"notify",		'b'},
	{"noclobber",	'C'},
	{"errexit",		'e'},
	{"noglob",		'f'},
	{"monitor",		'm'},
	{"noexec",		'n'},
	{"nounset",		'u'},
	{"verbose",		'v'},
	{"xtrace",		'x'},

	{NULL, 0}
};

static int parse_set_option(char *opt)
{
	return EXIT_SUCCESS;
}

static void dump_envs(const shenv_t *restrict sh)
{
	const env_t *e = NULL;

	for (size_t i = 0; (e = sh->private_envs[i]); i++)
	{
		printf("%s=%s", e->name, e->val ? e->val : "");
		if (e->readonly) printf(" (ro)");
		if (e->exported) printf(" (expt)");
		fputc('\n', stdout);
	}
}

static int cmd_set(const int ac, char *av[])
{
	int opt_show_vars = (ac == 1);
	int opt_show_options = 0;

	{
		int opt;
		while ((opt = getopt(ac, av, "abCefhmnuvxo")) != -1)
		{
			switch (opt)
			{
				case 'a':
				case 'b':
				case 'C':
				case 'e':
				case 'f':
				case 'm':
				case 'n':
				case 'u':
				case 'v':
				case 'x':
					if (parse_set('-',opt))
						return EXIT_FAILURE;
					break;
				case 'o':
					if (optind+1 == ac || *av[optind+1] == '-')
					{
						opt_show_options = 1;
						break;
					}

					if (parse_set_option(av[optind++]))
						return EXIT_FAILURE;
					break;
			}
		}
	}

	if (opt_show_vars) {
		dump_envs(cur_sh_env);
		return EXIT_SUCCESS;
	}

	if (opt_show_options) {
		;
	}

	return EXIT_SUCCESS;
}



void print_node(const node *n, int pad, int print_next)
{
	if (!pad) printf("\n");
	printf("%*s%s:", pad, pad_str, node_type(n->type));
	if (n->sep)
		printf(" *SEP[%c]*:", n->sep != '\n' ? n->sep : ' ');
	switch (n->type)
	{
		case N_STRING:
		case N_ASSIGN:
		case N_PATTERN:
			printf(" [%s]\n", n->value);
			break;
		case N_COMPOUND_COMMAND:
			printf("\n");
			if (n->arg0) {
				printf("%*scmd:", pad+1, pad_str);
				print_node(n->arg0, pad+1, 1);
			}
			break;
		case N_IOREDIRECT:
			printf(" [%d][%s][%s]\n", n->num, token(n->token), n->value);
			break;
		case N_FUNC:
			printf(" [%s]\n", n->value);
			if (n->arg0) {
				print_node(n->arg0, pad+1, 1);
			}
			break;
		case N_OP:
			printf(" [%s]\n", token(n->token));
			if (n->arg0) {
				printf("%*sLHS:", pad+1, pad_str);
				print_node(n->arg0, pad+1, 1);
			}
			if (n->arg1) {
				printf("%*sRHS:", pad+1, pad_str);
				print_node(n->arg1, pad+1, 1);
			}
			break;
		case N_CASE:
			printf(" [%s]\n", n->value);
			if (n->arg0) {
				printf("%*sitems:", pad+1, pad_str);
				print_node(n->arg0, pad+1, 1);
			}
			break;
		case N_CASEITEM:
			printf(" \n");
			if (n->arg0) {
				printf("%*scmd:", pad+1, pad_str);
				print_node(n->arg0, pad+1, 1);
			}
			break;
		case N_SIMPLE:
			printf("\n");
			if (n->arg0) {
				printf("%*spre:", pad+1, pad_str);
				print_node(n->arg0, pad+1, 1);
			}
			if (n->arg1) {
				printf("%*scmd:", pad+1, pad_str);
				print_node(n->arg1, pad+1, 1);
			}
			if (n->arg2) {
				printf("%*ssuf:", pad+1, pad_str);
				print_node(n->arg2, pad+1, 1);
			}
			break;
		case N_IF:
			printf("\n");
			if (n->arg0) {
				printf("%*sstmt:", pad+1, pad_str);
				print_node(n->arg0, pad+1, 1);
			}
			if (n->arg1) {
				printf("%*strue:", pad+1, pad_str);
				print_node(n->arg1, pad+1, 1);
			}
			if (n->arg2) {
				printf("%*sfalse:", pad+1, pad_str);
				print_node(n->arg2, pad+1, 1);
			}
			break;
		default:
			printf("\n");
			break;
	}
	node *tmp; int cnt;
	if(print_next)
	for (tmp = n->next, cnt = 0; tmp; tmp = tmp->next, cnt++)
	{
		printf("%*s [%d]:", pad,pad_str,cnt);
		print_node(tmp, pad+2, 0);
	}
}

static char *parseenv(const char *restrict val, int *restrict rc)
{
	const char *ptr = val;
	const char *start = NULL;
	char buf[BUFSIZ];
	char *env = NULL, *ret = NULL;
	env_t *genv = NULL;

	int opt_default = 0, opt_assign_def = 0, opt_err = 0, opt_alternate = 0;
	int opt_colon = 0;
	int opt_length = 0;
	int opt_rem_l_suf = 0, opt_rem_s_suf = 0;
	int opt_rem_l_pre = 0, opt_rem_s_pre = 0;

	if (*ptr == '#') { opt_length = 1; }
	
	start = ptr;
	while (*ptr && (isalnum(*ptr) || *ptr == '_')) ptr++;
	env = strndup(start, ptr - start);
	
	if(!strncmp(ptr, ":-", 2)) { opt_colon=1; opt_default=1; ptr+=2; }
	else if(!strncmp(ptr, ":=", 2)) { opt_colon=1; opt_assign_def=1; ptr+=2; }
	else if(!strncmp(ptr, ":?", 2)) { opt_colon=1; opt_err=1; ptr+=2; }
	else if(!strncmp(ptr, ":+", 2)) { opt_colon=1; opt_alternate=1; ptr+=2; }
	else if(!strncmp(ptr, "%%", 2)) { opt_rem_l_suf=1; ptr+=2; }
	else if(!strncmp(ptr, "##", 2)) { opt_rem_l_pre=1; ptr+=2; }
	else if(*ptr == '-') { opt_default=1; ptr++; }
	else if(*ptr == '=') { opt_assign_def=1; ptr++; }
	else if(*ptr == '?') { opt_err=1; ptr++; }
	else if(*ptr == '+') { opt_alternate=1; ptr++; }
	else if(*ptr == '%') { opt_rem_s_suf=1; ptr++; }
	else if(*ptr == '#') { opt_rem_l_pre=1; ptr++; }

	start = ptr;

	genv = getshenv(cur_sh_env, env);
	size_t genv_len = genv ? strlen(genv->val) : 0;

	if (opt_length) {
		if (!genv) goto fail;
		snprintf(buf, BUFSIZ, "%lu", strlen(genv->val));
		ret = strdup(buf);
	} else if (opt_rem_l_suf + opt_rem_l_pre + opt_rem_s_pre + opt_rem_s_suf) {
		// TODO
	} else if (opt_alternate) {
		/* :+ or : */
		if (genv && genv_len) ret = strdup(start);
		else if(genv) {
			/* set but null */
			if (opt_colon) ret = strdup("");
			else ret = strdup(start);
		} else {
			/* unset */
			ret = strdup("");
		}
	} else if (genv && strlen(genv->val)) {
		/* set and not null */
		ret = strdup(genv->val);
	} else if (opt_default) {
		/* :- or - */
		if (!genv || opt_colon) ret = strdup(start);
		else ret = strdup("");
	} else if (opt_assign_def) {
		/* := or = */
		if (!genv || opt_colon) {
			setshenv(cur_sh_env, env, start);
			ret = strdup(start);
		} else ret = strdup("");
	} else if (opt_err) {
		/* :? or ? */
		if (!genv || opt_colon) {
			warnx("%s: parameter null or not set", env);
			*rc = 1;
			goto fail;
		} else
			ret = strdup("");
	} else {
		err(EXIT_FAILURE, "reached a place we shouldn't get to");
	}

//done:
	if (env) { free(env); env = NULL; }
	return ret;
fail:
	if (env) { free(env); env = NULL; }
	return NULL;
}

char *expand(const char *restrict str, int *rc)
{
	char buf[BUFSIZ] = {0};
	char var[BUFSIZ] = {0};
	const char *src = str;
	char *dst = buf, *tmp = NULL, *val = NULL;
	bool in_double_quotes = false;
	size_t len = 0;
	env_t *env = NULL;

	//printf("expand: %s\n", str);

	if (!src) return NULL;

	while (*src)
	{
		char next = *(src+1);

		if (*src == '$') {
			if (next == '(' && *(src+2) == '(') {
				src+=3;
			} else if (next == '(') {
				src+=2;
			} else if (next == '{') {
				src+=2; tmp = var;
				while(*src && *src != '}') *tmp++ = *src++;
				*tmp = '\0';
				val = parseenv(var, rc);
				if (val && *val) {
					dst += (len = min(BUFSIZ - strlen(buf), strlen(val)));
					strncat(buf, val, len);
					free(val); val = NULL;
				}
				src++;
				continue;
			} else if (isalpha(next)) {
				src++; tmp = var;
				while(*src && !isblank(*src)) *tmp++ = *src++;
				*tmp = '\0';
				env = getshenv(cur_sh_env, var);
				if (env && *(env->val)) {
					dst += (len = min(BUFSIZ - strlen(buf), strlen(env->val)));
					strncat(buf, env->val, len);
				}
				continue;
			} else if (isdigit(next)) {
				src+=2;
			} else if (next == '?') {
				dst += snprintf(buf, BUFSIZ, "%u", *rc);
				src+=2;
			} else if (next == '*') {
				src+=2;
			} else if (next == '@') {
				src+=2;
			} else if (next == '-') {
				src+=2;
			} else if (next == '$') {
				dst += snprintf(buf, BUFSIZ, "%u", getpid());
				src+=2;
			} else if (next == '!') {
				src+=2;
			} else if (next == '#') {
				src+=2;
			}
		} else if(*src == '"') {
			in_double_quotes = !in_double_quotes;
			src++; 
			continue;
		} else if(!in_double_quotes && *src == '\'') {
			src++;
			while (*src && *src != '\'') *dst++ = *src++;
			src++;
			continue;
		} else if(*src == '\\') {
			src++;
		} else if(*src == '`') {
			src++;
			// FIXME mirror $( )
		} else
			*dst++ = *src++;
	}

	*dst = '\0';

	return strdup(buf);
}


int evaluate(node *n, int pad, int do_next)
{
	debug_printf("%*sEVAL: [%s]", pad, pad_str, node_type(n->type));
	node *tmp;
	int rc = 0;
	switch(n->type)
	{
		case N_FUNC:
			debug_printf(": name=%s\n", n->value);
			if (n->arg0) {
				debug_printf("%*sbody:\n", pad+1, pad_str);
				evaluate(n->arg0, pad+2, 1);
			}
			rc = true;
			break;
		case N_CASE:
			// TODO
			break;
		case N_STRING:
			rc = 0;
			n->evaluated = expand(n->value, &rc);
			debug_printf(": %s => %s\n", n->value, n->evaluated);
			break;
		case N_ASSIGN:
			debug_printf(": %s = ", n->value);
			if (n->arg0) {
				n->arg0->evaluated = expand(n->arg0->value, &rc);
				setshenv(cur_sh_env, n->value, n->arg0->evaluated);
			} else
				setshenv(cur_sh_env, n->value, "");
			break;
		case N_COMPOUND_COMMAND:
			debug_printf(":\n");
			if (n->arg0)
				rc = evaluate(n->arg0, pad+1, do_next);
			break;
		case N_IF:
			debug_printf("\n");
			if (n->arg0) {
				debug_printf("%*sif  :\n", pad+1, pad_str);
				rc = evaluate(n->arg0, pad+2, 1);
			}
			if (rc && n->arg1) {
				debug_printf("%*sthen:\n", pad+1, pad_str);
				evaluate(n->arg1, pad+2, 1);
			}
			if (!rc && n->arg2) {
				debug_printf("%*selse:\n", pad+1, pad_str);
				evaluate(n->arg2, pad+2, 1);
			}
			break;
		case N_SIMPLE:
			debug_printf("\n");

			if (n->arg0) {
				debug_printf("%*spre:\n", pad+1, pad_str);
				evaluate(n->arg0, pad+2, 1); 
			}

			if (n->arg1) {
				char **tmpargs = NULL;
				int tmpargc = 1;
				debug_printf("%*scmd:", pad+1, pad_str);
				
#ifdef NDEBUG
				print_node(n->arg1, pad+1, 1);
#endif
				if (n->arg1->type == N_STRING)
					n->arg1->evaluated = expand(n->arg1->value, &rc);
				else
					err(EXIT_FAILURE, "N_SIMPLE");
				push(&tmpargs, n->arg1->evaluated);

				if (n->arg2) {
					for (tmp = n->arg2; tmp; tmp=tmp->next)
					{
						if (tmp->type != N_STRING)
							continue;
						if (!push(&tmpargs, (tmp->evaluated = expand(tmp->value, &rc)))) {
							rc = 1;
							break;
						}
					}
					for (size_t i = 0; tmpargs && tmpargs[i]; i++, tmpargc++) {
						debug_printf("%*sarg[%lu]=%s\n", pad+2, pad_str, i, tmpargs[i]);
					}
				}

				// check for builtins etc FIXME
				
				const struct builtin *bi = NULL;
				for (size_t i = 0; (bi = &builtins[i])->name; i++)
					if (!strcmp(tmpargs[0], bi->name))
						break;

				pid_t chd_pid;
				
				if ( (bi->name && bi->fork) || !bi->name)
					chd_pid = fork();
				else
					chd_pid = 0;

				if (chd_pid == 0) {
					int rc;
					if (bi->name)
						rc = bi->func(tmpargc, tmpargs);
					else {
						rc = execvp(n->arg1->evaluated, tmpargs);
						if (rc == -1) err(EXIT_FAILURE, "execvp: %s", n->arg1->evaluated);
					}
				} else if (chd_pid == -1) {
					warn("execvp");
					rc = 1;
				} else {
					rc = 1;
					int res = 0;
					waitpid(chd_pid, &res, 0);
					if (WIFEXITED(rc)) rc = WEXITSTATUS(res);
				}

				if (tmpargs) {
					/* tmpargs contains n->evaluated pointers, freeNode() handles these */
					free(tmpargs);
					tmpargs = NULL;
				}
			}

			break;

		default:
			debug_printf("\n");
			break;
	}

	if (n->next && do_next)
		return evaluate(n->next, pad, 1);
	else
		return rc;
}

static node *newNode(const enum node_en type)
{
	node *ret = NULL;
	if ((ret = calloc(1, sizeof(node))) == NULL)
		err(EXIT_FAILURE, "newNode");

	ret->type = type;
	return ret;
}

void freeNode(node *restrict node, const bool free_next)
{
	if (!node) return;

	if(node->arg0)  { freeNode(node->arg0, true);	node->arg0 = NULL;		}
	if(node->arg1)  { freeNode(node->arg1, true);	node->arg1 = NULL;		}
	if(node->arg2)  { freeNode(node->arg2, true);	node->arg2 = NULL;		}
	if(node->arg3)  { freeNode(node->arg3, true);	node->arg3 = NULL;		}
	if(node->value) { free(node->value);			node->value = NULL;		}
	if(node->evaluated) { free(node->evaluated);	node->evaluated = NULL; }

	if(free_next && node->next) { freeNode(node->next, true); node->next = NULL; }
	
	free(node);
}

node *nCaseItem(node *restrict pattern, node *restrict compound_list)
{
	node *ret = newNode(N_CASEITEM);
	ret->arg0 = pattern;
	ret->arg1 = compound_list;
	return ret;
}

node *nFunc(char *restrict fname, node *restrict body)
{
	node *ret = newNode(N_FUNC);
	ret->value = strdup(fname);
	ret->arg0 = body;
	return ret;
}

node *nAssign(char *restrict str)
{
	node *ret = newNode(N_ASSIGN);
	char *tok = strchr(str, '=');
	ret->value = strndup(str, tok - str);
	ret->arg0 = nString(tok + 1);
	return ret;
}

node *nFor(char *restrict name, char *restrict wordlist, node *restrict do_group)
{
	node *ret = newNode(N_FOR);
	ret->value = strdup(name);
	if (wordlist)
		ret->arg0 = nString(wordlist);
	ret->arg1 = do_group;
	return ret;
}

node *nWhile(node *restrict compound_list, node *restrict do_group)
{
	node *ret = newNode(N_WHILE);
	ret->arg0 = compound_list;
	ret->arg1 = do_group;
	return ret;
}

node *nUntil(node *restrict compound_list, node *restrict do_group)
{
	node *ret = newNode(N_UNTIL);
	ret->arg0 = compound_list;
	ret->arg1 = do_group;
	return ret;
}

node *nCompound(node *restrict command, node *restrict redirect)
{
	node *ret = newNode(N_COMPOUND_COMMAND);
	ret->arg0 = command;
	ret->arg1 = redirect;
	return ret;
}

node *nodeAppend(node *restrict item, node *restrict to)
{
	node *tmp = to;
	for(; tmp->next; tmp=tmp->next) ;
	//printf(" appending %p[%s] to %p[%s]\n", item, node_type(item->type), tmp, node_type(tmp->type));
	tmp->next = item;
	return to;
}

node *nIf(node *restrict ifstmt, node *restrict iftrue, node *restrict iffalse)
{
	node *ret = newNode(N_IF);
	ret->arg0 = ifstmt;
	ret->arg1 = iftrue;
	ret->arg2 = iffalse;
	return ret;
}

node *nSimple(node *restrict pre, node *restrict cmd, node *restrict suf)
{
	node *ret = newNode(N_SIMPLE);
	ret->arg0 = pre;
	ret->arg1 = cmd;
	ret->arg2 = suf;
	//printf(" creating nSimple(%s,%s,%s)\n",
	//		pre ? node_type(pre->type) : "",
	//		cmd ? node_type(cmd->type) : "",
	//		suf ? node_type(suf->type) : ""
	//		);
	return ret;
}

node *nCase(char *restrict word, node *restrict case_list)
{
	node *ret = newNode(N_CASE);
	ret->value = strdup(word);
	ret->arg0 = case_list;
	return ret;
}

node *nIoRedirect(int func, char *restrict iofile)
{
	// FIXME
	node *ret = newNode(N_IOREDIRECT);
	ret->token = func;
	switch(func)
	{
		case '>':
		case DGREAT:
		case GREATAND:
		case CLOBBER:
			ret->num = 1;
			break;
		case LESSAND:
		case '<':
		case DLESS:
			ret->num = 0;
			break;
		default:
			errx(EXIT_FAILURE, "nIoRedirect: unknown %d", func);

	}
	ret->value = strdup(iofile);
	return ret;
}

node *nPattern(char *restrict str)
{
	node *ret = newNode(N_PATTERN);
	ret->value = strdup(str);
	return ret;
}

node *nString(char *restrict str)
{
	node *ret = newNode(N_STRING);
	ret->value = strdup(str);
	return ret;
}

node *nOp(int op, node *restrict lhs, node *restrict rhs)
{
	node *ret = newNode(N_OP);
	ret->token = op;
	ret->arg0 = lhs;
	ret->arg1 = rhs;
	return ret;
}

node *nSubshell(node *restrict body)
{
	node *ret = newNode(N_SUBSHELL);
	ret->arg0 = body;
	return ret;
}

static bool isquote(const char chr, const int type)
{
	static const char base[]	= "|&;<>()$`\\\"' \t\n";
	static const char special[]	= "*?[#~=%";
	static const char doubleq[]	= "$`\"\\\n";

	if (chr == '\0') return false;
	else if (type & QUOTE_BASE && (strchr(base, chr) != NULL)) return true;
	else if (type & QUOTE_SPECIAL && (strchr(special, chr) != NULL)) return true;
	else if (type & QUOTE_DOUBLE && (strchr(doubleq, chr) != NULL)) return true;

	return false;
}

static int isoperator(const char *src)
{
	static const char *ops2[] = {
		"&&",
		"||",
		";;",
		"<<",
		">>",
		"<&",
		">&",
		"<>",
		">|",
		NULL
	};
	static const char ops1[] = "&()\n|;<>";

	if (!*src) return 0;

	if (!strncmp("<<-", src, 3)) return 3;

	for (int i = 0; ops2[i]; i++)
		if (!strncmp(ops2[i], src, 2)) return 2;

	if (strchr(ops1, *src) != NULL) {
		return 1;
	}

	return 0;
}

inline static const map_t *lookup_token(const char *token)
{
	for (int i = 0; lookup[i].str; i++) {
		if (!strcmp(token, lookup[i].str)) return &lookup[i];
	}
	return NULL;
}

static void cleanup()
{
	regfree(&reg_assignment);
	regfree(&reg_name);
	if (here_doc_remaining) free(here_doc_remaining);
	if (here_doc_word) free(here_doc_word);
}

static const map_t *category(const char *token, const char *next, const map_t *restrict prev)
{
	size_t len = 0;
	const map_t *map = NULL;

	if (token == NULL || !strlen(token)) {
		return NULL;
	} else if ((map = lookup_token(token))) {
		if (map->tok == Lbrace) {
			if(in_func) func_brace = in_brace;
			in_brace++;
		} else if (map->tok == Rbrace) {
			in_brace--;
			if(in_brace == func_brace) in_func=0;
		} else if (map->tok == If) {
			in_if++;
		} else if (map->tok == Fi) {
			in_if--;
		} else if (map->tok == Case) {
			in_case = true;
		} else if (map->tok == For) {
			in_for = true;
		/* to support "A=1) ;;" */
		} else if (in_case && map->tok == In) {
			in_case_in = true;
		} else if (in_for && map->tok == In) {
			in_for_in = true;
		} else if (map->tok == Esac) {
			in_case = false; in_case_in = false;
		} else if (map->tok == DSEMI) {
			in_case_in = true;
		} else if (map->tok == Do) {
			in_for_in = false;
			in_do++;
		} else if (map->tok == Done) {
			in_do--;
			in_for_in = false;
			in_for = false;
		}
		return map;
	} else if ((!in_case && !in_case_in) && regexec(&reg_assignment, token, 0, NULL, 0) == 0) {
		return &assignment_word;
	} else if (isdigit(*token)) {
		char *tmp = (char *)(token + 1);
		while(*tmp) {
			if (!isdigit(*tmp) && (len = isoperator(tmp)) && (strlen(tmp) == len)) {
				*tmp = '\0';
				return &io_number;
			}
			else if (isdigit(*tmp)) tmp++;
			else break;
		}
	} else if (regexec(&reg_name, token, 0, NULL, 0) == 0) {
		if (prev && prev->tok == For) return &name;
		else if (next && *next) {
			char *tmp = (char *)next;
			while (*tmp) {
				if (isblank(*tmp)) tmp++;
				else if (*tmp == '(') {
					if (in_func) {
						warnx("nested functions are not supported");
						break;
					}
					in_func = 1;
					return &name;
				}
				else break;
			}
		}
	}
	return &word;
}

static char *get_next_token(char *const str, char **next)
{
	static char buf[BUFSIZ];
	char *dst = buf;
	char *src = str;
	char *mark = NULL, *tmp = NULL;
	size_t len = 0;

	memset(buf, 0, sizeof(buf));

	if (!src) return NULL;

	while(*src)
	{
		if (*src == '\\' && isquote(*(src+1), QUOTE_BASE)) {
			*dst++ = *++src;
		} else if (*src == '"') {
			mark = src;
			*dst++ = *src++;
			while(*src)
			{
				if (*src == '\\' && isquote(*(src+1), QUOTE_DOUBLE)) {
					*dst++ = *src++;
					*dst++ = *src++;
				} else if (*src == '"') {
					*dst++ = *src++;
					break;
				} else
					*dst++ = *src++;
			}
			if (!*src) errx(EXIT_FAILURE, "Unterminated double quote at: %s", mark);
		} else if (*src == '\'') {
			*dst++ = *src++;
			while(*src && *src != '\'') *dst++ = *src++;
			if (!*src) errx(EXIT_FAILURE, "Unterminated single quote");
			*dst++ = *src++;
		} else if (*src == '<' && *(src+1) == '<' && isalpha(*(src+2))) {
			here_doc = true;
			char *tmp = src+2;

			while (*tmp)
				if (!isalpha(*tmp)) break;
				else tmp++;

			if (tmp - (src+2) < 1)
				errx(EXIT_FAILURE, "No delimiter for heredoc");
			
			here_doc_delim = strndup(src+2, tmp - (src+2));
			if (here_doc_remaining)
				free(here_doc_remaining);
			here_doc_remaining = strdup(src + (tmp - src));
			
			if (next) *next = NULL;
			return NULL;
		} else if (*src == '$' || *src == '`') {
			if (*src == '$') {
				if (*(src+1) == '{') {
					while (*src != '}')
						*dst++ = *src++;
				} else if (*(src+1) == '(' && *(src+2) == '(') {
					char *end = strstr(src+3, "))");
					if (!end) errx(EXIT_FAILURE, "Unterminated $((");
					len = (end + 2) - src;
					strncpy(dst, src, len);
					dst += len;
					src += len;
				} else if (*(src+1) == '(') {
					int depth = 1;
					char *end = src+2;
					while(*end && depth) {
						if (*end == ')' && *(end-1) != '\\') depth--;
						else if (*end == '$' && *(end+1) == '(') depth++;
						else
							end++;
					}
					if (depth) errx(EXIT_FAILURE, "Unterminated $(");
					len = (end + 1) - src;
					strncpy(dst, src, len);
					dst += len;
					src += len;
				} else
					*dst++ = *src++;
			} else
				*dst++ = *src++;
		} else if ((len = isoperator(src)) != 0) {
			if ( (*src == '<' || *src == '>') && dst > buf) {
				for (tmp = src; tmp > str;) {
					if (!isdigit(*(tmp-1))) break;
					tmp--;
				}

				if (tmp >= str) {
					//dst -= (src - tmp);
					//*dst = '\0';
					//src -= (src - tmp);
					
					/* the operator is not part of IO_NUMBER, but if we remove it, 
					 * the categoriser has doesn't work */
					strncat(buf, src, len);
					break;
				} else {
					while(len--) *dst++ = *src++;
					break;
				}
			}
			if (dst > buf) break;
			while(len--) *dst++ = *src++;
			break;
		} else if (isblank(*src)) {
			while(*src && isblank(*src)) src++;
			if (dst > buf) 
				break;
		} else if (*src == '#') {
			while(*src && *src != '\n') src++;
		} else
			*dst++ = *src++;
	}

	if (next) {
		if (*src) {
			*next = src;
		} else
			*next = NULL;
	}

	len = strlen(buf) - 1;
	for(int i = len; i >= 0 && isblank(buf[i]); i--)
		buf[i] = '\0';

	return strlen(buf) ? buf : NULL;
}

static void parser_init()
{
	if (regcomp(&reg_assignment, reg_assignment_str, REG_EXTENDED))
		errx(EXIT_FAILURE, "regcomp");
	if (regcomp(&reg_name, reg_name_str, REG_EXTENDED))
		errx(EXIT_FAILURE, "regcomp");
	if ((cur_sh_env = calloc(1, sizeof(shenv_t))) == NULL)
		err(EXIT_FAILURE, "calloc: cur_sh_env");
	if ((cur_sh_env->private_envs = calloc(1, sizeof(env_t *))) == NULL)
		err(EXIT_FAILURE, "calloc: cur_sh_env[0]");

	char buf[BUFSIZ] = {0};

	for (size_t i = 0; environ && environ[i]; i++) {
		char *tok = strchr(environ[i], '=');
		char *env = strndup(environ[i], environ[i] - tok);
		setshenv(cur_sh_env, env, getenv(env));
		free(env);
	}

	char *shlvl_str = getenv("SHLVL");
	int shlvl = 1;
	if (shlvl_str)
		shlvl = atoi(shlvl_str) + 1;
	snprintf(buf, 10, "%u", shlvl);

	env_t *env = NULL;
	if ((env = setshenv(cur_sh_env, "SHLVL", buf)) == NULL) {
		if (errno)
			err(EXIT_FAILURE, NULL);
		else
			err(EXIT_FAILURE, "Unable to set SHLVL");
	}

	exportenv(env);

	atexit(cleanup);
}

/* global function defintions */


int yylex()
{
	char *old = NULL;

	if (left == NULL)
	{
		left = parser_string;
		parser_string = NULL;
	}

	if (!left) return 0;

	yyline++; yyrow=0;

again:
	while(*left && isblank(*left)) left++;
	if (!*left) return 0;
	/*
	static char buf[BUFSIZ] = {0};
	if (feof(stdin) || ferror(stdin)) return 0;
	if (left == NULL || !*left) {
		if ((left = fgets(buf, BUFSIZ, stdin)) == NULL)
			return 0;

		while(*left && isblank(*left)) left++;
		if(!*left) goto again;

		yyline++;
		yyrow = 0;
	}
	*/

	old = left;
	if (here_doc)
	{
		char *tmp = NULL;

		if (!strncmp(left, here_doc_delim, strlen(here_doc_delim)))
		{
			yylval.string = here_doc_word;
			free(here_doc_delim);
			here_doc_delim = NULL;
			here_doc = false;
			//here_doc_word = NULL;
			prev = &word;
			free_left = true;
			left = here_doc_remaining;
			free_me = here_doc_remaining;
			//here_doc_remaining = NULL;
			return word.tok;
		}

		if (here_doc_word == NULL && (here_doc_word = strdup(left)) == NULL) {
			err(EXIT_FAILURE, "yylex: strdup");
		} else if (here_doc_word) {
			int newlen = strlen(here_doc_word) + strlen(left) + 1;
			if ((tmp = realloc(here_doc_word, newlen)) == NULL) {
				err(EXIT_FAILURE, "yylex: realloc");
			} else {
				here_doc_word = tmp;
			}
			strcat(here_doc_word, left);
		}

		left = NULL;
		goto again;
	}
	else
	{
		char *yytext = NULL;
		while(*left && isblank(*left)) left++;
		if(!*left) goto again;
		
		//printf("get_next_token([%s])\n", left);
		if ((yytext = get_next_token(left, &left)) == NULL) {
			if (here_doc) 
				goto again;
			warnx("no match/no data for [%s@%p] %x\n", old, old, old ? *old : '!');
			return 0;
		}

		int ret = 0;

		if ((prev = category(yytext, left, prev)) != NULL) {
			/*
			printf("matched: %s: [%s] tok=%d\n", 
					prev->tok != NEWLINE ? prev->str : "\\n", 
					(yytext && *yytext && *yytext != '\n') ? yytext : "_" ,
					prev->tok);
					*/
			if (prev->tok == NEWLINE) 
				yylval.chr = '\n';
			else
				yylval.string = yytext;
			ret = prev->tok;
		} else {
			warnx("failed to match [%s@%p] %x\n", yytext, yytext, yytext ? *yytext : '!');
		}

		if (!left || !*left) {
			if (in_case || in_case_in || in_do>0 || in_for_in || in_for>0 || in_brace>0 || in_func || in_if>0) {
				// FIXME && || and EOL
				//printf("need more\n");
				// FIXME only needed if iteractive, need old method otherwise? or not?
				get_next_parser_string(1);
			}
		}

		return ret;
	}
}

void yyparse();

void yyerror(const char *s)
{
	warnx("\n[%d:%d] %s", yyline, yyrow, s);
}

static bool get_next_parser_string(int prompt)
{
	static char buf[BUFSIZ] = {0};
	memset(buf, 0, sizeof(buf));

	if (prompt)
		printf("> ");

	parser_string = fgets(buf, BUFSIZ, stdin);

	if (parser_string == NULL) {
		if (feof(stdin))
			return true;
		exit(EXIT_FAILURE);
	}
	return false;
}

int main(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	parser_init();

	while(1)
	{
		if (printf("# ") < 0)
			exit(EXIT_FAILURE);

		if (get_next_parser_string(0))
			break;

		//printf("parsing '%s'\n", parser_string);
		yyparse();
	}
}
