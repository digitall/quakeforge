/*
	dot_flow.c

	"emit" flow graphs to dot (graphvis).

	Copyright (C) 2011 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2011/01/21

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

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#include <stdlib.h>

#include <QF/dstring.h>
#include <QF/quakeio.h>
#include <QF/va.h>

#include "expr.h"
#include "statements.h"
#include "symtab.h"
#include "type.h"

static const char *
quote_string (const char *str)
{
	static dstring_t *q;
	char        c[2] = {0, 0};

	if (!str)
		return "(null)";
	if (!q)
		q = dstring_new ();
	dstring_clearstr (q);
	while ((c[0] = *str++)) {
		switch (c[0]) {
			case '\n':
				dstring_appendstr (q, "\\\\n");
				break;
			case '<':
				dstring_appendstr (q, "&lt;");
				break;
			case '>':
				dstring_appendstr (q, "&gt;");
				break;
			case '&':
				dstring_appendstr (q, "&amp;");
				break;
			case '"':
				dstring_appendstr (q, "&quot;");
				break;
			default:
				dstring_appendstr (q, c);
				break;
		}
	}
	return q->str;
}

static const char *
get_operand (operand_t *op)
{
	type_t     *type;

	if (!op)
		return "";
	switch (op->op_type) {
		case op_symbol:
			return op->o.symbol->name;
		case op_value:
			switch (op->o.value->type) {
				case ev_string:
					return quote_string (op->o.value->v.string_val);
				case ev_float:
					return va ("%g", op->o.value->v.float_val);
				case ev_vector:
					return va ("'%g %g %g'",
							   op->o.value->v.vector_val[0],
							   op->o.value->v.vector_val[1],
							   op->o.value->v.vector_val[2]);
				case ev_quat:
					return va ("'%g %g %g %g'",
							   op->o.value->v.quaternion_val[0],
							   op->o.value->v.quaternion_val[1],
							   op->o.value->v.quaternion_val[2],
							   op->o.value->v.quaternion_val[3]);
				case ev_pointer:
					return va ("ptr %d", op->o.value->v.pointer.val);
				case ev_field:
					return va ("field %d", op->o.value->v.pointer.val);
				case ev_entity:
					return va ("ent %d", op->o.value->v.integer_val);
				case ev_func:
					return va ("func %d", op->o.value->v.integer_val);
				case ev_integer:
					return va ("int %d", op->o.value->v.integer_val);
				case ev_uinteger:
					return va ("uint %u", op->o.value->v.uinteger_val);
				case ev_short:
					return va ("short %d", op->o.value->v.short_val);
				case ev_void:
					return "(void)";
				case ev_invalid:
					return "(invalid)";
				case ev_type_count:
					return "(type_count)";
			}
			break;
		case op_label:
			return op->o.label->name;
		case op_temp:
			return va ("tmp %p", op);
		case op_pointer:
			type = op->o.pointer->type;
			if (op->o.pointer->def)
				return va ("(%s)[%d]&lt;%s&gt;",
						   type ? pr_type_name[type->type] : "???",
						   op->o.pointer->val, op->o.pointer->def->name);
			else
				return va ("(%s)[%d]",
						   type ? pr_type_name[type->type] : "???",
						   op->o.pointer->val);
		case op_alias:
			return get_operand (op->o.alias);//FIXME better output
	}
	return ("??");
}

static void
flow_statement (dstring_t *dstr, statement_t *s)
{
	dasprintf (dstr, "        <tr>");
	dasprintf (dstr, "<td>%s</td>", quote_string (s->opcode));
	dasprintf (dstr, "<td>%s</td>", get_operand (s->opa));
	dasprintf (dstr, "<td>%s</td>", get_operand (s->opb));
	dasprintf (dstr, "<td>%s</td>", get_operand (s->opc));
	dasprintf (dstr, "</tr>\n");
}

static int
is_goto (statement_t *s)
{
	if (!s)
		return 0;
	return !strcmp (s->opcode, "<GOTO>");
}

static sblock_t *
get_target (statement_t *s)
{
	if (!s)
		return 0;
	if (!strncmp (s->opcode, "<IF", 3))
		return s->opb->o.label->dest;
	if (!strcmp (s->opcode, "<GOTO>"))
		return s->opa->o.label->dest;
	return 0;
}

static void
flow_sblock (dstring_t *dstr, sblock_t *sblock, int blockno)
{
	statement_t *s;
	sblock_t   *target;
	ex_label_t *l;

	dasprintf (dstr, "  sb_%p [shape=none,label=<\n", sblock);
	dasprintf (dstr, "    <table border=\"0\" cellborder=\"1\" "
					 "cellspacing=\"0\">\n");
	dasprintf (dstr, "      <tr>\n");
	dasprintf (dstr, "        <td>%p(%d)</td>\n", sblock, blockno);
	dasprintf (dstr, "        <td height=\"0\" colspan=\"2\" port=\"s\">\n");
	for (l = sblock->labels; l; l = l->next)
		dasprintf (dstr, "            %s(%d)\n", l->name, l->used);
	dasprintf (dstr, "        </td>\n");
	dasprintf (dstr, "        <td></td>\n");
	dasprintf (dstr, "      </tr>\n");
	for (s = sblock->statements; s; s = s->next)
		flow_statement (dstr, s);
	dasprintf (dstr, "      <tr>\n");
	dasprintf (dstr, "        <td></td>\n");
	dasprintf (dstr, "        <td height=\"0\" colspan=\"2\" "
					 "port=\"e\"></td>\n");
	dasprintf (dstr, "        <td></td>\n");
	dasprintf (dstr, "      </tr>\n");
	dasprintf (dstr, "    </table>>];\n");
	if (sblock->next && !is_goto ((statement_t *) sblock->tail))
		dasprintf (dstr, "  sb_%p:e -> sb_%p:s;\n", sblock, sblock->next);
	if ((target = get_target ((statement_t *) sblock->tail)))
		dasprintf (dstr, "  sb_%p:e -> sb_%p:s [label=\"%s\"];\n", sblock,
				   target, ((statement_t *) sblock->tail)->opcode);
	dasprintf (dstr, "\n");
}

void
print_flow (sblock_t *sblock, const char *filename)
{
	int         i;
	dstring_t  *dstr = dstring_newstr();

	dasprintf (dstr, "digraph flow_%p {\n", sblock);
	dasprintf (dstr, "  layout=dot; rankdir=TB;\n");
	for (i = 0; sblock; sblock = sblock->next, i++)
		flow_sblock (dstr, sblock, i);
	dasprintf (dstr, "}\n");

	if (filename) {
		QFile      *file;

		file = Qopen (filename, "wt");
		Qwrite (file, dstr->str, dstr->size - 1);
		Qclose (file);
	} else {
		fputs (dstr->str, stdout);
	}
	dstring_delete (dstr);
}
