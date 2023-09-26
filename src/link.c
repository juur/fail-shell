#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

static void show_usage(void)
{
    fprintf(stderr, "Usage: link file1 file2\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
        show_usage();

    if (link(argv[1], argv[2]))
        err(EXIT_FAILURE, "link");

    exit(EXIT_SUCCESS);
}
