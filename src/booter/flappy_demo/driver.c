/**
 * @file
 * @author Hamik Mukelyan
 *
 * Drives a text-based Flappy Bird knock-off that is intended to run in an
 * 80 x 25 console.
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
#include <sys/timerfd.h>

// TODO this program still needs vertical pipe collision detection. There are
// also issues that are obvious when the program is run... namely hanging and
// not registering up arrow presses.

/**
 * Number of rows in the console window.
 */
#define ROWS 25

/**
 * Number of columns in the console window.
 */
#define COLS 80

/**
 * Radius of each vertical pipe.
 */
#define PIPE_RADIUS 4

/**
 * Width of the opening in each pipe.
 */
#define OPENING_WIDTH 4

/** Flappy stays in this column. */
#define FLAPPY_COL 10

/** Gravitational acceleration constant */
#define GRAV 0.01

/** Initial velocity with up arrow press */
#define V0 -0.01

//------------------------------ Global variables -----------------------------

/**
 * Frame number.
 */
int frame = 0;

/**
 * Aiming for this many frame per second.
 */
float target_fps = 27;


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
 * The screen's characters in row-major order, indexed with the upper-left at
 * [0][0].
 */
char window[ROWS * (COLS - 1)];

//---------------------------------- Functions --------------------------------

/**
 * Returns the char at [i][j], where [0][0] is at the upper left corner of the
 * screen.
 *
 * @param i
 * @param j
 *
 * @return element at index [i][j]
 */
char getElem(int i, int j) {
	assert(i >= 0 && i < ROWS);
	assert(j >= 0 && j < COLS - 1);
	return window[(COLS - 1) * i + j];
}

/**
 * Set the element at [i][j] to the given char.
 *
 * @param i row
 * @param j col
 * @param c Char to set
 */
void setElem(int i, int j, char c) {
	assert(i >= 0 && i < ROWS);
	assert(j >= 0 && j < COLS - 1);
	window[(COLS - 1) * i + j] = c;
}

/**
 * Sets each element of the given array to the given char.
 *
 * @param arr
 * @param len
 * @param ch
 */
void zero_char_array(char *arr, int len, char ch) {
	int i;
	for (i = 0; i < len; i++)
		arr[i] = ch;
}

/**
 * Signal handler for SIGALRM. Alarms are triggered for each frame in the game.
 * Increments frame counter.
 *
 * @param signal
 */
void timer_handler(int signal) {
	frame++;
}

/**
 * Copied from http://www.2net.co.uk/tutorial/periodic_threads.
 */
int make_periodic (unsigned int period, struct periodic_info *info) {
	int ret;
	unsigned int ns;
	unsigned int sec;
	int fd;
	struct itimerspec itval;

	/* Create the timer */
	fd = timerfd_create (CLOCK_MONOTONIC, 0);
	info->wakeups_missed = 0;
	info->timer_fd = fd;
	if (fd == -1)
		return fd;

	/* Make the timer periodic */
	sec = period/1000000;
	ns = (period - (sec * 1000000)) * 1000;
	itval.it_interval.tv_sec = sec;
	itval.it_interval.tv_nsec = ns;
	itval.it_value.tv_sec = sec;
	itval.it_value.tv_nsec = ns;
	ret = timerfd_settime (fd, 0, &itval, NULL);
	return ret;
}

/**
 * Copied from http://www.2net.co.uk/tutorial/periodic_threads.
 */
void wait_period (struct periodic_info *info) {
	unsigned long long missed;
	int ret;

	/* Wait for the next timer event. If we have missed any the
	   number is written to "missed" */
	ret = read (info->timer_fd, &missed, sizeof (missed));
	if (ret == -1)
	{
		perror ("read timer");
		return;
	}

	/* "missed" should always be >= 1, but just to be sure, check it is not 0 anyway */
	if (missed > 0)
		info->wakeups_missed += (missed - 1);
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
	for (i = col_start; i < COLS - 1; i += spacing) {
		setElem(ceiling_row, i, ch);
		setElem(floor_row, i, ch);
	}
}

/**
 * Updates the pipe center and opening height for each new frame. If the pipe
 * is sufficiently far off-screen to the left the center is wrapped around to
 * the right, at which time the opening height is changed.
 */
void pipe_refresh(vpipe *p) {
	if(p->center + PIPE_RADIUS < 0) {
		p->center = COLS + PIPE_RADIUS;
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
			i < p.opening_height * (ROWS - 1) - OPENING_WIDTH / 2; i++) {
		if ((p.center - PIPE_RADIUS) >= 0 &&
				(p.center - PIPE_RADIUS) < COLS - 1) {
			setElem(i, p.center - PIPE_RADIUS, vch);
		}
		if ((p.center + PIPE_RADIUS) >= 0 &&
				(p.center + PIPE_RADIUS) < COLS - 1) {
			setElem(i, p.center + PIPE_RADIUS, vch);
		}
	}
	upper_terminus = i;

	// Draw horizontal part of upper part of pipe.
	for (i = -PIPE_RADIUS; i <= PIPE_RADIUS; i++) {
		if ((p.center + i) >= 0 &&
				(p.center + i) < COLS - 1) {
			setElem(upper_terminus, p.center + i, hch);
		}
	}

	// Draw vertical part of lower half of pipe.
	for(i = floor_row - 1;
			i > p.opening_height * (ROWS - 1) + OPENING_WIDTH / 2; i--) {
		if ((p.center - PIPE_RADIUS) >= 0 &&
				(p.center - PIPE_RADIUS) < COLS - 1) {
			setElem(i, p.center - PIPE_RADIUS, vch);
		}
		if ((p.center + PIPE_RADIUS) >= 0 &&
				(p.center + PIPE_RADIUS) < COLS - 1) {
			setElem(i, p.center + PIPE_RADIUS, vch);
		}
	}
	lower_terminus = i;

	// Draw horizontal part of lower part of pipe.
	for (i = -PIPE_RADIUS; i <= PIPE_RADIUS; i++) {
		if ((p.center + i) >= 0 &&
				(p.center + i) < COLS - 1) {
			setElem(lower_terminus, p.center + i, hch);
		}
	}
}

void draw_flappy(flappy f) {
	int h = ((int) (f.h0 + V0 * f.t + 0.5 * GRAV * f.t * f.t));

	if (h <= 0 || h >= ROWS - 1) {
		clear();
		printw("You're dead!\n");
		refresh();
		getch();
		endwin();
		exit(0);
	}

	setElem(h, FLAPPY_COL, '*');
}

//------------------------------------ Main -----------------------------------

int main()
{
	int leave_loop = 0;
	int i, j;
	vpipe p1, p2;
	int ch;
	periodic_info pinfo;
	flappy f;

	// Set up the frame rate timer.
	// make_periodic(1 / target_fps * 1000 * 1000, &pinfo);

	// Initialize ncurses window.
	initscr();			/* Start curses mode 		*/
	raw();				/* Line buffering disabled	*/
	keypad(stdscr, TRUE);		/* We get F1, F2 etc..		*/
	noecho();			/* Don't echo() while we do getch */

	srand(time(NULL));

	// Start the pipes just out of view on the right.
	p1.center = (int)(1.1 * COLS);
	p1.opening_height = rand() / ((float) INT_MAX) * 0.8 + 0.1;
	p2.center = (int)(1.6 * COLS);
	p2.opening_height = rand() / ((float) INT_MAX) * 0.8 + 0.1;

	// Initialize flappy
	f.h0 = ROWS / 2;
	f.t = 0;

	printw("Welcome to Flappy Texty Bird. Press <up> to keep "
				"Flappy flying!\n");
	refresh();
	sleep(3);

	while(!leave_loop) {
		//wait_period(&pinfo);
		usleep((unsigned int) (1000000 / target_fps));

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
		zero_char_array(window, ROWS * (COLS - 1), ' ');

		// Print "moving" floor and ceiling
		draw_floor_and_ceiling(0, ROWS - 1, '/', 2, frame % 2);

		// Update pipe locations and draw them.
		draw_pipe(p1, '|', '-', 0, ROWS - 1);
		draw_pipe(p2, '|', '-', 0, ROWS - 1);
		pipe_refresh(&p1);
		pipe_refresh(&p2);

		draw_flappy(f);

		for (i = 0; i < ROWS; i++) {
			for (j = 0; j < COLS - 1; j++) {
				printw("%c", getElem(i, j));
			}
			printw("\n");
		}
		refresh();
	}

	endwin();			/* End curses mode		  */

	return 0;
}
