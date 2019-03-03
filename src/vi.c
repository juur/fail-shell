#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <ncurses.h>
#include <string.h>

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
	printf("Done\n");
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

	idlok(stdscr, TRUE);
	scrollok(stdscr, TRUE);
	keypad(stdscr, TRUE);
	nonl();
	raw();
	noecho();
}

static int curs_x = 0, curs_y = 0;
static char mem[BUFSIZ];

static void draw()
{
	char *ptr = mem;
	int mem_x = 0, mem_y = 0;

	while (*ptr)
	{
		if (*ptr == '\n') {
			mem_x = 0;
			mem_y++;
		} else 
			mem_x++;

		if (mem_x < 80 && mem_y < 25)
			addch(*ptr);

		if (mem_y >= 25)
			break;
		ptr++;
	}
}

static int line_length()
{
	int row;
	char *first = mem-1;
	mvprintw(29, 0, "first = %p", first);
	for (row = 0; row < curs_y; row++)
	{
		if (!first) break;
		first = strchr(first+1, '\n');
		//mvprintw(30+row, 0, "first = %p               ", first);
		//refresh();
	}
	if (!first) return 0;
	char *second = strchr(first+1, '\n');
	mvprintw(30+row, 0,     "first =%p, second = %p   ", first, second);
	refresh();
	if (!second) return 0;
	return second-(first+1);
}

int main(const int argc, const char *restrict argv[])
{
	init();
	int ch;

	FILE *f = fopen("/etc/passwd", "r");
	fread(mem, 1, BUFSIZ, f);
	fclose(f);
	draw();

	while (1)
	{
		move(curs_y,curs_x);
		refresh();
		ch = getch();
		switch (ch)
		{
			case KEY_DOWN: curs_y++; break;
			case KEY_UP: curs_y--; break;
			case KEY_LEFT: curs_x--; break;
			case KEY_RIGHT: curs_x++; break;
			case CTRL('C'):
				exit(EXIT_SUCCESS);
		}
		if (curs_y < 0) curs_y = 0;
		if (curs_y > 24) curs_y = 24;
		if (curs_x < 0) curs_x = 0;
		if (curs_x > line_length()) curs_x = line_length();
	}
}
