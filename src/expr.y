%{
#define _XOPEN_SOURCE 700
#include <stdio.h>

static char tmpbuf[BUFSIZ];

int yylex();
void yyerror(const char *s);
%}

%union {
	char *	string;
	int		integer;
	int		op;
}


%token <op> GE LE NE 
%token <integer> INTEGER 
%token <string> STRING
%token <op> '|' '&' '=' '>' '<' '+' '-' '*' '/' '%' ':'

%type <string> expr

%%

start	:	expr				{ printf("%s\n", $1); }
		;

expr	:	'(' expr ')'		{ $$ = $2; }
		|	expr '|' expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) | atoi($3)); $$ = strdup(tmpbuf); }
		|	expr '&' expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) & atoi($3)); $$ = strdup(tmpbuf); }
		|	expr '=' expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) == atoi($3)); $$ = strdup(tmpbuf); }
		|	expr '>' expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) > atoi($3)); $$ = strdup(tmpbuf); }
		|	expr GE expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) >= atoi($3)); $$ = strdup(tmpbuf); }
		|	expr '<' expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) < atoi($3)); $$ = strdup(tmpbuf); }
		|	expr LE expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) <= atoi($3)); $$ = strdup(tmpbuf); }
		|	expr NE	expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) != atoi($3)); $$ = strdup(tmpbuf); }
		|	expr '+' expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) + atoi($3)); $$ = strdup(tmpbuf); }
		|	expr '-' expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) - atoi($3)); $$ = strdup(tmpbuf); }
		|	expr '*' expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) * atoi($3)); $$ = strdup(tmpbuf); }
		|	expr '/' expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) / atoi($3)); $$ = strdup(tmpbuf); }
		|	expr '%' expr		{ snprintf(tmpbuf, BUFSIZ, "%d", atoi($1) % atoi($3)); $$ = strdup(tmpbuf); }
		|	expr ':' expr
		|	STRING				{ $$ = $1; }
		;

%%
