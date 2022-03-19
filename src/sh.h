#ifndef _SH_H
#define _SH_H 1

#ifdef NDEBUG
#define debug_printf(...) printf(__VA_ARGS__)
#else
#define debug_printf(...)
#endif

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
} shell_state_t;


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

#endif /* _SH_H */
