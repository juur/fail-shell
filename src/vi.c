#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <ncurses.h>
#include <string.h>
#include <err.h>
#include <ctype.h>
#include <unistd.h>
#include <libgen.h>

#define	KEY_ESC	033
#define KEY_HT	'\t'
#define	KEY_CR	'\r'

#define CTRL(x) ((x)&31)

inline static ssize_t min(const ssize_t a, const ssize_t b)
{
	return (a < b) ? (a) : (b);
}

inline static ssize_t max(const ssize_t a, const ssize_t b)
{
	return (a > b) ? (a) : (b);
}


static void done(int sig)
{
	exit(EXIT_FAILURE + sig);
}

static void clean(void)
{
	echo();
	noraw();
	nl();
	endwin();
}

static void init(void)
{
	signal(SIGINT, done);
	signal(SIGQUIT, done);
	//signal(SIGILL, done);
	signal(SIGKILL, done);
	signal(SIGTERM, done);
	//signal(SIGSEGV, done);

	setvbuf(stderr, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stdin, NULL, _IONBF, 0);

	initscr();
	atexit(clean);

	nonl();
	raw();
	noecho();
	meta(stdscr, true);
	idlok(stdscr, true);
	scrollok(stdscr, false);
	keypad(stdscr, true);
}

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

enum edit_mode_en { EM_NULL=0, EM_CMD, EM_EDIT, EM_LINE };

static buffer_t *cur_buffer = NULL;
static line_t *cur_line = NULL;
static ssize_t scr_width = 80, scr_height = 25;
static ssize_t max_scr_x, max_scr_y;
static ssize_t file_y = 0, file_x = 0;
static ssize_t curs_x = 0, curs_y = 0;
static int tabstop = 8;
static enum edit_mode_en edit_mode = EM_EDIT;
static enum edit_mode_en prev_mode = EM_NULL;

inline static int next_tab(const ssize_t offset_x)
{
	if ( (offset_x+1) % tabstop == 0)
		return (tabstop);
	return (tabstop - ( (offset_x+1) % tabstop));
}

static void draw(void)
{
	for (ssize_t y = 0; y < scr_height; y++)
	{
		if ( (file_y + y) > cur_buffer->used) 
			break;
		
		const line_t *line = cur_buffer->lines[file_y + y];
		if (!line || !line->line) 
			continue;

		for (ssize_t scr_x = 0, x = 0; x < line->used-1; x++)
		{
			if (scr_x >= max_scr_x && (scr_x - file_x) >= max_scr_x) {
				mvaddch(y, max_scr_x, '$');
				break;
			}

			const char c = line->line[x];

			if (c == '\t') {
				scr_x += next_tab(scr_x);
			} else if (isprint(c)) {
				mvaddch(y, scr_x - file_x, c);
				scr_x++;
			} else {
				scr_x++;
			}

		}
	}
}

/* find the character offset in the string we are at, even if we 'join' the line half
 * way through a tab */
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
		if (( x< cur_line->used-1) && cur_line->line[x] == '\t') {
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
	//if (file_x > ((cur_line->used-1) - (scr_width-1))) file_x = (cur_line->used-1) - (scr_width-1);
	if (file_x < 0) 
		file_x = 0;
}

static void move_up(void)
{
	if (curs_y == 0 && file_y > 0) 
		file_y--;
	else if (curs_y > 0) 
		curs_y--;

	cur_line = cur_buffer->lines[file_y + curs_y];
	clampx();
	move(curs_y, curs_x);
}

static void move_down(void)
{
	if (curs_y == (scr_height - 1) && 
			(file_y + scr_height < cur_buffer->used)) 
		file_y++;
	else if (curs_y < (scr_height - 1)) 
		curs_y++;

	cur_line = cur_buffer->lines[file_y + curs_y];
	clampx();
	move(curs_y, curs_x);
}

/* convert a character offset in a string to an absolute screen offset */
static ssize_t compute_offsetx(const ssize_t pos)
{
	ssize_t x = 0, off = 0;

	while(1)
	{
		if (x == pos) 
			return off;
		if (cur_line->line[x] == '\t') {
			off += next_tab(off);
		} else {
			off++;
		}
		x++;
	}
}

static void move_left(void)
{
	const ssize_t actx = compute_linex();
	if (actx == 0) 
		return;

	const ssize_t oldx = compute_offsetx(actx);
	const ssize_t newx = compute_offsetx(actx-1);

	curs_x -= (oldx-newx);

	clampx();

	move(curs_y, curs_x);
}

static void move_right(void)
{
	const ssize_t actx = compute_linex();
	if (actx == (cur_line->used-1)) 
		return;

	const ssize_t oldx = compute_offsetx(actx);
	const ssize_t newx = compute_offsetx(actx+1);

	curs_x += (newx-oldx);

	clampx();

	move(curs_y, curs_x);
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

static void delete_line(void)
{
	ssize_t line = curs_y + file_y; // FIXME write func
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
	const ssize_t point = compute_linex()+pos;
	const ssize_t line = curs_y + file_y; //FIXME write func

	if (point < 0) {
		if (cur_line->used <= 2) {
			cur_buffer->modified = true;
			delete_line();
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
			//snprintf(merged, len, "%s%s", prv->line, cur->line);
			free(prv->line);
			prv->line = merged;
			prv->used = len - 1;
			prv->size = len;
			delete_line();

			while(old--) 
				move_right();
		}
		return;
	}

	cur_buffer->modified = true;
	if(pos<0) move_left();
	memmove(cur_line->line+point, cur_line->line+point+1, cur_line->used - (point));

	cur_line->used--;
}

static void insert_char(const char c)
{
	cur_buffer->modified = true;

	if (c == '\r') {
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
		warnx("growing from %lu to %lu", cur_line->size, len);
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

static int execute_cmd(const char *str)
{
	if (strncmp(str, "c", 1))
		exit(0);
	return 0;
}

static char edit_line_buf[BUFSIZ]	= {0};
static char *edit_line_ptr			= NULL;

static void process_line_ch(const char ch)
{
	switch(ch)
	{
		case CTRL('C'):
			edit_mode = prev_mode;
			break;
		case '\r':
			if (execute_cmd(edit_line_buf)) {
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

static void process_cmd(const char ch)
{
	switch(ch)
	{
		case 'a':
			move_right();
			goto insert_mode;
		case '^':
			curs_x = 0;
			file_x = 0;
			clampx();
			break;
		case '$':
		case 'A':
			curs_x = cur_line->used;
			clampx();
			if (ch == 'A') goto insert_mode;
			break;
		case 'x':
			delete_char(0);
			break;
		case 'G':
			curs_y = max_scr_y;
			file_y = max(0, cur_buffer->used - max_scr_y - 1);
			move_down();
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
		default:
			move(25, 0);
			clrtoeol();
			mvprintw(25, 0, "Error: %c: unknown command", ch);
			refresh();
			beep();
			sleep(2);
			break;
	}
}

static void print_loc(void) 
{
	const ssize_t actx = compute_linex();

	if (file_x+curs_x != actx) {
		mvprintw(25, max_scr_x - 18, "%lu,%lu-%lu",
				file_y+curs_y,
				actx,
				file_x+curs_x);
	} else {
		mvprintw(25, max_scr_x - 18, "%lu,%lu",
				file_y+curs_y,
				actx);
	}
}

int main(const int argc, const char *restrict argv[])
{
	const char *name;

	FILE *f = fopen((name = (argc>1) ? argv[1] : "/etc/passwd"), "r");
	cur_buffer = readfile(f, strdup(name));
	if (!cur_buffer)
		errx(1, "unable to read file");

	if (cur_buffer->lines)
		cur_line = cur_buffer->lines[0];

	init();
	max_scr_x = scr_width - 1;
	max_scr_y = scr_height - 1;

	erase();
	draw();
	move(curs_y,curs_x);
	refresh();

	int ch = 0;
	goto first;

	while (1)
	{
		ch = getch();
first:
		erase();
		//fprintf(stderr, "read: %x\n", ch);

		if (edit_mode == EM_LINE) {
			process_line_ch(ch);
		} 
		else switch (ch)
		{
			case 0:
				break;
			case KEY_DOWN: 
				move_down();
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
				curs_x=0; 
				file_x=0; 
				break;
			case KEY_END: 
				curs_x=cur_line ? cur_line->used : curs_x; 
				break;
			case KEY_BACKSPACE:
				if (edit_mode == EM_EDIT)
					delete_char(-1);
				else
					move_left();
				break;
			case KEY_DL:
				delete_char(0);
				break;
			
			case KEY_ESC:
				edit_mode = EM_CMD;
				break;

			case CTRL('C'):
				exit(EXIT_SUCCESS);

			default:
				switch(edit_mode)
				{
					case EM_EDIT:
						insert_char(ch);
						break;
					case EM_CMD:
						process_cmd(ch);
						break;
					case EM_NULL:
					case EM_LINE:
						errx(1, "invalid EM state");
				}
				break;
		}

		draw();
		move(25, 0);
		clrtoeol();

		switch(edit_mode)
		{
			case EM_LINE:
				{
					mvprintw(25, 0,
							":%s",
							edit_line_buf);
					move(25, strlen(edit_line_buf)+1);
				}
				break;
			case EM_CMD:
				mvprintw(25, 0, "\"%s\"%s %luL",
						cur_buffer->name,
						cur_buffer->modified ? " [+]" : "",
						cur_buffer->used);
				print_loc();
				break;
			case EM_EDIT:
				{
					attron(A_BOLD);
					mvprintw(25, 0, "-- INSERT --");
					attroff(A_BOLD);
					print_loc();

					/*
					   ssize_t actx = compute_linex();
					   char at = cur_line->line[actx];
					   mvprintw(25, 0, 
					   "-- INSERT -- c={%lu,%lu} f=[%3lu,%3lu/%3lu] "
					   "l=[%lu/%lu] ax=[%lu] ch@=%c ch=%02x", 
					   curs_x, curs_y, 
					   file_x, file_y, cur_buffer->used, 
					   cur_line ? cur_line->used : 0, 
					   cur_line ? cur_line->size : 0,
					   actx,
					   isprint(at) ? at : '?',
					   ch);
					   */
				}
				break;
			case EM_NULL:
				errx(1, "invalid EM state");
		}
		if (edit_mode != EM_LINE)
			move(curs_y,curs_x);
		refresh();
	}
}
