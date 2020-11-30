D	[0-9]
L	[a-zA-Z_]
H	[a-fA-F0-9]
E	[Ee][+-]?{D}+
FS	(f|F|l|L)
IS	(u|U|l|L)*

%{
#define _XOPEN_SOURCE 700
#include "y.tab.h"
#include "awk.h"
#include <stdio.h>

%}

%%

"#*"		{ ; }

"BEGIN"		{ return(Begin);	}
"END"		{ return(End);		}

"break"		{ return(Break);	}
"continue"	{ return(Continue);	}
"delete"	{ return(Delete);	}
"do"		{ return(Do);		}
"else"		{ return(Else);		}
"exit"		{ return(Exit);		}
"for"		{ return(For);		}
"function"	{ return(Function);	}
"if"		{ return(If);		}
"in"		{ return(In);		}
"next"		{ return(Next);		}
"print"		{ return(Print);	}
"printf"	{ return(Printf);	}
"return"	{ return(Return);	}
"while"		{ return(While);	}

"atan2"|"cos"	{ return(BUILTIN_FUNC_NAME);	}
"getline"		{ return(GETLINE);				}

{L}({L}|{D})*		{	yylval = yytext;	return(NAME);	}
{D}+				{	yylval = yytext;	return(NUMBER);	}
\"(\\.|[^\\"])*\"	{	yylval = yytext;	return(STRING);	}

"+="	{ return ADD_ASSIGN; }
"-="	{ return SUB_ASSIGN; }
"*="	{ return MUL_ASSIGN; }
"/="	{ return DIV_ASSIGN; }
"%="	{ return MOD_ASSIGN; }
"^="	{ return POW_ASSIGN; }

"||"	{ return OR;		}
"&&"	{ return AND;		}
"!~"	{ return NO_MATCH;	}
"=="	{ return EQ;		}
"<="	{ return LE;		}
">="	{ return GE;		}
"!="	{ return NE;		}
"++"	{ return INCR;		}
"--"	{ return DECR;		}
">>"	{ return APPEND;	}

"{"		{ return '{'; }
"}"		{ return '}'; }
"("		{ return '('; }
")"		{ return ')'; }
"["		{ return '['; }
"]"		{ return ']'; }
","		{ return ','; }
";"		{ return ';'; }
"\n"	{ return NEWLINE; }
"+"		{ return '+'; }
"-"		{ return '-'; }
"*"		{ return '*'; }
"%"		{ return '%'; }
"^"		{ return '^'; }
"!"		{ return '!'; }
">"		{ return '>'; }
"<"		{ return '<'; }
"|"		{ return '|'; }
"?"		{ return '?'; }
":"		{ return ':'; }
"~"		{ return '~'; }
"$"		{ return '$'; }
"="		{ return '='; }

[ \t\v\f]	{ ; }
.			{ }

%%

int yywrap()
{
	return(1);
}
