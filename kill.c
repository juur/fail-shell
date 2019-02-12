#define _XOPEN_SOURCE	700

#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <strings.h>

static void show_usage(const char *cmd)
{
	fprintf(stdout, "Usage: kill -l [exit_status] -s signal_name pid..\n");
	exit(EXIT_FAILURE);
}

struct sig_entry {
	const char *name;
	int sig;
};

static struct sig_entry signals[] = {
	{"HUP",		SIGHUP},
	{"INT",		SIGINT},
	{"QUIT",	SIGQUIT},
	{"ILL",		SIGILL},
	{"ABRT",	SIGABRT},
	{"FPE",		SIGFPE},
	{"KILL",	SIGKILL},
	{"SEGV",	SIGSEGV},
	{"PIPE",	SIGPIPE},
	{"ALRM",	SIGALRM},
	{"TERM",	SIGTERM},
	{"USR1",	SIGUSR1},
	{"USR2",	SIGUSR2},
	{"CHLD",	SIGCHLD},
	{"CONT",	SIGCONT},
	{"STOP",	SIGSTOP},
	{"TSTP",	SIGTSTP},
	{"TTIN",	SIGTTIN},
	{"TTOU",	SIGTTOU},
	{"BUS",		SIGBUS},
	{"POLL",	SIGPOLL},
	{"PROF",	SIGPROF},
	{"SYS",		SIGSYS},
	{"TRAP",	SIGTRAP},
	{"URG",		SIGURG},
	{"VTALRM",	SIGVTALRM},
	{"XCPU",	SIGXCPU},
	{"XFSZ",	SIGXFSZ},

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

int lookup_signal(const char *arg)
{
	int i = -1;

	if( !strncasecmp("SIG", arg, 3) )
		arg = arg + 3;

	if( *arg == '\0' ) 
		return 0;

	while( signals[++i].name ) {
		if( !strcasecmp(signals[i].name, arg) )
			return signals[i].sig;
	}

	return 0;
}

int isnumber(const char *str)
{
	while(*str != '\0')
	{
		if(!isdigit(*str++)) return 0;
	}

	return 1;
}

int main(int argc, char *argv[])
{
	int opt_show_signals = 0;
	int opt_signal = SIGTERM;

	if( argc == 1 )
		show_usage(argv[0]);
	int opt;

	if( argv[1][0]=='-' && (isdigit(argv[1][1]) || strlen(argv[1]) > 2) ) {
		if( isdigit(argv[1][1]) ) {
			opt_signal = atoi(&argv[1][1]);
		} else {
			show_usage(argv[0]);
		}
	} else {
		while ((opt = getopt(argc, argv, "ls:")) != -1) {
			switch(opt) {
				case 's':
					if( isnumber(optarg) ) 
						opt_signal = atoi(optarg);
					else
						opt_signal = lookup_signal(optarg);

					if( opt_signal == 0 )
						errx(EXIT_FAILURE, "%s: invalid signal", optarg);
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

	int failure = 0;

	for( int i = optind; i < argc; i++ ) {
		if( argv[i][0] == '%' ) {
			warnx("%s: job IDs are not supported", argv[i]);
			failure = 1;
			continue;
		}
		pid_t pid = atoi(argv[i]);
		if( pid == 0 ) {
			warnx("%s: arguments must be a numerical PID", argv[i]);
			failure = 1;
		} else if( kill(pid, opt_signal) == -1 ) {
			warn("(%d)", pid);
			failure = 1;
		}
	}

	if( failure )
		exit(EXIT_FAILURE);
}
