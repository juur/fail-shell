#define _XOPEN_SOURCE 800

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char base64_values[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
    'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '+', '/'
};

static const int line_width = 76;

int main(void)
{
    int idx = 0;
    FILE *fp, *out_fp;

    fp = stdin;
    out_fp = stdout;

    fputs("begin-base64 644 -\n", out_fp);

    while(1)
    {
        uint8_t buf[3], words[4];
        uint32_t word;
        ssize_t rc;
        int tmp;

        rc = fread(buf, 1, sizeof(buf), fp); 

        if (rc == 0 && ferror(fp))
            exit(EXIT_FAILURE);

        if (rc == 0)
            goto done;

        tmp = rc;

        word = buf[0];

        if (--tmp) {
            word <<= 8;
            word |= buf[1];
        }

        if (--tmp) {
            word <<=8;
            word |= buf[2];
        }

        word <<= (3 - rc) * 8;

        if (rc == 3)
            words[3] = (word & 0x3f);
        word >>= 6;
        
        if (rc >= 2)
            words[2] = (word & 0x3f);
        word >>= 6;
        
        words[1] = (word & 0x3f);
        word >>= 6;
        
        words[0] = (word & 0x3f);

        fputc(base64_values[words[0]], out_fp);
        if(++idx == line_width) { 
            fputc('\n', out_fp);
            idx = 0;
        }
        
        fputc(base64_values[words[1]], out_fp);
        if(++idx == line_width) {
            fputc('\n', out_fp);
            idx = 0;
        }

        if(rc >= 2) {
            fputc(base64_values[words[2]], out_fp);
            if(++idx == line_width) {
                fputc('\n', out_fp);
                idx = 0;
            }
        }

        if(rc == 3) {
            fputc(base64_values[words[3]], out_fp);
            if(++idx == line_width) {
                fputc('\n', out_fp);
                idx = 0;
            }
        }

        if(rc < 3) {
            fputc('=', out_fp);
            if(++idx == line_width) {
                fputc('\n', out_fp);
                idx = 0;
            }
        }

        if(rc < 2) {
            fputc('=', out_fp);
            if(++idx == line_width) {
                fputc('\n', out_fp);
                idx = 0;
            }
        }

    }

done:
    if (idx != line_width)
        fputc('\n', out_fp);

    fputs("====\n", out_fp);

    exit(EXIT_SUCCESS);
}
