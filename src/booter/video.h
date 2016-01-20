#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

/* Available colors from the 16-color palette used for EGA and VGA, and
 * also for text-mode VGA output.
 */
#define BLACK          0
#define BLUE           1
#define GREEN          2
#define CYAN           3
#define RED            4
#define MAGENTA        5
#define BROWN          6
#define LIGHT_GRAY     7
#define DARK_GRAY      8
#define LIGHT_BLUE     9
#define LIGHT_GREEN   10
#define LIGHT_CYAN    11
#define LIGHT_RED     12
#define LIGHT_MAGENTA 13
#define YELLOW        14
#define WHITE         15

#define NROWS 		  25
#define NCOLS 		  80

// TODO comment
void set_bkg(uint8_t bkgcol);

/** Clears the screen to the given background color. */
void clear_screen();

/**
 * Prints the given null-terminated string at the given screen coordinates.
 *
 * @param r 0-indexed row coordinate
 * @param c 0-indexed column coordinate
 * @param bkgcol Background color chosen from first 8 defines in this file.
 * @param txtcol Foreground color chosen from the 16 defines in this file.
 * @param str Null-terminated string to print to the screen.
 */
void mvprintfcol(uint8_t r, uint8_t c, uint8_t bkgcol, uint8_t txtcol,
		const char *str);

// TODO comment
void refresh_screen();

#endif /* VIDEO_H */
