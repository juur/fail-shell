#define _XOPEN_SOURCE 700

#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include <err.h>
#include <string.h>

int main(int argc, char *argv[])
{
	char *suffix = NULL;

	if (argc < 2 || argc > 3)
		errx(EXIT_FAILURE, "Usage: basename string [suffix]");

	if (!*argv[1]) {
		printf("\n");
		exit(EXIT_SUCCESS);
	}

	if (argc == 3)
		suffix = argv[2];

	char *bname = strdup(basename(argv[1]));

	if (!bname)
		err(EXIT_FAILURE, NULL);

	if (suffix && *suffix) {
		int suf_len = strlen(suffix);
		int bn_len = strlen(bname);
		
		if (suf_len && bn_len) {
			char *suf_start = bname + bn_len - suf_len;
			if (!strcmp(suffix, suf_start))
				*suf_start = '\0';
		}
	}
done:	
	printf("%s\n", bname);
	free(bname);

	exit(EXIT_SUCCESS);
}
