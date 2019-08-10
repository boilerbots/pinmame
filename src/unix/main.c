/*
 * X-mame main-routine
 */

#define __MAIN_C_
#include "xmame.h"

#ifdef __QNXNTO__
#include <sys/mman.h>
#endif

#if defined(LISY_SUPPORT)
extern int lisy_set_gamename( char *arg_from_main, char *lisy_gamename);
#include "lisy/lisyversion.h"
#endif

#include "led-matrix-c.h"
extern struct RGBLedMatrixOptions matrix_options;
extern struct RGBLedMatrix  *matrix;
extern struct LedCanvas *offscreen_canvas;

/* From video.c. */
void osd_video_initpre();

/* put here anything you need to do when the program is started. Return 0 if */
/* initialization was successful, nonzero otherwise. */
int osd_init(void)
{
	/* now invoice system-dependent initialization */
	if (osd_input_initpre() !=OSD_OK) return OSD_NOT_OK;

	if (osd_input_initpost() != OSD_OK)
		return -1;

	return OSD_OK;
}

/*
 * Cleanup routines to be executed when the program is terminated.
 */
void osd_exit(void)
{
	osd_input_close();
}


int main(int argc, char **argv)
{
	int res, res2;

	/* some display methods need to do some stuff with root rights */
	res2 = sysdep_init();

#ifdef DMD_DIRECT 
  memset(&matrix_options, 0, sizeof(matrix_options));
  matrix_options.hardware_mapping = "regular";
  matrix_options.rows = 32;
  matrix_options.cols = 64;
  matrix_options.chain_length = 2;
  matrix_options.parallel = 1;
  matrix_options.led_rgb_sequence = "RBG";

  matrix = led_matrix_create_from_options(&matrix_options, &argc, &argv);
  if (matrix == NULL)
    return 1;

  offscreen_canvas = led_matrix_create_offscreen_canvas(matrix);
  if (offscreen_canvas == NULL)
  {
    fprintf(stderr, "Failed to allocate LED canvas\n");
    exit(1);
  }

  {
    int width, height;
    led_canvas_get_size(offscreen_canvas, &width, &height);
    printf("DMD width=%d height=%d\n", width, height);
  }
#endif

	/* to be absolutely safe force giving up root rights here in case
	   a display method doesn't */
	if (setuid(getuid()))
	{
		perror("setuid");
		sysdep_close();
		return OSD_NOT_OK;
	}

	/* Set the title, now auto build from defines from the makefile */
	sprintf(title,"%s (%s) version %s", NAME, DISPLAY_METHOD,
			build_version);

	/* parse configuration file and environment */
	if ((res = config_init(argc, argv)) != 1234 || res2 == OSD_NOT_OK)
		goto leave;

	/* Check the colordepth we're requesting */
	//if (!options.color_depth && !sysdep_display_16bpp_capable())
		//options.color_depth = 8;

	/* 
	 * Initialize whatever is needed before the display is actually 
	 * opened, e.g., artwork setup.
	 */
	osd_video_initpre();

	/* go for it */
	res = run_game (game_index);

leave:
	sysdep_close();
	/* should be done last since this also closes stdout and stderr */
	config_exit();

	return res;
}
