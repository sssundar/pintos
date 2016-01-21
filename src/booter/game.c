#include "interrupts.h"
#include "video.h"
#include "timer.h"
#include "keyboard.h"
#include "ports.h"

//-------------------------------- Definitions --------------------------------

#define INT_MAX (1 << 31)

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

/** Represents Flappy the Bird. */
typedef struct flappy {
	/* Height of Flappy the Bird at the last up arrow press. */
	int h0;

	/* Time since last up arrow pressed. */
	int t;
} flappy;

//------------------------------ Global Constants -----------------------------

/** Gravitational acceleration constant */
const float GRAV = 0.05;

/** Initial velocity with up arrow press */
const float V0 = -0.5;

/** Number of rows in the console window. */
const int NUM_ROWS = 25;

/** Number of columns in the console window. */
const int NUM_COLS = 80;

/** Radius of each vertical pipe. */
const int PIPE_RADIUS = 3;

/** Width of the opening in each pipe. */
const int OPENING_WIDTH = 7;

/** Flappy stays in this column. */
const int FLAPPY_COL = 10;

/** Aiming for this many frames per second. */
const float TARGET_FPS = 24;

/** Amount of time the splash screen stays up. */
const float START_TIME_SEC = 3;

/** Length of the "progress bar" on the status screen. */
const int PROG_BAR_LEN = 76;

/** Row number at which the progress bar will show. */
const int PROG_BAR_ROW = 22;

const int SCORE_START_COL = 62;

//----------------------------- Utility Functions -----------------------------

/**
 * Gets the number of digits in the integer argument.
 *
 * @param x Find the number of digits in this number.
 *
 * @return Number of digits in 'x'.
 */
int num_digits(int x) {
	int digs = 0;
	if (x == 0)
		return 1;
	while(x != 0) {
		x /= 10;
		digs++;
	}
	return digs;
}

/**
 * Converts the given char into a string.
 *
 * @param ch Char to convert to a string.
 * @param[out] str Receives 'ch' into a null-terminated C string. Assumes
 * str had 2 bytes allocated.
 */
void chtostr(char ch, char *str) {
	str[0] = ch;
	str[1] = '\0';
}

/**
 * Gets a (very) pseudo-random number between 0 and the argument, which should
 * be prime. Assumes that the global variable 'seed' has been set to something
 * that depends on time or user input.
 *
 * @param prime
 *
 * @return A (very) pseudo-random number.
 */
int randintp(int prime, int *seed) {
  if (*seed == 0) {
    *seed = 1;
  }
  return (*seed = (31 * *seed) % prime);
}

/**
 * Gets the length of its string argument.
 *
 * @param str
 *
 * @return Length of 'str'.
 */
int mystrlen(const char *str) {
	int i = 0;
	while (str[i++] != '\0');
	return i - 1;
}

//--------------------------------- Functions ---------------------------------

/**
 * Puts a string of the format " Score: <score>  Best: <best score>" into
 * the given string.
 *
 * @param[out] str Pointer to char array with enough space for the global score
 * and best score and the other non-digit characters.
 * @param score
 * @param best_score
 */
void print_score_to_str(char *str, int score, int best_score) {
	char *tmp1 = " Score: ", *tmp2 = "  Best: ";
	int i, j, k, btmp = best_score, stmp = score;

	// Put the " Score: " part of the string in.
	for (i = 0; i < mystrlen(tmp1); i++) {
		str[i] = tmp1[i];
	}

	// Put the actual score in.
	for(j = num_digits(score) - 1; j >= 0; j--) {
		str[i + j] = (stmp % 10 + '0');
		stmp /= 10;
	}
	i += num_digits(score);

	// Put the " Best: " part of the string in.
	for (k = 0; k < mystrlen(tmp2); k++) {
		str[i++] = tmp2[k];
	}

	// Put the actual score in.
	for(j = num_digits(best_score) - 1; j >= 0; j--) {
		str[i + j] = (btmp % 10 + '0');
		btmp /= 10;
	}
	i+= num_digits(best_score);

	str[i] = '\0';
}

/**
 * Get Flappy's height along its parabolic arc.
 *
 * @param f Flappy!
 *
 * @return height as a row count
 */
int get_flappy_position(flappy f) {
	return f.h0 + V0 * f.t + 0.5 * GRAV * f.t * f.t;
}

/**
 * Returns true if Flappy crashed into a pipe.
 *
 * @param f Flappy!
 * @param p The vertical pipe obstacle.
 *
 * @return 1 if Flappy crashed, 0 otherwise.
 */
int crashed_into_pipe(flappy f, vpipe p) {
	if (FLAPPY_COL >= p.center - PIPE_RADIUS - 1 &&
			FLAPPY_COL <= p.center + PIPE_RADIUS + 1) {

		if (get_flappy_position(f) >= get_orow(p, 1)  + 1 &&
				get_flappy_position(f) <= get_orow(p, 0) - 1) {
			return 0;
		}
		else {
			return 1;
		}
	}
	return 0;
}

/**
 * Gets the row number of the top or bottom of the opening in the given pipe.
 *
 * @param p The pipe obstacle.
 * @param top Should be 1 for the top, 0 for the bottom.
 *
 * @return Row number.
 */
int get_orow(vpipe p, int top) {
	return p.opening_height * (NUM_ROWS - 1) -
			(top ? 1 : -1) * OPENING_WIDTH / 2;
}

/**
 * Updates the pipe center and opening height for each new frame. If the pipe
 * is sufficiently far off-screen to the left the center is wrapped around to
 * the right, at which time the opening height is changed.
 *
 * @param p
 * @param[out] seed Random number generator seed.
 * @param[out] score Current score.
 */
void pipe_refresh(vpipe *p, int *seed, int *score) {

	// If pipe exits screen on the left then wrap it to the right side of the
	// screen.
	if(p->center + PIPE_RADIUS < 0) {
		p->center = NUM_COLS + PIPE_RADIUS;

		// Get an opening height fraction.
		p->opening_height = randintp(97, seed) / 97.0 * 0.5 + 0.25;
		(*score)++;
	}
	p->center--;
}

/**
 * Print a splash screen and show a progress bar. NB the ASCII art was
 * generated by patorjk.com.
 */
void draw_splash_screen() {
	int i;
	int r = NUM_ROWS / 2 - 6;
	int c = NUM_COLS / 2 - 22;

	clear_screen();

	// Print the title.
	mvprintfcol(r, c, BLACK, WHITE,
			" ___ _                       ___ _        _ ");
	mvprintfcol(r + 1, c, BLACK, WHITE,
			"| __| |__ _ _ __ _ __ _  _  | _ |_)_ _ __| |");
	mvprintfcol(r + 2, c, BLACK, WHITE,
			"| _|| / _` | '_ \\ '_ \\ || | | _ \\ | '_/ _` |");
	mvprintfcol(r + 3, c, BLACK, WHITE,
			"|_| |_\\__,_| .__/ .__/\\_, | |___/_|_| \\__,_|");
	mvprintfcol(r + 4, c, BLACK, WHITE,
			"           |_|  |_|   |__/                  ");
	mvprintfcol(NUM_ROWS / 2 + 1, NUM_COLS / 2 - 10, BLACK, WHITE,
			"Press 'f' to flap!");

	// Print the progress bar.
	mvprintfcol(PROG_BAR_ROW, NUM_COLS / 2 - PROG_BAR_LEN / 2 - 1,
			BLACK, WHITE, "[");
	mvprintfcol(PROG_BAR_ROW, NUM_COLS / 2 + PROG_BAR_LEN / 2,
			BLACK, WHITE, "]");
	refresh_screen();
	for(i = 0; i < PROG_BAR_LEN; i++) {
		mysleep(1000 * START_TIME_SEC / (float) PROG_BAR_LEN);
		mvprintfcol(PROG_BAR_ROW, NUM_COLS / 2 - PROG_BAR_LEN / 2 + i,
				BLACK, WHITE, "=");
		refresh_screen();
	}
	mysleep(1000 * 0.45);
}

/**
 * Prints a failure screen asking the user to either play again or quit.
 *
 * @param[out] score
 * @param[out] best_score
 *
 * @return 1 if the user wants to play again. Returns 0 if the game should
 * exit.
 */
int draw_failure_screen(int *score, int *best_score) {
	char ch;
	clear_screen();
	mvprintfcol(NUM_ROWS / 2 - 1, NUM_COLS / 2 - 22, BLACK, WHITE,
			"Flappy died :-(. 'f' to flap, 'q' to quit.\n");
	refresh_screen();
	ch = -1; // TODO block here = getch();
	switch(ch) {
	case 'q': // Quit.
		return -1;
		break;
	default:
		if (score > best_score)
			best_score = score;
		score = 0;
	}
	return 0; // Restart game.
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
 * @param score
 * @param best_score
 */
void draw_floor_and_ceiling(int ceiling_row, int floor_row,
		char ch, int spacing, int col_start, int score, int best_score) {
	char c[2];
	chtostr(ch, c);
	int i;
	for (i = col_start; i < NUM_COLS; i += spacing) {
		if (i < SCORE_START_COL - num_digits(score) - num_digits(best_score)) {
			mvprintfcol(ceiling_row, i, BLACK, WHITE, c);
		}
		mvprintfcol(floor_row, i, BLACK, WHITE, c);
	}
}

/**
 * Draws the given pipe on the window using 'vch' as the character for the
 * vertical part of the pipe and 'hch' as the character for the horizontal
 * part.
 *
 * @param p
 * @param vch Character for vertical part of pipe
 * @param hcht Character for horizontal part of top pipe
 * @param hchb Character for horizontal part of lower pipe
 * @param ceiling_row Start the pipe just below this
 * @param floor_row Star the pipe jut above this
 */
void draw_pipe(vpipe p, char vch, char hcht, char hchb,
		int ceiling_row, int floor_row) {
	int i, upper_terminus, lower_terminus;
	char c[2];

	// Draw vertical part of upper half of pipe.
	for(i = ceiling_row + 1; i < get_orow(p, 1); i++) {
		if ((p.center - PIPE_RADIUS) >= 0 &&
				(p.center - PIPE_RADIUS) < NUM_COLS - 1) {
			chtostr(vch, c);
			mvprintfcol(i, p.center - PIPE_RADIUS, BLACK, WHITE, c);
		}
		if ((p.center + PIPE_RADIUS) >= 0 &&
				(p.center + PIPE_RADIUS) < NUM_COLS - 1) {
			chtostr(vch, c);
			mvprintfcol(i, p.center + PIPE_RADIUS, BLACK, WHITE, c);
		}
	}
	upper_terminus = i;

	// Draw horizontal part of upper part of pipe.
	for (i = -PIPE_RADIUS; i <= PIPE_RADIUS; i++) {
		if ((p.center + i) >= 0 &&
				(p.center + i) < NUM_COLS - 1) {
			chtostr(hcht, c);
			mvprintfcol(upper_terminus, p.center + i, BLACK, WHITE, c);
		}
	}

	// Draw vertical part of lower half of pipe.
	for(i = floor_row - 1; i > get_orow(p, 0); i--) {
		if ((p.center - PIPE_RADIUS) >= 0 &&
				(p.center - PIPE_RADIUS) < NUM_COLS - 1) {
			chtostr(vch, c);
			mvprintfcol(i, p.center - PIPE_RADIUS, BLACK, WHITE, c);
		}
		if ((p.center + PIPE_RADIUS) >= 0 &&
				(p.center + PIPE_RADIUS) < NUM_COLS - 1) {
			chtostr(vch, c);
			mvprintfcol(i, p.center + PIPE_RADIUS, BLACK, WHITE, c);
		}
	}
	lower_terminus = i;

	// Draw horizontal part of lower part of pipe.
	for (i = -PIPE_RADIUS; i <= PIPE_RADIUS; i++) {
		if ((p.center + i) >= 0 &&
				(p.center + i) < NUM_COLS - 1) {
			chtostr(hchb, c);
			mvprintfcol(lower_terminus, p.center + i, BLACK, WHITE, c);
		}
	}
}

/**
 * Draws Flappy to the screen and shows death message if Flappy collides with
 * ceiling or floor. The user can continue to play or can exit if Flappy
 * dies.
 *
 * @param f Flappy the bird!
 * @param[out] score
 * @param[out] best_score
 * @param p1 Pipe 1.
 * @param p2 Pipe 2.
 * @param frame
 *
 * @return 0 if Flappy didn't crash, 1 if the game should restart,
 * -1 if the game should exit.
 */
int draw_flappy(flappy f, int *score, int *best_score, vpipe p1, vpipe p2,
		int frame) {
	char c[2];
	int h = get_flappy_position(f);

	// If Flappy crashed into the ceiling or the floor...
	if (h <= 0 || h >= NUM_ROWS - 1)
		return draw_failure_screen(score, best_score);

	// If Flappy crashed into a pipe...
	if (crashed_into_pipe(f, p1) || crashed_into_pipe(f, p2)) {
		// TODO uncomment!!! draw_failure_screen(score, best_score);
	}

	// If going down, don't flap
	if (GRAV * f.t + V0 > 0) {
		chtostr('\\', c);
		mvprintfcol(h, FLAPPY_COL - 1, BLACK, WHITE, c);
		mvprintfcol(h - 1, FLAPPY_COL - 2, BLACK, WHITE, c);
		chtostr('0', c);
		mvprintfcol(h, FLAPPY_COL, BLACK, WHITE, c);
		chtostr('/', c);
		mvprintfcol(h, FLAPPY_COL + 1, BLACK, WHITE, c);
		mvprintfcol(h - 1, FLAPPY_COL + 2, BLACK, WHITE, c);
	}

	// If going up, flap!
	else {
		// Left wing
		if (frame % 6 < 3) {
			chtostr('/', c);
			mvprintfcol(h, FLAPPY_COL - 1, BLACK, WHITE, c);
			mvprintfcol(h + 1, FLAPPY_COL - 2, BLACK, WHITE, c);
		}
		else {
			chtostr('\\', c);
			mvprintfcol(h, FLAPPY_COL - 1, BLACK, WHITE, c);
			mvprintfcol(h - 1, FLAPPY_COL - 2, BLACK, WHITE, c);
		}

		// Body
		chtostr('0', c);
		mvprintfcol(h, FLAPPY_COL, BLACK, WHITE, c);

		// Right wing
		if (frame % 6 < 3) {
			chtostr('\\', c);
			mvprintfcol(h, FLAPPY_COL + 1, BLACK, WHITE, c);
			mvprintfcol(h + 1, FLAPPY_COL + 2, BLACK, WHITE, c);
		}
		else {
			chtostr('/', c);
			mvprintfcol(h, FLAPPY_COL + 1, BLACK, WHITE, c);
			mvprintfcol(h - 1, FLAPPY_COL + 2, BLACK, WHITE, c);
		}
	}

	return 0;
}

//----------------------------------- Driver ----------------------------------

/* This is the entry-point for the game! */
void c_start(void) {
	int frame = 0;        // Current frame number.
	int score = 0;        // Current score.
	int best_score = 0;   // Best score so far.
	vpipe p1, p2;         // The pipe obstacles.
	int seed;             // Random number generator seed.
	int tmp;
	int leave_loop = 0;
	int ch;
	flappy f;
	int restart = 1;
	char score_str[50];
  	
	init_interrupts(); // Masks all interrupts, clears IDT, installs it.
	init_timer();
	init_keyboard(); 			
	IRQ_clear_mask(0); // timer unmasked
	IRQ_clear_mask(1); // keyboard unmasked
	enable_interrupts();
	
	/* ----------------------------------------------------------------------*/

	/* Flappy bird code starts here */

	seed = currtime();

	// Initialize video
	set_bkg(BLACK);
	// TODO make sure line buffering is disabled.
	// TODO make sure arrow and 2-scan-code keys are enabled.
	// TODO make sure getch doesn't echo ever
	// TODO make sure cursor doesn't blink
	// TODO make sure getch doens't block except at death screen

	draw_splash_screen();

	while(!leave_loop) {

		// If we're just starting a game then do some initializations.
		if (restart) {
			// Start the pipes just out of view on the right. We get a random
			// number in [0.25, 0.75] so the pipe openings won't be too low
			// or too high.
			p1.center = (int)(1.2 * (NUM_COLS - 1));
			p1.opening_height = randintp(97, &seed) / 97.0 * 0.5 + 0.25;
			p2.center = (int)(1.75 * (NUM_COLS - 1));
			p2.opening_height = randintp(97, &seed) / 97.0 * 0.5 + 0.25;

			// Initialize flappy
			f.h0 = NUM_ROWS / 2;
			f.t = 0;
			restart = 0;
		}

		mysleep((unsigned int) (1000 / TARGET_FPS));

		// Process keystrokes.			
		ch = 0; // TODO = getch(0); // Don't block on input.
		switch (ch) {
		case 'q': // Quit.
			leave_loop = 1;
			break;
		case 'f': // Give Flappy a boost!
			f.h0 = get_flappy_position(f);
			f.t = 0;
			break;
		default: // Let Flappy fall along his parabola.
		// TODO UNDO!!!!!!!!JN!KEJ!EJEKEH  f.t++;
			f.t = 0;
		}

		if (leave_loop)
			break;

		clear_screen();

		// Print "moving" floor and ceiling
		draw_floor_and_ceiling(0, NUM_ROWS - 1, '/', 2, frame % 2,
				score, best_score);

		// Update pipe locations and draw them.
		draw_pipe(p1, '|', '=', '=', 0, NUM_ROWS - 1);
		draw_pipe(p2, '|', '=', '=', 0, NUM_ROWS - 1);
		pipe_refresh(&p1, &seed, &score);
		pipe_refresh(&p2, &seed, &score);

		// Draw Flappy.
		tmp = draw_flappy(f, &score, &best_score, p1, p2, frame);
		if (tmp == 1) {      // Flappy crashed and user wants a restart.
			restart = 1;
			continue;
		}
		else if (tmp == 0) { // Flappy didn't crash.
			// Do nothing.
		}
		else {
			// Flappy crashed and user wants to exit program.
			leave_loop = 1;
			break;
		}

		print_score_to_str(score_str, score, best_score);
		mvprintfcol(0, SCORE_START_COL - num_digits(score) -
				num_digits(best_score), BLACK, WHITE, score_str);

		// Display all the chars for this frame.
		refresh_screen();

		frame++;
	}
	clear_screen();
	refresh_screen();

	/* Flappy bird code ends here */

  /* Loop forever, so that we don't fall back into the bootloader code. */
  while (1) {}
}
