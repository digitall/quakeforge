/*
	#FILENAME#

	#DESCRIPTION#

	Copyright (C) 2001 #AUTHOR#

	Author: #AUTHOR#
	Date: #DATE#

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
#include <stdlib.h>

#include "QF/pr_comp.h"

#include "debug.h"
#include "qfcc.h"

pr_auxfunction_t *
new_auxfunction (void)
{
	if (pr.num_auxfunctions == pr.auxfunctions_size) {
		pr.auxfunctions_size += 1024;
		pr.auxfunctions = realloc (pr.auxfunctions,
								   pr.auxfunctions_size 
								   * sizeof (pr_auxfunction_t));
	}
	memset (&pr.auxfunctions[pr.num_auxfunctions], 0,
			sizeof (pr_auxfunction_t));
	return &pr.auxfunctions[pr.num_auxfunctions++];
}

pr_lineno_t *
new_lineno (void)
{
	if (pr.num_linenos == pr.linenos_size) {
		pr.linenos_size += 1024;
		pr.linenos = realloc (pr.linenos,
							  pr.linenos_size * sizeof (pr_lineno_t));
	}
	memset (&pr.linenos[pr.num_linenos], 0, sizeof (pr_lineno_t));
	return &pr.linenos[pr.num_linenos++];
}

ddef_t *
new_local (void)
{
	if (pr.num_locals == pr.locals_size) {
		pr.locals_size += 1024;
		pr.locals = realloc (pr.locals, pr.locals_size * sizeof (ddef_t));
	}
	memset (&pr.locals[pr.num_locals], 0, sizeof (ddef_t));
	return &pr.locals[pr.num_locals++];
}
