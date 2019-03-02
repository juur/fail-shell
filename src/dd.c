#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* file handles for if= and of= */
static int		fh_if = -1;
static int		fh_of = -1;

/* global options */
static char*	opt_if = NULL;
static char*	opt_of = NULL;
static ssize_t	opt_ibs = 512;
static ssize_t	opt_obs = 512;
static ssize_t	opt_bs = 0;
static ssize_t	opt_cbs = 0;
static ssize_t	opt_skip = 0;
static ssize_t	opt_seek = 0;
static ssize_t	opt_count = 0;
static int		opt_conv = 0;

/* operands from the command line */
typedef struct {
	const char	*name;
	void		*value;
	const int	 type;
} oper_t;

#define TYPE_CONV	0
#define	TYPE_BYTE	1
#define TYPE_STR	2
#define	TYPE_LONG	3

static const oper_t oper_lst[] = {

	{ "if",		&opt_if,	TYPE_STR },
	{ "of",		&opt_of,	TYPE_STR },
	{ "ibs",	&opt_ibs,	TYPE_BYTE },
	{ "obs",	&opt_obs,	TYPE_BYTE },
	{ "bs",		&opt_bs,	TYPE_BYTE },
	{ "cbs",	&opt_cbs,	TYPE_BYTE },
	{ "skip",	&opt_skip,	TYPE_LONG },
	{ "seek",	&opt_seek,	TYPE_LONG },
	{ "count",	&opt_count,	TYPE_LONG },
	{ "conv",	NULL,		TYPE_CONV },

	{NULL, NULL, 0}
};

/* conversion specifiers for conv= */
typedef struct {
	const char	*name;
	const int	 val;
} conv_t;

#define CONV_ASCII		(1 << 0)
#define	CONV_EBCDIC		(1 << 1)
#define CONV_IBM		(1 << 2)
#define CONV_BLOCK		(1 << 3)
#define CONV_UNBLOCK	(1 << 4)
#define CONV_LCASE		(1 << 5)
#define CONV_UCASE		(1 << 6)
#define CONV_SWAB		(1 << 7)
#define CONV_NOERROR	(1 << 8)
#define CONV_NOTRUNC	(1 << 9)
#define CONV_SYNC		(1 << 10)

static const conv_t conv_lst[] = {

	{ "ascii",		CONV_ASCII	 },
	{ "ebcdic",		CONV_EBCDIC	 },
	{ "ibm",		CONV_IBM	 },
	{ "block",		CONV_BLOCK	 },
	{ "unblock",	CONV_UNBLOCK },
	{ "lcase",		CONV_LCASE	 },
	{ "ucase",		CONV_UCASE	 },
	{ "swab",		CONV_SWAB	 },
	{ "noerror",	CONV_NOERROR },
	{ "notrunc",	CONV_NOTRUNC },
	{ "sync",		CONV_SYNC	 },

	{NULL, 0}
};

/* parse a conversion specifier */
static void check_conv(const char *restrict val)
{
	const conv_t *c = NULL;
	for (int i = 0; conv_lst[i].name; i++)
	{
		if (!strcmp(conv_lst[i].name, val)) {
			c = &conv_lst[i];
			break;
		}
	}

	if (c == NULL)
		errx(EXIT_FAILURE, "%s: unknown conversion specifier", val);

	if (	(c->val == CONV_ASCII && (opt_conv & (CONV_EBCDIC|CONV_IBM))) ||
			(c->val == CONV_EBCDIC && (opt_conv & (CONV_ASCII|CONV_IBM))) ||
			(c->val == CONV_IBM && (opt_conv & (CONV_EBCDIC|CONV_ASCII))) )
		errx(EXIT_FAILURE, "conv=ascii,ebcdic,ibm are mutually exclusive");

	if (	(c->val == CONV_LCASE && (opt_conv & CONV_UCASE)) ||
			(c->val == CONV_UCASE && (opt_conv & CONV_LCASE)) )
		errx(EXIT_FAILURE, "conv=lcase,ucase are mutually exclusive");

	if (	(c->val == CONV_BLOCK && (opt_conv & CONV_UNBLOCK)) ||
			(c->val == CONV_UNBLOCK && (opt_conv & CONV_BLOCK)) )
		errx(EXIT_FAILURE, "conv=block,unblock are mutually exclusive");

	opt_conv |= c->val;
}

/* confirm NULL terminated string solely contains digits */
static bool isnumber(const char *restrict val)
{
	for (const char *p = val; *p; p++) if (!isdigit(*p)) return false;
	return true;
}

/* parse a long, exit if not a number */
static ssize_t parse_long(const char *restrict val)
{
	char *endptr = NULL;

	errno = 0;
	const ssize_t ret = strtol(val, &endptr, 10);

	if (*val == '\0' || *endptr != '\0') {
		if (errno)
			err(EXIT_FAILURE, "%s", val);
		else
			errx(EXIT_FAILURE, "%s: invalid number", val);
	}

	return ret;
}

/* parse a string of \d+, \d+[bk] or \d+x\d+ into a resultant byte count.
 * exit the program if invalid or an error */
static ssize_t parse_byte(const char *val)
{
	if (isnumber(val)) 
		return parse_long(val);

	if (strchr(val, 'x')) 
	{
		ssize_t a, b;
		if ((sscanf(val, "%lux%lu", &a, &b)) != 2)
			errx(EXIT_FAILURE, "%s: invalid byte format", val);
		return a * b;
	}

	const char last = val[strlen(val)-1];
	if (isalpha(last)) {
		
		char *tmp = strndup(val, strlen(val) - 2);
		const ssize_t num = parse_long(tmp);
		free(tmp); tmp = NULL;

		switch (last)
		{
			case 'b':
				return num * 512;
				break;
			case 'k':
				return num * 1024;
				break;
			default:
				errx(EXIT_FAILURE, "%s: invalid suffix", val);
		}
	}

	errx(EXIT_FAILURE, "%s: invalid byte specification", val);
}

/* statistical reporting variables */
static ssize_t block_read	= 0,	block_write		= 0;
static ssize_t partial_read = 0,	partial_write	= 0;
static ssize_t total_read	= 0,	total_write		= 0;
static ssize_t block_trunc	= 0;

/* pad character for sync */
static char pad;

/* read_block
 * 
 * populates in_buf with up to opt_ibs bytes of data.
 * applies conv=sync if needed using global pad
 * applies conv=swab if needed
 * applies conv=lcase,ucase if needed
 * TODO conv=ibm,ascii,ebcdic
 * TODO conv=block,unblock
 *
 * returns number of bytes read
 */
static ssize_t read_block(char *restrict in_buf)
{
	ssize_t in_bytes = 0;

	if ((in_bytes = read(fh_if, in_buf, opt_ibs)) == -1) 
	{
		warn("%s", opt_if);
		return -1;
	} 
	else if (in_bytes > 0)
	{
		total_read += in_bytes;

		if (in_bytes == opt_ibs)
			block_read++;
		else {
			/* padd this in block */
			if (opt_conv & CONV_SYNC) {
				memset(in_buf + in_bytes, pad, opt_ibs - in_bytes);
				in_bytes = opt_ibs;
			}
			partial_read++;
		}

		/* avoid scanning the block if we have no conversion */
		if (opt_conv & ~(CONV_SYNC|CONV_NOERROR)) {

			/* swap bytes first */
			if (opt_conv & CONV_SWAB)
				for (int i = 0; i < (in_bytes & ~1); i+=2)
				{
					char t = in_buf[i];
					in_buf[i] = in_buf[i+1];
					in_buf[i+1] = t;
				}

			/* apply lcase or ucase */
			if (opt_conv & (CONV_LCASE|CONV_UCASE))
				for (int i = 0; i < in_bytes; i++) {
					if (opt_conv & CONV_LCASE)
						in_buf[i] = tolower(in_buf[i]);
					else if (opt_conv & CONV_UCASE)
						in_buf[i] = toupper(in_buf[i]);
				}
		}
	}
	
	//fprintf(stderr, "read_block(%lu) => %lu\n", opt_ibs, in_bytes);
	return in_bytes;
}

static ssize_t write_block(char *restrict out_buf, ssize_t len)
{

	/* pad this out block */
	if ((opt_conv & CONV_SYNC) && len < opt_obs)
	{
		memset(out_buf + len, pad, opt_obs - len);
		len = opt_obs;
	}

	ssize_t out_bytes;

	/* write out a full output buffer (or partial, if input now empty) */
	if ((out_bytes = write(fh_of, out_buf, len)) == -1) 
	{
		warn("%s", opt_of);
		return -1;
	} else if (out_bytes > 0) {

		total_write += out_bytes;

		if (out_bytes == opt_obs)
			block_write++;
		else
			partial_write++;
	}

	//fprintf(stderr, "write_block(%lu/%lu) => %lu\n", len, opt_obs, out_bytes);

	return out_bytes;
}

inline static ssize_t min(const ssize_t a, const ssize_t b)
{
	return a < b ? a : b;
}

static void perform_dd(char *restrict in_buf, char *restrict out_buf)
{
	/* main dd loop */
	bool running = true;
	ssize_t in_bytes = 0;
	ssize_t out_bytes = 0;
	ssize_t in_buf_size = -1;
	ssize_t out_buf_size = 0;
	char *in_ptr = NULL;
	bool input = true;

	//fprintf(stderr, "ibs = %5ld obs = %5ld\n", opt_ibs, opt_obs);

	while (running)
	{
		//fprintf(stderr, " in = %5ld out = %5ld in_buf = %5ld out_buf = %5ld input=%d\n",
		//		in_bytes, out_bytes, in_buf_size, out_buf_size, input);

		/*
		 * write a full out_block or a partial if read is 0
		 * if input is not done:
		 *  repeat:
		 *   if leftovers: 
		 *    append (partial) leftovers of in_block to out_block (avoid overflow)
		 *   else: 
		 *    read in_block
		 *    append (partial) in_block to out_block (avoid overflow)
		 *  until read bytes >= obs or read is 0
		 */

		if (in_ptr && in_buf_size>0) {
			const ssize_t c = min(in_buf_size, (opt_obs - out_buf_size));
			memcpy(out_buf + out_buf_size, in_ptr, c);
			in_ptr += c;
			in_buf_size -= c;
			out_buf_size += c;
			//fprintf(stderr, " in = %5ld out = %5ld in_buf = %5ld out_buf = %5ld input=%d (copy)\n",
			//		in_bytes, out_bytes, in_buf_size, out_buf_size, input);
		}
		
		if (in_ptr && out_buf_size>0 && (out_buf_size == opt_obs || !input)) {
			const ssize_t c = min(out_buf_size, opt_obs);
			if ((out_bytes = write_block(out_buf, c)) == -1) break;
			out_buf_size -= out_bytes;
			if (out_buf_size < 0) {
			//	fprintf(stderr, "                                                            (sync of %ld)\n",
			//			-out_buf_size);
				out_buf_size = 0;
			}
			//fprintf(stderr, " in = %5ld out = %5ld in_buf = %5ld out_buf = %5ld input=%d (write)\n",
			//		in_bytes, out_bytes, in_buf_size, out_buf_size, input);
		}

		if (opt_count && (block_read + partial_read) >= opt_count) {
			input = false;
		} else if (in_buf_size <= 0) {
			if ((in_bytes = read_block(in_buf)) == -1) break;
			input = (in_bytes != 0);
			in_buf_size = in_bytes;
			in_ptr = in_buf;
			//fprintf(stderr, " in = %5ld out = %5ld in_buf = %5ld out_buf = %5ld input=%d (read)\n",
			//		in_bytes, out_bytes, in_buf_size, out_buf_size, input);
		}

		if ( (in_buf_size == 0 && out_buf_size == 0) ) break;
	}
}

int main(const int argc, const char *restrict argv[])
{
	fh_if = STDIN_FILENO;
	fh_of = STDOUT_FILENO;

	/* process arguments for operands */
	for (int i = 1; i<argc; i++)
	{
		char *oper = NULL;
		char *value = NULL;

		if ((sscanf(argv[i], "%m[a-z]=%m[^\n]", &oper, &value)) != 2)
			errx(EXIT_FAILURE, "%s: invalid argument", argv[i]);

		const oper_t *op = NULL;
		for (int opn = 0; oper_lst[opn].name; opn++)
		{
			if (!strcmp(oper_lst[opn].name, oper)) {
				op = &oper_lst[opn];
			}
		}

		if (op == NULL)
			errx(EXIT_FAILURE, "%s: unknown operand", oper);

		free(oper); oper = NULL;

		switch (op->type)
		{
			case TYPE_CONV:
				{
					char *restrict ptr = strtok(value, ",");
					while(1)
					{
						if (!ptr) break;
						check_conv(ptr);
						ptr = strtok(NULL, ",");
					}
				}
				break;
			case TYPE_STR:
				*(char **)op->value = strdup(value);
				break;
			case TYPE_BYTE:
				*(ssize_t *)op->value = parse_byte(value);
				break;
			case TYPE_LONG:
				*(ssize_t *)op->value = parse_long(value);
				break;
		}

		free(value); value = NULL;
	}

	/* validate full set of operands */

	/* bs= overrides ibs= or obs= */
	if (opt_bs != 0)
	{
		opt_ibs = opt_bs;
		opt_obs = opt_bs;
	}

	if (opt_conv & CONV_NOERROR)
		errx(EXIT_FAILURE, "conv=noerror is not supported");

	/* check for mutual exclusion violations */
	if (opt_ibs == 0 || opt_obs == 0)
		errx(EXIT_FAILURE, "ibs and obs cannot be zero");

	if (opt_conv & (CONV_UNBLOCK|CONV_BLOCK) && opt_cbs == 0)
		errx(EXIT_FAILURE, "cbs cannot be zero with unblock,block");

	if (opt_conv & (CONV_ASCII|CONV_EBCDIC|CONV_IBM))
		errx(EXIT_FAILURE, "EBCDIC/IBM/ASCII conversion is not supported");

	/* process if=, if provided */
	if (opt_if) {
		if ((fh_if = open(opt_if, O_RDONLY)) == -1)
			err(EXIT_FAILURE, "%s: unable to open", opt_if);
	}

	/* process of=, if provided */
	if (opt_of) {
		int opt = O_WRONLY|O_CREAT;
		if (opt_conv | ~CONV_NOTRUNC && opt_seek == 0)
			opt |= O_TRUNC;

		if ((fh_of = open(opt_of, opt, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)) == -1)
			err(EXIT_FAILURE, "%s: unable to open", opt_of);
	}

	char *in_buf = NULL;
	char *out_buf = NULL;

	/* allocate input and output buffers */
	if ((in_buf = malloc(opt_ibs)) == NULL)
		err(EXIT_FAILURE, NULL);

	if ((out_buf = malloc(opt_obs)) == NULL)
		err(EXIT_FAILURE, NULL);

	/* set fake filenames if stdin or stdout are used */
	if (opt_if == NULL) opt_if = strdup("<stdin>");
	if (opt_of == NULL) opt_if = strdup("<stdout>");

	const bool if_seekable = (lseek(fh_if, 0, SEEK_CUR) != -1);
	const bool of_seekable = (lseek(fh_of, 0, SEEK_CUR) != -1);

	/* process skip= */
	if (opt_skip) {
		if (if_seekable) {
			if (lseek(fh_if, opt_seek * opt_ibs, SEEK_CUR) == -1)
				err(EXIT_FAILURE, NULL);
		} else {
			for (int i = 0; i < opt_seek; i++)
				if (read(fh_if, in_buf, opt_ibs) == -1)
					err(EXIT_FAILURE, NULL);
		}

	}

	/* process seek= */
	if (opt_seek) {
		if (of_seekable) {
			if (lseek(fh_of, opt_seek * opt_obs, SEEK_CUR) == -1)
				err(EXIT_FAILURE, NULL);
		} else {
			errx(EXIT_FAILURE,
					"%s: unable to emulate seek on unseekable file, not implemented",
					opt_of);
		}
	}

	/* set pad character */
	pad = (opt_conv & (CONV_BLOCK|CONV_UNBLOCK)) ? ' ' : '\0';

	perform_dd(in_buf, out_buf);

	/* print summary information */
	fprintf(stderr, "%ld+%ld records in\n", block_read, partial_read);
	fprintf(stderr, "%ld+%ld records out\n", block_read, partial_read);
	if (block_trunc)
		fprintf(stderr, "%ld truncated %s\n", block_trunc, 
				(block_trunc == 1) ? "record" : "records");

	free(in_buf); in_buf = NULL;
	free(out_buf); out_buf = NULL;
	free(opt_if); opt_if = NULL;
	free(opt_of); opt_of = NULL;

	exit(EXIT_SUCCESS);
}
