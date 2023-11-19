%{
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include "sh.h"
#include "sh.y.tab.h"

int yylex(YYSTYPE *, void *);
void yyerror(void *, const char *s);
%}

%lex-param   {void *scanner}
%parse-param {void *scanner}
%pure-parser

%token-table

%union
{
	char	*string;
	node	*node;
	char	 chr;
}

/* -------------------------------------------------------
   The grammar symbols
   ------------------------------------------------------- */
%token<string>  WORD
%token<string>  ASSIGNMENT_WORD
%token<string>  NAME
%token<chr>		NEWLINE
%token<string>  IO_NUMBER
%token<string>	PARAMETER


/*%token TOK_VAR_EXP
%token TOK_COLHYP*/

/* The following are the operators (see XBD Operator)
   containing more than one character. */


%token  AND_IF    OR_IF    DSEMI
/*      '&&'      '||'     ';;'    */

%token  DLESS  DGREAT  LESSAND  GREATAND  LESSGREAT  DLESSDASH
/*      '<<'   '>>'    '<&'     '>&'      '<>'       '<<-'   */


%token  CLOBBER
/*      '>|'   */


/* The following are the reserved words. */


%token  If    Then    Else    Elif    Fi    Do    Done
/*      'if'  'then'  'else'  'elif'  'fi'  'do'  'done'   */


%token  Case    Esac    While    Until    For
/*      'case'  'esac'  'while'  'until'  'for'   */


/* These are reserved words, not operator tokens, and are
   recognized when reserved words are recognized. */


%token  Lbrace    Rbrace    Bang
/*      '{'       '}'       '!'   */


%token  In
/*      'in'   */

%type<node> program complete_commands complete_command list and_or
%type<node> pipeline pipe_sequence command compound_command
%type<node> subshell compound_list term for_clause
%type<node> case_clause case_list_ns case_list case_item_ns
%type<node> case_item if_clause else_part while_clause until_clause
%type<node> function_definition
%type<node> function_body brace_group do_group simple_command
%type<node> cmd_suffix cmd_name cmd_prefix io_redirect cmd_word
%type<node> redirect_list io_file pattern io_here
/*%type<node> expansion*/

%type<string> name 
%type<string> wordlist in fname filename
%type<string> here_end 
/*%type<string> variable_expansion*/

%type<chr> separator_op separator sequential_sep linebreak newline_list ';' '&'

/* -------------------------------------------------------
   The Grammar
   ------------------------------------------------------- */
%start program
%%
program          : linebreak complete_commands linebreak			{ 
																	debug_printf("program.1 [%0x,%0x]\n",$1,$3); 
																	//print_node($2,0,1);
																	//evaluate($2,0,1);
																	freeNode($2,true);
																	$$=NULL;
																	}
                 | linebreak										{ debug_printf("program.2 [%0x]\n", $1); }
                 ;

complete_commands: complete_commands newline_list complete_command	{ 
																	debug_printf("complete_commands.1 [%02x]\n", $2);
																	$$=nodeAppend($3,$1);
																	}
                 |                                complete_command	{ debug_printf("complete_commands.2\n"); 
																	cur_sh_env->rc = evaluate($1,0,1);
																	}
                 ;

complete_command : list separator_op					{ debug_printf("complete.1 [%02xc]\n", $2); $1->sep = $2; $$ = $1; }
                 | list									{ debug_printf("complete.2\n"); }
                 ;
list             : list separator_op and_or				{
														debug_printf("list.1 [%02xc]\n", $2);
														$1->sep = $2;
														$$ = nodeAppend($3, $1);
														}
                 |                   and_or				{ debug_printf("list.2\n"); }
                 ;

and_or           :                         pipeline		{ debug_printf("and_or.1\n"); }
                 | and_or AND_IF linebreak pipeline		{ 
														  debug_printf("and_or.2 [%02x]\n",$3);
														  $$ = nOp(AND_IF,$1,$4);
														  $$->sep = $3;
														}
                 | and_or OR_IF  linebreak pipeline		{ 
														  debug_printf("and_or.3 [%02x]\n",$3); 
														  $$ = nOp(OR_IF,$1,$4);
														  $$->sep = $3;
														}
                 ;
pipeline         :      pipe_sequence					{ debug_printf("pipeline.1\n"); }
                 | Bang pipe_sequence					{
														  debug_printf("pipeline.2 [!]\n");
														  $$ = nOp('!', NULL, $2);
														}
                 ;
pipe_sequence    :                             command	{ debug_printf("pipe_seq.1\n"); }
                 | pipe_sequence '|' linebreak command	{ 
														  debug_printf("pipe_seq.2 [|, %0x]\n", $3); 
														  $$ = nOp('|',$1,$4);
														  $$->sep = $3;
														}
                 ;
command          : simple_command						{ debug_printf("command.1 [%02x]\n", $1->sep); }
                 | compound_command						{ debug_printf("command.2 [%02x]\n", $1->sep); }
                 | compound_command redirect_list		{ 
														  debug_printf("command.3 [%02x]\n", $1->sep);
														  $1->arg1 = $2;
														  $$ = $1;
														}
                 | function_definition					{ debug_printf("command.4]\n"); }
                 ;
compound_command : brace_group							{ debug_printf("compound.1\n"); $$=nCompound($1,NULL); }
                 | subshell								{ debug_printf("compound.2\n"); $$=nCompound($1,NULL); }
                 | for_clause							{ debug_printf("compound.4\n"); $$=nCompound($1,NULL); }
                 | case_clause							{ debug_printf("compound.5\n"); $$=nCompound($1,NULL); }
                 | if_clause							{ debug_printf("compound.6\n"); $$=nCompound($1,NULL); }
                 | while_clause							{ debug_printf("compound.7\n"); $$=nCompound($1,NULL); }
                 | until_clause							{ debug_printf("compound.8\n"); $$=nCompound($1,NULL); }
                 ;
subshell         : '(' compound_list ')'				{ $$ = nSubshell($2); }
                 ;
compound_list    : linebreak term				{ 
												  debug_printf("compound_list.1 [%02x]\n", $1);
												  $$ = $2;
												}
                 | linebreak term separator		{
												  debug_printf("compound_list.2 [%02x, %c]\n", $1, $3);
												  $2->sep = $3;
												  $$ = $2;
												}
                 ;
term             : term separator and_or		{
												  debug_printf("term.1 [%c]\n", $2);
												  $1->sep = $2;
												  $$ = nodeAppend($3, $1);
												}
                 |                and_or		{ debug_printf("term.2\n"); }
                 ;

for_clause       : For name                                      do_group	
					{ 
					debug_printf("for_clause.1\n");
					$$ = nFor($2,NULL,$3);
					free($2);
					}
                 | For name                       sequential_sep do_group	
					{ 
					debug_printf("for_clause.2\n");
					$$ = nFor($2,NULL,$4);
					free($2);
					}
                 | For name linebreak in          sequential_sep do_group	
					{ 
					debug_printf("for_clause.3\n");
					$$ = nFor($2,NULL,$6);
					free($2);
					}
                 | For name linebreak in wordlist sequential_sep do_group	
					{
					debug_printf("for_clause.4\n");
					$$ = nFor($2,$5,$7);
					free($2); free($5);
					}
                 ;
name             : NAME                     { debug_printf("name.1 [%s]\n", $1); $$=strdup($1); }
				 ;							/* Apply rule.5 */

in               : In                       { debug_printf("in.1 [in]\n"); $$="in"; } /* Apply rule.6 */
                 ;
wordlist         : wordlist WORD			{ 
											debug_printf("wordlist.1 [%s %s]\n", $1, $2);
											const int len = strlen($1) + 1 + strlen($2) + 1;
											char *r = malloc(len);
											snprintf(r, len, "%s %s", $1, $2);
											$$ = r;
											free($1);
											}
                 |          WORD			{ debug_printf("wordlist.2 [%s]\n", $1); $$=strdup($1); }
                 ;

case_clause      : Case WORD linebreak in linebreak case_list    Esac	{
																		debug_printf("case_clause.1\n");
																		$$=nCase($2,$6);
																		}
                 | Case WORD linebreak in linebreak case_list_ns Esac	{
																		debug_printf("case_clause.2\n");
																		$$=nCase($2,$6);
																		}
                 | Case WORD linebreak in linebreak              Esac	{ 
																		debug_printf("case_clause.3\n");
																		$$=nCase($2,NULL);
																		}
                 ;

case_list_ns     : case_list case_item_ns				{ debug_printf("case_list_ns.1\n"); $$ = nodeAppend($2,$1); }
                 |           case_item_ns				{ debug_printf("case_list_ns.2\n"); }
                 ;

case_list        : case_list case_item					{ debug_printf("case_list.1\n"); $$ = nodeAppend($2,$1); }
                 |           case_item					{ debug_printf("case_list.2\n");}
                 ;

case_item_ns     :     pattern ')' linebreak			{ debug_printf("case_item_ns.1\n"); $$ = nCaseItem($1,NULL); }
                 |     pattern ')' compound_list		{ debug_printf("case_item_ns.2\n"); $$ = nCaseItem($1,$3);   }
                 | '(' pattern ')' linebreak			{ debug_printf("case_item_ns.3\n"); $$ = nCaseItem($2,NULL); }
                 | '(' pattern ')' compound_list		{ debug_printf("case_item_ns.4\n"); $$ = nCaseItem($2,$4);   }
                 ;
case_item        :     pattern ')' linebreak     DSEMI linebreak	{ debug_printf("case_item.1\n"); $$ = nCaseItem($1,NULL); }
                 |     pattern ')' compound_list DSEMI linebreak	{ debug_printf("case_item.2\n"); $$ = nCaseItem($1,$3);   }
                 | '(' pattern ')' linebreak     DSEMI linebreak	{ debug_printf("case_item.3\n"); $$ = nCaseItem($2,NULL); }
                 | '(' pattern ')' compound_list DSEMI linebreak	{ debug_printf("case_item.4\n"); $$ = nCaseItem($2,$4);   }
                 ;
pattern          :             WORD         {
											debug_printf("pattern.1 [%s]\n",$1);
											$$ = nPattern($1);
											}
											/* Apply rule.4 */
                 | pattern '|' WORD         {
											debug_printf("pattern.2 |[%s]\n",$3); 
											$$ = nodeAppend(nPattern($3), $1);
											/*
											const int len = strlen($1) + 1 + strlen($3) + 1;
											char *r = malloc(len);
											sndebug_printf(r, len, "%s|%s", $1, $3);
											$$ = r;
											free($1);
											*/
											}
											/* Do not apply rule.4 */
                 ;
if_clause        : If compound_list Then compound_list else_part Fi	
														{ 
														debug_printf ("if.1\n");
														$$ = nIf($2,$4,$5);
														}
                 | If compound_list Then compound_list Fi	{
															debug_printf ("if.2\n");
															$$ = nIf($2,$4,NULL);
															}
                 ;

else_part        : Elif compound_list Then compound_list	{
															debug_printf ("else.1\n");
															$$ = nIf($2,$4,NULL);
															}
                 | Elif compound_list Then compound_list else_part	{
																	debug_printf ("else.2\n");
																	$$ = nIf($2,$4,$5);
																	}
                 | Else compound_list				{ 
													debug_printf ("else.3\n");
													$$ = $2;
													}
                 ;
while_clause     : While compound_list do_group		{
													debug_printf("while.1\n");
													$$=nWhile($2,$3);
													}
													
                 ;
until_clause     : Until compound_list do_group		{
													debug_printf("until.1\n");
													$$=nUntil($2,$3);
													}
                 ;
function_definition : fname '(' ')' linebreak function_body { 
															debug_printf("func_def.1 [%0x]\n", $4);
															$$ = nFunc($1, $5); free($1);
															}
                 ;
function_body    : compound_command					{ debug_printf("function_body.1\n"); }
													/* Apply rule.9 */
                 | compound_command redirect_list	{ 
													  debug_printf("function_body.2\n");
													  $1->arg1 = $2;
													  $$=$1;
													}
													/* Apply rule.9 */
                 ;
fname            : NAME								{ debug_printf("fname.1 [%s]\n", $1); $$=strdup($1); }
				 ;									/* Apply rule.8 */
brace_group      : Lbrace compound_list Rbrace		{
													debug_printf("bracegroup.1\n");
													$$=$2;
													}
                 ;
do_group         : Do compound_list Done			{
													debug_printf("do_group.1\n");
													$$ = $2;
													}
													/* Apply rule.6 */
                 ;
simple_command   : cmd_prefix cmd_word cmd_suffix	{ debug_printf("simple.1\n"); $$ = nSimple($1,$2,$3); }
                 | cmd_prefix cmd_word				{ debug_printf("simple.2\n"); $$ = nSimple($1,$2,NULL); }
                 | cmd_prefix						{ debug_printf("simple.3\n"); $$ = nSimple($1,NULL,NULL); }
                 | cmd_name cmd_suffix				{
														debug_printf("simple.4 (_,%s,%s)\n", $1->value,$2->value);
														$$ = nSimple(NULL,$1,$2); 
													}
                 | cmd_name							{ 
														debug_printf("simple.5 (%s)\n", $1->value); 
														$$ = nSimple(NULL,$1,NULL); 
													}
                 ;
cmd_name         : WORD							    { debug_printf("@cmd_name.1 [%s]\n", $1); $$ = nString($1); }	/* Apply rule.7a */
                 ;
cmd_word         : WORD								{ debug_printf("@cmd_word.1 [%s]\n", $1); $$ = nString($1); }	/* Apply rule.7b */
                 ;
cmd_prefix       :            io_redirect			{ debug_printf("cmd_prefix.1\n"); }
                 | cmd_prefix io_redirect			{ debug_printf("cmd_prefix.2\n"); $$ = nodeAppend($2, $1); }
                 |            ASSIGNMENT_WORD		{ debug_printf("@cmd_prefix.3 [%s]\n", $1); $$ = nAssign($1); }
                 | cmd_prefix ASSIGNMENT_WORD		{ debug_printf("@cmd_prefix.4 +[%s]\n", $2); $$ = nodeAppend(nAssign($2), $1); }
                 ;
cmd_suffix       :            io_redirect			{ debug_printf("cmd_suffix.1\n"); }
                 | cmd_suffix io_redirect			{ debug_printf("cmd_suffix.2\n"); $$ = nodeAppend($2, $1); }
                 |            WORD					{ debug_printf("@cmd_suffix.3 [%s]\n", $1); $$ = nString($1); }
                 | cmd_suffix WORD					{ debug_printf("@cmd_suffix.4 +[%s]\n", $2); $$ = nodeAppend(nString($2), $1); }
/*				 |            expansion				{ debug_printf("cmd_suffix.5\n"); $$ = nString($1); }
				 | cmd_suffix expansion				{ debug_printf("cmd_suffix.6\n"); $$ = nodeAppend($2, $1); }*/
                 ;
redirect_list    :               io_redirect		{ debug_printf("redirect_list.1\n"); }
                 | redirect_list io_redirect		{ debug_printf("redirect_list.2\n"); $$ = nodeAppend($2, $1); }
                 ;
io_redirect      :           io_file				{ debug_printf("io_direct.1\n"); }
                 | IO_NUMBER io_file				{ debug_printf("io_direct.2 [%s]\n", $1); $2->num = atoi($1); $$=$2; }
                 |           io_here				{ debug_printf("io_direct.3\n"); }
                 | IO_NUMBER io_here				{ debug_printf("io_direct.4\n"); $2->num = atoi($1); $$=$2; }
                 ;
io_file          : '<'       filename				{ $$ = nIoRedirect('<',		 $2); free($2); }
                 | LESSAND   filename				{ $$ = nIoRedirect(LESSAND,  $2); free($2); }
                 | '>'       filename				{ $$ = nIoRedirect('>',		 $2); free($2); }
                 | GREATAND  filename				{ $$ = nIoRedirect(GREATAND, $2); free($2); }
                 | DGREAT    filename				{ $$ = nIoRedirect(DGREAT,	 $2); free($2); }
                 | LESSGREAT filename				{ $$ = nIoRedirect(LESSGREAT,$2); free($2); }
                 | CLOBBER   filename				{ $$ = nIoRedirect(CLOBBER,	 $2); free($2); }
                 ;
filename         : WORD								{ debug_printf("filename.1 [%s]\n", $1); $$=strdup($1); }
													/* Apply rule.2 */
                 ;
io_here          : DLESS     here_end		{ $$ = nIoRedirect(DLESS,	  $2); free($2); }
                 | DLESSDASH here_end		{ $$ = nIoRedirect(DLESSDASH, $2); free($2); }
                 ;
here_end         : WORD                     { $$ = strdup($1); } 
											/* Apply rule.3 */
                 ;
newline_list     :              NEWLINE		{ debug_printf("nl_list.1 [%02x]\n", $1);				$$='\n'; }
                 | newline_list NEWLINE		{ debug_printf("nl_list.2 [%02x %02x]\n", $1, $2);	$$='\n'; }
                 ;
linebreak        : newline_list				{ debug_printf("lb.1 [%02x]\n", $1); $$=$1;		}
                 |							{ debug_printf("lb.2\n");	$$='\n';    } /* empty */
                 ;
separator_op     : '&'						{ debug_printf("&\n"); $$='&'; debug_printf("sep.1: [&]\n"); }
                 | ';'						{ debug_printf(";\n"); $$=';'; debug_printf("sep.2: [;]\n"); }
                 ;
separator        : separator_op linebreak	{ debug_printf("sep.1 [%c %02x]\n", $1, $2);	$$=$1; }
                 | newline_list				{ debug_printf("sep.2 [%02x]\n", $1);			$$=$1; }
                 ;
sequential_sep   : ';' linebreak			{ debug_printf("seq_sep.1 [; %02x]\n", $2); $$=$2; }
                 | newline_list				{ debug_printf("seq_sep.2 [%02x]\n", $1); $$=$1; }
                 ;
/*
expansion		   : variable_expansion		{ debug_printf("expansion.1\n"); $$=$1; }
				   ;

variable_expansion : TOK_VAR_EXP PARAMETER Rbrace			{ debug_printf("variable_expansion.1\n"); $$=$2; }
				   | TOK_VAR_EXP PARAMETER TOK_COLHYP		{ debug_printf("variable_expansion.2\n"); $$=$2; }
				   | TOK_VAR_EXP PARAMETER TOK_COLHYP WORD	{ debug_printf("variable_expansion.3\n"); $$=$2; }
				   ;
*/
%%
#include "sh.h"
