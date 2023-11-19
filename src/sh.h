#ifndef _SH_H
#define _SH_H 1

#ifdef NDEBUG
#define debug_printf(...) printf(__VA_ARGS__)
#else
#define debug_printf(...)
#endif

#include <fcntl.h>

enum node_en {
	N_NONE,
	N_STRING,
	N_ASSIGN,
	N_IF,
	N_SIMPLE,
	N_IOREDIRECT,
	N_OP,
	N_SUBSHELL,
	N_FUNC,
	N_CASE,
	N_CASEITEM,
	N_WHILE,
	N_COMPOUND_COMMAND,
	N_UNTIL,
	N_FOR,
	N_PATTERN
};

typedef struct _node node;

struct _node {
	enum node_en type;
	node *next;
	node *arg0;
	node *arg1;
	node *arg2;
	node *arg3;
	char *value;
	char *evaluated;
	int num;
	char sep;			// & or ;
	int token;
};

#include <stdbool.h>

typedef struct {
	int skip;
    int once;
} shell_state_t;

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

/* for shenv_t */
#define	MAX_TRAP	15
#define	MAX_OPTS	10
#define NUM_FDS		10

/* for list_t */
#define LIST_AND	0
#define	LIST_OR		1


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
	int		  argc;
    int       rc;
} shenv_t;



extern node *nodeAppend(node*, node*);
extern node *nIf(node*, node*, node*);			// arg3 = redirect_list
extern node *nSimple(node *, node *, node *);
extern node *nIoRedirect(int, char *);
extern node *nCase(char *, node *);
extern node *nCaseItem(node *, node *);
extern node *nWhile(node *, node *);
extern node *nFor(char *, char *, node *);
extern node *nUntil(node *, node *);
extern node *nString(char *);
extern node *nPattern(char *);
extern node *nAssign(char *);
extern node *nCompound(node *, node *);
extern node *nOp(int, node *, node *);		// arg0 is LHS/unary, arg1 is RHS, char is type, int = %token
extern node *nSubshell(node *);
extern node *nFunc(char *, node *);
extern void print_node(const node *, int, int);
extern int evaluate(node *, int, int);
extern void freeNode(node *, const bool);

extern shenv_t *cur_sh_env;

#endif /* _SH_H */
