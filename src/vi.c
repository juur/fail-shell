/* vi.c - a partial implementation of POSIX.1-2017
 * Copyright © 2019 Ian Kirk. All rights reserved. */

#define _XOPEN_SOURCE 700

/* preprocessor includes */

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <ncurses.h>
#include <string.h>
#include <err.h>
#include <ctype.h>
#include <unistd.h>
#include <libgen.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>

/* preprocessor defines */

#define	KEY_ESC	033
#define KEY_HT	'\t'
#define	KEY_CR	'\r'

#define CTRLCODE(x) ((x)&31)

/* enum, structure, union and type definitions */

typedef struct {
	char	*line;
	ssize_t	 size;
	ssize_t	 used;
} line_t;

typedef struct {
	line_t	**lines;
	ssize_t	  num;
	ssize_t	  used;
	char	 *name;
	bool	  modified;
} buffer_t;

enum edit_mode_en { EM_NULL=0, EM_CMD, EM_EDIT, EM_LINE, EM_SEARCH };

/* local variables */

static   char        edit_line_buf[BUFSIZ];
static   int         cmd_ch_buf[BUFSIZ];
static   ssize_t     max_scr_x;
static   ssize_t     max_scr_y;
static   buffer_t   *cur_buffer    = NULL;
static   line_t     *cur_line      = NULL;
static   ssize_t     scr_width     = 80;
static   ssize_t     scr_height    = 25;
static   ssize_t     file_y        = 0;
static   ssize_t     file_x        = 0;
static   ssize_t     curs_x        = 0;
static   ssize_t     curs_y        = 0;
static   int         tabstop       = 8;
static   char       *edit_line_ptr = NULL;
static   bool        push_next     = false;
static   bool        push_digit    = false;
static   int        *cmd_ptr       = cmd_ch_buf;
static   int         cmd_repeat    = 1;

static int (*cmd_handler)(const int *) = NULL;
static enum edit_mode_en edit_mode     = EM_EDIT;
static enum edit_mode_en prev_mode     = EM_NULL;

/* local function definitions */

inline static ssize_t min(const ssize_t a, const ssize_t b)
{
	return (a < b) ? (a) : (b);
}

inline static ssize_t max(const ssize_t a, const ssize_t b)
{
	return (a > b) ? (a) : (b);
}

inline static bool isnumber(const char *restrict str)
{
	while(*str) if(!isdigit(*str++)) return false;
	return true;
}

static void clean(void)
{
	static bool done = false;

	if (done) 
		return;

	echo();
	nocbreak();
	nl();
	endwin();
	done = true;
}

static void done(const int sig)
{
	clean();
	errx(EXIT_FAILURE, "exiting with signal %d", sig);
}

static void resize(const int sig)
{
	if (sig != SIGWINCH)
		warnx("Non-SIGWINCH passed to resize");

	struct winsize size;
	if (ioctl(0, TIOCGWINSZ, (char *)&size) == -1)
		err(1, "ioctl: TIOCGWINSZ");

	scr_width = size.ws_col;
	max_scr_x = scr_width - 1;
	
	scr_height = size.ws_row;
	max_scr_y = scr_height - 2;

	// FIXME need to have all instances of curs_y/file_y/cur_line setting
	// in a function
	curs_x = min(max_scr_x, curs_x);
	curs_y = min(max_scr_y, curs_y);
	cur_line = cur_buffer->lines[file_y + curs_y];
}

static void init(void)
{
	signal(SIGINT, done);
	signal(SIGQUIT, done);
	signal(SIGKILL, done);
	signal(SIGTERM, done);

	setvbuf(stderr, NULL, _IONBF, 0);

	initscr();
	atexit(clean);
	cbreak();
	halfdelay(1);
	noecho();
	nonl();

	if (meta(stdscr, true) == ERR)
		errx(1, "meta");
	if (idlok(stdscr, true) == ERR)
		warnx("idleok");
	if (scrollok(stdscr, false) == ERR)
		errx(1, "scrollok");
	if (keypad(stdscr, true) == ERR)
		errx(1, "keypad");
}

__attribute__((nonnull))
static buffer_t *readfile(FILE *restrict f, char *restrict name)
{
	char buf[BUFSIZ];
	char *line = NULL;
	buffer_t *ret = NULL;

	if ((ret = calloc(1, sizeof(buffer_t))) == NULL)
		goto warnfail;

	if ((ret->name = strdup(basename(name))) == NULL)
		goto warnfail;

	ret->modified = false;
	ret->num = 1024;
	if ((ret->lines = calloc(ret->num, sizeof(line_t *))) == NULL)
		goto warnfail;

	ssize_t pos = 0;

	while ((line = fgets(buf, sizeof(buf), f)) != NULL)
	{
		const ssize_t len = strlen(line);

		if (pos > ret->num) {
			line_t **tmp = realloc(ret->lines, 
					(ret->num+256) * sizeof(line_t *));
			if (tmp == NULL) 
				goto warnfail;
			ret->lines = tmp;
			ret->num += 256;
		}

		if ((ret->lines[pos] = malloc(sizeof(line_t))) == NULL)
			goto warnfail;

		ret->lines[pos]->size = len + 1;
		ret->lines[pos]->used = len;

		if ((ret->lines[pos]->line = strdup(line)) == NULL)
			goto warnfail;

		pos++;
	}

	ret->used = pos;
	free(name);
	name = NULL;
	return ret;

fail:
	free(name);
	name = NULL;
	for(ssize_t i = 0; ret->lines && i < ret->num; ret->lines++) 
	{
		if(ret->lines[i]) {
			if (ret->lines[i]->line) {
				free(ret->lines[i]->line);
				ret->lines[i]->line = NULL;
				ret->lines[i]->size = 0;
			}
			free(ret->lines[i]);
			ret->lines[i] = NULL;
		}
	}
	if (ret) {
		if (ret->name) free(ret->name);
		free(ret); 
		ret = NULL; 
	}
	return NULL;

warnfail:
	warn("readfile");
	goto fail;
}

inline static int next_tab(const ssize_t offset_x)
{
	if ( (offset_x+1) % tabstop == 0)
		return (tabstop);
	return (tabstop - ( (offset_x+1) % tabstop));
}

static void draw(void)
{
	for (ssize_t y = 0; y <= max_scr_y; y++)
	{
		if ( (file_y + y) > cur_buffer->used) 
			break;

		const line_t *line = cur_buffer->lines[file_y + y];
		if (!line || !line->line) 
			continue;

		for (ssize_t scr_x = 0, x = 0; x < line->used-1; x++)
		{
			if (scr_x >= max_scr_x && (scr_x - file_x) >= max_scr_x) {
				mvwaddch(stdscr, y, max_scr_x, '$');
				break;
			}

			const char c = line->line[x];

			if (c == KEY_HT) {
				scr_x += next_tab(scr_x);
			} else if (isprint(c)) {
				mvwaddch(stdscr, y, scr_x - file_x, c);
				scr_x++;
			} else {
				scr_x++;
			}

		}
	}
}

/* find the character offset in the string we are at, even if 
 * we 'join' the line half way through a tab */
static ssize_t compute_linex(void)
{
	const ssize_t target = file_x + curs_x;
	ssize_t x = 0, off = 0;

	while (1)
	{
		if (off == target) return x;
		if (off > target) return x;
		/* this first check is needed of we press up and go beyond 
		 * the end of line */
		if (( x< cur_line->used-1) && cur_line->line[x] == KEY_HT) {
			off += next_tab(off);
		} else {
			off++;
		}
		x++;
	}
}

static void clampx(void)
{
	if (compute_linex() > (cur_line->used-1)) 
		curs_x-=(compute_linex()) - (cur_line->used-1);
	
    if (curs_x < 0) { 
		file_x += curs_x; 
		curs_x = 0; 
	} else if (curs_x > max_scr_x) { 
		file_x += curs_x - max_scr_x; 
		curs_x = max_scr_x; 
	}

	if (file_x < 0) 
		file_x = 0;
}

static void move_to_line(size_t tgt)
{
	cur_line = cur_buffer->lines[tgt = max(0, min(tgt, cur_buffer->used - 1))];
	curs_y += tgt - (curs_y + file_y);
	curs_y = max(0, min(curs_y, max_scr_y));
	file_y = max(0, tgt - curs_y);
	clampx();
	wmove(stdscr, curs_y, curs_x);
}

inline static void move_up(void)
{
	move_to_line(curs_y + file_y - 1);
}

inline static void move_down(void)
{
	move_to_line(curs_y + file_y + 1);
}

/* convert a character offset in a string to an absolute screen offset */
static ssize_t compute_offsetx(const ssize_t pos)
{
	ssize_t x = 0, off = 0;

	while(1)
	{
		if (x == pos) 
			return off;
		if (cur_line->line[x] == KEY_HT) {
			off += next_tab(off);
		} else {
			off++;
		}
		x++;
	}
}

static void move_to_col(const size_t target)
{
	size_t tgt = max(0, min(target, cur_line->used - 1));

	const ssize_t actx = compute_linex();
	const ssize_t oldx = compute_offsetx(actx);
	const ssize_t newx = compute_offsetx(tgt);

	curs_x += (newx - oldx);
	clampx();

	wmove(stdscr, curs_y, curs_x);
}

static void move_left(void)
{
	const ssize_t actx = compute_linex();
	if (actx == 0) 
		return;

	move_to_col(actx - 1);
}

static void move_right(void)
{
	const ssize_t actx = compute_linex();
	if (actx == (cur_line->used-1)) 
		return;

	move_to_col(actx + 1);
}

static void insert_line(const bool shift, line_t *newline)
{
	const ssize_t line = curs_y + file_y; // FIXME make a function
	line_t *nl = NULL;

	if (newline == NULL)
	{
		if ((nl = malloc(sizeof(line_t))) == NULL)
			err(1, "insert_line");

		if ((nl->line = strdup("\n")) == NULL)
			err(1, "insert_line");

		nl->used = 1;
		nl->size = 2;
	} else
		nl = newline;

	if (cur_buffer->used == cur_buffer->num)
	{
		line_t **tmp = realloc(cur_buffer->lines,
				(cur_buffer->num+256) * sizeof(line_t *));
		if (tmp == NULL)
			err(1, "insert_line");
		cur_buffer->lines = tmp;
		cur_buffer->num += 256;
	}

	if (shift)
		memmove(&cur_buffer->lines[line+1], &cur_buffer->lines[line], 
				sizeof(line_t *) * (cur_buffer->used - line));
	else if (line < cur_buffer->used)
		memmove(&cur_buffer->lines[line+2], &cur_buffer->lines[line+1], 
				sizeof(line_t *) * (cur_buffer->used - (line+1)));

	cur_buffer->used++;
	cur_buffer->lines[line + (shift ? 0 : 1)] = nl;

	move_down();
}

static void delete_line(const ssize_t line)
{
	if (line < 0 || line > cur_buffer->used-1) return;
	line_t *nl = cur_buffer->lines[line];

	if (nl->line) free(nl->line);
	free(nl);
	cur_buffer->lines[line] = NULL;
	cur_buffer->used--;

	if (line < cur_buffer->used)
		memmove(&cur_buffer->lines[line], &cur_buffer->lines[line+1],
				sizeof(line_t *) * (cur_buffer->used - line));

	move_up();
}


static void delete_char(const int pos)
{
	const ssize_t point = compute_linex() + pos;
	const ssize_t line = curs_y + file_y; //FIXME write func

	if (point < 0) {
		if (cur_line->used <= 2) {
			cur_buffer->modified = true;
			delete_line(line);
		} else if (line > 0) {
			cur_buffer->modified = true;
			char *merged = NULL;

			const line_t *cur = cur_buffer->lines[line];
			line_t *prv = cur_buffer->lines[line-1];

			size_t old = (prv->used-1);
			const size_t len = (cur->used-1) + (prv->used) + 1;

			if ((merged = malloc(len)) == NULL)
				err(1, "delete_char");

			prv->line[prv->used-1]='\0';
			prv->line[prv->used]='\0';
			strcpy(merged, prv->line);
			strcat(merged, cur->line);
			free(prv->line);
			prv->line = merged;
			prv->used = len - 1;
			prv->size = len;
			delete_line(line);

			while(old--) 
				move_right();
		}
		return;
	}

	cur_buffer->modified = true;
	if(pos<0) move_left();
	memmove(cur_line->line+point, 
			cur_line->line+point+1, 
			cur_line->used - (point));

	cur_line->used--;
}

static void insert_char(const char c)
{
	cur_buffer->modified = true;

	if (c == KEY_CR) {
		if (curs_x + file_x == 0) {
			// new line, cursor at start
			insert_line(true, NULL);
		} else if (compute_linex() == cur_line->used - 1) {
			// new line, cursor at end
			insert_line(false, NULL);
		} else {
			char *new_string = strdup(cur_line->line + compute_linex());
			const ssize_t linex = compute_linex();

			cur_line->line[linex] = '\n';
			cur_line->line[linex+1] = '\0';

			cur_line->used = strlen(cur_line->line); // TODO is this linex?

			line_t *nl = malloc(sizeof(line_t));
			if (nl == NULL) 
				err(1, "insert char");

			nl->line = new_string;
			nl->used = strlen(new_string);
			nl->size = nl->used + 1;

			insert_line(false, nl);
			curs_x = file_x = 0;
		}
		return;
	}

	if (cur_line->used == cur_line->size) {
		const size_t len = (cur_line->size + 128) & ~127;
		char *tmp = realloc(cur_line->line, len);
		if (tmp == NULL)
			err(1, "insert_char");
		cur_line->line = tmp;
		cur_line->size = len;
	}

	const ssize_t pos = compute_linex();

	memmove(&cur_line->line[pos + 1], 
			&cur_line->line[pos], 
			strlen(cur_line->line+pos));

	cur_line->line[pos] = c;
	cur_line->used++;
	move_right();
}

static int execute_line_cmd(const char *const str)
{
	if (!str || !*str)
		return 1;

	if (isnumber(str))
		move_to_line(strtoull(str, NULL, 10));
	else if (isdigit(*str)) {
		const char *tmp = str;
		for(; *tmp && isdigit(*tmp); tmp++) ;
		
		char *cntstr = strndup(str, tmp-str);
		int rc = 0; 
		size_t limit = strtoull(cntstr, NULL, 10);

		while(limit--)
			rc += execute_line_cmd(tmp);
		free(cntstr);
		return rc ? 1 :0;
	} else if (!strncmp(str, "q", 1))
		exit(0);
	return 0;
}

static int execute_search_cmd(const char *const str)
{
	if (!str || !*str)
		return 1;

	return 0;
}

static void process_line_ch(const int ch, int md)
{
	int rc = 0;

	switch(ch)
	{
		case CTRLCODE('C'):
			edit_mode = prev_mode;
			break;
		case KEY_CR:
			switch(md) {
				case EM_LINE: rc = execute_line_cmd(edit_line_buf); break;
				case EM_SEARCH: rc = execute_search_cmd(edit_line_buf); break;
			}
			if (rc) {
				beep();
				// handle error
			} else {
				edit_mode = prev_mode;
			}
			break;
		default:
			*edit_line_ptr++ = ch;
			break;
	}
}

static void stop_cmd(void)
{
	push_next = false;
	push_digit = false;
	memset(cmd_ch_buf, 0, sizeof(cmd_ch_buf));
	cmd_ptr = cmd_ch_buf;
	cmd_repeat = 1;
}

__attribute__((nonnull))
static int handler_g(const int *const ch)
{
	if (ch[1] == 0)
		return 0;

	switch(ch[1])
	{
		case 'g': move_to_line(0); break;
		default: stop_cmd(); return -1;
	}
	stop_cmd();
	return 1;
}

__attribute__((nonnull))
static int handler_d(const int *const ch)
{
	if (ch[1] == 0) 
		return 0;

	switch(ch[1])
	{
		case KEY_UP:
			while(cmd_repeat-- > 0)
				delete_line(file_y + curs_y - 1);
			break;
		case KEY_DOWN:
			while(cmd_repeat-- > 0) {
				delete_line(file_y + curs_y + 1);
				move_down();
			}
			break;
		case 'd':
			while(cmd_repeat-- > 0) {
				delete_line(file_y + curs_y); // FIXME write get_cur_line();
				if(file_y + curs_y > 0)
					move_down();
			}
			break;
		default:
			stop_cmd();
			return -1;
	}
	stop_cmd();
	return 1;
}

__attribute__((nonnull))
static int (*lookup_cmd_func(const int ch))(const int *)
{
	switch(ch)
	{
		case 'd':
			return handler_d;
			break;
		case 'g':
			return handler_g;
			break;
		default:
			return NULL;
	}
}

static void push_cmd_ch(const int ch)
{
	if (ch == CTRLCODE(ch)) {
		push_next = false;
		cmd_ptr = cmd_ch_buf;
		*cmd_ptr = '\0';
		return;
	}

	*cmd_ptr++ = ch;
	*cmd_ptr   = '\0';
}

static void process_cmd_ch(const int ch)
{
	int tmp = 25;

	if (push_next) {
		push_cmd_ch(ch);
		cmd_handler(cmd_ch_buf);
	} else if(ch < 0xff && isdigit(ch) && push_digit) {
		cmd_repeat *= 10;
		cmd_repeat += (ch - '0');
	} else if(ch < 0xff && isdigit(ch) && (cmd_ptr == cmd_ch_buf)) {
		cmd_repeat = (ch - '0');
		push_digit = true;
	} else {
		push_digit = false;

		if (cmd_repeat <= 0)
			cmd_repeat = 1;

		switch(ch)
		{
			case 'j':
			case KEY_DOWN:
				while(cmd_repeat-- > 0) move_down();
				break;
			case KEY_PPAGE:
				while(cmd_repeat-- > 0) while(tmp-- > 0) move_up();
				break;
			case KEY_NPAGE:
				while(cmd_repeat-- > 0) while(tmp-- > 0) move_down();
				break;
			case 'k':
			case KEY_UP:
				while(cmd_repeat-- > 0) move_up();
				break;
			case 'h':
			case KEY_BACKSPACE:
			case KEY_LEFT:
				while(cmd_repeat-- > 0) move_left();
				break;
			case 'l':
			case KEY_RIGHT:
				while(cmd_repeat-- > 0) move_right();
				break;
			case 'a':
				move_right();
				goto insert_mode;
			case '^':
			case KEY_HOME:
				move_to_col(0);
				break;
			case '$':
			case KEY_END:
			case 'A':
				move_to_col(cur_line->used-1);
				if (ch == 'A') goto insert_mode;
				break;
			case KEY_DL:
			case 'x':
				while(cmd_repeat-- > 0) delete_char(0);
				break;
			case 'G':
				move_to_line(cur_buffer->used-1);
				break;
			case '/':
				prev_mode = EM_CMD;
				edit_mode = EM_SEARCH;
				memset(edit_line_buf, 0, sizeof(edit_line_buf));
				edit_line_ptr = edit_line_buf;
				break;
			case ':':
				prev_mode = EM_CMD;
				edit_mode = EM_LINE;
				memset(edit_line_buf, 0, sizeof(edit_line_buf));
				edit_line_ptr = edit_line_buf;
				break;
			case 'i':
insert_mode:
				prev_mode = EM_CMD;
				edit_mode = EM_EDIT;
				break;
			case CTRLCODE('C'):
				exit(EXIT_SUCCESS);
				break;
			case 'g':
			case 'd':
				push_cmd_ch(ch);
				cmd_handler = lookup_cmd_func(ch);
				push_next = true;
				break;
			default:
				wmove(stdscr, max_scr_y+1, 0);
				wclrtoeol(stdscr);
				mvwprintw(stdscr, max_scr_y+1, 0, 
						"Error: %c: unknown command", ch);
				wrefresh(stdscr);
				beep();
				sleep(2);
				break;
		}
	}
}

static void print_loc(void) 
{
	const ssize_t actx = compute_linex();

	if (file_x+curs_x != actx) {
		mvwprintw(stdscr, max_scr_y+1, max_scr_x - 18, "%lu,%lu-%lu",
				file_y+curs_y,
				actx,
				file_x+curs_x);
	} else {
		mvwprintw(stdscr, max_scr_y+1, max_scr_x - 18, "%lu,%lu",
				file_y+curs_y,
				actx);
	}
}

static void process_edit_ch(const int ch)
{
	int tmp = 25;

	switch (ch)
	{
		case KEY_NPAGE:
			while(tmp-- > 0) move_down();
			break;
		case KEY_DOWN: 
			move_down();
			break;
		case KEY_PPAGE:
			while(tmp-- > 0) move_up();
			break;
		case KEY_UP: 
			move_up();
			break;
		case KEY_LEFT: 
			move_left();
			break;
		case KEY_RIGHT:
			move_right();
			break;
		case KEY_HOME: 
			curs_x = 0; 
			file_x = 0; 
			clampx();
			break;
		case KEY_END: 
			curs_x = cur_line ? cur_line->used : curs_x; 
			clampx();
			break;
		case KEY_BACKSPACE:
			delete_char(-1);
			break;
		case KEY_DL:
			delete_char(0);
			break;
		case KEY_ESC:
			edit_mode = EM_CMD;
			stop_cmd();
			break;
		case CTRLCODE('C'):
			exit(EXIT_SUCCESS);
		default:
			insert_char(ch);
			break;
	}
}

__attribute__((nonnull))
static int process_esc_sequence(const int *restrict buf)
{
	return ERR;
}

/* global function definitions */

int main(const int argc, const char *const argv[])
{
	const char *name = (argc>1) ? argv[1] : "/etc/passwd";

	FILE *f = fopen(name, "r");
	cur_buffer = readfile(f, strdup(name));
	if (!cur_buffer)
		errx(1, "unable to read file");

	if (cur_buffer->lines)
		cur_line = cur_buffer->lines[0];

	init();

	getmaxyx(stdscr, scr_height, scr_width);

	max_scr_x = scr_width - 1;
	max_scr_y = scr_height - 2;

	werase(stdscr);
	draw();
	wmove(stdscr,curs_y,curs_x);
	wrefresh(stdscr);

	int	buf[BUFSIZ];
	int pos = 0;

	const struct timespec ts = {
		.tv_sec = 0,
		/* 1ms or 1000μs */
		.tv_nsec = 1000 * 1000
	};

	bool esc_capture = false;

	while (1)
	{
		int ch = wgetch(stdscr);

		/* we should only do this here for ESC sequence processing 
		 * instead, process_cmd_ch should concat chars if valid, e.g.
		 * [0-9]*[:print:](.*)? with the last group's validity dependent
		 * on the character in [:print:]
		 */

		if (ch == KEY_RESIZE) {
			resize(SIGWINCH);
			ch = 0;
		} else if (esc_capture && ch == ERR) {
			buf[pos] = '\0';
			ch = process_esc_sequence(buf);
			pos = 0;
			esc_capture = false;
			if (ch == ERR) continue;
		} else if (ch == ERR) {
			continue;
		} else if (esc_capture) {
			buf[pos++] = ch;
			continue;
		}

		werase(stdscr);

		if (ch) switch(edit_mode)
		{
			case EM_LINE:   process_line_ch(ch, edit_mode); break;
			case EM_SEARCH: process_line_ch(ch, edit_mode); break;
			case EM_EDIT:   process_edit_ch(ch); break;
			case EM_CMD:    process_cmd_ch(ch);  break;
			case EM_NULL:   errx(1, "edit_mode"); 
		}

		draw();
		wmove(stdscr, max_scr_y+1, 0);
		wclrtoeol(stdscr);

		switch(edit_mode)
		{
			case EM_SEARCH:
				mvwprintw(stdscr, max_scr_y+1, 0, "/%s", edit_line_buf);
				wmove(stdscr, max_scr_y+1, strlen(edit_line_buf)+1);
				break;
			case EM_LINE:
				mvwprintw(stdscr, max_scr_y+1, 0, ":%s", edit_line_buf);
				wmove(stdscr, max_scr_y+1, strlen(edit_line_buf)+1);
				break;
			case EM_CMD:
				mvwprintw(stdscr, max_scr_y+1, 0, "\"%s\"%s %luL",
						cur_buffer->name,
						cur_buffer->modified ? " [+]" : "",
						cur_buffer->used);
					wmove(stdscr, max_scr_y+1, 30);
					if (push_digit || cmd_repeat>1)
						wprintw(stdscr, "%ld", cmd_repeat);
					if (push_next) {
						int *tmp = cmd_ch_buf;
						while(*tmp) {
							if (isprint(*tmp) && *tmp != '\n') 
								waddch(stdscr, *tmp);
						tmp++;
					}
				}
				print_loc();
				break;
			case EM_EDIT:
				wattron(stdscr, A_BOLD);
				mvwprintw(stdscr, max_scr_y+1, 0, "-- INSERT --");
				wattroff(stdscr, A_BOLD);
				print_loc();
				break;
			case EM_NULL: errx(1, "invalid EM state");
		}

		if (edit_mode != EM_LINE && edit_mode != EM_SEARCH)
			wmove(stdscr, curs_y, curs_x);

		wrefresh(stdscr);
		
		if (nanosleep(&ts, NULL) == -1 && errno != EINTR)
			err(1, "nanosleep");
	}
}
