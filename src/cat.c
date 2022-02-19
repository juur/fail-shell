#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <err.h>
#include <unistd.h>
#include <string.h>

static void show_usage()
{
	errx(EXIT_FAILURE, "Usage: cat [-u] [FILES...]");
}

static int opt_ndelay = 0;

/* read BUFSIZ blocks from file to stdout, call setvbuf on file as
 * appropriate */
static int cat_one_file(FILE *file)
{

	if (opt_ndelay)
		setvbuf(file, NULL, _IONBF, 0);

	int running = 1;
	char buf[BUFSIZ];

	while(running) 
	{
		if(fgets(buf, BUFSIZ, file) == NULL)
			running = 0;
		else if(fputs(buf, stdout) == EOF)
			running = 0;
	}

	if (ferror(file))
		return EXIT_FAILURE;
	else 
		return EXIT_SUCCESS;
}


int main(int argc, char *argv[])
{
	/* process command line options */
	{
		int opt;
		while ((opt = getopt(argc, argv, "u")) != -1) 
		{
			switch (opt) 
			{
				case 'u':
					opt_ndelay = 1;
					setvbuf(stdout, NULL, _IONBF, 0);
					break;

				default:
					show_usage();
			}
		}
	}

	int rc = EXIT_SUCCESS;

	/* special case for no files */
	if (optind >= argc) {
		rc = cat_one_file(stdin);
	} else {

		/* loop over each file argument */
		for(int i = optind; i < argc; i++ ) 
		{
			int is_stdin = 0;
			char *file_name = argv[i];

			/* skip over empty strings */
			if(file_name == NULL || *file_name == '\0') 
				continue;

			FILE *file = stdin;

			/* treat - as stdin, otherwise open the file for reading */
			if(strcmp(file_name, "-")) {
				if ((file = fopen(file_name, "r")) == NULL) {
					rc = EXIT_FAILURE;
					warn("%s", file_name);
					continue;
				}
			} else {
				is_stdin = 1;
			}

			/* perform the actual cat */
			if(cat_one_file(file)) {
				rc = EXIT_FAILURE;
				warn("%s", is_stdin ? "<stdin>" : file_name);
			}

			if(!is_stdin)
				fclose(file);
		}
	}

	exit(rc);
}


