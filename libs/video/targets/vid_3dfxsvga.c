/*
	vid_3dfxsvga.c

	OpenGL device driver for 3Dfx chipsets running Linux

	Copyright (C) 1996-1997  Id Software, Inc.
	Copyright (C) 1999,2000  contributors of the QuakeForge project
	Please see the file "AUTHORS" for a list of contributors

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to:

		Free Software Foundation, Inc.
		59 Temple Place - Suite 330
		Boston, MA  02111-1307, USA

*/
static const char rcsid[] = 
	"$Id$";

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#ifdef HAVE_DLOPEN
# include <dlfcn.h>
#endif

#include <setjmp.h>
#include <sys/signal.h>

#include "QF/console.h"
#include "QF/cvar.h"
#include "QF/qargs.h"
#include "QF/qendian.h"
#include "QF/sys.h"
#include "QF/va.h"
#include "QF/vid.h"
#include "QF/GL/extensions.h"
#include "QF/GL/funcs.h"

#include "compat.h"
#include "sbar.h"
#include "r_cvar.h"

#define WARP_WIDTH				320
#define WARP_HEIGHT				200

#define GLAPI extern
#define GLAPIENTRY

#define FXMESA_NONE				0		// to terminate attribList
#define FXMESA_DOUBLEBUFFER     10
#define FXMESA_ALPHA_SIZE       11		// followed by an integer
#define FXMESA_DEPTH_SIZE		12		// followed by an integer

#define GL_DITHER				0x0BD0

typedef struct tfxMesaContext *fxMesaContext;

typedef long	FxI32;
typedef FxI32	GrScreenResolution_t;
typedef FxI32	GrDitherMode_t;
typedef FxI32	GrScreenRefresh_t;

#define GR_REFRESH_75Hz			0x3

#define GR_DITHER_2x2			0x1
#define GR_DITHER_4x4			0x2
#define GR_RESOLUTION_320x200	0x0
#define GR_RESOLUTION_320x240	0x1
#define GR_RESOLUTION_400x256	0x2
#define GR_RESOLUTION_512x384	0x3
#define GR_RESOLUTION_640x200	0x4
#define GR_RESOLUTION_640x350	0x5
#define GR_RESOLUTION_640x400	0x6
#define GR_RESOLUTION_640x480	0x7
#define GR_RESOLUTION_800x600	0x8
#define GR_RESOLUTION_960x720	0x9
#define GR_RESOLUTION_512x256	0xb
#define GR_RESOLUTION_856x480	0xa
#define GR_RESOLUTION_400x300	0xF

void (* qf_fxMesaDestroyContext) (fxMesaContext ctx);
void (* qf_fxMesaSwapBuffers) (void);
fxMesaContext (* qf_fxMesaCreateContext) (GLuint win, GrScreenResolution_t, GrScreenRefresh_t, const GLint attribList[]);
void (* qf_fxMesaMakeCurrent) (fxMesaContext ctx);

// FIXME!!!!! This belongs in include/qfgl_ext.h -- deek
typedef void (GLAPIENTRY * QF_3DfxSetDitherModeEXT) (GrDitherMode_t mode);

static fxMesaContext fc = NULL;

int			VID_options_items = 0;


#if defined(HAVE_DLOPEN)

void * (* glGetProcAddress) (const char *symbol)= NULL;
void * (* getProcAddress) (void *handle, const char *symbol);

void *
QFGL_LoadLibrary (void)
{
	void   *handle;

	if (!(handle = dlopen (gl_driver->string, RTLD_NOW))) {
		Sys_Error ("Couldn't load OpenGL library %s: %s", gl_driver->string,
				   dlerror ());
	}
	getProcAddress = dlsym;
	glGetProcAddress = dlsym (handle, "glXGetProcAddressARB");
	return handle;
}
#else

# error "Cannot load libraries: %s was not configured with DSO support"

// the following is to avoid other compiler errors
void * (* getProcAddress) (void *handle, const char *symbol);

void *
QFGL_LoadLibrary (void)
{
	return 0;
}
#endif  // HAVE_DLOPEN


void
VID_Shutdown (void)
{
	if (!fc)
		return;

	qf_fxMesaDestroyContext (fc);
}

static jmp_buf  aiee_abort;

static void
aiee (int sig)
{
	printf ("AIEE, signal %d in shutdown code, giving up\n", sig);
	longjmp (aiee_abort, 1);
}

void
signal_handler (int sig)
{
	printf ("Received signal %d, exiting...\n", sig);
	
	switch (sig) {
		case SIGHUP:
		case SIGINT:
		case SIGTERM:
			signal (SIGHUP, SIG_DFL);
			signal (SIGINT, SIG_DFL);
			signal (SIGTERM, SIG_DFL);
			Sys_Quit ();
		default:
			signal (SIGQUIT, aiee);
			signal (SIGILL, aiee);
			signal (SIGTRAP, aiee);
			signal (SIGIOT, aiee);
			signal (SIGBUS, aiee);
			signal (SIGSEGV, aiee);

			if (!setjmp (aiee_abort))
				Sys_Shutdown ();

			signal (SIGQUIT, SIG_DFL);
			signal (SIGILL, SIG_DFL);
			signal (SIGTRAP, SIG_DFL);
			signal (SIGIOT, SIG_DFL);
			signal (SIGBUS, SIG_DFL);
			signal (SIGSEGV, SIG_DFL);
	}
}

void
InitSig (void)
{
	signal (SIGHUP, signal_handler);
	signal (SIGINT, signal_handler);
	signal (SIGQUIT, signal_handler);
	signal (SIGILL, signal_handler);
	signal (SIGTRAP, signal_handler);
//  signal(SIGIOT, signal_handler);
	signal (SIGBUS, signal_handler);
//  signal(SIGFPE, signal_handler);
	signal (SIGSEGV, signal_handler);
	signal (SIGTERM, signal_handler);
}

void
GL_Init (void)
{
	QF_3DfxSetDitherModeEXT dither_select = NULL;
	int p;
	
	GL_Init_Common ();

	if (!(QFGL_ExtensionPresent ("3DFX_set_dither_mode")))
		return;

	if (!(dither_select = QFGL_ExtensionAddress ("gl3DfxSetDitherModeEXT")))
		return;

	Con_Printf ("Dithering: ");

	if ((p = COM_CheckParm ("-dither")) && p < com_argc) {
		if (strequal (com_argv[p+1], "2x2")) {
			dither_select (GR_DITHER_2x2);
			Con_Printf ("2x2.\n");
		}
		if (strequal (com_argv[p+1], "4x4")) {
			dither_select (GR_DITHER_4x4);
			Con_Printf ("4x4.\n");
		}
	} else {
		qfglDisable (GL_DITHER);
		Con_Printf ("disabled.\n");
	}
}

void
GL_EndRendering (void)
{
	qfglFinish ();
	qf_fxMesaSwapBuffers ();
	Sbar_Changed ();
}

static int  resolutions[][3] = {
	{320, 200, GR_RESOLUTION_320x200},
	{320, 240, GR_RESOLUTION_320x240},
	{400, 256, GR_RESOLUTION_400x256},
	{400, 300, GR_RESOLUTION_400x300},
	{512, 256, GR_RESOLUTION_512x256},
	{512, 384, GR_RESOLUTION_512x384},
	{640, 200, GR_RESOLUTION_640x200},
	{640, 350, GR_RESOLUTION_640x350},
	{640, 400, GR_RESOLUTION_640x400},
	{640, 480, GR_RESOLUTION_640x480},
	{800, 600, GR_RESOLUTION_800x600},
	{856, 480, GR_RESOLUTION_856x480},
	{960, 720, GR_RESOLUTION_960x720},
#ifdef GR_RESOLUTION_1024x768
	{1024, 768, GR_RESOLUTION_1024x768},
#endif
#ifdef GR_RESOLUTION_1152x864
	{1152, 864, GR_RESOLUTION_1152x864},
#endif
#ifdef GR_RESOLUTION_1280x960
	{1280, 960, GR_RESOLUTION_1280x960},
#endif
#ifdef GR_RESOLUTION_1280x1024
	{1280, 1024, GR_RESOLUTION_1280x1024},
#endif
#ifdef GR_RESOLUTION_1600x1024
	{1600, 1024, GR_RESOLUTION_1600x1024},
#endif
#ifdef GR_RESOLUTION_1600x1200
	{1600, 1200, GR_RESOLUTION_1600x1200},
#endif
#ifdef GR_RESOLUTION_1792x1344
	{1792, 1344, GR_RESOLUTION_1792x1344},
#endif
#ifdef GR_RESOLUTION_1856x1392
	{1856, 1392, GR_RESOLUTION_1856x1392},
#endif
#ifdef GR_RESOLUTION_1920x1440
	{1920, 1440, GR_RESOLUTION_1920x1440},
#endif
#ifdef GR_RESOLUTION_2048x1536
	{2048, 1536, GR_RESOLUTION_2048x1536},
#endif
#ifdef GR_RESOLUTION_2048x2048
	{2048, 2048, GR_RESOLUTION_2048x2048}
#endif
};

#define NUM_RESOLUTIONS		(sizeof (resolutions) / (sizeof (int) * 3))

static int
findres (int *width, int *height)
{
	int         i;

	for (i = 0; i < NUM_RESOLUTIONS; i++) {
		if ((*width <= resolutions[i][0]) && (*height <= resolutions[i][1])) {
			*width = resolutions[i][0];
			*height = resolutions[i][1];
			return resolutions[i][2];
		}
	}

	*width = 640;
	*height = 480;
	return GR_RESOLUTION_640x480;
}

void
VID_Init (unsigned char *palette)
{
	int         i;
	GLint       attribs[32];

	GL_Pre_Init ();

	qf_fxMesaCreateContext = QFGL_ProcAddress (libgl_handle,
											"fxMesaCreateContext", true);
	qf_fxMesaDestroyContext = QFGL_ProcAddress (libgl_handle,
											 "fxMesaDestroyContext", true);
	qf_fxMesaMakeCurrent = QFGL_ProcAddress (libgl_handle,
										  "fxMesaMakeCurrent", true);
	qf_fxMesaSwapBuffers = QFGL_ProcAddress (libgl_handle,
										  "fxMesaSwapBuffers", true);
	
	VID_GetWindowSize (640, 480);
	Con_CheckResize (); // Now that we have a window size, fix console

	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.colormap8 = vid_colormap;
	vid.fullbright = 256 - LittleLong (*((int *) vid.colormap8 + 2048));

	// interpret command-line params

	// set vid parameters
	attribs[0] = FXMESA_DOUBLEBUFFER;
	attribs[1] = FXMESA_ALPHA_SIZE;
	attribs[2] = 1;
	attribs[3] = FXMESA_DEPTH_SIZE;
	attribs[4] = 1;
	attribs[5] = FXMESA_NONE;

	if ((i = COM_CheckParm ("-conwidth")))
		vid.conwidth = atoi (com_argv[i + 1]);
	else
		vid.conwidth = 640;

	vid.conwidth &= 0xfff8;				// make it a multiple of eight

	vid.conwidth = max (vid.conwidth, 320);

	// pick a conheight that matches with correct aspect
	vid.conheight = vid.conwidth * 3 / 4;

	if ((i = COM_CheckParm ("-conheight")) != 0)
		vid.conheight = atoi (com_argv[i + 1]);

	vid.conheight = max (vid.conheight, 200);

	fc = qf_fxMesaCreateContext (0, findres (&scr_width, &scr_height),
							  GR_REFRESH_75Hz, attribs);
	if (!fc)
		Sys_Error ("Unable to create 3DFX context.");

	qf_fxMesaMakeCurrent (fc);

	vid.width = vid.conwidth = min (vid.conwidth, scr_width);
	vid.height = vid.conheight = min (vid.conheight, scr_height);

	vid.aspect = ((float) vid.height / (float) vid.width) * (320.0 / 240.0);
	vid.numpages = 2;

	InitSig ();							// trap evil signals

	vid_gamma_avail = 1;
	
	GL_Init ();

	VID_InitGamma (palette);
	VID_SetPalette (palette);

	// Check for 3DFX Extensions and initialize them.
	VID_Init8bitPalette ();

	vid.initialized = true;

	Con_Printf ("Video mode %dx%d initialized.\n", scr_width, scr_height);

	vid.recalc_refdef = 1;				// force a surface cache flush
}

void
VID_Init_Cvars (void)
{
	vid_system_gamma = Cvar_Get ("vid_system_gamma", "1", CVAR_ARCHIVE, NULL,
								 "Use system gamma control if available");
}

void
VID_ExtraOptionDraw (unsigned int options_draw_cursor)
{
/* Port specific Options menu entrys */
}

void
VID_ExtraOptionCmd (int option_cursor)
{
/*
	switch(option_cursor)
	{
	case 12:  // Always start with 12
	break;
	}
*/
}

void
VID_SetCaption (const char *text)
{
}

qboolean
VID_SetGamma (double gamma)
{
	return true;
}
