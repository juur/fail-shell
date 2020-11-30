DIGIT	[0-9]+
ALPHA	[a-zA-Z_]+
SPACE	[ \t]+
EOL		[\r\n]+

%{
#define _XOPEN_SOURCE 700
#include <stdio.h>

#include "make.y.tab.h"
#include "make.h"

%}

%%

"#*"	{ ; }
"$("	{ return(VAR_OPEN); }
"${"	{ return(VAR_OPEN); }
DIGIT	{ return(DIGIT);	}
ALPHA	{ return(ALPHA);	}
SPACE	{ return(SP);		}
EOL		{ return(EOL);		}

%%

int yywrap()
{
	return(1);
}
