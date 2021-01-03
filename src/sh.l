%{
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "sh.h"
#include "sh.y.tab.h"
%}

%x STRING_SQ STRING_DQ ST_WORD ST_VAR_EXP

%option noyywrap warn stack nodefault debug reentrant bison-bridge

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

<STRING_SQ>{
	
	{SQ}	{
				yylval->string = strdup(yytext);
				yy_pop_state(yyscanner);
				return WORD;
			}

	{CHAR}/{SQ}	{
					yylval->string = strdup(yytext);
					input(yyscanner);
					yy_pop_state(yyscanner);
					return WORD;
				}
}

<STRING_DQ>{
	
	{DQ}	{
				yylval->string = strdup(yytext);
				yy_pop_state(yyscanner);
				return WORD;
			}

	${CHAR}/{DQ}	{
		yylval->string = strdup(yytext);
		input(yyscanner);
		yy_pop_state(yyscanner);
		return WORD;
	}
}

<ST_WORD>{

	"${" {
		yy_push_state(ST_VAR_EXP, yyscanner);
		return TOK_VAR_EXP;
	}
}

<ST_VAR_EXP>{
	
	"}" {
		yy_pop_state(yyscanner);
		return Rbrace;
	}

	":-" {
		yy_pop_state(yyscanner);
		yy_push_state(ST_WORD, yyscanner);
		return TOK_COLHYP;
	}

	{CHAR}/("}"|":") {
				   yylval->string = strdup(yytext);
				   return PARAMETER;
			   }

	{CHAR} { yymore(); }
}

<ST_WORD>{

	"}" {
		yy_pop_state(yyscanner);
		yylval->string = strdup(yytext);
		return WORD;
	}
		
	({SQ}|{DQ})/({WS}+|{CHAR}+) {
		yy_push_state(*yytext == 0x22 ? STRING_DQ : STRING_SQ, yyscanner);
	}

	{NEWLINE} { yy_pop_state(yyscanner); return NEWLINE; }
	{SEMI}    { yy_pop_state(yyscanner); return ';'; }

	{CHAR}/({SQ}|{DQ}) {
						   yylval->string = strdup(yytext);
						   yy_push_state(input(yyscanner) == 0x22 ? STRING_SQ : STRING_SQ, yyscanner);
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
							if (*yytext == 0x27 || *yytext == 0x22) {
								yyless(yyleng);
								input(yyscanner);
								yy_push_state(*yytext == 0x27 ? STRING_SQ : STRING_DQ, yyscanner);
							} else {
								yylval->string = strdup(yytext);
								yy_pop_state(yyscanner);
								return WORD;
							}
						}

	{CHAR}				{ yymore(); }
}

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


{EQ}	{ return '='; }

{WS}+

{IDENT}+{EQ} {
				yylval->string = strdup(yytext);
				return ASSIGNMENT_WORD;
			 }

{SEMI}	{ return ';'; }

{CHAR}	{
	yyless(yyleng-1);
	yy_push_state(ST_WORD, yyscanner);
		}

{NEWLINE}	{ return NEWLINE; }

<*><<EOF>>	{
	static int once = 0;
	if(yyleng && (once = !once) ) {
		if(!yytext || !*yytext || !strlen(yytext)) 
			return 0;
		if(*yytext == '\n' || *yytext == '\r')
			return NEWLINE;
		yylval->string = strdup(yytext);
		return WORD;
	} else
		return 0;
}

%%
