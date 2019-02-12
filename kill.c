#define _XOPEN_SOURCE	700

#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <stdio.h>

static void show_usage(const char *cmd)
{
	exit(EXIT_FAILURE);
}

struct sig_entry {
	const char *name;
	int sig;
};

struct sig_entry signals[] = {
	{"HUP",SIGHUP},
	{"INT",SIGINT},
	{"QUIT",SIGQUIT},
	{"ILL",SIGILL},
	{"ABRT",SIGABRT},
	{"FPE",SIGFPE},
	{"KILL",SIGKILL},
	{"SEGV",SIGSEGV},
	{"PIPE",SIGPIPE},
	{"ALRM",SIGALRM},
	{"TERM",SIGTERM},
	{"USR1",SIGUSR1},
	{"USR2",SIGUSR2},
	{"CHLD",SIGCHLD},
	{"CONT",SIGCONT},
	{"STOP",SIGSTOP},
	{"TSTP",SIGTSTP},
	{"TTIN",SIGTTIN},
	{"TTOU",SIGTTOU},
	{"BUS",SIGBUS},
	{"POLL",SIGPOLL},
	{"PROF",SIGPROF},
	{"SYS",SIGSYS},
	{"TRAP",SIGTRAP},
	{"URG",SIGURG},
	{"VTALRM",SIGVTALRM},
	{"XCPU",SIGXCPU},
	{"XFSZ",SIGXFSZ},
	{NULL,0}
};

static void list_signals()
{
	int i = -1;
	while(signals[++i].name) {
		printf("%s", signals[i].name);
		if(!signals[i+1].name)
			putchar('\n');
		else
			putchar(' ');
	}
	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	int opt_show_signals = 0;
	int opt_signal = SIGTERM;

	if( argc == 1 )
		show_usage(argv[0]);

	if( argv[1][0]=='-' && (isdigit(argv[1][1]) || strlen(argv[1]) > 2) ) {
		if( isdigit(argv[1][1]) ) {
			opt_signal = atoi(&argv[1][1]);
		} else {
			show_usage(argv[0]);
		}
	} else {
		int opt;
		while ((opt = getopt(argc, argv, "ls:")) != -1) {
			switch(opt) {
				case 's':
					opt_signal = atoi(optarg);
					break;
				case 'l':
					opt_show_signals = 1;
					break;
				default:
					show_usage(argv[0]);
			}
		}
	}

	if( opt_show_signals )
		list_signals();

	if( optind >= argc )
		show_usage(argv[0]);
}
