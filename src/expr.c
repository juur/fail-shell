#define _XOPEN_SOURCE 700

#include <string.h>
#include <stdio.h>

#include "y.tab.h"

static int current_arg;
static char **args;
static int num_args;

extern void yyparse();

int main(int argc, char *argv[])
{
	current_arg = 1;
	num_args = argc;
	args = argv;

	yyparse();
}

int yylex()
{
	if (current_arg >= num_args)
		return 0;

	const char *c = args[current_arg++];

	if (!strcmp(">=", c)) return GE;
	if (!strcmp("<=", c)) return LE;
	if (!strcmp("!=", c)) return NE;

	if (strlen(c) == 1) {
		switch (*c)
		{
			case '(':
			case ')':
			case '|':
			case '&':
			case '=':
			case '>':
			case '<':
			case '+':
			case '-':
			case '*':
			case '/':
			case '%':
			case ':':
				return *c;
				break;
		}
	}

	yylval.string = strdup(c);
	return STRING;
}

void yyerror(const char *str)
{
	fprintf(stderr, "%s\n", str);
}
