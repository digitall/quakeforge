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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static __attribute__ ((used)) const char rcsid[] =
	"$Id$";

#include <stdlib.h>
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "QF/dstring.h"
#include "QF/progs.h"
#include "QF/va.h"

#include "obj_file.h"
#include "qfprogs.h"
#include "reloc.h"

static int
cmp (const void *_a, const void *_b)
{
	const ddef_t *a = (const ddef_t *)_a;
	const ddef_t *b = (const ddef_t *)_b;

	return a->ofs - b->ofs;
}

void
dump_globals (progs_t *pr)
{
	unsigned int i;
	const char *name;
	const char *type;
	int         saveglobal;
	int         offset;
	const char *comment;
	ddef_t     *global_defs = pr->pr_globaldefs;

	if (sorted) {
		global_defs = malloc (pr->progs->numglobaldefs * sizeof (ddef_t));
		memcpy (global_defs, pr->pr_globaldefs,
				pr->progs->numglobaldefs * sizeof (ddef_t));
		qsort (global_defs, pr->progs->numglobaldefs, sizeof (ddef_t), cmp);
	}
	for (i = 0; i < pr->progs->numglobaldefs; i++) {
		ddef_t *def = &global_defs[i];

		if (!def->type && !def->ofs && !def->s_name)
			continue;

		name = PR_GetString (pr, def->s_name);
		type = pr_type_name[def->type & ~DEF_SAVEGLOBAL];
		saveglobal = (def->type & DEF_SAVEGLOBAL) != 0;
		offset = def->ofs;

		comment = "";

		if (def->type == ev_func) {
			func_t      func = G_FUNCTION (pr, offset);
			int         start;
			if (func >= 0 && func < pr->progs->numfunctions) {
				start = pr->pr_functions[func].first_statement;
				if (start > 0)
					comment = va (" %d @ %d", func, start);
				else
					comment = va (" %d = #%d", func, -start);
			} else {
				comment = va (" %d = illegal function", func);
			}
		}
		if (def->type == ev_field)
			comment = va (" %d", G_INT (pr, offset));

		printf ("%d %d %s %s%s\n", offset, saveglobal, name, type, comment);
	}
}

void
dump_fields (progs_t *pr)
{
	unsigned int i;
	const char *name;
	const char *type;
	int         offset;
	const char *comment;

	for (i = 0; i < pr->progs->numfielddefs; i++) {
		ddef_t *def = &pr->pr_fielddefs[i];

		name = PR_GetString (pr, def->s_name);
		type = pr_type_name[def->type & ~DEF_SAVEGLOBAL];
		offset = def->ofs;

		comment = "";

		printf ("%d %s %s%s\n", offset, name, type, comment);
	}
}

void
dump_functions (progs_t *pr)
{
	int         i, j;
	const char *name;
	int         start, count;
	const char *comment;

	for (i = 0; i < pr->progs->numfunctions; i++) {
		dfunction_t *func = &pr->pr_functions[i];

		name = PR_GetString (pr, func->s_name);

		comment = "";

		start = func->first_statement;
		if (start > 0)
			comment = va (" @ %d", start);
		else
			comment = va (" = #%d", -start);

		printf ("%-5d %s%s: %d (", i, name, comment, func->numparms);
		if (func->numparms < 0)
			count = -func->numparms - 1;
		else
			count = func->numparms;
		for (j = 0; j < count; j++)
			printf (" %d", func->parm_size[j]);
		printf (") %d @ %d", func->locals, func->parm_start);
		puts ("");
	}
}

static const char *
flags_string (pr_uint_t flags)
{
	static dstring_t *str;
	if (!str)
		str = dstring_newstr ();
	dstring_clearstr (str);
	dstring_appendstr (str, (flags & QFOD_INITIALIZED) ? "I" : "-");
	dstring_appendstr (str, (flags & QFOD_CONSTANT)    ? "C" : "-");
	dstring_appendstr (str, (flags & QFOD_ABSOLUTE)    ? "A" : "-");
	dstring_appendstr (str, (flags & QFOD_GLOBAL)      ? "G" : "-");
	dstring_appendstr (str, (flags & QFOD_EXTERNAL)    ? "E" : "-");
	dstring_appendstr (str, (flags & QFOD_LOCAL)       ? "L" : "-");
	dstring_appendstr (str, (flags & QFOD_SYSTEM)      ? "S" : "-");
	dstring_appendstr (str, (flags & QFOD_NOSAVE)      ? "N" : "-");
	return str->str;
}

void
qfo_globals (qfo_t *qfo)
{
	qfo_def_t  *def;
	int         i;

	for (i = 0; i < qfo->num_defs; i++) {
		def = &qfo->defs[i];
		printf ("%-5d %s %s", def->ofs, flags_string (def->flags),
				qfo->strings + def->name);
		if (!(def->flags & QFOD_EXTERNAL))
			printf (" %d", qfo->data[def->ofs].integer_var);
		puts ("");
	}
}

void
qfo_relocs (qfo_t *qfo)
{
	qfo_reloc_t  *reloc;
	qfo_def_t    *def;
	qfo_func_t   *func;
	int           i;

	for (i = 0; i < qfo->num_relocs; i++) {
		reloc = qfo->relocs + i;
		if ((unsigned) reloc->type > rel_def_field_ofs) {
			printf ("%d unknown reloc: %d\n", i, reloc->type);
			continue;
		}
		printf ("%s", reloc_names[reloc->type]);
		def = 0;
		func = 0;
		switch ((reloc_type) reloc->type) {
			case rel_none:
				break;
			case rel_op_a_def:
			case rel_op_b_def:
			case rel_op_c_def:
				def = qfo->defs + reloc->def;
				printf (" op.%c %d %d %s", reloc->type - rel_op_a_def + 'a',
						reloc->ofs, def->ofs, qfo->strings + def->name);
				break;
			case rel_op_a_op:
			case rel_op_b_op:
			case rel_op_c_op:
				printf (" op.%c op@%d", reloc->type - rel_op_a_def + 'a',
						reloc->ofs);
				break;
			case rel_def_op:
				printf (" def@%d op@%d", reloc->ofs, reloc->def);
				break;
			case rel_def_def:
				def = qfo->defs + reloc->def;
				printf (" def@%d def@%d %s", reloc->ofs, reloc->def,
						qfo->strings + def->name);
				break;
			case rel_def_func:
				func = qfo->funcs + reloc->def;
				printf (" def@%d func@%d %s", reloc->ofs, reloc->def,
						qfo->strings + func->name);
				break;
			case rel_def_string:
				printf (" def@%d string:`%s'", reloc->ofs,
						qfo->strings + qfo->data[reloc->ofs].string_var);
				break;
			case rel_def_field:
				def = qfo->defs + reloc->def;
				printf (" def@%d def@%d %s", reloc->ofs, reloc->def,
						qfo->strings + def->name);
				break;
			case rel_op_a_def_ofs:
			case rel_op_b_def_ofs:
			case rel_op_c_def_ofs:
				def = qfo->defs + reloc->def;
				printf (" op.%c %d %d %s", reloc->type - rel_op_a_def + 'a',
						reloc->ofs, def->ofs, qfo->strings + def->name);
				break;
			case rel_def_def_ofs:
				def = qfo->defs + reloc->def;
				printf (" def@%d def@%d+%d %s+%d", reloc->ofs, reloc->def,
						qfo->data[reloc->ofs].integer_var,
						qfo->strings + def->name,
						qfo->data[reloc->ofs].integer_var);
				break;
			case rel_def_field_ofs:
				def = qfo->defs + reloc->def;
				printf (" def@%d def@%d+%d %s+%d", reloc->ofs, reloc->def,
						qfo->data[reloc->ofs].integer_var,
						qfo->strings + def->name,
						qfo->data[reloc->ofs].integer_var);
				break;
		}
		if (def && def->flags & QFOD_EXTERNAL)
			printf (" external");
		if (func && qfo->defs[func->def].flags & QFOD_EXTERNAL)
			printf (" external");
		if (def && (i < def->relocs || i >= def->relocs + def->num_relocs))
			printf (" BOGUS reloc!");
		if (func && (i < func->relocs || i >= func->relocs + func->num_relocs))
			printf (" BOGUS reloc!");
		puts ("");
	}
}
