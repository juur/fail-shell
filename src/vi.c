#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <ncurses.h>
#include <string.h>
#include <err.h>
#include <ctype.h>

inline static ssize_t min(const ssize_t a, const ssize_t b)
{
	return (a < b) ? (a) : (b);
}

inline static ssize_t max(const ssize_t a, const ssize_t b)
{
	return (a > b) ? (a) : (b);
}

#define CTRL(x) ((x)&31)

static void done(int sig)
{
	exit(EXIT_SUCCESS);
}

static void clean()
{
	echo();
	noraw();
	nl();
	endwin();
}

static void init()
{
	signal(SIGINT, done);
	signal(SIGQUIT, done);
	signal(SIGILL, done);
	signal(SIGKILL, done);
	signal(SIGTERM, done);
	signal(SIGSEGV, done);

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
	size_t	 size;
	size_t	 used;
} line_t;

typedef struct {
	line_t	**lines;
	size_t	  num;
	size_t	  used;
} buffer_t;

static buffer_t *readfile(FILE *restrict f)
{
	char buf[BUFSIZ];
	char *line = NULL;
	buffer_t *ret = NULL;

	if ((ret = calloc(1, sizeof(buffer_t))) == NULL)
		goto warnfail;

	ret->num = 1024;
	if ((ret->lines = calloc(ret->num, sizeof(line_t *))) == NULL)
		goto warnfail;

	size_t pos = 0;

	while ((line = fgets(buf, sizeof(buf), f)) != NULL)
	{
		const size_t len = strlen(line);

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

	return ret;

fail:
	for(size_t i = 0; ret->lines && i < ret->num; ret->lines++) 
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
	if (ret) { free(ret); ret = NULL; }
	return NULL;
warnfail:
	warn("readfile");
	goto fail;
}

static buffer_t *cur_buffer = NULL;
static line_t *cur_line = NULL;
static size_t scr_width = 80, scr_height = 25;
static size_t max_scr_x, max_scr_y;
static ssize_t file_y = 0, file_x = 0;
static ssize_t curs_x = 0, curs_y = 0;
static ssize_t tab_off_x = 0;
static int tabstop = 8;

static void draw(void)
{
	for (ssize_t y = 0; y < scr_height; y++)
	{
		if ( (file_y + y) > cur_buffer->used) break;
		
		const line_t *line = cur_buffer->lines[file_y + y];
		if (!line || !line->line) continue;

		for (ssize_t scr_x = 0, x = 0; x < line->used-1; x++)
		{
			if (scr_x >= max_scr_x && (scr_x - file_x) >= max_scr_x) {
				mvaddch(y, max_scr_x, '$');
				break;
			}

			const char c = line->line[x];

			if (c == '\t') {
				scr_x += (tabstop-1);
			} else if (isprint(c)) {
				mvaddch(y, scr_x - file_x, c);
				scr_x++;
			} else {
				scr_x++;
			}

		}
	}
}

static void insert_line(void)
{
}

static void delete_line(void)
{
}

static ssize_t compute_linex(void)
{
	const ssize_t target = file_x + curs_x;
	ssize_t x = 0, off = 0;

	while (1)
	{
		if (off == target) return x;
		if (off > target) return x;
		if (x < (cur_line->used-1) && cur_line->line[x] == '\t') off+= (tabstop-1);
		else off++;
		x++;
	}
}


static void clampx(void)
{
	if (compute_linex() > (cur_line->used-1)) curs_x-=(compute_linex()) - (cur_line->used-1);
	if (curs_x < 0) { file_x += curs_x; curs_x = 0; }
	else if (curs_x > scr_width-1) { file_x += curs_x - (scr_width-1); curs_x = scr_width-1; }
	//if (file_x > ((cur_line->used-1) - (scr_width-1))) file_x = (cur_line->used-1) - (scr_width-1);
	if (file_x < 0) file_x = 0;
}

static void move_up(void)
{
	if (curs_y == 0 && file_y > 0) file_y--;
	else if (curs_y > 0) curs_y--;
	cur_line = cur_buffer->lines[file_y + curs_y];
	clampx();
	move(curs_y, curs_x);
}

static void move_down(void)
{
	if (curs_y == (scr_height - 1) && (file_y + scr_height < cur_buffer->used)) file_y++;
	else if (curs_y < (scr_height - 1)) curs_y++;
	cur_line = cur_buffer->lines[file_y + curs_y];
	clampx();
	move(curs_y, curs_x);
}

static ssize_t compute_offsetx(ssize_t pos)
{
	ssize_t x = 0, off = 0;

	while(1)
	{
		if (x == pos) return off;
		if (cur_line->line[x] == '\t') off+= (tabstop-1);
		else off++;
		x++;
	}
}

static void move_left()
{
	const ssize_t actx = compute_linex();
	if (actx == 0) return;

	const ssize_t oldx = compute_offsetx(actx);
	const ssize_t newx = compute_offsetx(actx-1);

	curs_x -= (oldx-newx);

	clampx();

	move(curs_y, curs_x);
}

static void move_right()
{
	const ssize_t actx = compute_linex();
	if (actx == (cur_line->used-1)) return;

	const ssize_t oldx = compute_offsetx(actx);
	const ssize_t newx = compute_offsetx(actx+1);

	curs_x += (newx-oldx);

	clampx();

	move(curs_y, curs_x);
}

static void delete_char(int pos)
{
	ssize_t point = compute_linex()+pos;
	if (point < 0) {
		if (cur_line->used <= 2) {
			delete_line();
		} else {
			// TODO concat lines, remove surplus\n
		}
		return;
	}

	warnx("deleting %ld", point);

	if(pos<0) move_left();
	memmove(cur_line->line+point, cur_line->line+point+1, cur_line->used - (point));
	cur_line->used--;
}

static void insert_char(const char c)
{
	warnx("inserting %02x", c);

	if (c == '\r') {
		insert_line();
		return;
	}

	if (cur_line->used == cur_line->size) {
		size_t len = (cur_line->size + 128) & ~127;
		warnx("growing from %lu to %lu", cur_line->size, len);
		char *tmp = realloc(cur_line->line, len);
		if (tmp == NULL)
			err(1, "insert_char");
		cur_line->line = tmp;
		cur_line->size = len;
	}

	ssize_t pos = compute_linex();

	memmove(&cur_line->line[pos + 1], &cur_line->line[pos], strlen(cur_line->line+pos));
	cur_line->line[pos] = c;
	cur_line->used++;
	move_right();
}

int main(const int argc, const char *restrict argv[])
{
	int ch;

	FILE *f = fopen(argv[1] ? argv[1] : "/etc/passwd", "r");
	cur_buffer = readfile(f);
	if (cur_buffer->lines)
		cur_line = cur_buffer->lines[0];

	init();
	max_scr_x = scr_width - 1;
	max_scr_y = scr_height - 1;

	erase();
	draw();
	move(curs_y,curs_x);
	refresh();


	while (1)
	{
		ch = getch();
		erase();
		fprintf(stderr, "read: %x\n", ch);
		switch (ch)
		{
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
			case CTRL('C'):
				exit(EXIT_SUCCESS);
			case KEY_BACKSPACE:
				delete_char(-1);
				break;
			case KEY_DL:
				delete_char(0);
				break;
			case 0x1b:
				break;
			default:
				insert_char(ch);
				break;
		}

		/*
		if (curs_y < 0) { file_y -= -curs_y; curs_y = 0; }
		if (curs_y > 24) { file_y += curs_y - 24; curs_y = 24; }
		if (curs_x < 0) { file_x -= -curs_x; curs_x = 0; }
		if (curs_x >= scr_width) { file_x += curs_x - (scr_width-1); curs_x = (scr_width-1); }
		if (file_y > max(0, cur_buffer->used-24-1)) file_y = cur_buffer->used-24-1;
		if (file_y < 0) file_y = 0;
		if (file_x < 0) file_x = 0;

		cur_line = cur_buffer->lines[file_y+curs_y];

		if (cur_line) {
			ssize_t actx = file_x + curs_x + tab_off_x;
			if (actx > cur_line->used-1) {
				curs_x -= (actx - (cur_line->used-1));
				if (curs_x < 0) {
					file_x = max(0, file_x + curs_x + tab_off_x);
					curs_x = 0;
				}
			}*/
			/*
			if (file_x > cur_line->used-scr_width-1) {
				file_x = cur_line->used-scr_width-1;
				if (curs_x<0) curs_x = 0;
				curs_x = scr_width-1;
			}
			if (curs_x >= cur_line->used) {
				file_x -= (curs_x - cur_line->used);
				curs_x = cur_line->used-1;
			}
			*/
		/*
		} else {
			file_x = 0;
			curs_x = 0;
		}

		if (file_x < 0) file_x = 0;
		*/
		move(25, 0);
		clrtoeol();
		ssize_t actx = compute_linex();
		char at = cur_line->line[actx];
		mvprintw(25, 0, "curs={%3lu,%3lu} file=[%3lu,%3lu/%3lu] line=[%3lu/%3lu] actx=[%3lu] ch@=%c ch=%02x", 
				curs_x, curs_y, 
				file_x, file_y, cur_buffer->used, 
				cur_line ? cur_line->used : 0, 
				cur_line ? cur_line->size : 0,
				actx,
				isprint(at) ? at : '?',
				ch);
		draw();
		move(curs_y,curs_x);
		refresh();
	}
}
