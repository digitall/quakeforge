/*
	menu.c

	Menu support code and interface to QC

	Copyright (C) 2001 Bill Currie

	Author: Bill Currie
	Date: 2002/1/18

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

#include <stdlib.h>
#include <string.h>

#include "QF/cmd.h"
#include "QF/console.h"
#include "QF/csqc.h"
#include "QF/draw.h"
#include "QF/hash.h"
#include "QF/plugin.h"
#include "QF/progs.h"
#include "QF/render.h"
#include "QF/sys.h"
#include "QF/vfs.h"

typedef struct menu_pic_s {
	struct menu_pic_s *next;
	int         x, y;
	const char *name;
} menu_pic_t;

typedef struct menu_item_s {
	struct menu_item_s *parent;
	struct menu_item_s **items;
	int         num_items;
	int         max_items;
	int         cur_item;
	int         x, y;
	func_t      func;
	func_t      cursor;
	func_t      keyevent;
	func_t      draw;
	const char *text;
	menu_pic_t *pics;
} menu_item_t;

static progs_t  menu_pr_state;
static menu_item_t *menu;
static hashtab_t *menu_hash;
static func_t   menu_init;
static func_t   menu_quit;
static const char *top_menu;

static int
menu_resolve_globals (void)
{
	char       *sym;
	dfunction_t *f;

	if (!(f = ED_FindFunction (&menu_pr_state, sym = "menu_init")))
		goto error;
	menu_init = (func_t)(f - menu_pr_state.pr_functions);
	return 1;
error:
	Con_Printf ("%s: undefined function %s\n", menu_pr_state.progs_name, sym);
	return 0;
}

static const char *
menu_get_key (void *m, void *unused)
{
	return ((menu_item_t *)m)->text;
}

static void
menu_free (void *_m, void *unused)
{
	menu_item_t *m = (menu_item_t *)_m;

	if (m->text)
		free ((char*)m->text);
	if (m->items) {
		int         i;

		for (i = 0; i < m->num_items; i++)
			if (m->items[i]->func)
				menu_free (m->items[i], 0);
		free (m->items);
	}
	while (m->pics) {
		menu_pic_t *p = m->pics;
		m->pics = p->next;
		if (p->name)
			free ((char*)p->name);
		free (p);
	}
}

static void
menu_add_item (menu_item_t *m, menu_item_t *i)
{
	if (m->num_items == m->max_items) {
		m->items = realloc (m->items,
							(m->max_items + 8) * sizeof (menu_item_t *));
		m->max_items += 8;
	}
	i->parent = m;
	m->items[m->num_items++] = i;
}

static void
bi_Menu_Begin (progs_t *pr)
{
	int         x = G_INT (pr, OFS_PARM0);
	int         y = G_INT (pr, OFS_PARM1);
	const char *text = G_STRING (pr, OFS_PARM2);
	menu_item_t *m = calloc (sizeof (menu_item_t), 1);

	m->x = x;
	m->y = y;
	m->text = strdup (text);
	if (menu)
		menu_add_item (menu, m);
	menu = m;
	Hash_Add (menu_hash, m);
}

static void
bi_Menu_Draw (progs_t *pr)
{
	func_t      func = G_FUNCTION (pr, OFS_PARM0);

	menu->draw = func;
}

static void
bi_Menu_Pic (progs_t *pr)
{
	int         x = G_INT (pr, OFS_PARM0);
	int         y = G_INT (pr, OFS_PARM1);
	const char *name = G_STRING (pr, OFS_PARM2);
	menu_pic_t *pic = malloc (sizeof (menu_pic_t));

	pic->x = x;
	pic->y = y;
	pic->name = strdup (name);
	pic->next = menu->pics;
	menu->pics = pic;
}

static void
bi_Menu_CenterPic (progs_t *pr)
{
	int         x = G_INT (pr, OFS_PARM0);
	int         y = G_INT (pr, OFS_PARM1);
	const char *name = G_STRING (pr, OFS_PARM2);
	menu_pic_t *pic = malloc (sizeof (menu_pic_t));
	qpic_t     *qpic = Draw_CachePic (name, 1);

	if (!qpic) {
		free (pic);
		return;
	}

	pic->x = x - qpic->width / 2;
	pic->y = y;
	pic->name = strdup (name);
	pic->next = menu->pics;
	menu->pics = pic;
}

static void
bi_Menu_Item (progs_t *pr)
{
	int         x = G_INT (pr, OFS_PARM0);
	int         y = G_INT (pr, OFS_PARM1);
	const char *text = G_STRING (pr, OFS_PARM2);
	func_t      func = G_FUNCTION (pr, OFS_PARM3);
	menu_item_t *mi = calloc (sizeof (menu_item_t), 1);

	mi->x = x;
	mi->y = y;
	mi->text = strdup (text);
	mi->func = func;
	mi->parent = menu;
	menu_add_item (menu, mi);
}

static void
bi_Menu_Cursor (progs_t *pr)
{
	func_t      func = G_FUNCTION (pr, OFS_PARM0);

	menu->cursor = func;
}

static void
bi_Menu_KeyEvent (progs_t *pr)
{
	func_t      func = G_FUNCTION (pr, OFS_PARM0);

	menu->keyevent = func;
}

static void
bi_Menu_End (progs_t *pr)
{
	menu = menu->parent;
}

static void
bi_Menu_TopMenu (progs_t *pr)
{
	const char *name = G_STRING (pr, OFS_PARM0);

	if (top_menu)
		free ((char*)top_menu);
	top_menu = strdup (name);
}

static void
bi_Menu_SelectMenu (progs_t *pr)
{
	const char *name = G_STRING (pr, OFS_PARM0);

	menu = 0;
	if (name && *name)
		menu = Hash_Find (menu_hash, name);
	if (menu) {
		key_dest = key_menu;
		game_target = IMT_CONSOLE;
	} else {
		if (name && *name)
			Con_Printf ("no menu \"%s\"\n", name);
		if (con_data.force_commandline) {
			key_dest = key_console;
			game_target = IMT_CONSOLE;
		} else {
			key_dest = key_game;
			game_target = IMT_0;
		}
	}
}

static void
bi_Menu_SetQuit (progs_t *pr)
{
	func_t      func = G_FUNCTION (pr, OFS_PARM0);

	menu_quit = func;
}

static void
bi_Menu_Quit (progs_t *pr)
{
	if (con_data.quit)
		con_data.quit ();
	Sys_Quit ();
}

static void
togglemenu_f (void)
{
	if (menu)
		Menu_Leave ();
	else
		Menu_Enter ();
}

static void
quit_f (void)
{
	if (menu_quit) {
		PR_ExecuteProgram (&menu_pr_state, menu_quit);
		if (!G_INT (&menu_pr_state, OFS_RETURN))
			return;
	}
	bi_Menu_Quit (&menu_pr_state);
}

void
Menu_Init (void)
{
	menu_pr_state.progs_name = "menu.dat";

	menu_hash = Hash_NewTable (61, menu_get_key, menu_free, 0);

	PR_AddBuiltin (&menu_pr_state, "Menu_Begin", bi_Menu_Begin, -1);
	PR_AddBuiltin (&menu_pr_state, "Menu_Draw", bi_Menu_Draw, -1);
	PR_AddBuiltin (&menu_pr_state, "Menu_Pic", bi_Menu_Pic, -1);
	PR_AddBuiltin (&menu_pr_state, "Menu_CenterPic", bi_Menu_CenterPic, -1);
	PR_AddBuiltin (&menu_pr_state, "Menu_Item", bi_Menu_Item, -1);
	PR_AddBuiltin (&menu_pr_state, "Menu_Cursor", bi_Menu_Cursor, -1);
	PR_AddBuiltin (&menu_pr_state, "Menu_KeyEvent", bi_Menu_KeyEvent, -1);
	PR_AddBuiltin (&menu_pr_state, "Menu_End", bi_Menu_End, -1);
	PR_AddBuiltin (&menu_pr_state, "Menu_TopMenu", bi_Menu_TopMenu, -1);
	PR_AddBuiltin (&menu_pr_state, "Menu_SelectMenu", bi_Menu_SelectMenu, -1);
	PR_AddBuiltin (&menu_pr_state, "Menu_SetQuit", bi_Menu_SetQuit, -1);
	PR_AddBuiltin (&menu_pr_state, "Menu_Quit", bi_Menu_Quit, -1);

	Cbuf_Progs_Init (&menu_pr_state);
	File_Progs_Init (&menu_pr_state);
	String_Progs_Init (&menu_pr_state);
	PR_Cmds_Init (&menu_pr_state);
	R_Progs_Init (&menu_pr_state);

	Cmd_AddCommand ("togglemenu", togglemenu_f,
					"Toggle the display of the menu");
	Cmd_AddCommand ("quit", quit_f, "Exit the program");
}

void
Menu_Load (void)
{
	int         size;
	VFile      *file;

	menu_pr_state.time = con_data.realtime;

	if (menu_pr_state.progs) {
		free (menu_pr_state.progs);
		menu_pr_state.progs = 0;
	}
	Hash_FlushTable (menu_hash);
	menu = 0;
	top_menu = 0;

	if ((size = COM_FOpenFile (menu_pr_state.progs_name, &file)) != -1) {
		menu_pr_state.progs = malloc (size + 256 * 1024);
		Qread (file, menu_pr_state.progs, size);
		Qclose (file);
		memset ((char *)menu_pr_state.progs + size, 0, 256 * 1024);
		PR_LoadProgsFile (&menu_pr_state, 0);

		if (!PR_RelocateBuiltins (&menu_pr_state)
			|| !PR_ResolveGlobals (&menu_pr_state)
			|| !menu_resolve_globals ()) {
			free (menu_pr_state.progs);
			menu_pr_state.progs = 0;
		} else {
			PR_LoadStrings (&menu_pr_state);
			PR_LoadDebug (&menu_pr_state);
			PR_Check_Opcodes (&menu_pr_state);
		}
	}
	if (!menu_pr_state.progs) {
		// Not a fatal error, just means no menus
		Con_SetOrMask (0x80);
		Con_Printf ("Menu_Load: could not load %s\n",
					menu_pr_state.progs_name);
		Con_SetOrMask (0x00);
		return;
	}
	PR_ExecuteProgram (&menu_pr_state, menu_init);
}

void
Menu_Draw (void)
{
	menu_pic_t *m_pic;
	int         i;
	menu_item_t *item;

	if (!menu)
		return;

	*menu_pr_state.globals.time = *menu_pr_state.time;

	if (menu->draw) {
		PR_ExecuteProgram (&menu_pr_state, menu->draw);
		return;
	}

	item = menu->items[menu->cur_item];

	for (m_pic = menu->pics; m_pic; m_pic = m_pic->next) {
		qpic_t     *pic = Draw_CachePic (m_pic->name, 1);
		if (!pic)
			continue;
		Draw_Pic (m_pic->x, m_pic->y, pic);
	}
	for (i = 0; i < menu->num_items; i++) {
		if (menu->items[i]->text) {
			Draw_String (menu->items[i]->x + 8, menu->items[i]->y,
						 menu->items[i]->text);
		}
	}
	if (menu->cursor) {
		G_INT (&menu_pr_state, OFS_PARM0) = item->x;
		G_INT (&menu_pr_state, OFS_PARM1) = item->y;
		PR_ExecuteProgram (&menu_pr_state, menu->cursor);
	} else {
		Draw_Character (item->x, item->y,
						12 + ((int) (*con_data.realtime * 4) & 1));
	}
}

void
Menu_KeyEvent (knum_t key, short unicode, qboolean down)
{
	if (!menu)
		return;
	if (menu->keyevent) {
		G_INT (&menu_pr_state, OFS_PARM0) = key;
		G_INT (&menu_pr_state, OFS_PARM1) = unicode;
		G_INT (&menu_pr_state, OFS_PARM2) = down;
		PR_ExecuteProgram (&menu_pr_state, menu->keyevent);
		if (G_INT (&menu_pr_state, OFS_RETURN))
			return;
	}
	if (!menu || !menu->items)
		return;
	switch (key) {
		case QFK_DOWN:
		case QFM_WHEEL_DOWN:
			menu->cur_item++;
			menu->cur_item %= menu->num_items;
			break;
		case QFK_UP:
		case QFM_WHEEL_UP:
			menu->cur_item += menu->num_items - 1;
			menu->cur_item %= menu->num_items;
			break;
		case QFK_RETURN:
		case QFM_BUTTON1:
			{
				menu_item_t *item = menu->items[menu->cur_item];
				if (item->func) {
					G_INT (&menu_pr_state, OFS_PARM0) = key;
					G_INT (&menu_pr_state, OFS_PARM1) =
						PR_SetString (&menu_pr_state, item->text);
					PR_ExecuteProgram (&menu_pr_state, item->func);
				} else {
					menu = item;
				}
			}
			break;
		default:
			break;
	}
}

void
Menu_Enter ()
{
	if (!top_menu) {
		key_dest = key_console;
		game_target = IMT_CONSOLE;
		return;
	}
	key_dest = key_menu;
	game_target = IMT_CONSOLE;
	menu = Hash_Find (menu_hash, top_menu);
}

void
Menu_Leave ()
{
	if (menu) {
		menu = menu->parent;
		if (!menu) {
			if (con_data.force_commandline) {
				key_dest = key_console;
				game_target = IMT_CONSOLE;
			} else {
				key_dest = key_game;
				game_target = IMT_0;
			}
		}
	}
}
