/*
	view.c

	console view object

	Copyright (C) 2003 Bill Currie

	Author: Bill Currie <bill@taniwha.org>
	Date: 2003/5/5

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
#include <stdlib.h>

#include "QF/view.h"

view_t *
view_new (int xp, int yp, int xl, int yl, grav_t grav)
{
	view_t     *view = calloc (1, sizeof (view_t));
	view->xpos = xp;
	view->ypos = yp;
	view->xlen = xl;
	view->ylen = yl;
	view->gravity = grav;
	return view;
}

void
view_add (view_t *par, view_t *view)
{
	switch (view->gravity) {
		case grav_center:
			view->xrel = view->xpos + (par->xlen - view->xlen) / 2;
			view->yrel = view->ypos + (par->ylen - view->ylen) / 2;
			break;
		case grav_north:
			view->xrel = view->xpos + (par->xlen - view->xlen) / 2;
			view->yrel = view->ypos;
			break;
		case grav_northeast:
			view->xrel = par->xlen - view->xpos - view->xlen;
			view->yrel = view->ypos;
			break;
		case grav_east:
			view->xrel = par->xlen - view->xpos - view->xlen;
			view->yrel = view->ypos + (par->ylen - view->ylen) / 2;
			break;
		case grav_southeast:
			view->xrel = par->xlen - view->xpos - view->xlen;
			view->yrel = par->ylen - view->ypos - view->ylen;
			break;
		case grav_south:
			view->xrel = view->xpos + (par->xlen - view->xlen) / 2;
			view->yrel = par->ylen - view->ypos - view->ylen;
			break;
		case grav_southwest:
			view->xrel = view->xpos;
			view->yrel = par->ylen - view->ypos - view->ylen;
			break;
		case grav_west:
			view->xrel = view->xpos;
			view->yrel = view->ypos + (par->ylen - view->ylen) / 2;
			break;
		case grav_northwest:
			view->xrel = view->xpos;
			view->yrel = view->ypos;
			break;
	}
	view->xabs = par->xabs + view->xrel;
	view->yabs = par->yabs + view->yrel;
	view->parent = par;
	if (par->num_children == par->max_children) {
		par->max_children += 8;
		par->children = realloc (par->children,
								 par->max_children * sizeof (view_t *));
		memset (par->children + par->num_children, 0,
				(par->max_children - par->num_children) * sizeof (view_t *));
	}
	par->children[par->max_children++] = view;
}

void
view_remove (view_t *par, view_t *view)
{
	int        i;

	for (i = 0; i < par->num_children; i++) {
		if (par->children[i] == view) {
			memcpy (par->children + i, par->children + i + 1,
					(par->num_children - i - 1) * sizeof (view_t *));
			par->children [par->num_children--] = 0;
			break;
		}
	}
}

void
view_delete (view_t *view)
{
	if (view->parent)
		view_remove (view->parent, view);
	while (view->num_children)
		view_delete (view->children[0]);
	free (view);
}

void
view_draw (view_t *view)
{
	int         i;

	for (i = 0; i < view->num_children; i++) {
		view_t     *v = view->children[i];
		if (v->enabled && v->draw)
			v->draw (v);
	}
}
