%{
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include "make.h"

extern int yylex();
%}

%union {
	struct node *node;
}

%token VAR_OPEN DIGIT ALPHA SP EOL

%start makefile
%%

makefile	: body_list
			;

body_list	: body_list
			| body
			;

body		: assignment
			| makerule
			| SP
			;

assignment	: identifier_sp '=' rvalue_list_sp
			| identifier_sp '='
			;

identifier_sp	: SP identifier SP
				| SP identifier
				| identifier SP
				| identifier
				;

rvalue_list_sp	: SP rvalue_list SP
				| SP rvalue_list
				| rvalue_list SP
				| rvalue_list
				;

rvalue_list		: rvalue_list
				| rvalue
				;

rvalue			: expansion
				| internal_macro
				| text
				| SP
				;

internal_macro	: '$@'
			    | '$<'
				;

expansion		: VAR_OPEN function ')'
				| VAR_OPEN identifier ')'
				;

function		: function_name
				| function_name argument_list
				;

function_name	: 'wildcard'
				;

argument_list	: argument_list
				| SP argument
				;

argument		: expansion
				| text
				;

identifier		: ident_start
				| ident_start ident_end_list
				;

ident_start		: ALPHA
				;

ident_end_list	: ident_end_list
				| ident_end
				;

ident_end		: ALPHA
				| DIGIT
				;

makerule		: target_line
				| target_line command_line_list
				;

target_line		: target_list ':'
				| target_list ':' SP target_line_suffix
				| target_list ':' target_line_suffix
				;

target_line_suffix	: prereq_list
					| prereq_list SP target_command_list
					| prereq_list target_command_list
					;

target_command_list	: ';' SP command_list
					| ';' command_list
					;

command_line_list	: command_line_list
					| command_line
					;

command_line		: '\t' command
					;

command_list		: command_list
					| SP command
					;

command				: rvalue_list
					;

prereq				: target
					;

text				: ALPHA
					| DIGIT
					| '-' | '.' | ',' | ':' | ';' | '\'' | '"'
					;

target_list			: target_list
					| target
					;

target				: target


%%
#include <stdio.h>

void yyerror(char *s)
{
	fflush(stdout);
	fprintf(stderr, "\n%s\n", s);
}
