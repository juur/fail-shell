#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <err.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <uchar.h>

struct charmap_entry {
    struct charmap_entry *next;
    char32_t codepoint;
    int len;
    int spacing;
    unsigned char byteseq[];
};

static char *opt_charmap = NULL;
static char *opt_sourcefile = NULL;
static char *opt_code_set_name = NULL;
static char *opt_name = NULL;
static bool opt_force_create = false;

[[gnu::nonnull]] static void show_usage(FILE *out)
{
    fprintf(out, "Usage: localdef [-c] [-f charmap] [-i sourcefile] [-u code_set_name] name\n");
}

[[gnu::nonnull]] static inline void trim(char *txt)
{
    register char *ptr = txt + strlen(txt) - 1;

    while (ptr >= txt && isspace(*ptr))
        *(ptr--) = '\0';
}

[[gnu::nonnull]] static inline uint32_t codepoint_conv(const char *from)
{
    const char *ptr = from;

    if (*ptr++ != '<')
        return -1;
    if (*ptr++ != 'U')
        return -1;

    const char *start = ptr;

    while (*ptr)
    {
        if (*ptr == '>')
            break;

        if (!isxdigit(*ptr))
            return -1;

        ptr++;
    }

    char buf[20];
    char *endptr;

    memset(buf, 0, sizeof(buf));
    memcpy(buf, start, ptr - start);
    
    long ret = strtol(buf, &endptr, 16);

    if (*endptr != '\0')
        return -1;

    if (errno == ERANGE && (ret == LONG_MAX || ret == LONG_MIN))
        return -1;

    if (((uint32_t)ret) > UINT32_MAX)
        return -1;

    //printf("codepoint_conv: <%s> = %lu\n", buf, ret);

    return ret;
}

[[gnu::nonnull]] static int byteseq_conv(unsigned adjust, const char *arg, char escape_char, int mb_cur_min, int mb_cur_max, unsigned char **out)
{
    const char *ptr;
    const char *num_start;
    char *endptr;
    long value;
    int pos;
    unsigned char *byteseq;

    if ((byteseq = malloc(mb_cur_max)) == NULL) {
        warn("byteseq_conv: malloc");
        return -1;
    }

    memset(byteseq, 0, mb_cur_min);
    *out = NULL;

    pos = 0;
    ptr = arg;

    while (*ptr)
    {
        if (pos >= mb_cur_max) {
            warnx("byteseq_conv: too many bytes (max %d)", mb_cur_max);
            return -1;
        }

        if (*ptr != escape_char) {
            warnx("byteseq_conv: missing escape_char '%c' in <%s> pos %d: %s", escape_char, arg, pos, ptr);
            return -1;
        }

        ptr++; /* skip past escape_char */
        num_start = ptr + 1; /* skip past number type */
        /* ptr is now 'x' */

        switch (*ptr)
        {
            case 'x':
                ptr++; /* skip past x */

                while (isxdigit(*ptr))
                    ptr++; /* skip past digits */

                value = strtol(num_start, &endptr, 16);
                if (endptr == num_start) {
                    warnx("byteseq_conv: not a number");
                    return -1;
                }
                if (errno == ERANGE && (value == LONG_MIN || value == LONG_MAX))
                    return -1;
                break;
            
            case 'd':
                ptr++;

                while (isdigit(*ptr)) 
                    ptr++;

                value = strtol(num_start, &endptr, 10);
                if (endptr == num_start)
                    return -1;
                if (errno == ERANGE && (value == LONG_MIN || value == LONG_MAX))
                    return -1;

                break;
            
            default:
                return -1;
        }
        //printf("byteseq_conv[%d]: %lx\n", pos, value);
        byteseq[pos] = value + adjust;
        pos++;
        //ptr++;
    }

    if (pos < mb_cur_min) {
        warnx("byteseq_conv: insufficient bytes (need %d)", mb_cur_min);
        return -1;
    }

    /*printf("ret: ");
    for (int i = 0; i < pos; i++)
        printf("%02x ", byteseq[i]);
    printf("\n");*/

    //printf("byteseq_conv: done\n");
    *out = byteseq;
    return pos;
}

[[gnu::nonnull]] static int parse_charmap(FILE *fp)
{
    enum state_enum { CM_HEADER, CM_CHARMAP, CM_WIDTHS };

    char comment_char = '%';
    char escape_char = '/';
    int mb_cur_min = 1;
    int mb_cur_max = 1;
    struct charmap_entry *cme_new, *cme_prev, *cme_first;
    char *keyword, *arg, *line, *from, *to;
    bool is_range, running;
    char32_t from_u32, to_u32;
    ssize_t rc;
    size_t line_len;
    enum state_enum state; 
    int line_num, width;

    keyword = NULL;
    arg = NULL;
    line = NULL;
    line_num = -1;
    state = CM_HEADER;
    running = true;
    line_len = 0;
    cme_new = NULL;
    cme_prev = NULL;
    cme_first = NULL;

    while (running)
    {
        if (keyword) {
            free(keyword);
            keyword = NULL;
        }

        if (arg) {
            free(arg);
            arg = NULL;
        }

        if ((rc = getline(&line, &line_len, fp)) == -1) {
            running = false;
            continue;
        }

        if (line == NULL)
            continue;

        line_num++;

        if (*line == comment_char)
            continue;

        trim(line);

        if (strlen(line) == 0)
            continue;
        
        //printf("line_num: %d\n", line_num);
        
        switch(state)
        {
            case CM_HEADER:
                if (*line == '<') {
                    if (sscanf(line, "%ms %ms ", &keyword, &arg) != 2) {
                        warnx("invalid keyword on line %d\n", line_num);
                    } else if (!strcmp("<comment_char>", keyword)) {
                        if (strlen(arg) != 1) {
                            warnx("invalid comment_char <%s> on line %d", arg, line_num);
                        } else
                            comment_char = *arg;
                    } else if(!strcmp("<mb_cur_min>", keyword)) {
                        if ((mb_cur_min = atoi(arg)) <= 0)
                            warnx("invalid mb_cur_min <%s> on line %d", arg, line_num);
                    } else if(!strcmp("<mb_cur_max>", keyword)) {
                        if ((mb_cur_max = atoi(arg)) <= 0)
                            warnx("invalid mb_cur_max <%s> on line %d", arg, line_num);
                    } else if(!strcmp("<escape_char>", keyword)) {
                        if (strlen(arg) != 1) {
                            warnx("invalid escape_char <%s> on line %d", arg, line_num);
                        } else
                            escape_char = *arg;
                    }
                } else if (!strcmp(line, "CHARMAP")) {
                    state = CM_CHARMAP;
                } else if (!strcmp(line, "WIDTH")) {
                    state = CM_WIDTHS;
                } else
                    warnx("invalid header on line %d\n", line_num);
                break;

            case CM_CHARMAP:
                if (!strcmp(line, "END CHARMAP")) {
                    state = CM_HEADER;
                } else if (sscanf(line, "%ms %ms %*[^\n]", &keyword, &arg) < 2) {
                    warnx("invalid charmap on line %d", line_num);
                } else {
                    if (strstr(keyword, "..")) {
                        is_range = true;
                        from = strtok(keyword, "..");
                        to = strtok(NULL, "..");
                    } else {
                        is_range = false;
                        from = keyword;
                        to = NULL;
                    }

                    from_u32 = codepoint_conv(from);
                    if (is_range)
                        to_u32 = codepoint_conv(to);
                    else
                        to_u32 = (uint32_t)-1;
                    
                    //printf("from=<%s>[%4x] to=<%s>[%2x]\n",
                    //        from, from_u32, to ? to : "", to ? to_u32 : 0U);

                    /* from/to can be <Uxxxx> or <greater-than-sign> etc */

                    /* arg is 1 or more <escape_char>[xcd]<number> */
                    unsigned char *ret = NULL;
                    int len;
                    unsigned count;

                    count = is_range ? to_u32 - from_u32 + 1 : 1;

                    for (unsigned i = 0; i < count; i++) 
                    {
                        //printf("loop %d out of %d\n", i, count);
                        len = byteseq_conv(i, arg, escape_char, mb_cur_min, mb_cur_max, &ret);

                        if (ret == NULL || len <= 0) {
                            warnx("failed to build byteseq on line %d (%p, %d)", line_num, ret, len);
                            continue;
                        }

                        cme_prev = cme_new;

                        if ((cme_new = malloc(sizeof(struct charmap_entry) + len)) == NULL) 
                            warn("parse_charmap: malloc");
                        else {
                            cme_new->next = NULL;
                            cme_new->spacing = 1; /* TODO WIDTH_DEFAULT */
                            cme_new->codepoint = from_u32 + i;
                            cme_new->len = len;
                            memcpy(cme_new->byteseq, ret, len);
                            
                            if (cme_first == NULL)
                                cme_first = cme_new;
                            if (cme_prev)
                                cme_prev->next = cme_new;
                        }

                        free(ret);
                    }
                }

                break;

            case CM_WIDTHS:
                if (!strcmp(line, "END WIDTH")) {
                    state = CM_HEADER;
                } else if ((rc = sscanf(line, "%ms %d", &keyword, &width)) != 2) {
                    warnx("invalid width on line %d", line_num);
                } else {
                    if (strstr(keyword, "..")) {
                        is_range = true;
                        from = strtok(keyword, "..");
                        to = strtok(NULL, "..");
                    } else {
                        is_range = false;
                        from = keyword;
                        to = NULL;
                    }

                    from_u32 = codepoint_conv(from);
                    if (is_range)
                        to_u32 = codepoint_conv(to);
                    else
                        to_u32 = (char32_t)-1;

                    unsigned count;

                    count = is_range ? to_u32 - from_u32 :1;
                    struct charmap_entry *cme, *loop;
                    bool found;
                    cme = cme_first;
                    
                    for (unsigned i = 0; i < count; i++)
                    {
                        if (cme->codepoint != from_u32+i) {
                            found = false;
                            for (loop = cme, cme = cme->next; !found && cme != loop; cme = cme->next ? cme->next : cme_first) {
                                //printf("comparing %x with %x\n", cme->codepoint, from_u32 + i);
                                if (cme->codepoint == from_u32+i)
                                    found = true;
                            }
                            if (!found)
                                warnx("width without codepoint: %x", from_u32 + i);
                            else
                                cme->spacing = width;
                        }
                        //printf("%x width is %d\n", from_u32 + i, width);
                    }
                }
                break;
        }
    }

    if (cme_first == NULL)
        return -1;

    /*for (struct charmap_entry *cme = cme_first; cme; cme = cme->next)
    {
        printf("cp: 0x%x len:%d width:%d <", cme->codepoint, cme->len, cme->spacing);
        fwrite(cme->byteseq, 1, cme->len, stdout);
        putchar('\n');
    }*/

    return 0;
}

int main(int argc, char *argv[])
{
    {
        int opt;

        while ((opt = getopt(argc, argv, "cf:i:u:")) != -1)
        {
            switch (opt) 
            {
                case 'c':
                    opt_force_create = true;
                    break;

                case 'f':
                    opt_charmap = optarg;
                    break;

                case 'i':
                    opt_sourcefile = optarg;
                    break;

                case 'u':
                    opt_code_set_name = optarg;
                    break;

                default:
bad_usage:
                    show_usage(stderr);
                    exit(EXIT_FAILURE);
            }
        }

        if ((argc - optind) != 1)
            goto bad_usage;
    }

    opt_name = argv[optind++];

    if (opt_charmap != NULL) {
        FILE *charmap;

        if ((charmap = fopen(opt_charmap, "r")) == NULL)
            err(EXIT_FAILURE, "unable to open charmap <%s>", opt_charmap);

        parse_charmap(charmap);
        fclose(charmap);
    }
}
