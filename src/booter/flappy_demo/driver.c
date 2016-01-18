/**
 * @file
 * @author Hamik Mukelyan
 *
 * Drives a text-based Flappy Bird knock-off that is intended to run in an
 * 80 x 24 console.
 */

#include <ncurses.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <limits.h>

//-------------------------------- Definitions --------------------------------

/**
 * Represents a vertical pipe through which Flappy The Bird is supposed to fly.
 */
typedef struct vpipe {

	/*
	 * The height of the opening of the pipe as a fraction of the height of the
	 * console window.
	 */
	float opening_height;

	/*
	 * Center of the pipe is at this column number (e.g. somewhere in [0, 79]).
	 * When the center + radius is negative then the pipe's center is rolled
	 * over to somewhere > the number of columns and the opening height is
	 * changed.
	 */
	int center;
} vpipe;

/**
 * Represents Flappy the Bird
 */
typedef struct flappy {
	/*
	 * Height of Flappy the Bird at the last up arrow press.
	 */
	int h0;

	/*
	 * Time since last up arrow pressed.
	 */
	int t;
} flappy;

/**
 * Copied from http://www.2net.co.uk/tutorial/periodic_threads.
 */
typedef struct periodic_info {
	int timer_fd;
	unsigned long long wakeups_missed;
} periodic_info;

/**
 * Represents a character at a particular location in a window. These are
 * intended to be chained together in a singly-linked list.
 */
typedef struct tpixel {
	char ch;
	int row, col;
	struct tpixel *next;
} tpixel;

//------------------------------ Global variables -----------------------------

/** Gravitational acceleration constant */
const float GRAV = 0.05;

/** Initial velocity with up arrow press */
const float V0 = -0.5;

/**
 * Number of rows in the console window.
 */
const int NUM_ROWS = 24;

/**
 * Number of columns in the console window.
 */
const int NUM_COLS = 80;

/**
 * Radius of each vertical pipe.
 */
const int PIPE_RADIUS = 4;

/**
 * Width of the opening in each pipe.
 */
const int OPENING_WIDTH = 4;

/** Flappy stays in this column. */
const int FLAPPY_COL = 10;

/**
 * Aiming for this many frame per second.
 */
const float TARGET_FPS = 20;

/**
 * Frame number.
 */
int frame = 0;

/**
 * Linked list of "text pixels" in the current frame.
 */
tpixel *head = NULL;

//---------------------------------- Functions --------------------------------

/**
 * Set the element at window position [i][j] to the given char.
 *
 * @param i row
 * @param j col
 * @param c Char to set
 */
void set_elem(int i, int j, char c) {
	tpixel *tmp;

	assert(i >= 0 && i < NUM_ROWS);
	assert(j >= 0 && j < NUM_COLS - 1);

	if (head == NULL) {
		head = (tpixel *) malloc(sizeof(tpixel));
		if (head == NULL) {
			perror("Malloc failed.");
			exit(1);
		}
		head->next = NULL;
	}
	else {
		tmp = (tpixel *) malloc(sizeof(tpixel));
		if (tmp == NULL) {
			perror("Malloc failed.");
			exit(1);
		}
		tmp->next= head;
		head = tmp;
	}
	head->ch = c;
	head->row = i;
	head->col = j;
}

/**
 * "Moving" floor and ceiling are written into the window array.
 *
 * @param ceiling_row
 * @param floor_row
 * @param ch Char to use for the ceiling and floor.
 * @param spacing Between chars in the floor and ceiling
 * @param col_start Stagger the beginning of the floor and ceiling chars
 * by this much
 */
void draw_floor_and_ceiling(int ceiling_row, int floor_row,
		char ch, int spacing, int col_start) {
	int i;
	for (i = col_start; i < NUM_COLS - 1; i += spacing) {
		set_elem(ceiling_row, i, ch);
		set_elem(floor_row, i, ch);
	}
}

/**
 * Updates the pipe center and opening height for each new frame. If the pipe
 * is sufficiently far off-screen to the left the center is wrapped around to
 * the right, at which time the opening height is changed.
 */
void pipe_refresh(vpipe *p) {
	if(p->center + PIPE_RADIUS < 0) {
		p->center = NUM_COLS + PIPE_RADIUS;
		p->opening_height = rand() / ((float) INT_MAX) * 0.8 + 0.1;
	}
	p->center--;
}

/**
 * Draws the given pipe on the window using 'vch' as the character for the
 * vertical part of the pipe and 'hch' as the character for the horizontal
 * part.
 *
 * @param p
 * @param vch Character for vertical part of pipe
 * @param hch Character for horizontal part of pipe
 * @param ceiling_row Start the pipe just below this
 * @param floor_row Star the pipe jut above this
 */
void draw_pipe(vpipe p, char vch, char hch, int ceiling_row, int floor_row) {
	int i, upper_terminus, lower_terminus;

	// Draw vertical part of upper half of pipe.
	for(i = ceiling_row + 1;
			i < p.opening_height * (NUM_ROWS - 1) - OPENING_WIDTH / 2; i++) {
		if ((p.center - PIPE_RADIUS) >= 0 &&
				(p.center - PIPE_RADIUS) < NUM_COLS - 1) {
			set_elem(i, p.center - PIPE_RADIUS, vch);
		}
		if ((p.center + PIPE_RADIUS) >= 0 &&
				(p.center + PIPE_RADIUS) < NUM_COLS - 1) {
			set_elem(i, p.center + PIPE_RADIUS, vch);
		}
	}
	upper_terminus = i;

	// Draw horizontal part of upper part of pipe.
	for (i = -PIPE_RADIUS; i <= PIPE_RADIUS; i++) {
		if ((p.center + i) >= 0 &&
				(p.center + i) < NUM_COLS - 1) {
			set_elem(upper_terminus, p.center + i, hch);
		}
	}

	// Draw vertical part of lower half of pipe.
	for(i = floor_row - 1;
			i > p.opening_height * (NUM_ROWS - 1) + OPENING_WIDTH / 2; i--) {
		if ((p.center - PIPE_RADIUS) >= 0 &&
				(p.center - PIPE_RADIUS) < NUM_COLS - 1) {
			set_elem(i, p.center - PIPE_RADIUS, vch);
		}
		if ((p.center + PIPE_RADIUS) >= 0 &&
				(p.center + PIPE_RADIUS) < NUM_COLS - 1) {
			set_elem(i, p.center + PIPE_RADIUS, vch);
		}
	}
	lower_terminus = i;

	// Draw horizontal part of lower part of pipe.
	for (i = -PIPE_RADIUS; i <= PIPE_RADIUS; i++) {
		if ((p.center + i) >= 0 &&
				(p.center + i) < NUM_COLS - 1) {
			set_elem(lower_terminus, p.center + i, hch);
		}
	}
}

void draw_flappy(flappy f) {
	int h = ((int) (f.h0 + V0 * f.t + 0.5 * GRAV * f.t * f.t));

	if (h <= 0 || h >= NUM_ROWS - 1) {
		clear();
		printw("You're dead!\n");
		refresh();
		getch();
		endwin();
		exit(0);
	}

	set_elem(h, FLAPPY_COL, '*');
}

//------------------------------------ Main -----------------------------------

int main()
{
	int leave_loop = 0;
	vpipe p1, p2;
	int ch;
	flappy f;
	tpixel *tmp;

	// Initialize ncurses window.
	initscr();			/* Start curses mode 		*/
	raw();				/* Line buffering disabled	*/
	keypad(stdscr, TRUE);		/* We get F1, F2 etc..		*/
	noecho();			/* Don't echo() while we do getch */
	timeout(0);

	srand(time(NULL));

	// Start the pipes just out of view on the right.
	p1.center = (int)(1.1 * NUM_COLS);
	p1.opening_height = rand() / ((float) INT_MAX) * 0.8 + 0.1;
	p2.center = (int)(1.6 * NUM_COLS);
	p2.opening_height = rand() / ((float) INT_MAX) * 0.8 + 0.1;

	// Initialize flappy
	f.h0 = NUM_ROWS / 2;
	f.t = 0;

	printw("Welcome to Flappy Texty Bird. Press <up> to keep "
				"Flappy flying!\n");
	refresh();
	sleep(1);

	while(!leave_loop) {
		usleep((unsigned int) (1000000 / TARGET_FPS));

		// Process keystrokes.
		ch = -1;
		ch = getch();
		switch (ch) {
		case 'q':
			endwin();
			exit(0);
			break;
		case KEY_UP:
			f.h0 = f.h0 + V0 * f.t + 0.5 * GRAV * f.t * f.t;
			f.t = 0;
			break;
		default:
			f.t++;
		}

		clear();

		// Print "moving" floor and ceiling
		draw_floor_and_ceiling(0, NUM_ROWS - 1, '/', 2, frame % 2);

		// Update pipe locations and draw them.
		draw_pipe(p1, '|', '-', 0, NUM_ROWS - 1);
		draw_pipe(p2, '|', '-', 0, NUM_ROWS - 1);
		pipe_refresh(&p1);
		pipe_refresh(&p2);

		draw_flappy(f);

		while(head != NULL) {
			mvprintw(head->row, head->col, "%c", head->ch);
			tmp = head;
			head = head->next;
			free(tmp);
		}
		head = NULL;
		refresh();
		frame++;
	}

	endwin();			/* End curses mode		  */

	return 0;
}
