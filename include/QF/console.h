/*
	console.h

	Console definitions and prototypes

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

#ifndef __QF_console_h
#define __QF_console_h

#include <stdarg.h>

#include "QF/keys.h"
#include "QF/qtypes.h"

/** \defgroup console 2d Console Stuff
*/

#define		CON_TEXTSIZE	32764
typedef struct
{
	char	text[CON_TEXTSIZE];
	int		current;		// line where next message will be printed
	int		x;				// offset in current line for next print
	int		display;		// bottom of console displays this line
	int		numlines;		// number of non-blank text lines, used for backscroling
} old_console_t;

typedef struct {
	byte	*text;
	size_t	len;
} con_line_t;

typedef struct {
	byte		*buffer;
	size_t		buffer_size;
	con_line_t	*lines;
	int			max_lines;			// size of lines array
	int			num_lines;			// number of lines used
	int			cur_line;			// current line
} con_buffer_t;

extern	int			con_linewidth;
extern struct plugin_s *con_module;
extern struct console_data_s con_data;

//extern int con_totallines;
//extern qboolean con_initialized;
//extern byte *con_chars;

void Con_CheckResize (void);
void Con_DrawConsole (void);

void Con_Printf (const char *fmt, ...) __attribute__((format(PRINTF, 1, 2)));
void Con_Print (const char *fmt, va_list args) __attribute__((format(PRINTF, 1, 0)));
void Con_ToggleConsole_f (void);

struct inputline_s;
// wrapper function to attempt to either complete the command line
// or to list possible matches grouped by type
// (i.e. will display possible variables, aliases, commands
// that match what they've typed so far)
void Con_BasicCompleteCommandLine (struct inputline_s *il);

// Generic libs/util/console.c function to display a list
// formatted in columns on the console
void Con_DisplayList(const char **list, int con_linewidth);
extern void (*con_list_print)(const char *fmt, ...) __attribute__((format(PRINTF, 1, 2)));

con_buffer_t *Con_CreateBuffer (size_t buffer_size, int max_lines);
void Con_DestroyBuffer (con_buffer_t *buffer);
void Con_BufferAddText (con_buffer_t *buf, const char *text);
#define Con_BufferLine(b,l) ((b)->lines + ((l) + (b)->max_lines) % (b)->max_lines)

// init/shutdown functions
void Con_Init (const char *plugin_name);
void Con_ExecLine (const char *line);

void Con_ProcessInput (void);
void Con_KeyEvent (knum_t key, short unicode, qboolean down);
void Con_SetOrMask (int mask);
void Con_NewMap (void);

void Con_Maplist_f (void);
void Con_Skinlist_f (void);
void Con_Skyboxlist_f (void);
void Con_Demolist_QWD_f (void);
void Con_Demolist_DEM_f (void);

//FIXME need a better way to communicate this (bah, need a better menu system
// in general :P)
void C_DrawInputLine (struct inputline_s *il);

struct view_s;
void Menu_Init (void);
void Menu_Load (void);
void Menu_Draw (struct view_s *view);
void Menu_Draw_Hud (struct view_s *view);
int Menu_KeyEvent (knum_t key, short unicode, qboolean down);
void Menu_Enter (void);
void Menu_Leave (void);

void Menu_Enter_f (void);
void Menu_Leave_f (void);
void Menu_Prev_f (void);
void Menu_Next_f (void);

#endif//__QF_console_h
