/*
	cl_screen.c

	master for refresh, status bar, console, chat, notify, etc

	Copyright (C) 1996-1997  Id Software, Inc.

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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static __attribute__ ((unused)) const char rcsid[] = 
	"$Id$";

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include <time.h>

#include "QF/console.h"
#include "QF/cvar.h"
#include "QF/draw.h"
#include "QF/image.h"
#include "QF/pcx.h"
#include "QF/screen.h"

#include "client.h"
#include "r_cvar.h"
#include "r_local.h"
#include "sbar.h"


static void
SCR_DrawNet (void)
{
	if (realtime - cl.last_received_message < 0.3)
		return;
	if (cls.demoplayback)
		return;

	Draw_Pic (scr_vrect.x + 64, scr_vrect.y, scr_net);
}

static SCR_Func scr_funcs[] = {
	Draw_Crosshair,
	SCR_DrawRam,
	SCR_DrawNet,
	SCR_DrawTurtle,
	SCR_DrawPause,
	SCR_CheckDrawCenterString,
	Sbar_Draw,
	Con_DrawConsole,
	0
};

void
CL_UpdateScreen (double realtime)
{
	cl_wateralpha = r_wateralpha->value;

	V_PrepBlend ();
	SCR_UpdateScreen (realtime, scr_funcs);
}
