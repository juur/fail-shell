#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <err.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <libgen.h>
#include <sys/stat.h>
#include <termios.h>

#define BUF_SIZE	0x4000

static int invoke_shell(char *, int);

/* check if a NULL terminated string is a valid sequence of digits */
static int isnumber(const char *str)
{
	while(*str)
	{
		if(!isdigit(*str++)) return 0;
	}

	return 1;
}

/* check if a NULL/length terminated string is a valid POSIX 'Name' */
static int isname(const char *str, int max_len)
{
	if (!(*str == '_' || isalpha(*str))) return 0;

	int len = 0;

	while (*++str && (len++ < max_len))
	{
		if (isalnum(*str)) continue;
		else if (*str == '_') continue;
		else return 0;
	}
	return 1;
}

static inline int isnewline(const char c)
{
	return c == '\n';
}

static int opt_command_string = 0;
static int opt_interactive = 0;
static int opt_read_stdin = 0;

/* enviromental ones */
static int opt_allexport = 0;
static int opt_notify = 0;
static int opt_noclobber = 0;
static int opt_errexit = 0;
static int opt_noglob = 0;
static int opt_monitor = 0;
static int opt_noexec = 0;
static int opt_nounset = 0;
static int opt_verbose = 0;
static int opt_xtrace = 0;

/* umask builtin */
static int cmd_umask(int argc, char *argv[])
{
	const char *usage = "Usage: umask [-S] [mask]";

	int opt_symbolic = 0;
	int mask = -1;
	{
		int opt;
		while ((opt = getopt(argc, argv, "S")) != -1)
		{
			switch (opt)
			{
				case 'S':
					opt_symbolic = 1;
					break;
				default:
					warnx(usage);
					return EXIT_FAILURE;
			}
		}
		if (argc - optind > 1) {
			warnx(usage);
			return EXIT_FAILURE;
		}
		if (argc - optind == 1) {
			if (isnumber(argv[optind])) {
				mask = atoi(argv[optind]);
			} else {
				warnx("mask (%s) is not numerical", argv[optind]);
				return EXIT_FAILURE;
			}
		}
	}

	if (mask == -1) {
		mode_t um = umask(0);
		umask(um);
		if (opt_symbolic)
			printf("symbolic not supported\n");
		printf("%04u\n", um);
	} else
		umask(mask);

	return EXIT_SUCCESS;
}

/* replace the current process with another, effectively implements exec */
static int execute(const int ac, char *av[])
{
	execvp(av[0], av);
	warn(av[0]);
	return EXIT_FAILURE;
}

/* execute a builtin in a seperate process. builtins that should run in the
 * shell process, should not use this */
static int builtin(int (*func)(int, char **), int ac, char **av)
{
	int status;

	if(ac == 0 || av == NULL) {
		warn("builtin: no args");
		return EXIT_FAILURE;
	}

	pid_t newpid = fork();

	if(newpid == -1) {
		warn(av[0]);
		return EXIT_FAILURE;
	} else if(newpid == 0) {
		exit(func(ac,av));
	} else {
		if(waitpid(newpid, &status, 0) == -1) {
			warn(av[0]);
			return EXIT_FAILURE;
		}
		if(WIFEXITED(status)) {
			return WEXITSTATUS(status);
		} else
			return EXIT_SUCCESS;
	}
}

static int process_args(const char *buf, int *argc, char ***argv, char **next, int rc)
{
	const char *ptr = buf;
	int ac = 0;
	char **av = NULL;
	int in_single_quote = 0;
	int in_double_quotes = 0;
	*next = NULL;

	/* outer loop, each interation skips unquoted spaces & unprintable characters
	 * before processing a single argument, pushing onto *argv */
	while(*ptr != '\0')
	{
		if(isspace(*ptr)) goto next;
		if(!isprint(*ptr)) goto next;
		if(*ptr == ';') goto next;

		/* the length of this argument */
		int len = 0;
		char *tmp = (char *)ptr;

		char buf[BUFSIZ+1];
		char *dst = buf;
		memset(dst, 0, BUFSIZ+1);

		/* inner loop, extracts a single argument. handles single & double quotes
		 * as well as backslask escapes */
		while(*tmp && isprint(*tmp)) {
			if (!in_single_quote && *tmp == '\\') {
				if (!*(tmp+1)) {
					warnx("trailing escape '\\' at end");
					goto clean_nowarn;
				}
				*(dst++) = *(tmp+1);
				memmove((void *)tmp, tmp+1, strlen(tmp));
				len++;
				tmp++;
				continue;
			} else if (!in_single_quote && !in_double_quotes && *tmp == '\'') {
				in_single_quote = 1;
				ptr++;
				len--;
				tmp++;
				continue;
			} else if (!in_single_quote && !in_double_quotes && *tmp == '"') {
				in_double_quotes = 1;
				ptr++;
				len--;
				tmp++;
				continue;
			} else if (in_single_quote && *tmp == '\'') {
				in_single_quote = 0;
				len++;
				tmp++;
				break;
			} else if (in_double_quotes && *tmp == '"') {
				in_double_quotes = 0;
				len++;
				tmp++;
				break;
			} else if (!in_single_quote && !in_double_quotes && isspace(*tmp)) {
				break;
			} 
			/* handle $ followed by something other than whitespace */
			if (!in_single_quote && *tmp == '$' && *(tmp+1) && !isspace(*(tmp+1))) {
				char nextch = *(tmp+1);
				char *end = NULL;
				char *var = NULL;
				char *tmpstr = NULL;
				int wrl;

				/* $ENV */
				if (isalpha(nextch)) 
				{
					end = tmp + 2;
					wrl = 1;
					while (*end && isalnum(*end++)) wrl++;
					
					if ((tmpstr = strndup(tmp + 1, wrl)) == NULL) {
						warn(NULL);
						goto clean_nowarn;
					}

					if ((var = getenv(tmpstr)) != NULL)
					{
						wrl = snprintf(dst, BUFSIZ - (dst - buf), "%s", var);
						dst += wrl;
						len += wrl;
					}
					free(tmpstr);
					tmp = end;
				} 
				/* $0 - $9 */
				else if (isdigit(nextch)) 
				{
					wrl = snprintf(dst, BUFSIZ - (dst - buf), "ARGV[%d]", nextch - 0x30);
					len += wrl;
					dst += wrl;
					tmp+=2;
				}
				/* $?... */
				else 
				{

					switch (*(tmp+1))
					{
						case '{':
							end = strchr(tmp+2, '}');
							if (end == NULL) {
								warnx("unterminated ${}");
								goto clean_nowarn;
							}
							*end = '\0';
							if ((var = getenv(tmp+2)) != NULL)
							{
								wrl = snprintf(dst, BUFSIZ - (dst - buf), "%s", var);
								dst += wrl;
								len += wrl;
							}
							tmp = end + 1;
							break;

						case '(':
							{
								int open = 1;
								int inner_double = 0, inner_single = 0;
								var = tmp+2;
								while (*var)
								{
									if (!inner_single && *var == '\\') {
										if (!*(var+1)) {
											warnx("trailing escape '\\' at end");
											goto clean_nowarn;
										}
										var++;
									} else if (!inner_single && !inner_double && *var == '\'') {
										inner_single = 1;
									} else if (!inner_single && !inner_double && *var == '"') {
										inner_double = 1;
									} else if (inner_single && *var == '\'') {
										inner_single = 0;
									} else if (inner_double && *var == '"') {
										inner_double = 0;
									} else if ((inner_single && *var != '\'') || 
											(inner_double && *var != '"')) {
										;
									} else {
										if (*var=='(') open++;
										if (*var==')') open--;
										if (!open) break;
									}
									var++;
								}
								if (open) {
									warnx("unterminated ) %d %d", inner_double, inner_single);
									goto clean_nowarn;
								}
								*var = '\0';
								puts("invoke from (\n");
								invoke_shell(tmp+2, rc);
								tmp = var + 1;
							}
							break;

						case '?':
							wrl = snprintf(dst, BUFSIZ - (dst - buf), "%d", rc);
							dst += wrl;
							len += wrl;
							tmp+=2;
							break;

						default:
							*(dst++) = *tmp;
							len++;
							tmp++;
							break;
					}
				}
			} else {
				*(dst++) = *tmp;
				len++;
				tmp++;
			}
		}

		if (in_single_quote) {
			warnx("unterminated single quote");
			goto clean_nowarn;
		}

		if (in_double_quotes) {
			warnx("unterminated double quotes");
			goto clean_nowarn;
		}
		
		/* if we have an argument, grow *argv and append a pointer */
		if(len>0) {
			*dst = '\0';

			char **nav = realloc(av, sizeof(char *) * (ac+1));
			if (nav == NULL) goto clean_fail;
			av = nav;

			if (!strcmp(buf, ";")) {
				puts("solo\n");
				*next = tmp;
				break;
			} 

			//av[ac] = strndup(ptr, len);
			av[ac] = strdup(buf);
			if (av[ac] == NULL) goto clean_fail;

			ac++;

			if(buf[(len = (strlen(buf)-1))] == ';') 
			{
				*next = tmp;
				*(av[ac-1]+len) = '\0';
				break;
			}
		}
		ptr = tmp; // was before }
next:
		ptr++;
		continue;
	}
	
	/* ensure *argv is NULL terminated */
	char **nav = realloc(av, sizeof(char *) * (ac+1));
	if (nav == NULL) goto clean_fail;
	av = nav;

	av[ac] = NULL;

	*argc = ac;
	*argv = av;

	return EXIT_SUCCESS;

clean_fail:
	warn(NULL);
clean_nowarn:
	if (av) {
		for (int i = 0; i < ac; i++)
		{
			if (av[i])
				free(av[i]);
		}
		free(av);
	}
	return EXIT_FAILURE;
}

static int cmd_pwd(int argc, char *argv[])
{
	char pwd[BUFSIZ];
	if (getcwd(pwd, BUFSIZ) == NULL)
		err(EXIT_FAILURE, NULL);
	printf("%s\n", pwd);
	exit(EXIT_SUCCESS);
}

static int cmd_cd(int argc, char *argv[])
{
	char *dir = NULL;

	if(argc == 1) {
		dir = getenv("HOME");
	} else {
		dir = argv[1];
	}

	if(dir == NULL) {
		fprintf(stderr, "%s: HOME not set\n", argv[0]);
		return EXIT_FAILURE;
	}

	int hyphen = !strcmp("-", argv[1]);

	if(hyphen) {
		dir = getenv("OLDPWD");
		if(dir == NULL) {
			fprintf(stderr, "%s: OLDPWD not set\n", argv[0]);
			return EXIT_FAILURE; 
		}
	}

	char oldpwd[BUFSIZ]; 
	getcwd(oldpwd, BUFSIZ);

	if(chdir(dir) == -1) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], dir, strerror(errno));
		return EXIT_FAILURE;
	}

	char pwd[BUFSIZ];
	getcwd(pwd, BUFSIZ);

	setenv("OLDPWD", oldpwd, 1);
	setenv("PWD", pwd, 1);

	if(hyphen)
		printf("%s\n", pwd);

	return EXIT_SUCCESS;
}

static int cmd_basename(int argc, char *argv[])
{
	//char *suffix = NULL;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage: %s string [suffix]\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	//if (argc == 3)
	//	suffix = argv[2];

	char *bname = basename(argv[1]);

	// TODO implement suffix here
	
	printf("%s\n", bname);
	exit(EXIT_SUCCESS);
}

static void trim(char *buf)
{
	char *ptr = buf + strlen(buf) - 1;
	while (*ptr && isspace(*ptr)) *ptr-- = '\0';
}

static int inside_if = 0;
static int inside_then = 0;
static int inside_else = 0;

static int invoke_shell(char *line, int old_rc)
{
	int argc = 0;
	char **argv = NULL;
	char *next;


	/* skip leading and trailing white space */
	while (*line && isspace(*line)) line++;
	trim(line);

	//printf("invoke_shell(%s)\n", line);
	if (process_args(line, &argc, &argv, &next, old_rc) != EXIT_SUCCESS)
		return EXIT_FAILURE;
	//printf(" argc=%d, rest=%s\n", argc, next);
	

	if(argc == 0 || argv == NULL || argv[0] == NULL)
		return EXIT_SUCCESS;

	int rc = old_rc;

	/* line us now useless, as expansions will have happened */
	int arglen = 0;
	char *allargs = calloc(1,2);
	for (int i = 1; i < argc; i++)
	{
		arglen += strlen(argv[i]);
		if(!arglen) continue;
		allargs = realloc(allargs, arglen+2);
		strcat(allargs, argv[i]);
		strcat(allargs, " ");
	}

	if(!strcmp(argv[0], "if")) {
		if (inside_if && !inside_then) goto unexpected;
		inside_if++;
		//printf(" invoke from if\n");
		rc = invoke_shell(allargs, old_rc);
	} else if(!strcmp(argv[0], "then")) {
		if (!inside_if || inside_then) goto unexpected;
		inside_then++;
		//printf(" invoke from then");
		if (!rc) rc = invoke_shell(allargs, rc);
	} else if(!strcmp(argv[0], "else")) {
		if (!inside_if || !inside_then) goto unexpected;
		if (inside_then) inside_then--;
		inside_else++;
		//printf(" invoke from else");
		if (rc) rc = invoke_shell(allargs, rc);
	} else if(!strcmp(argv[0], "fi")) {
		if (!inside_if && !(inside_then || inside_else)) goto unexpected;
		if (inside_then) inside_then--;
		if (inside_else) inside_else--;
		inside_if--;
	} else if(!strcmp(argv[0], "umask")) {
		rc = cmd_umask(argc, argv);
	} else if(!strcmp(argv[0], "cd")) {
		rc = cmd_cd(argc, argv);
	} else if(!strcmp(argv[0], "pwd")) {
		rc = builtin(cmd_pwd, argc, argv);
	} else if(!strcmp(argv[0], "basename")) {
		rc = builtin(cmd_basename, argc, argv);
	} else if(!strcmp(argv[0], "exit")) {
		int val = 0;
		if(argc == 2)
			val = atoi(argv[1]);
		exit(val);
	} else {
		rc = builtin(execute, argc, argv);
	}

done:
	for (int i = 0; i < argc; i++ )
	{
		if (argv[i])
			free(argv[i]);
	}
	free(argv);

	if (next) {
		//fprintf(stdout, " recurse: if=%d, then=%d, else=%d, next=%s\n",
		//		inside_if, inside_then, inside_else, next);
		rc = invoke_shell(next, rc);
	}

	return rc;
unexpected:
	warnx("unexpected %s (if=%d,then=%d,else=%d)", argv[0],
			inside_if, inside_then, inside_else);
	inside_if = inside_then = inside_else = 0;
	goto done;
}

static int parse_set(char mod, char opt)
{
	int add = mod == '+' ? 1 : 0;

	switch (opt)
	{
		case 'a':
			opt_allexport = add;
			break;
		case 'b':
			opt_notify = add;
			break;
		case 'C':
			opt_noclobber = add;
			break;
		case 'e':
			opt_errexit = add;
			break;
		case 'f':
			opt_noglob = add;
			break;
		case 'm':
			opt_monitor = add;
			break;
		case 'n':
			opt_noexec = add;
			break;
		case 'u':
			opt_nounset = add;
			break;
		case 'v':
			opt_verbose = add;
			break;
		case 'x':
			opt_xtrace = add;
			break;
		default:
			warnx("%c: unknown set option", opt);
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

static int parse_set_option(char *opt)
{
	return EXIT_SUCCESS;
}

static void reset_terminal()
{
	struct termios tios;
	if (tcgetattr(STDIN_FILENO, &tios) == -1)
		err(EXIT_FAILURE, "unable to reset terminal");

	tios.c_lflag |= (ICANON|ECHO);

	if (tcsetattr(STDIN_FILENO, TCSANOW, &tios) == -1)
		err(EXIT_FAILURE, "unable to reset terminal");
}

int main(int ac, char *av[])
{
	char *command_string = NULL;
	char *command_name = NULL; 
	char *command_file = NULL;

	{
		int opt;
		while ((opt = getopt(ac, av, "abCefhimnuvxo:cs")) != -1)
		{
			switch (opt)
			{
				case 'a':
				case 'b':
				case 'C':
				case 'e':
				case 'f':
				case 'm':
				case 'n':
				case 'u':
				case 'v':
				case 'x':
					if (parse_set('-',opt))
						exit(EXIT_FAILURE);
					break;
				case 'o':
					if (parse_set_option(optarg))
						exit(EXIT_FAILURE);
					break;

				case 'c':
					opt_command_string = 1;
					break;
				case 'i':
					opt_interactive = 1;
					break;
				case 's':
					opt_read_stdin = 1;
					break;
			}
		}

		if (optind < ac && (!strcmp(av[optind], "-") || !strcmp(av[optind], "--")))
			optind++;

		if (optind >= ac) {
			if (opt_command_string)
				errx(EXIT_FAILURE, "missing command string");
			opt_read_stdin = 1;
			goto opt_skip;
		}


		if (opt_command_string)
			command_string = av[optind++];

		if (optind >= ac) goto opt_skip;

		if (opt_command_string)
			command_name = av[optind++];
		else if (!opt_read_stdin)
			command_file = av[optind++];
opt_skip:	
		;

		if (!command_string && !command_name && !command_file && isatty(STDIN_FILENO))
			opt_interactive = 1;
	}

	printf("zero-shell\nNB: this is not yet a proper implementation of sh(1)\n\n");

	if (opt_command_string ) {
		int rc = EXIT_SUCCESS;
		exit(rc);
	}

	if (!opt_read_stdin && command_string) {
		int rc = EXIT_SUCCESS;
		exit(rc);
	}

	if (opt_interactive && isatty(STDIN_FILENO))
	{
		/*
		struct termios tios;
		if (tcgetattr(STDIN_FILENO, &tios) == -1)
			err(EXIT_FAILURE, NULL);

		tios.c_lflag &= ~(ICANON|ECHO);
		*/
		atexit(reset_terminal);
		/*
		if (tcsetattr(STDIN_FILENO, TCSANOW, &tios) == -1)
			err(EXIT_FAILURE, NULL);
			*/
	}

	/* -i and/or -s */
	char buf[BUFSIZ];
	char *line = buf;
	*line = '\0';
	//int c;
	//int newline = 1;
	int rc = 0;
	while(1)
	{
		/*
		if (opt_interactive)
		{
			if (line < buf) line = buf;
			else if (line >= buf+sizeof(buf)) line = buf + sizeof(buf) - 1;

			if (newline) {*/
				if (printf("# ") < 0)
					exit(EXIT_FAILURE);

				fflush(stdout);/*
				newline = 0;
			}

			if ((c = fgetc(stdin)) == EOF) {
				exit(feof(stdin) ? EXIT_SUCCESS : EXIT_FAILURE);
			}

			if (c == '\n' ) {
				*line = '\0';
				fputc(c, stdout);
				rc = invoke_shell(buf, rc);
				memset(buf, 0, strlen(buf)+1);
				line = buf;
				newline = 1;
			} else {
				*line++ = c;
				fputc(c, stdout);
			}
		}

		if (!opt_interactive)
		{*/
			line = fgets(buf, BUFSIZ, stdin);
		
			if(line == NULL) {
				if(feof(stdin))
					exit(EXIT_SUCCESS);
				exit(EXIT_FAILURE);
			}

			rc = invoke_shell(buf, rc);
		/*}

		if (opt_interactive) {*/
			fflush(stdout);
			fflush(stderr);
		//}
	}
}
