#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <err.h>
#include <signal.h>

static _Noreturn void show_version(FILE *fp, int rc)
{
    fprintf(fp,
            "tee 0.1.0\n"
            "\n"
            "Copyright (C) 2025 github.com/juur\n"
           );
    exit(rc);
}

static _Noreturn void show_usage(FILE *fp, int rc, const char *name)
{
    fprintf(fp,
            "tee duplicates standard input\n"
            "Usage: %s [-aihv] [file..]\n"
            "\n"
            "Options:\n"
            "  -h    show this help\n"
            "  -a    append to each file\n"
            "  -i    ignore SIGINT\n"
            "  -v    display version information\n"
            "\n",
            name
            );
    exit(rc);
}

static bool opt_append_files = false;
static bool opt_ignore_sigint = false;
static int num_files;
static FILE **files;

static void clean_files(void)
{
    if (files == NULL)
        return;

    for (int i = 0; i < num_files; i++)
        if (files[i])
            fclose(files[i]);

    free(files);
}

int main(int argc, char *argv[])
{
    {
        int opt = 0;

        while ((opt = getopt(argc, argv, "aih?v")) != -1)
        {
            switch (opt)
            {
                case 'h':
                case '?':
                    show_usage(stdout, EXIT_SUCCESS, argv[0]);
                case 'v':
                    show_version(stdout, EXIT_SUCCESS);
                case 'a':
                    opt_append_files = true;
                    break;
                case 'i':
                    opt_ignore_sigint = true;
                    break;
                default:
                    show_usage(stderr, EXIT_FAILURE, argv[0]);
            }
        }
    }

    if (opt_ignore_sigint) {
        struct sigaction sa;

        sa.sa_handler = SIG_IGN;
        sa.sa_flags = 0;

        sigemptyset(&sa.sa_mask);

        if (sigaction(SIGINT, &sa, NULL) == -1)
            err(EXIT_FAILURE, "main:sigaction(SIGINT)");
    }

    num_files = argc - optind;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (num_files) {
        if ((files = calloc(num_files + 1, sizeof(FILE *))) == NULL)
            err(EXIT_FAILURE, "main:calloc");

        atexit(clean_files);

        for (int i = 0; i < num_files; i++)
        {
            if ((files[i] = fopen(argv[optind + i],
                            opt_append_files ? "a" : "w")) == NULL)
                err(EXIT_FAILURE, "main:fopen");
            setvbuf(files[i], NULL, _IONBF, 0);
        }
    }

    char buf[BUFSIZ];
    bool running = true;
    size_t rc;

    while (running)
    {
        rc = fread(buf, 1, BUFSIZ, stdin);
        
        if (ferror(stdin))
            err(EXIT_FAILURE, "fread(stdin)");
        
        if (rc == 0) {
            running = false;
            continue;
        }

        fwrite(buf, 1, rc, stdout);
        if (ferror(stdout))
            err(EXIT_FAILURE, "fwrite(stdout)");

        for (int i = 0; i < num_files; i++) {
            fwrite(buf, 1, rc, files[i]);
            if (ferror(files[i]))
                err(EXIT_FAILURE, "fwrite(%s)", argv[optind + i]);
        }

        if (feof(stdin))
            running = false;
    }

    exit(EXIT_SUCCESS);
}
