#define _XOPEN_SOURCE 700

#include <utmpx.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

static char *uttype(const short ut)
{
	switch(ut)
	{
		case EMPTY:
			return "EMPTY";
		case BOOT_TIME:
			return "BOOT_TIME";
		case OLD_TIME:
			return "OLD_TIME";
		case NEW_TIME:
			return "NEW_TIME";
		case USER_PROCESS:
			return "USER_PROCESS";
		case INIT_PROCESS:
			return "INIT_PROCESS";
		case LOGIN_PROCESS:
			return "LOGIN_PROCESS";
		case DEAD_PROCESS:
			return "DEAD_PROCESS";
		default:
			return NULL;
	}
}

int main(const int argc, const char *restrict argv[])
{
	setutxent();

	struct utmpx *ut;
	struct tm *tm;

	while ((ut = getutxent()) != NULL)
	{
		char *type = uttype(ut->ut_type);
		if (type == NULL)
			printf("ut_type = %d\n", ut->ut_type);
		else
			printf("ut_type = %s\n", type);

		switch (ut->ut_type)
		{
			case USER_PROCESS:
			case LOGIN_PROCESS:
				printf(" ut_line: %s\n", ut->ut_line);
				printf(" ut_user: %s\n", ut->ut_user);
			case DEAD_PROCESS:
			case INIT_PROCESS:
				//printf("   ut_id: %s\n", ut->ut_id);
				printf("  ut_pid: %u\n", ut->ut_pid);
			case BOOT_TIME:
			case OLD_TIME:
			case NEW_TIME:
				tm = localtime((time_t*)&ut->ut_tv.tv_sec);
				printf("   ut_tv: %s\n", asctime(tm));
			case EMPTY:
				break;
		}
	}

	endutxent();
}
