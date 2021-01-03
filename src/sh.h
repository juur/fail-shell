#ifndef _SH_H
#define _SH_H 1

#define NDEBUG

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

node *nodeAppend(node*, node*);
node *nIf(node*, node*, node*);			// arg3 = redirect_list
node *nSimple(node *, node *, node *);
node *nIoRedirect(int, char *);
node *nCase(char *, node *);
node *nCaseItem(node *, node *);
node *nWhile(node *, node *);
node *nFor(char *, char *, node *);
node *nUntil(node *, node *);
node *nString(char *);
node *nPattern(char *);
node *nAssign(char *);
node *nCompound(node *, node *);
node *nOp(int, node *, node *);		// arg0 is LHS/unary, arg1 is RHS, char is type, int = %token
node *nSubshell(node *);
node *nFunc(char *, node *);
void print_node(const node *, int, int);
int evaluate(node *, int, int);
void freeNode(node *, const bool);

#endif /* _SH_H */
