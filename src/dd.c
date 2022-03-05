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

__attribute__((unused)) static const unsigned char ascii_to_ebcdic[0400] = {
	0000,0001,0002,0003,0067,0055,0056,0057,
	0026,0005,0045,0013,0014,0015,0016,0017,
	0020,0021,0022,0023,0074,0075,0062,0046,
	0030,0031,0077,0047,0034,0035,0036,0037,
	0100,0132,0177,0173,0133,0154,0120,0175,
	0115,0135,0134,0116,0153,0140,0113,0141,
	0360,0361,0362,0363,0364,0365,0366,0367,
	0370,0371,0172,0136,0114,0176,0156,0157,

	0174,0301,0302,0303,0304,0305,0306,0307,
	0310,0311,0321,0322,0323,0324,0325,0326,
	0327,0330,0331,0342,0343,0344,0345,0346,
	0347,0350,0351,0255,0340,0275,0232,0155,
	0171,0201,0202,0203,0204,0205,0206,0207,
	0210,0211,0221,0222,0223,0224,0225,0226,
	0227,0230,0231,0242,0243,0244,0245,0246,
	0247,0250,0251,0300,0117,0320,0137,0007,

	0040,0041,0042,0043,0044,0225,0006,0027,
	0050,0051,0052,0053,0054,0011,0012,0033,
	0060,0061,0032,0063,0064,0065,0066,0010,
	0070,0071,0072,0073,0004,0024,0076,0341,
	0101,0102,0103,0104,0105,0106,0107,0110,
	0111,0121,0122,0123,0124,0125,0126,0127,
	0130,0131,0142,0143,0144,0145,0146,0147,
	0150,0151,0160,0161,0162,0163,0164,0165,

	0166,0167,0170,0200,0212,0213,0214,0215,
	0216,0217,0220,0152,0233,0234,0235,0236,
	0237,0240,0252,0253,0254,0112,0256,0257,
	0260,0261,0262,0263,0264,0265,0266,0267,
	0270,0271,0272,0273,0274,0241,0276,0277,
	0312,0313,0314,0315,0316,0316,0332,0333,
	0334,0335,0336,0337,0352,0353,0354,0355,
	0356,0357,0372,0373,0374,0375,0376,0377
};

__attribute__((unused)) static const unsigned char ascii_to_ibm_ebcdic[0400] = {
	0000,0001,0002,0003,0067,0055,0056,0057,
	0026,0005,0045,0013,0014,0015,0016,0017,
	0020,0021,0022,0023,0074,0075,0062,0046,
	0030,0031,0077,0047,0034,0035,0036,0037,
	0100,0132,0177,0173,0133,0154,0120,0175,
	0115,0135,0134,0116,0153,0140,0113,0141,
	0360,0361,0362,0363,0364,0365,0366,0367,
	0370,0371,0172,0136,0114,0176,0156,0157,

	0174,0301,0302,0303,0304,0305,0306,0307,
	0310,0311,0321,0322,0323,0324,0325,0326,
	0327,0330,0331,0342,0343,0344,0345,0346,
	0347,0350,0351,0255,0340,0275,0232,0155,
	0171,0201,0202,0203,0204,0205,0206,0207,
	0210,0211,0221,0222,0223,0224,0225,0226,
	0227,0230,0231,0242,0243,0244,0245,0246,
	0247,0250,0251,0300,0117,0320,0137,0007,

	0040,0041,0042,0043,0044,0225,0006,0027,
	0050,0051,0052,0053,0054,0011,0012,0033,
	0060,0061,0032,0063,0064,0065,0066,0010,
	0070,0071,0072,0073,0004,0024,0076,0341,
	0101,0102,0103,0104,0105,0106,0107,0110,
	0111,0121,0122,0123,0124,0125,0126,0127,
	0130,0131,0142,0143,0144,0145,0146,0147,
	0150,0151,0160,0161,0162,0163,0164,0165,

	0166,0167,0170,0200,0212,0213,0214,0215,
	0216,0217,0220,0152,0233,0234,0235,0236,
	0237,0240,0252,0253,0254,0112,0256,0257,
	0260,0261,0262,0263,0264,0265,0266,0267,
	0270,0271,0272,0273,0274,0241,0276,0277,
	0312,0313,0314,0315,0316,0316,0332,0333,
	0334,0335,0336,0337,0352,0353,0354,0355,
	0356,0357,0372,0373,0374,0375,0376,0377
};


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

	/* gnu dd treats cbs=0 as 'entire line' and cvs>0 as truncate input, 
	 * however the standard requires it
	if (    (c->val == CONV_IBM || c->val == CONV_EBCDIC || c->val == CONV_ASCII) &&
			opt_cbs == 0 )
		errx(EXIT_FAILURE, "cbs is required for conv=ibm,ebcdic,ascii");
	*/

	/* this breaks cbs requirement
	if (c->val == CONV_IBM || c->val == CONV_EBCDIC)
		opt_conv |= CONV_BLOCK; 

	if (c->val == CONV_ASCII)
		opt_conv |= CONV_UNBLOCK; */

	if (c->val == CONV_BLOCK)
		opt_conv |= CONV_SYNC; /* The standard is a bit vague on if this is implicit 
                                * however is silent on UNBLOCK */
	

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
		if ((sscanf(val, "%ldx%ld", &a, &b)) != 2)
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
	return -1;
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
 * FIXME conv=ibm,ascii,ebcdic - doesn't enforce block/unblock
 * TODO conv=block,unblock
 *
 * returns number of bytes read
 */
static ssize_t read_block(char *restrict in_buf)
{
	ssize_t in_bytes = 0;

	if (opt_conv & CONV_BLOCK) {
		/* "... independent of the input block boundaries."
		 * TODO this might be super inefficient
		 */

		int tmp;

		while(1) {
			tmp = read(fh_if, &in_buf[in_bytes], 1);
			if (tmp == -1) {
				warn("%s", opt_if);
				return -1;
			} 
			
			if (tmp == 0)
				break;

			if (in_buf[in_bytes] == '\n') {
				in_buf[in_bytes] = '\0';
				break;
			}

			in_bytes++;
		}
	} else {
		/* normal operation, read up to opt_ibs */
		if ((in_bytes = read(fh_if, in_buf, opt_ibs)) == -1) 
		{
			warn("%s", opt_if);
			return -1;
		} 
	}
	
	if (in_bytes > 0)
	{
		total_read += in_bytes;

		if ( (in_bytes == opt_ibs) && ~(opt_conv & CONV_BLOCK) )
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

			/* finally convert to EBCDIC or IBM EBCDIC */
			if (opt_conv & CONV_EBCDIC)
				for (int i = 0; i < in_bytes; i++)
					in_buf[i] = ascii_to_ebcdic[(unsigned)in_buf[i]];
			else if (opt_conv & CONV_IBM)
				for (int i = 0; i < in_bytes; i++)
					in_buf[i] = ascii_to_ibm_ebcdic[(unsigned)in_buf[i]];
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

int main(const int argc, const char * argv[])
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

	if (opt_conv & (CONV_ASCII/*|CONV_EBCDIC|CONV_IBM*/))
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

	/* set pad character TODO implement block/unblock */
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
