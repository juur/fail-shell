#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

static void show_usage(void)
{
    fprintf(stderr, 
            "Usage: mv [-if] source_file target_file\n"
            "       mv [-if] source_file... target_dir\n"
           );
    exit(EXIT_FAILURE);
}

static int opt_force = 0;
static int opt_interactive = 0;

static int do_mv(const char *from, const char *to)
{
    struct stat sb_to, sb_from;

    if (stat(from, &sb_from)) {
        warn("stat: <%s>", sb_from);
        return 1;
    }

    if (stat(to, &sb_to) || access(to, W_OK)) {

    }

    return 1;
}

int main(int argc, char *argv[])
{
    char opt;
    while ((opt = getopt(argc, argv, "if")) != -1)
    {
        switch (opt)
        {
            case 'f':
                opt_force = 1;
                opt_interactive = 0;
                break;
            case 'i':
                opt_interactive = 1;
                opt_force = 0;
                break;

            default:
                show_usage();
        }
    }

    if ( (optind >= argc) || (argc - optind < 2) ) {
        warnx("source_file and target_file required.");
        show_usage();
    }

    int failure = 0;

    if (argc - optind > 2) {
        struct stat sb;

        if (stat(argv[argc - 1], &sb))
            err(EXIT_FAILURE, "stat");

        if (!S_ISDIR(sb.st_mode))
            errx(EXIT_FAILURE, "not a directory: <%s>\n", argv[argc - 1]);

        for (int i = optind; i < argc-1; i++) {
            failure += do_mv(argv[i], argv[argc - 1]);
        }
    }

    exit(failure ? EXIT_FAILURE : EXIT_SUCCESS);
}
