#include "video.h"

/**
 * This is the address of the VGA text-mode video buffer.  Note that this
 * buffer actually holds 8 pages of text, but only the first page (page 0)
 * will be displayed.
 */
const char *VIDEO_BUFFER = (char *) 0xB8000;

/**
 * Pointer to the start of the second page, which is the buffering page.
 */
const char *PAGETWO = (char *) (0xB8000 + NROWS * NCOLS * 2);

/**
 * Default color which is displayed after a clear and refresh.
 */
uint8_t defbkgcol = BLACK;

void set_bkg(uint8_t bkgcol) {
	defbkgcol = bkgcol;
}

void clear_screen() {
	int i;
	volatile char *vbuf = (volatile char *) PAGETWO;
	for (i = 0; i < NROWS * NCOLS * 2; i++) {
		*vbuf++ = 0;
		*vbuf++ = defbkgcol << 4;
	}
}

void mvprintfcol(uint8_t r, uint8_t c, uint8_t bkgcol, uint8_t txtcol,
		const char *str) {
	unsigned char color = (bkgcol << 4) + txtcol;
	volatile char *vbuf = (volatile char *) PAGETWO;
	vbuf += 2 * (r * NCOLS + c);
	while (*str != '\0') {
		*vbuf++ = *str++;
		*vbuf++ = color;
	}
}

void refresh_screen() {
	int i;
	volatile char *vbuf = (volatile char *) VIDEO_BUFFER;
	volatile char *pagetwo = (volatile char *) PAGETWO;
	for (i = 0; i < NCOLS * NROWS * 2; i++) {
		*vbuf++ = *pagetwo++;
	}
}
