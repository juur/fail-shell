%{
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "sh.h"
#include "sh.y.tab.h"
%}

%x STRING_SQ STRING_DQ ST_WORD ST_VAR_EXP ST_ASSIGNMENT
%x EXP_VAR EXP_SH

%option bison-bridge noyywrap warn stack 
%option nodefault debug reentrant extra-type="shell_state_t *"

SQ		'
DQ		\"
BSLASH	\\
CHAR	[^\r\n]
NEWLINE	(\n|\r|\r\n)
WS		[ \t\v]
EQ		=
IDENT	[A-Za-z_0-9]
SEMI	;

%%

<*>{BSLASH}"#"/{CHAR}*		{ *yytext = '#'; yytext[1] = '\0'; yyleng = 1; yymore(); }
<*>"#"{CHAR}*{NEWLINE}

<*>"${"		{ 
	yy_push_state(EXP_VAR, yyscanner); 
	yyextra->skip++;
//	if(yyextra->skip++) 
		yymore(); 
//	printf("--exp_var skip now %d\n", yyextra->skip);
}

<*>"$("		{ 
	yy_push_state(EXP_SH, yyscanner);  
	yyextra->skip++;
//	if(yyextra->skip++)
		yymore();
//	printf("--exp_sh  skip now %d\n", yyextra->skip);
}

<ST_WORD,ST_ASSIGNMENT,EXP_VAR,EXP_SH>{
	
	({SQ}|{DQ})/({WS}+|{CHAR}+) {
		yy_push_state(yytext[yyleng-1] == '\"' ? STRING_DQ : STRING_SQ, yyscanner);
		if (yyextra->skip) {
			yyextra->skip++;
//			printf("--skip now %d\n", yyextra->skip);
			yymore();
		}
	}
}

<EXP_VAR>{
	/* TODO: \} */
	"}"		{ 
				yy_pop_state(yyscanner); 
				yyextra->skip--;
//				printf("--exp_var skip now %d\n", yyextra->skip);
				
				if (!yyextra->skip) {
					//printf("--exp_var returning WORD \"%s\"\n", yytext);
					yylval->string = strdup(yytext);
					return WORD;
				} else
					yymore();
			}
	.		{ yymore(); }
}

<EXP_SH>{
	/* TODO: \) */
	")"     { 
				yy_pop_state(yyscanner); 
				yyextra->skip--; 
//				printf("--exp_sh: skip now %d\n", yyextra->skip);

				if(!yyextra->skip) {
					//printf("--exp_sh  returning WORD \"%s\"\n", yytext);
					yylval->string = strdup(yytext);
					return WORD;
				} else
					yymore();
			}
	.		{ yymore(); }
}

<STRING_SQ>{
	
	{SQ}	{
				yy_pop_state(yyscanner);

				if(yyextra->skip) {
					yyextra->skip--;
					//printf("--string_sq skip now %d\n", yyextra->skip);
					yymore();
				} else {
					yylval->string = strdup(yytext);
					return WORD;
				}
			}

	./{SQ}	{
				input(yyscanner);
				yy_pop_state(yyscanner);

				if(yyextra->skip) {
					yyextra->skip--;
					//printf("--string_sq skip now %d\n", yyextra->skip);
					yymore();
				} else {
					yylval->string = strdup(yytext);
					return WORD;
				}
			}

	. { yymore(); }
}

<STRING_DQ>{
	
	"\\"{DQ} { yyleng-=2; yytext[yyleng] = '"'; yymore(); }
	
	{DQ}	{
				yy_pop_state(yyscanner);
				if(yyextra->skip) {
					yyextra->skip--;
					//printf("--string_dq skip now %d\n", yyextra->skip);
					yymore();
				} else {
					yylval->string = strdup(yytext);
					return WORD;
				}
			}


	.{DQ}	{
				yy_pop_state(yyscanner);
				if(yyextra->skip) {
					yyextra->skip--;
					//printf("--string_dq skip now %d\n", yyextra->skip);
					yymore();
				} else {
					yyless(yyleng-1);
					input(yyscanner);
					yylval->string = strdup(yytext);
					return WORD;
				}
			}

	. { yymore(); }
}

<ST_ASSIGNMENT>{
	
	{CHAR}/{WS}	{	
						yymore(); 
					}
	
	/* We need to handle the case of A=1<EOF> */
	{CHAR}+|{WS}+	{
				yy_pop_state(yyscanner);
				yylval->string = strdup(yytext);
				return ASSIGNMENT_WORD;
			}
}

<ST_WORD>{
	
	{NEWLINE} { yy_pop_state(yyscanner); return NEWLINE; }
	{SEMI}    { yy_pop_state(yyscanner); return ';'; }

	{CHAR}/({SQ}|{DQ}) {
						   yylval->string = strdup(yytext);
						   yy_push_state(input(yyscanner) == '\"' ? STRING_SQ : STRING_SQ, yyscanner);
						   return WORD;
					   }

	{BSLASH}/(${NEWLINE}|{SEMI}) {
							input(yyscanner);
						}



	{CHAR}/({EQ}|{WS}+|{SEMI})		{
							yylval->string = strdup(yytext);
							yy_pop_state(yyscanner);
							return WORD;
						}

	{CHAR}/{NEWLINE}	{
							if (*yytext == '\'' || *yytext == '\"') {
								yyless(yyleng);
								input(yyscanner);
								yy_push_state(*yytext == '\'' ? STRING_SQ : STRING_DQ, yyscanner);
							} else {
								yylval->string = strdup(yytext);
								yy_pop_state(yyscanner);
								return WORD;
							}
						}
	{WS}+
	
	{CHAR}				{ yymore(); }
}

"&&"  { return AND_IF;   }
"||"  { return OR_IF;    }
";;"  { return DSEMI;    }
"<<"  { return DLESS;    }
">>"  { return DGREAT;   }
"<&"  { return LESSAND;  }
">&"  { return GREATAND; }
"<>"  { return LESSGREAT;}
"<<-" { return DLESSDASH;}

"{" { return Lbrace; }
"}" { return Rbrace; }
"!" { return Bang;   }

"if"/({NEWLINE}|{SEMI}|{WS}+)		{ return If;    }
"then"/({NEWLINE}|{SEMI}|{WS}+)		{ return Then;   }
"else"/({NEWLINE}|{SEMI}|{WS}+)		{ return Else;  }
"elif"/({NEWLINE}|{SEMI}|{WS}+)		{ return Elif;  }
"fi"/({NEWLINE}|{SEMI}|{WS}+)		{ return Fi;    }
"do"/({NEWLINE}|{SEMI}|{WS}+)		{ return Do;    }
"done"/({NEWLINE}|{SEMI}|{WS}+)		{ return Done;  }
"case"/({NEWLINE}|{SEMI}|{WS}+)		{ return Case;  }
"esac"/({NEWLINE}|{SEMI}|{WS}+)		{ return Esac;  }
"while"/({NEWLINE}|{SEMI}|{WS}+)	{ return While; }
"until"/({NEWLINE}|{SEMI}|{WS}+)	{ return Until; }
"for"/({NEWLINE}|{SEMI}|{WS}+)		{ return For;   }
"in"/({NEWLINE}|{SEMI}|{WS}+)		{ return In;    }



{WS}+

{SEMI}		{ 
				if(!yyextra->skip)
					yymore();
				else
					return ';'; 
			}

	/* We need to handle the case of A= and A=<<EOF>> */
{IDENT}+{EQ}/{CHAR} { yy_push_state(ST_ASSIGNMENT, yyscanner); yymore(); }
{IDENT}+{EQ}        { yylval->string = strdup(yytext); return ASSIGNMENT_WORD; }

{EQ}		{ return '='; }

{CHAR}		{
				yyless(yyleng-1);
				yy_push_state(ST_WORD, yyscanner);
			}

{NEWLINE}	{ 
				//printf("--newline with %s\n", yytext);
				return NEWLINE; 
			}

<*><<EOF>>	{
	static int once = 0;
	if(yyleng && (once = !once) ) {
		/*
		if (YY_START == ST_ASSIGNMENT) {
			if(yytext)
				yylval->string = strdup(yytext);
			printf("--returning ASSIGNMENT_WORD from state %d with %s\n", YY_START, yytext);
			yy_pop_state(yyscanner);
			return ASSIGNMENT_WORD;
		}
		*/
		//printf("--eof\n");
		if(!yytext || !*yytext || !strlen(yytext)) 
			return 0;
		if(*yytext == '\n' || *yytext == '\r')
			return NEWLINE;
		//printf("--eof returning %s\n", yytext);
		yylval->string = strdup(yytext);
		return WORD;
	} else
		return 0;
}

%%
