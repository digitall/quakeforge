/*
	expr.c

	expression construction and manipulations

	Copyright (C) 2001 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2001/06/15

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

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#include <stdlib.h>

#include <QF/dstring.h>
#include <QF/mathlib.h>
#include <QF/sys.h>
#include <QF/va.h>

#include "qfcc.h"
#include "class.h"
#include "def.h"
#include "defspace.h"
#include "emit.h"
#include "expr.h"
#include "function.h"
#include "idstuff.h"
#include "immediate.h"
#include "method.h"
#include "options.h"
#include "reloc.h"
#include "strpool.h"
#include "struct.h"
#include "symtab.h"
#include "type.h"
#include "qc-parse.h"

static expr_t *free_exprs;
int         lineno_base;

etype_t     qc_types[] = {
	ev_void,							// ex_error
	ev_void,							// ex_state
	ev_void,							// ex_bool
	ev_void,							// ex_label
	ev_void,							// ex_block
	ev_void,							// ex_expr
	ev_void,							// ex_uexpr
	ev_void,							// ex_def
	ev_void,							// ex_symbol
	ev_void,							// ex_temp

	ev_void,							// ex_nil
	ev_string,							// ex_string
	ev_float,							// ex_float
	ev_vector,							// ex_vector
	ev_entity,							// ex_entity
	ev_field,							// ex_field
	ev_func,							// ex_func
	ev_pointer,							// ex_pointer
	ev_quat,							// ex_quaternion
	ev_integer,							// ex_integer
	ev_integer,							// ex_uinteger
	ev_short,							// ex_short
};

type_t     *ev_types[ev_type_count] = {
	&type_void,
	&type_string,
	&type_float,
	&type_vector,
	&type_entity,
	&type_field,
	&type_function,
	&type_pointer,
	&type_quaternion,
	&type_integer,
	&type_short,
	&type_void,							// FIXME what type?
};

expr_type   expr_types[] = {
	ex_nil,								// ev_void
	ex_string,							// ev_string
	ex_float,							// ev_float
	ex_vector,							// ev_vector
	ex_entity,							// ev_entity
	ex_field,							// ev_field
	ex_func,							// ev_func
	ex_pointer,							// ev_pointer
	ex_quaternion,						// ev_quat
	ex_integer,							// ev_integer
	ex_uinteger,						// ev_uinteger
	ex_short,							// ev_short
	ex_nil,								// ev_struct
	ex_nil,								// ev_object
	ex_nil,								// ev_class
	ex_nil,								// ev_sel
	ex_nil,								// ev_array
};

type_t *
get_type (expr_t *e)
{
	switch (e->type) {
		case ex_label:
		case ex_error:
			return 0;					// something went very wrong
		case ex_bool:
			if (options.code.progsversion == PROG_ID_VERSION)
				return &type_float;
			return &type_integer;
		case ex_nil:
		case ex_state:
			return &type_void;
		case ex_block:
			if (e->e.block.result)
				return get_type (e->e.block.result);
			return &type_void;
		case ex_expr:
		case ex_uexpr:
			return e->e.expr.type;
		case ex_def:
			return e->e.def->type;
		case ex_symbol:
			return e->e.symbol->type;
		case ex_temp:
			return e->e.temp.type;
		case ex_pointer:
			return pointer_type (e->e.pointer.type);
		case ex_integer:
			if (options.code.progsversion == PROG_ID_VERSION) {
				e->type = ex_float;
				e->e.float_val = e->e.integer_val;
			}
			// fall through
		case ex_string:
		case ex_float:
		case ex_vector:
		case ex_entity:
		case ex_field:
		case ex_func:
		case ex_quaternion:
		case ex_uinteger:
		case ex_short:
			return ev_types[qc_types[e->type]];
	}
	return 0;
}

etype_t
extract_type (expr_t *e)
{
	type_t     *type = get_type (e);

	if (type)
		return type->type;
	return ev_type_count;
}

const char *
get_op_string (int op)
{
	switch (op) {
		case PAS:	return ".=";
		case OR:	return "||";
		case AND:	return "&&";
		case EQ:	return "==";
		case NE:	return "!=";
		case LE:	return "<=";
		case GE:	return ">=";
		case LT:	return "<";
		case GT:	return ">";
		case '=':	return "=";
		case '+':	return "+";
		case '-':	return "-";
		case '*':	return "*";
		case '/':	return "/";
		case '%':	return "%";
		case '&':	return "&";
		case '|':	return "|";
		case '^':	return "^";
		case '~':	return "~";
		case '!':	return "!";
		case SHL:	return "<<";
		case SHR:	return ">>";
		case '.':	return ".";
		case 'i':	return "<if>";
		case 'n':	return "<ifnot>";
		case IFBE:	return "<ifbe>";
		case IFB:	return "<ifb>";
		case IFAE:	return "<ifae>";
		case IFA:	return "<ifa>";
		case 'g':	return "<goto>";
		case 'r':	return "<return>";
		case 'b':	return "<bind>";
		case 's':	return "<state>";
		case 'c':	return "<call>";
		case 'C':	return "<cast>";
		case 'M':	return "<move>";
		default:
			return "unknown";
	}
}

expr_t *
type_mismatch (expr_t *e1, expr_t *e2, int op)
{
	dstring_t  *t1 = dstring_newstr ();
	dstring_t  *t2 = dstring_newstr ();

	print_type_str (t1, get_type (e1));
	print_type_str (t2, get_type (e2));

	e1 = error (e1, "type mismatch: %s %s %s",
				t1->str, get_op_string (op), t2->str);
	dstring_delete (t1);
	dstring_delete (t2);
	return e1;
}

expr_t *
param_mismatch (expr_t *e, int param, const char *fn, type_t *t1, type_t *t2)
{
	dstring_t  *s1 = dstring_newstr ();
	dstring_t  *s2 = dstring_newstr ();

	print_type_str (s1, t1);
	print_type_str (s2, t2);

	e = error (e, "type mismatch for parameter %d of %s: %s %s", param, fn,
			   s1->str, s2->str);
	dstring_delete (s1);
	dstring_delete (s2);
	return e;
}

expr_t *
cast_error (expr_t *e, type_t *t1, type_t *t2)
{
	dstring_t  *s1 = dstring_newstr ();
	dstring_t  *s2 = dstring_newstr ();

	print_type_str (s1, t1);
	print_type_str (s2, t2);

	e =  error (e, "can not cast from %s to %s", s1->str, s2->str);
	dstring_delete (s1);
	dstring_delete (s2);
	return e;
}

expr_t *
test_error (expr_t *e, type_t *t)
{
	dstring_t  *s = dstring_newstr ();

	print_type_str (s, t);

	e =  error (e, "%s cannot be tested", s->str);
	dstring_delete (s);
	return e;
}

static void
check_initialized (expr_t *e)
{
	const char *name;

	if (e->type == ex_def
		&& !(e->e.def->type->type == ev_func
			 && !e->e.def->local)
		&& !is_struct (e->e.def->type)
		&& !e->e.def->external
		&& !e->e.def->initialized) {
		name = e->e.def->name;
		if (options.warnings.uninited_variable && !e->e.def->suppress) {
			if (options.code.local_merging)
				warning (e, "%s may be used uninitialized", name);
			else
				notice (e, "%s may be used uninitialized", name);
		}
		e->e.def->suppress = 1;	// warn only once
		if (options.traditional && options.code.local_merging
			&& !e->e.def->set) {
			def_t      *def = e->e.def;
			e->e.def->set = 1;	// auto-init only once
			e = assign_expr (e, new_nil_expr ());
			e->file = def->file;
			e->line = def->line;
			e->next = current_func->var_init;
			current_func->var_init = e;
			notice (e, "auto-initializing %s", name);
		}
	}
}

expr_t *
inc_users (expr_t *e)
{
	if (e && e->type == ex_temp)
		e->e.temp.users++;
	else if (e && e->type == ex_block)
		inc_users (e->e.block.result);
	return e;
}

expr_t *
dec_users (expr_t *e)
{
	if (e && e->type == ex_temp)
		e->e.temp.users--;
	else if (e && e->type == ex_block)
		dec_users (e->e.block.result);
	return e;
}

expr_t *
new_expr (void)
{
	expr_t     *e;

	ALLOC (16384, expr_t, exprs, e);

	e->line = pr.source_line;
	e->file = pr.source_file;
	return e;
}

expr_t *
copy_expr (expr_t *e)
{
	expr_t     *n;
	expr_t     *t;

	if (!e)
		return 0;
	switch (e->type) {
		case ex_error:
		case ex_def:
		case ex_symbol:
		case ex_nil:
		case ex_string:
		case ex_float:
		case ex_vector:
		case ex_entity:
		case ex_field:
		case ex_func:
		case ex_pointer:
		case ex_quaternion:
		case ex_integer:
		case ex_uinteger:
		case ex_short:
			// nothing to do here
			n = new_expr ();
			*n = *e;
			return n;
		case ex_state:
			return new_state_expr (copy_expr (e->e.state.frame),
								   copy_expr (e->e.state.think),
								   copy_expr (e->e.state.step));
		case ex_bool:
			n = new_expr ();
			*n = *e;
			if (e->e.bool.true_list) {
				int         count = e->e.bool.true_list->size;
				size_t      size = (size_t)&((ex_list_t *) 0)->e[count];
				n->e.bool.true_list = malloc (size);
				while (count--)
					n->e.bool.true_list->e[count] =
						copy_expr ( e->e.bool.true_list->e[count]);
			}
			if (e->e.bool.false_list) {
				int         count = e->e.bool.false_list->size;
				size_t      size = (size_t)&((ex_list_t *) 0)->e[count];
				n->e.bool.false_list = malloc (size);
				while (count--)
					n->e.bool.false_list->e[count] =
						copy_expr ( e->e.bool.false_list->e[count]);
			}
			n->e.bool.e = copy_expr (e->e.bool.e);
			return n;
		case ex_label:
			/// Create a fresh label
			return new_label_expr ();
		case ex_block:
			n = new_expr ();
			*n = *e;
			n->e.block.head = 0;
			n->e.block.tail = &n->e.block.head;
			n->e.block.result = 0;
			for (t = e->e.block.head; t; t = t->next) {
				if (t == e->e.block.result) {
					n->e.block.result = copy_expr (t);
					append_expr (n, n->e.block.result);
				} else {
					append_expr (n, copy_expr (t));
				}
			}
			if (e->e.block.result && !n->e.block.result) {
				error (e, "internal: bogus block result?");
				abort ();
			}
			break;
		case ex_expr:
			n = new_expr ();
			*n = *e;
			n->e.expr.e1 = copy_expr (e->e.expr.e1);
			n->e.expr.e2 = copy_expr (e->e.expr.e2);
			return n;
		case ex_uexpr:
			n = new_expr ();
			*n = *e;
			n->e.expr.e1 = copy_expr (e->e.expr.e1);
			return n;
		case ex_temp:
			n = new_expr ();
			*n = *e;
			n->e.temp.expr = copy_expr (e->e.temp.expr);
			e->e.temp.users = 0;	//FIXME?
			return n;
	}
	error (e, "internal: invalid expression");
	abort ();
}

const char *
new_label_name (void)
{
	static int  label = 0;
	int         lnum = ++label;
	const char *fname = current_func->def->name;
	char       *lname;

	lname = nva ("$%s_%d", fname, lnum);
	SYS_CHECKMEM (lname);
	return lname;
}
#if 0
static expr_t *
new_error_expr (void)
{
	expr_t     *e = new_expr ();
	e->type = ex_error;
	return e;
}
#endif
expr_t *
new_state_expr (expr_t *frame, expr_t *think, expr_t *step)
{
	expr_t     *s = new_expr ();

	s->type = ex_state;
	s->e.state.frame = frame;
	s->e.state.think = think;
	s->e.state.step = step;
	return s;
}

expr_t *
new_bool_expr (ex_list_t *true_list, ex_list_t *false_list, expr_t *e)
{
	expr_t     *b = new_expr ();

	b->type = ex_bool;
	b->e.bool.true_list = true_list;
	b->e.bool.false_list = false_list;
	b->e.bool.e = e;
	return b;
}

expr_t *
new_label_expr (void)
{

	expr_t     *l = new_expr ();

	l->type = ex_label;
	l->e.label.name = new_label_name ();
	l->e.label.next = pr.labels;
	pr.labels = &l->e.label;
	return l;
}

expr_t *
new_block_expr (void)
{
	expr_t     *b = new_expr ();

	b->type = ex_block;
	b->e.block.head = 0;
	b->e.block.tail = &b->e.block.head;
	return b;
}

expr_t *
new_binary_expr (int op, expr_t *e1, expr_t *e2)
{
	expr_t     *e = new_expr ();

	if (e1->type == ex_error)
		return e1;
	if (e2 && e2->type == ex_error)
		return e2;

	inc_users (e1);
	inc_users (e2);

	e->type = ex_expr;
	e->e.expr.op = op;
	e->e.expr.e1 = e1;
	e->e.expr.e2 = e2;
	return e;
}

expr_t *
new_unary_expr (int op, expr_t *e1)
{
	expr_t     *e = new_expr ();

	if (e1 && e1->type == ex_error)
		return e1;

	inc_users (e1);

	e->type = ex_uexpr;
	e->e.expr.op = op;
	e->e.expr.e1 = e1;
	return e;
}

expr_t *
new_def_expr (def_t *def)
{
	expr_t     *e = new_expr ();
	e->type = ex_def;
	e->e.def = def;
	return e;
}

expr_t *
new_symbol_expr (symbol_t *symbol)
{
	expr_t     *e = new_expr ();
	e->type = ex_symbol;
	e->e.symbol = symbol;
	return e;
}

expr_t *
new_temp_def_expr (type_t *type)
{
	expr_t     *e = new_expr ();

	e->type = ex_temp;
	e->e.temp.type = type;
	return e;
}

expr_t *
new_nil_expr (void)
{
	expr_t     *e = new_expr ();
	e->type = ex_nil;
	return e;
}

expr_t *
new_name_expr (const char *name)
{
	expr_t     *e = new_expr ();
	symbol_t   *sym;

	sym = symtab_lookup (current_symtab, name);
	if (!sym)
		return error (0, "undefined symbol %s", name);
	e->type = ex_symbol;
	e->e.symbol = sym;
	return e;
}

expr_t *
new_string_expr (const char *string_val)
{
	expr_t     *e = new_expr ();
	e->type = ex_string;
	e->e.string_val = string_val;
	return e;
}

expr_t *
new_float_expr (float float_val)
{
	expr_t     *e = new_expr ();
	e->type = ex_float;
	e->e.float_val = float_val;
	return e;
}

expr_t *
new_vector_expr (const float *vector_val)
{
	expr_t     *e = new_expr ();
	e->type = ex_vector;
	memcpy (e->e.vector_val, vector_val, sizeof (e->e.vector_val));
	return e;
}

expr_t *
new_entity_expr (int entity_val)
{
	expr_t     *e = new_expr ();
	e->type = ex_entity;
	e->e.entity_val = entity_val;
	return e;
}

expr_t *
new_field_expr (int field_val, type_t *type, def_t *def)
{
	expr_t     *e = new_expr ();
	e->type = ex_field;
	e->e.pointer.val = field_val;
	e->e.pointer.type = type;
	e->e.pointer.def = def;
	return e;
}

expr_t *
new_func_expr (int func_val)
{
	expr_t     *e = new_expr ();
	e->type = ex_func;
	e->e.func_val = func_val;
	return e;
}

expr_t *
new_pointer_expr (int val, type_t *type, def_t *def)
{
	expr_t     *e = new_expr ();
	e->type = ex_pointer;
	e->e.pointer.val = val;
	e->e.pointer.type = type;
	e->e.pointer.def = def;
	return e;
}

expr_t *
new_quaternion_expr (const float *quaternion_val)
{
	expr_t     *e = new_expr ();
	e->type = ex_quaternion;
	memcpy (e->e.quaternion_val, quaternion_val, sizeof (e->e.quaternion_val));
	return e;
}

expr_t *
new_integer_expr (int integer_val)
{
	expr_t     *e = new_expr ();
	e->type = ex_integer;
	e->e.integer_val = integer_val;
	return e;
}

expr_t *
new_uinteger_expr (unsigned int uinteger_val)
{
	expr_t     *e = new_expr ();
	e->type = ex_uinteger;
	e->e.uinteger_val = uinteger_val;
	return e;
}

expr_t *
new_short_expr (short short_val)
{
	expr_t     *e = new_expr ();
	e->type = ex_short;
	e->e.short_val = short_val;
	return e;
}

int
is_constant (expr_t *e)
{
	if (e->type >= ex_nil
		|| (e->type == ex_def && e->e.def->constant))
		return 1;
	return 0;
}

expr_t *
constant_expr (expr_t *var)
{
	def_t      *def;

	if (var->type != ex_def || !var->e.def->constant)
		return var;

	def = var->e.def;
	def->used = 1;
	switch (def->type->type) {
		case ev_string:
			return new_string_expr (G_GETSTR (def->ofs));
		case ev_float:
			return new_float_expr (G_FLOAT (def->ofs));
		case ev_vector:
			return new_vector_expr (G_VECTOR (def->ofs));
		case ev_field:
			return new_field_expr (G_INT (def->ofs), def->type, def);
		case ev_integer:
			return new_integer_expr (G_INT (def->ofs));
//		case ev_uinteger:
//			return new_uinteger_expr (G_INT (def->ofs));
		default:
			return var;
	}
}

expr_t *
new_bind_expr (expr_t *e1, expr_t *e2)
{
	expr_t     *e;

	if (!e2 || e2->type != ex_temp) {
		error (e1, "internal error");
		abort ();
	}
	e = new_expr ();
	e->type = ex_expr;
	e->e.expr.op = 'b';
	e->e.expr.e1 = e1;
	e->e.expr.e2 = e2;
	e->e.expr.type = get_type (e2);
	return e;
}

expr_t *
new_self_expr (void)
{
	def_t      *def = get_def (&type_entity, ".self", pr.scope, st_extern);

	def_initialized (def);
	return new_def_expr (def);
}

expr_t *
new_this_expr (void)
{
	type_t     *type = field_type (&type_id);
	def_t      *def = get_def (type, ".this", pr.scope, st_extern);

	def_initialized (def);
	def->nosave = 1;
	return new_def_expr (def);
}

static expr_t *
param_expr (const char *name, type_t *type)
{
	def_t      *def = get_def (&type_param, name, pr.scope, st_extern);
	expr_t     *def_expr;

	def_initialized (def);
	def->nosave = 1;
	def_expr = new_def_expr (def);
	return unary_expr ('.', address_expr (def_expr, 0, type));
}

expr_t *
new_ret_expr (type_t *type)
{
	return param_expr (".return", type);
}

expr_t *
new_param_expr (type_t *type, int num)
{
	return param_expr (va (".param_%d", num), type);
}

expr_t *
new_move_expr (expr_t *e1, expr_t *e2, type_t *type)
{
	expr_t     *e = new_binary_expr ('M', e1, e2);
	e->e.expr.type = type;
	return e;
}

expr_t *
append_expr (expr_t *block, expr_t *e)
{
	if (block->type != ex_block)
		abort ();

	if (!e || e->type == ex_error)
		return block;

	if (e->next) {
		error (e, "append_expr: expr loop detected");
		abort ();
	}

	*block->e.block.tail = e;
	block->e.block.tail = &e->next;

	return block;
}

void
print_expr (expr_t *e)
{
	printf (" ");
	if (!e) {
		printf ("(nil)");
		return;
	}
	switch (e->type) {
		case ex_error:
			printf ("(error)");
			break;
		case ex_state:
			printf ("[");
			print_expr (e->e.state.frame);
			printf (",");
			print_expr (e->e.state.think);
			printf (",");
			print_expr (e->e.state.step);
			printf ("]");
			break;
		case ex_bool:
			printf ("bool");	//FIXME
			break;
		case ex_label:
			printf ("%s", e->e.label.name);
			break;
		case ex_block:
			if (e->e.block.result) {
				print_expr (e->e.block.result);
				printf ("=");
			}
			printf ("{\n");
			for (e = e->e.block.head; e; e = e->next) {
				print_expr (e);
				puts ("");
			}
			printf ("}");
			break;
		case ex_expr:
			print_expr (e->e.expr.e1);
			if (e->e.expr.op == 'c') {
				expr_t     *p = e->e.expr.e2;

				printf ("(");
				while (p) {
					print_expr (p);
					if (p->next)
						printf (",");
					p = p->next;
				}
				printf (")");
			} else if (e->e.expr.op == 'b') {
				printf (" <-->");
				print_expr (e->e.expr.e2);
			} else {
				print_expr (e->e.expr.e2);
				printf (" %s", get_op_string (e->e.expr.op));
			}
			break;
		case ex_uexpr:
			print_expr (e->e.expr.e1);
			printf (" u%s", get_op_string (e->e.expr.op));
			break;
		case ex_def:
			if (e->e.def->name)
				printf ("%s", e->e.def->name);
			if (!e->e.def->global) {
				printf ("<%d>", e->e.def->ofs);
			} else {
				printf ("[%d]", e->e.def->ofs);
			}
			break;
		case ex_symbol:
			printf ("%s", e->e.symbol->name);
			break;
		case ex_temp:
			printf ("(");
			print_expr (e->e.temp.expr);
			printf (":");
			if (e->e.temp.def) {
				if (e->e.temp.def->name) {
					printf ("%s", e->e.temp.def->name);
				} else {
					printf ("<%d>", e->e.temp.def->ofs);
				}
			} else {
				printf ("<>");
			}
			printf (":%s:%d)@", pr_type_name[e->e.temp.type->type],
					e->e.temp.users);
			break;
		case ex_nil:
			printf ("NIL");
			break;
		case ex_string:
			printf ("\"%s\"", e->e.string_val);
			break;
		case ex_float:
			printf ("%g", e->e.float_val);
			break;
		case ex_vector:
			printf ("'%g", e->e.vector_val[0]);
			printf (" %g", e->e.vector_val[1]);
			printf (" %g'", e->e.vector_val[2]);
			break;
		case ex_quaternion:
			printf ("'%g", e->e.quaternion_val[0]);
			printf (" %g", e->e.quaternion_val[1]);
			printf (" %g", e->e.quaternion_val[2]);
			printf (" %g'", e->e.quaternion_val[3]);
			break;
		case ex_pointer:
			printf ("(%s)[%d]", pr_type_name[e->e.pointer.type->type],
					e->e.pointer.val);
			break;
		case ex_field:
			printf ("%d", e->e.pointer.val);
			break;
		case ex_entity:
		case ex_func:
		case ex_integer:
			printf ("%d", e->e.integer_val);
			break;
		case ex_uinteger:
			printf ("%u", e->e.uinteger_val);
			break;
		case ex_short:
			printf ("%d", e->e.short_val);
			break;
	}
}

static expr_t *
field_expr (expr_t *e1, expr_t *e2)
{
	return type_mismatch (e1, e2, '.');
}

expr_t *
test_expr (expr_t *e, int test)
{
	static float zero[4] = {0, 0, 0, 0};
	expr_t     *new = 0;
	etype_t     type;

	if (e->type == ex_error)
		return e;

	if (!test)
		return unary_expr ('!', e);

	type = extract_type (e);
	check_initialized (e);
	if (e->type == ex_error)
		return e;
	switch (type) {
		case ev_type_count:
			error (e, "internal error");
			abort ();
		case ev_void:
			if (options.traditional) {
				if (options.warnings.traditional)
					warning (e, "void has no value");
				return e;
			}
			return error (e, "void has no value");
		case ev_string:
			new = new_string_expr (0);
			break;
//		case ev_uinteger:
		case ev_integer:
		case ev_short:
			return e;
		case ev_float:
			if (options.code.fast_float
				|| options.code.progsversion == PROG_ID_VERSION)
				return e;
			new = new_float_expr (0);
			break;
		case ev_vector:
			new = new_vector_expr (zero);
			break;
		case ev_entity:
			new = new_entity_expr (0);
			break;
		case ev_field:
			new = new_field_expr (0, 0, 0);
			break;
		case ev_func:
			new = new_func_expr (0);
			break;
		case ev_pointer:
			new = new_nil_expr ();
			break;
		case ev_quat:
			new = new_quaternion_expr (zero);
			break;
		case ev_invalid:
			return test_error (e, get_type (e));
	}
	new->line = e->line;
	new->file = e->file;
	new = binary_expr (NE, e, new);
	new->line = e->line;
	new->file = e->file;
	return new;
}

void
backpatch (ex_list_t *list, expr_t *label)
{
	int         i;
	expr_t     *e;

	if (!list)
		return;

	for (i = 0; i < list->size; i++) {
		e = list->e[i];
		if (e->type == ex_uexpr && e->e.expr.op == 'g')
			e->e.expr.e1 = label;
		else if (e->type == ex_expr && (e->e.expr.op == 'i'
										|| e->e.expr.op == 'n'))
			e->e.expr.e2 = label;
		else {
			error (e, "internal compiler error");
			abort ();
		}
	}
}

static ex_list_t *
merge (ex_list_t *l1, ex_list_t *l2)
{
	ex_list_t  *m;

	if (!l1 && !l2) {
		error (0, "internal error");
		abort ();
	}
	if (!l2)
		return l1;
	if (!l1)
		return l2;
	m = malloc ((size_t)&((ex_list_t *)0)->e[l1->size + l2->size]);
	m->size = l1->size + l2->size;
	memcpy (m->e, l1->e, l1->size * sizeof (expr_t *));
	memcpy (m->e + l1->size, l2->e, l2->size * sizeof (expr_t *));
	return m;
}

static ex_list_t *
make_list (expr_t *e)
{
	ex_list_t  *m;

	m = malloc ((size_t)&((ex_list_t *) 0)->e[1]);
	m->size = 1;
	m->e[0] = e;
	return m;
}

expr_t *
convert_bool (expr_t *e, int block)
{
	expr_t     *b;

	if (e->type == ex_expr && (e->e.expr.op == '=' || e->e.expr.op == PAS)
		&& !e->paren) {
		if (options.warnings.precedence)
			warning (e, "suggest parentheses around assignment "
					 "used as truth value");
	}

	if (e->type == ex_uexpr && e->e.expr.op == '!') {
		e = convert_bool (e->e.expr.e1, 0);
		if (e->type == ex_error)
			return e;
		e = unary_expr ('!', e);
	}
	if (e->type != ex_bool) {
		e = test_expr (e, 1);
		if (e->type == ex_error)
			return e;
		if (e->type == ex_integer) {
			b = new_unary_expr ('g', 0);
			if (e->e.integer_val)
				e = new_bool_expr (make_list (b), 0, b);
			else
				e = new_bool_expr (0, make_list (b), b);
		} else {
			b = new_block_expr ();
			append_expr (b, new_binary_expr ('i', e, 0));
			append_expr (b, new_unary_expr ('g', 0));
			e = new_bool_expr (make_list (b->e.block.head),
							   make_list (b->e.block.head->next), b);
		}
	}
	if (block && e->e.bool.e->type != ex_block) {
		expr_t     *block = new_block_expr ();
		append_expr (block, e->e.bool.e);
		e->e.bool.e = block;
	}
	return e;
}

static expr_t *
convert_from_bool (expr_t *e, type_t *type)
{
	expr_t     *zero;
	expr_t     *one;
	expr_t     *cond;

	if (type == &type_float) {
		one = new_float_expr (1);
		zero = new_float_expr (0);
	} else if (type == &type_integer) {
		one = new_integer_expr (1);
		zero = new_integer_expr (0);
//	} else if (type == &type_uinteger) {
//		one = new_uinteger_expr (1);
//		zero = new_uinteger_expr (0);
	} else {
		return error (e, "can't convert from bool value");
	}
	cond = new_expr ();
	*cond = *e;
	cond->next = 0;

	cond = conditional_expr (cond, one, zero);
	e->type = cond->type;
	e->e = cond->e;
	return e;
}

expr_t *
bool_expr (int op, expr_t *label, expr_t *e1, expr_t *e2)
{
	expr_t     *block;

	if (!options.code.short_circuit)
		return binary_expr (op, e1, e2);

	e1 = convert_bool (e1, 0);
	if (e1->type == ex_error)
		return e1;

	e2 = convert_bool (e2, 0);
	if (e2->type == ex_error)
		return e2;

	block = new_block_expr ();
	append_expr (block, e1);
	append_expr (block, label);
	append_expr (block, e2);

	switch (op) {
		case OR:
			backpatch (e1->e.bool.false_list, label);
			return new_bool_expr (merge (e1->e.bool.true_list,
										 e2->e.bool.true_list),
								  e2->e.bool.false_list, block);
			break;
		case AND:
			backpatch (e1->e.bool.true_list, label);
			return new_bool_expr (e2->e.bool.true_list,
								  merge (e1->e.bool.false_list,
										 e2->e.bool.false_list), block);
			break;
	}
	error (e1, "internal error");
	abort ();
}

void
convert_int (expr_t *e)
{
	e->type = ex_float;
	e->e.float_val = e->e.integer_val;
}

void
convert_uint (expr_t *e)
{
	e->type = ex_float;
	e->e.float_val = e->e.uinteger_val;
}

void
convert_short (expr_t *e)
{
	e->type = ex_float;
	e->e.float_val = e->e.short_val;
}

void
convert_uint_int (expr_t *e)
{
	e->type = ex_integer;
	e->e.integer_val = e->e.uinteger_val;
}

void
convert_int_uint (expr_t *e)
{
	e->type = ex_uinteger;
	e->e.uinteger_val = e->e.integer_val;
}

void
convert_short_int (expr_t *e)
{
	e->type = ex_integer;
	e->e.integer_val = e->e.short_val;
}

void
convert_short_uint (expr_t *e)
{
	e->type = ex_uinteger;
	e->e.uinteger_val = e->e.short_val;
}

void
convert_nil (expr_t *e, type_t *t)
{
	e->type = expr_types[t->type];
	if (e->type == ex_pointer)
		e->e.pointer.type = &type_void;
}

int
is_compare (int op)
{
	if (op == EQ || op == NE || op == LE || op == GE || op == LT || op == GT
		|| op == '>' || op == '<')
		return 1;
	return 0;
}

int
is_math (int op)
{
	if (op == '*' || op == '/' || op == '+' || op == '-')
		return 1;
	return 0;
}

int
is_logic (int op)
{
	if (op == OR || op == AND)
		return 1;
	return 0;
}

static expr_t *
check_precedence (int op, expr_t *e1, expr_t *e2)
{
	if (e1->type == ex_uexpr && e1->e.expr.op == '!' && !e1->paren) {
		if (options.traditional) {
			if (op != AND && op != OR && op != '=') {
				notice (e1, "precedence of `!' and `%s' inverted for "
							"traditional code", get_op_string (op));
				e1->e.expr.e1->paren = 1;
				return unary_expr ('!', binary_expr (op, e1->e.expr.e1, e2));
			}
		} else if (op == '&' || op == '|') {
			if (options.warnings.precedence)
				warning (e1, "ambiguous logic. Suggest explicit parentheses "
						 "with expressions involving ! and %s",
						 get_op_string (op));
		}
	}
	if (options.traditional) {
		if (e2->type == ex_expr && !e2->paren) {
			if (((op == '&' || op == '|')
				 && (is_math (e2->e.expr.op) || is_compare (e2->e.expr.op)))
				|| (op == '='
					&& (e2->e.expr.op == OR || e2->e.expr.op == AND))) {
				notice (e1, "precedence of `%s' and `%s' inverted for "
							"traditional code", get_op_string (op),
							get_op_string (e2->e.expr.op));
				e1 = binary_expr (op, e1, e2->e.expr.e1);
				e1->paren = 1;
				return binary_expr (e2->e.expr.op, e1, e2->e.expr.e2);
			}
			if (((op == EQ || op == NE) && is_compare (e2->e.expr.op))
				|| (op == OR && e2->e.expr.op == AND)
				|| (op == '|' && e2->e.expr.op == '&')) {
				notice (e1, "precedence of `%s' raised to `%s' for "
							"traditional code", get_op_string (op),
							get_op_string (e2->e.expr.op));
				e1 = binary_expr (op, e1, e2->e.expr.e1);
				e1->paren = 1;
				return binary_expr (e2->e.expr.op, e1, e2->e.expr.e2);
			}
		} else if (e1->type == ex_expr && !e1->paren) {
			if (((op == '&' || op == '|')
				 && (is_math (e1->e.expr.op) || is_compare (e1->e.expr.op)))
				|| (op == '='
					&& (e1->e.expr.op == OR || e1->e.expr.op == AND))) {
				notice (e1, "precedence of `%s' and `%s' inverted for "
							"traditional code", get_op_string (op),
							get_op_string (e1->e.expr.op));
				e2 = binary_expr (op, e1->e.expr.e2, e2);
				e2->paren = 1;
				return binary_expr (e1->e.expr.op, e1->e.expr.e1, e2);
			}
		}
	} else {
		if (e2->type == ex_expr && !e2->paren) {
			if ((op == '&' || op == '|' || op == '^')
				&& (is_math (e2->e.expr.op) || is_compare (e2->e.expr.op))) {
				if (options.warnings.precedence)
					warning (e2, "suggest parentheses around %s in "
							 "operand of %c",
							 is_compare (e2->e.expr.op)
							 		? "comparison"
							 		: get_op_string (e2->e.expr.op),
							 op);
			}
		}
		if (e1->type == ex_expr && !e1->paren) {
			if ((op == '&' || op == '|' || op == '^')
				&& (is_math (e1->e.expr.op) || is_compare (e1->e.expr.op))) {
				if (options.warnings.precedence)
					warning (e1, "suggest parentheses around %s in "
							 "operand of %c",
							 is_compare (e1->e.expr.op)
							 		? "comparison"
							 		: get_op_string (e1->e.expr.op),
							 op);
			}
		}
	}
	return 0;
}

static int
has_function_call (expr_t *e)
{
	switch (e->type) {
		case ex_bool:
			return has_function_call (e->e.bool.e);
		case ex_block:
			if (e->e.block.is_call)
				return 1;
			for (e = e->e.block.head; e; e = e->next)
				if (has_function_call (e))
					return 1;
			return 0;
		case ex_expr:
			if (e->e.expr.op == 'c')
				return 1;
			return (has_function_call (e->e.expr.e1)
					|| has_function_call (e->e.expr.e2));
		case ex_uexpr:
			if (e->e.expr.op != 'g')
				return has_function_call (e->e.expr.e1);
		default:
			return 0;
	}
}

expr_t *
binary_expr (int op, expr_t *e1, expr_t *e2)
{
	type_t     *t1, *t2;
	type_t     *type = 0;
	expr_t     *e;

	if (e1->type == ex_error)
		return e1;
	if (e2->type == ex_error)
		return e2;

	if (e1->type == ex_block && e1->e.block.is_call
		&& has_function_call (e2) && e1->e.block.result) {
		e = new_temp_def_expr (get_type (e1->e.block.result));
		inc_users (e);					// for the block itself
		e1 = assign_expr (e, e1);
	}

	if (op == '.')
		return field_expr (e1, e2);
	check_initialized (e1);

	check_initialized (e2);

	if (op == OR || op == AND) {
		e1 = test_expr (e1, true);
		e2 = test_expr (e2, true);
	}

	if (e1->type == ex_error)
		return e1;
	if (e2->type == ex_error)
		return e2;
	e1 = constant_expr (e1);
	e2 = constant_expr (e2);
	t1 = get_type (e1);
	t2 = get_type (e2);
	if (!t1 || !t2) {
		error (e1, "internal error");
		abort ();
	}
	if (op == EQ || op == NE) {
		if (e1->type == ex_nil) {
			t1 = t2;
			convert_nil (e1, t1);
		} else if (e2->type == ex_nil) {
			t2 = t1;
			convert_nil (e2, t2);
		}
	}

	if (e1->type == ex_bool)
		e1 = convert_from_bool (e1, t2);

	if (e2->type == ex_bool)
		e2 = convert_from_bool (e2, t1);

	if (e1->type == ex_short) {
		if (t2 == &type_integer) {
			convert_short_int (e1);
			t1 = &type_integer;
//		} else if (t2 == &type_uinteger) {
//			convert_short_uint (e1);
//			t1 = &type_uinteger;
		}
	}

	if (e2->type == ex_short) {
		if (t1 == &type_integer) {
			convert_short_int (e2);
			t2 = &type_integer;
//		} else if (t1 == &type_uinteger) {
//			convert_short_uint (e2);
//			t2 = &type_uinteger;
		}
	}

	if (e1->type == ex_integer) {
		if (t2 == &type_float
			|| t2 == &type_vector
			|| t2 == &type_quaternion) {
			convert_int (e1);
			t1 = &type_float;
//		} else if (t2 == &type_uinteger) {
//			convert_int_uint (e1);
//			t1 = &type_uinteger;
		}
	} else if (e1->type == ex_uinteger) {
		if (t2 == &type_float
			|| t2 == &type_vector
			|| t2 == &type_quaternion) {
			convert_uint (e1);
			t1 = &type_float;
		} else if (t2 == &type_integer) {
			convert_uint_int (e1);
			t1 = &type_integer;
		}
	} else if (e2->type == ex_integer) {
		if (t1 == &type_float
			|| t1 == &type_vector
			|| t1 == &type_quaternion) {
			convert_int (e2);
			t2 = &type_float;
//		} else if (t1 == &type_uinteger) {
//			convert_int_uint (e2);
//			t2 = &type_uinteger;
		}
	} else if (e2->type == ex_uinteger) {
		if (t1 == &type_float
			|| t1 == &type_vector
			|| t1 == &type_quaternion) {
			convert_uint (e2);
			t2 = &type_float;
		} else if (t1 == &type_integer) {
			convert_uint_int (e2);
			t2 = &type_integer;
		}
	}

	if ((e = check_precedence (op, e1, e2)))
		return e;

	if (t1 != t2) {
		switch (t1->type) {
			case ev_float:
				if (t2 == &type_vector || t2 == &type_quaternion) {
					type = &type_vector;
				} else {
					type = &type_float;
				}
				break;
			case ev_vector:
				if (t2 == &type_quaternion) {
					type = &type_quaternion;
				} else {
					type = &type_vector;
				}
				break;
			case ev_field:
				if (t1->t.fldptr.type == t2) {
					type = t1->t.fldptr.type;
				} else {
					goto type_mismatch;
				}
				break;
			case ev_func:
				if (e1->type == ex_func && !e1->e.func_val) {
					type = t2;
				} else if (e2->type == ex_func && !e2->e.func_val) {
					type = t1;
				} else {
					goto type_mismatch;
				}
				break;
			case ev_pointer:
				if (!type_assignable (t1, t2) && !type_assignable (t2, t1))
					goto type_mismatch;
				type = t1;
				break;
			default:
			  type_mismatch:
				type = t1;
				break;
				//return type_mismatch (e1, e2, op);
		}
	} else {
		type = t1;
	}
	if (is_compare (op) || is_logic (op)) {
		if (options.code.progsversion > PROG_ID_VERSION)
			type = &type_integer;
		else
			type = &type_float;
	} else if (op == '*' && t1 == &type_vector && t2 == &type_vector) {
		type = &type_float;
	}
	if (!type)
		error (e1, "internal error");

	if (options.code.progsversion == PROG_ID_VERSION) {
		switch (op) {
			case '%':
				{
					expr_t     *tmp1, *tmp2, *tmp3, *t1, *t2;
					e = new_block_expr ();
					t1 = new_temp_def_expr (&type_float);
					t2 = new_temp_def_expr (&type_float);
					tmp1 = new_temp_def_expr (&type_float);
					tmp2 = new_temp_def_expr (&type_float);
					tmp3 = new_temp_def_expr (&type_float);

					append_expr (e, new_bind_expr (e1, t1));
					e1 = binary_expr ('&', t1, t1);
					append_expr (e, new_bind_expr (e1, tmp1));

					append_expr (e, new_bind_expr (e2, t2));
					e2 = binary_expr ('&', t2, t2);
					append_expr (e, new_bind_expr (e2, tmp2));

					e1 = binary_expr ('/', tmp1, tmp2);
					append_expr (e, assign_expr (tmp3, e1));

					e2 = binary_expr ('&', tmp3, tmp3);
					append_expr (e, new_bind_expr (e2, tmp3));

					e1 = binary_expr ('*', tmp2, tmp3);
					e2 = binary_expr ('-', tmp1, e1);
					e->e.block.result = e2;
					return e;
				}
				break;
		}
	}
	e = new_binary_expr (op, e1, e2);
	e->e.expr.type = type;
	return e;
}

expr_t *
asx_expr (int op, expr_t *e1, expr_t *e2)
{
	if (e1->type == ex_error)
		return e1;
	else if (e2->type == ex_error)
		return e2;
	else {
		expr_t     *e = new_expr ();

		*e = *e1;
		e2->paren = 1;
		return assign_expr (e, binary_expr (op, e1, e2));
	}
}

expr_t *
unary_expr (int op, expr_t *e)
{
	check_initialized (e);
	if (e->type == ex_error)
		return e;
	switch (op) {
		case '-':
			switch (e->type) {
				case ex_error:
				case ex_label:
				case ex_state:
					error (e, "internal error");
					abort ();
				case ex_uexpr:
					if (e->e.expr.op == '-')
						return e->e.expr.e1;
				case ex_block:
					if (!e->e.block.result)
						return error (e, "invalid type for unary -");
				case ex_expr:
				case ex_bool:
				case ex_def:
				case ex_temp:
					{
						expr_t     *n = new_unary_expr (op, e);

						n->e.expr.type = (e->type == ex_def)
							? e->e.def->type : e->e.expr.type;
						return n;
					}
				case ex_symbol:
					{
						expr_t     *n = new_unary_expr (op, e);

						n->e.expr.type = e->e.symbol->type;
						return n;
					}
				case ex_short:
					e->e.short_val *= -1;
					return e;
				case ex_integer:
				case ex_uinteger:
					e->e.integer_val *= -1;
					return e;
				case ex_float:
					e->e.float_val *= -1;
					return e;
				case ex_nil:
				case ex_string:
				case ex_entity:
				case ex_field:
				case ex_func:
				case ex_pointer:
					return error (e, "invalid type for unary -");
				case ex_vector:
					e->e.vector_val[0] *= -1;
					e->e.vector_val[1] *= -1;
					e->e.vector_val[2] *= -1;
					return e;
				case ex_quaternion:
					e->e.quaternion_val[0] *= -1;
					e->e.quaternion_val[1] *= -1;
					e->e.quaternion_val[2] *= -1;
					e->e.quaternion_val[3] *= -1;
					return e;
			}
			break;
		case '!':
			switch (e->type) {
				case ex_error:
				case ex_label:
				case ex_state:
					error (e, "internal error");
					abort ();
				case ex_bool:
					return new_bool_expr (e->e.bool.false_list,
										  e->e.bool.true_list, e);
				case ex_block:
					if (!e->e.block.result)
						return error (e, "invalid type for unary !");
				case ex_uexpr:
				case ex_expr:
				case ex_def:
				case ex_symbol:
				case ex_temp:
					{
						expr_t     *n = new_unary_expr (op, e);

						if (options.code.progsversion > PROG_ID_VERSION)
							n->e.expr.type = &type_integer;
						else
							n->e.expr.type = &type_float;
						return n;
					}
				case ex_nil:
					return error (e, "invalid type for unary !");
				case ex_short:
					e->e.short_val = !e->e.short_val;
					return e;
				case ex_integer:
				case ex_uinteger:
					e->e.integer_val = !e->e.integer_val;
					return e;
				case ex_float:
					e->e.integer_val = !e->e.float_val;
					e->type = ex_integer;
					return e;
				case ex_string:
					e->e.integer_val = !e->e.string_val || !e->e.string_val[0];
					e->type = ex_integer;
					return e;
				case ex_vector:
					e->e.integer_val = !e->e.vector_val[0]
						&& !e->e.vector_val[1]
						&& !e->e.vector_val[2];
					e->type = ex_integer;
					return e;
				case ex_quaternion:
					e->e.integer_val = !e->e.quaternion_val[0]
						&& !e->e.quaternion_val[1]
						&& !e->e.quaternion_val[2]
						&& !e->e.quaternion_val[3];
					e->type = ex_integer;
					return e;
				case ex_entity:
				case ex_field:
				case ex_func:
				case ex_pointer:
					error (e, "internal error");
					abort ();
			}
			break;
		case '~':
			switch (e->type) {
				case ex_error:
				case ex_label:
				case ex_state:
					error (e, "internal error");
					abort ();
				case ex_uexpr:
					if (e->e.expr.op == '~')
						return e->e.expr.e1;
					goto bitnot_expr;
				case ex_block:
					if (!e->e.block.result)
						return error (e, "invalid type for unary ~");
					goto bitnot_expr;
				case ex_expr:
				case ex_bool:
				case ex_def:
				case ex_symbol:
				case ex_temp:
bitnot_expr:
					if (options.code.progsversion == PROG_ID_VERSION) {
						expr_t     *n1 = new_integer_expr (-1);
						return binary_expr ('-', n1, e);
					} else {
						expr_t     *n = new_unary_expr (op, e);
						type_t     *t = get_type (e);

						if (t != &type_integer && t != &type_float
							&& t != &type_quaternion)
							return error (e, "invalid type for unary ~");
						n->e.expr.type = t;
						return n;
					}
				case ex_short:
					e->e.short_val = ~e->e.short_val;
					return e;
				case ex_integer:
				case ex_uinteger:
					e->e.integer_val = ~e->e.integer_val;
					return e;
				case ex_float:
					e->e.float_val = ~(int) e->e.float_val;
					return e;
				case ex_quaternion:
					QuatConj (e->e.quaternion_val, e->e.quaternion_val);
					return e;
				case ex_nil:
				case ex_string:
				case ex_vector:
				case ex_entity:
				case ex_field:
				case ex_func:
				case ex_pointer:
					return error (e, "invalid type for unary ~");
			}
			break;
		case '.':
			if (extract_type (e) != ev_pointer)
				return error (e, "invalid type for unary .");
			e = new_unary_expr ('.', e);
			e->e.expr.type = get_type (e->e.expr.e1)->t.fldptr.type;
			return e;
		case '+':
			return e;			// FIXME typechecking
	}
	error (e, "internal error");
	abort ();
}

expr_t *
build_function_call (expr_t *fexpr, type_t *ftype, expr_t *params)
{
	expr_t     *e;
	int         arg_count = 0, parm_count = 0;
	int         i;
	expr_t     *args = 0, **a = &args;
	type_t     *arg_types[MAX_PARMS];
	expr_t     *arg_exprs[MAX_PARMS][2];
	int         arg_expr_count = 0;
	expr_t     *call;
	expr_t     *err = 0;

	for (e = params; e; e = e->next) {
		if (e->type == ex_error)
			return e;
		arg_count++;
	}

	if (arg_count > MAX_PARMS) {
		return error (fexpr, "more than %d parameters", MAX_PARMS);
	}
	if (ftype->t.func.num_params < -1) {
		if (-arg_count > ftype->t.func.num_params + 1) {
			if (!options.traditional)
				return error (fexpr, "too few arguments");
			if (options.warnings.traditional)
				warning (fexpr, "too few arguments");
		}
		parm_count = -ftype->t.func.num_params - 1;
	} else if (ftype->t.func.num_params >= 0) {
		if (arg_count > ftype->t.func.num_params) {
			return error (fexpr, "too many arguments");
		} else if (arg_count < ftype->t.func.num_params) {
			if (!options.traditional)
				return error (fexpr, "too few arguments");
			if (options.warnings.traditional)
				warning (fexpr, "too few arguments");
		}
		parm_count = ftype->t.func.num_params;
	}
	for (i = arg_count - 1, e = params; i >= 0; i--, e = e->next) {
		type_t     *t = get_type (e);
 
		if (!type_size (t))
			err = error (e, "type of formal parameter %d is incomplete",
						 i + 1);
		if (type_size (t) > type_size (&type_param))
			err = error (e, "formal parameter %d is too large to be passed by"
						 " value", i + 1);
		check_initialized (e);
		if (ftype->t.func.param_types[i] == &type_float && e->type == ex_integer) {
			convert_int (e);
			t = &type_float;
		}
		if (i < parm_count) {
			if (e->type == ex_nil)
				convert_nil (e, t = ftype->t.func.param_types[i]);
			if (e->type == ex_bool)
				convert_from_bool (e, ftype->t.func.param_types[i]);
			if (e->type == ex_error)
				return e;
			if (!type_assignable (ftype->t.func.param_types[i], t)) {
				err = param_mismatch (e, i + 1, fexpr->e.def->name,
									  ftype->t.func.param_types[i], t);
			}
			t = ftype->t.func.param_types[i];
		} else {
			if (e->type == ex_nil)
				convert_nil (e, t = &type_vector);	//XXX largest param size
			if (e->type == ex_bool)
				convert_from_bool (e, get_type (e));
			if (e->type == ex_integer
				&& options.code.progsversion == PROG_ID_VERSION)
				convert_int (e);
			if (e->type == ex_integer && options.warnings.vararg_integer)
				warning (e, "passing integer constant into ... function");
		}
		arg_types[arg_count - 1 - i] = t;
	}
	if (err)
		return err;

	call = new_block_expr ();
	call->e.block.is_call = 1;
	for (e = params, i = 0; e; e = e->next, i++) {
		if (has_function_call (e)) {
			*a = new_temp_def_expr (arg_types[i]);
			arg_exprs[arg_expr_count][0] = cast_expr (arg_types[i], e);
			arg_exprs[arg_expr_count][1] = *a;
			arg_expr_count++;
		} else {
			*a = cast_expr (arg_types[i], e);
		}
		// new_binary_expr calls inc_users for both args, but in_users doesn't
		// walk expression chains so only the first arg expression in the chain
		// (the last arg in the call) gets its user count incremented, thus
		// ensure all other arg expressions get their user counts incremented.
		if (a != &args)
			inc_users (*a);
		a = &(*a)->next;
	}
	for (i = 0; i < arg_expr_count - 1; i++) {
		append_expr (call, assign_expr (arg_exprs[i][1], arg_exprs[i][0]));
	}
	if (arg_expr_count) {
		e = new_bind_expr (arg_exprs[arg_expr_count - 1][0],
						   arg_exprs[arg_expr_count - 1][1]);
		inc_users (arg_exprs[arg_expr_count - 1][0]);
		inc_users (arg_exprs[arg_expr_count - 1][1]);
		append_expr (call, e);
	}
	e = new_binary_expr ('c', fexpr, args);
	e->e.expr.type = ftype->t.func.type;
	append_expr (call, e);
	if (ftype->t.func.type != &type_void) {
		call->e.block.result = new_ret_expr (ftype->t.func.type);
	} else if (options.traditional) {
		call->e.block.result = new_ret_expr (&type_float);
	}
	return call;
}

expr_t *
function_expr (expr_t *fexpr, expr_t *params)
{
	type_t     *ftype;

	find_function (fexpr, params);
	ftype = get_type (fexpr);

	if (fexpr->type == ex_error)
		return fexpr;
	if (ftype->type != ev_func) {
		if (fexpr->type == ex_def)
			return error (fexpr, "Called object \"%s\" is not a function",
						  fexpr->e.def->name);
		else
			return error (fexpr, "Called object is not a function");
	}

	if (fexpr->type == ex_def && params && params->type == ex_string) {
		// FIXME eww, I hate this, but it's needed :(
		// FIXME make a qc hook? :)
		def_t      *func = fexpr->e.def;
		def_t      *e = ReuseConstant (params, 0);

		if (strncmp (func->name, "precache_sound", 14) == 0)
			PrecacheSound (e, func->name[14]);
		else if (strncmp (func->name, "precache_model", 14) == 0)
			PrecacheModel (e, func->name[14]);
		else if (strncmp (func->name, "precache_file", 13) == 0)
			PrecacheFile (e, func->name[13]);
	}

	return build_function_call (fexpr, ftype, params);
}

expr_t *
return_expr (function_t *f, expr_t *e)
{
	type_t     *t;

	if (!e) {
		if (f->def->type->t.func.type != &type_void) {
			if (options.traditional) {
				if (options.warnings.traditional)
					warning (e,
							 "return from non-void function without a value");
				e = new_nil_expr ();
			} else {
				e = error (e, "return from non-void function without a value");
				return e;
			}
		}
		return new_unary_expr ('r', 0);
	}

	t = get_type (e);

	if (e->type == ex_error)
		return e;
	if (f->def->type->t.func.type == &type_void) {
		if (!options.traditional)
			return error (e, "returning a value for a void function");
		if (options.warnings.traditional)
			warning (e, "returning a value for a void function");
	}
	if (e->type == ex_bool)
		e = convert_from_bool (e, f->def->type->t.func.type);
	if (f->def->type->t.func.type == &type_float && e->type == ex_integer) {
		e->type = ex_float;
		e->e.float_val = e->e.integer_val;
		t = &type_float;
	}
	check_initialized (e);
	if (t == &type_void) {
		if (e->type == ex_nil) {
			t = f->def->type->t.func.type;
			e->type = expr_types[t->type];
			if (e->type == ex_nil)
				return error (e, "invalid return type for NIL");
		} else {
			if (!options.traditional)
				return error (e, "void value not ignored as it ought to be");
			if (options.warnings.traditional)
				warning (e, "void value not ignored as it ought to be");
			//FIXME does anything need to be done here?
		}
	}
	if (!type_assignable (f->def->type->t.func.type, t)) {
		if (!options.traditional)
			return error (e, "type mismatch for return value of %s",
						  f->def->name);
		if (options.warnings.traditional)
			warning (e, "type mismatch for return value of %s",
					 f->def->name);
	} else {
		if (f->def->type->t.func.type != t)
			e = cast_expr (f->def->type->t.func.type, e);
	}
	return new_unary_expr ('r', e);
}

expr_t *
conditional_expr (expr_t *cond, expr_t *e1, expr_t *e2)
{
	expr_t     *block = new_block_expr ();
	type_t     *type1 = get_type (e1);
	type_t     *type2 = get_type (e2);
	expr_t     *tlabel = new_label_expr ();
	expr_t     *flabel = new_label_expr ();
	expr_t     *elabel = new_label_expr ();

	if (cond->type == ex_error)
		return cond;
	if (e1->type == ex_error)
		return e1;
	if (e2->type == ex_error)
		return e2;

	cond = convert_bool (cond, 1);
	if (cond->type == ex_error)
		return cond;

	backpatch (cond->e.bool.true_list, tlabel);
	backpatch (cond->e.bool.false_list, flabel);

	block->e.block.result = (type1 == type2) ? new_temp_def_expr (type1) : 0;
	append_expr (block, cond);
	append_expr (cond->e.bool.e, flabel);
	if (block->e.block.result)
		append_expr (block, assign_expr (block->e.block.result, e2));
	else
		append_expr (block, e2);
	append_expr (block, new_unary_expr ('g', elabel));
	append_expr (block, tlabel);
	if (block->e.block.result)
		append_expr (block, assign_expr (block->e.block.result, e1));
	else
		append_expr (block, e1);
	append_expr (block, elabel);
	return block;
}

expr_t *
incop_expr (int op, expr_t *e, int postop)
{
	expr_t     *one;

	if (e->type == ex_error)
		return e;

	one = new_integer_expr (1);		// integer constants get auto-cast to float
	if (postop) {
		expr_t     *t1, *t2;
		type_t     *type = get_type (e);
		expr_t     *block = new_block_expr ();
		expr_t     *res = new_expr ();

		t1 = new_temp_def_expr (type);
		t2 = new_temp_def_expr (type);
		append_expr (block, assign_expr (t1, e));
		append_expr (block, assign_expr (t2, binary_expr (op, t1, one)));
		res = copy_expr (e);
		if (res->type == ex_uexpr && res->e.expr.op == '.')
			res = pointer_expr (address_expr (res, 0, 0));
		append_expr (block, assign_expr (res, t2));
		block->e.block.result = t1;
		return block;
	} else {
		return asx_expr (op, e, one);
	}
}

expr_t *
array_expr (expr_t *array, expr_t *index)
{
	type_t     *array_type = get_type (array);
	type_t     *index_type = get_type (index);
	expr_t     *scale;
	expr_t     *e;
	int         size;

	if (array->type == ex_error)
		return array;
	if (index->type == ex_error)
		return index;

	if (array_type->type != ev_pointer && !is_array (array_type))
		return error (array, "not an array");
	if (index_type != &type_integer)
		return error (index, "invalid array index type");
	if (array_type->t.func.num_params
		&& index->type >= ex_integer
		&& (index->e.integer_val < array_type->t.array.base
			|| index->e.integer_val - array_type->t.array.base
				>= array_type->t.array.size))
			return error (index, "array index out of bounds");
	size = type_size (array_type->t.array.type);
	scale = new_expr ();
	scale->type = expr_types[index_type->type];
	scale->e.integer_val = size;
	index = binary_expr ('*', index, scale);
	index = binary_expr ('-', index,
				 binary_expr ('*',
							  new_integer_expr (array_type->t.array.base),
							  scale));
	index = fold_constants (index);
	if ((index->type == ex_integer
		 && index->e.integer_val < 32768 && index->e.integer_val >= -32768)
		|| (index->type == ex_uinteger
			&& index->e.uinteger_val < 32768)) {
		index->type = ex_short;
	}
	if (is_array (array_type)) {
		e = address_expr (array, index, array_type->t.array.type);
	} else {
		if (index->type != ex_short || index->e.integer_val) {
			e = new_binary_expr ('&', array, index);
			//e->e.expr.type = array_type->aux_type;
			e->e.expr.type = array_type;
		} else {
			e = array;
		}
	}
	e = unary_expr ('.', e);
	return e;
}

expr_t *
pointer_expr (expr_t *pointer)
{
	type_t     *pointer_type = get_type (pointer);

	if (pointer->type == ex_error)
		return pointer;
	if (pointer_type->type != ev_pointer)
		return error (pointer, "not a pointer");
	return array_expr (pointer, new_integer_expr (0));
}

expr_t *
address_expr (expr_t *e1, expr_t *e2, type_t *t)
{
	expr_t     *e;
	type_t     *type;

	if (e1->type == ex_error)
		return e1;

	if (!t)
		t = get_type (e1);

	switch (e1->type) {
		case ex_def:
			{
				def_t      *def = e1->e.def;
				def->used = 1;
				type = def->type;
				if (is_struct (type) || is_class (type)) {
					e = new_pointer_expr (0, t, def);
					e->line = e1->line;
					e->file = e1->file;
				} else if (is_array (type)) {
					e = e1;
					e->type = ex_pointer;
					e->e.pointer.val = 0;
					e->e.pointer.type = t;
					e->e.pointer.def = def;
				} else {
					e = new_unary_expr ('&', e1);
					e->e.expr.type = pointer_type (t);
				}
				break;
			}
		case ex_expr:
			if (e1->e.expr.op == '.') {
				e = e1;
				e->e.expr.op = '&';
				e->e.expr.type = pointer_type (e->e.expr.type);
				break;
			} else if (e1->e.expr.op == 'b') {
				e = new_unary_expr ('&', e1);
				e->e.expr.type = pointer_type (e1->e.expr.type);
				break;
			}
			return error (e1, "invalid type for unary &");
		case ex_uexpr:
			if (e1->e.expr.op == '.') {
				e = e1->e.expr.e1;
				if (e->type == ex_expr && e->e.expr.op == '.') {
					e->e.expr.type = pointer_type (e->e.expr.type);
					e->e.expr.op = '&';
				}
				break;
			}
			return error (e1, "invalid type for unary &");
		default:
			return error (e1, "invalid type for unary &");
	}
	if (e2) {
		if (e2->type == ex_error)
			return e2;
		if (e->type == ex_pointer && e2->type == ex_short) {
			e->e.pointer.val += e2->e.short_val;
			e->e.pointer.type = t;
		} else {
			if (e2->type != ex_short || e2->e.short_val) {
				if (e->type == ex_expr && e->e.expr.op == '&') {
					e = new_binary_expr ('&', e->e.expr.e1,
										 binary_expr ('+', e->e.expr.e2, e2));
				} else {
					e = new_binary_expr ('&', e, e2);
				}
			}
			if (e->type == ex_expr || e->type == ex_uexpr)
				e->e.expr.type = pointer_type (t);
		}
	}
	return e;
}

expr_t *
build_if_statement (expr_t *test, expr_t *s1, expr_t *s2)
{
	int         line = pr.source_line;
	string_t    file = pr.source_file;
	expr_t     *if_expr;
	expr_t     *tl = new_label_expr ();
	expr_t     *fl = new_label_expr ();

	pr.source_line = test->line;
	pr.source_file = test->file;

	if_expr = new_block_expr ();

	test = convert_bool (test, 1);
	if (test->type != ex_error) {
		backpatch (test->e.bool.true_list, tl);
		backpatch (test->e.bool.false_list, fl);
		append_expr (test->e.bool.e, tl);
		append_expr (if_expr, test);
	}
	append_expr (if_expr, s1);

	if (s2) {
		expr_t     *nl = new_label_expr ();
		append_expr (if_expr, new_unary_expr ('g', nl));

		append_expr (if_expr, fl);
		append_expr (if_expr, s2);
		append_expr (if_expr, nl);
	} else {
		append_expr (if_expr, fl);
	}

	pr.source_line = line;
	pr.source_file = file;

	return if_expr;
}

expr_t *
build_while_statement (expr_t *test, expr_t *statement,
					   expr_t *break_label, expr_t *continue_label)
{
	int         line = pr.source_line;
	string_t    file = pr.source_file;
	expr_t     *l1 = new_label_expr ();
	expr_t     *l2 = break_label;
	expr_t     *while_expr;

	pr.source_line = test->line;
	pr.source_file = test->file;

	while_expr = new_block_expr ();

	append_expr (while_expr, new_unary_expr ('g', continue_label));
	append_expr (while_expr, l1);
	append_expr (while_expr, statement);
	append_expr (while_expr, continue_label);

	test = convert_bool (test, 1);
	if (test->type != ex_error) {
		backpatch (test->e.bool.true_list, l1);
		backpatch (test->e.bool.false_list, l2);
		append_expr (test->e.bool.e, l2);
		append_expr (while_expr, test);
	}

	pr.source_line = line;
	pr.source_file = file;

	return while_expr;
}

expr_t *
build_do_while_statement (expr_t *statement, expr_t *test,
						  expr_t *break_label, expr_t *continue_label)
{
	expr_t *l1 = new_label_expr ();
	int         line = pr.source_line;
	string_t    file = pr.source_file;
	expr_t     *do_while_expr;

	pr.source_line = test->line;
	pr.source_file = test->file;

	do_while_expr = new_block_expr ();

	append_expr (do_while_expr, l1);
	append_expr (do_while_expr, statement);
	append_expr (do_while_expr, continue_label);

	test = convert_bool (test, 1);
	if (test->type != ex_error) {
		backpatch (test->e.bool.true_list, l1);
		backpatch (test->e.bool.false_list, break_label);
		append_expr (test->e.bool.e, break_label);
		append_expr (do_while_expr, test);
	}

	pr.source_line = line;
	pr.source_file = file;

	return do_while_expr;
}

expr_t *
build_for_statement (expr_t *init, expr_t *test, expr_t *next,
					 expr_t *statement,
					 expr_t *break_label, expr_t *continue_label)
{
	expr_t     *tl = new_label_expr ();
	expr_t     *fl = break_label;
	expr_t     *l1 = 0;
	expr_t     *t;
	int         line = pr.source_line;
	string_t    file = pr.source_file;
	expr_t     *for_expr;

	if (next)
		t = next;
	else if (test)
		t = test;
	else if (init)
		t = init;
	else
		t = continue_label;
	pr.source_line = t->line;
	pr.source_file = t->file;

	for_expr = new_block_expr ();

	append_expr (for_expr, init);
	if (test) {
		l1 = new_label_expr ();
		append_expr (for_expr, new_unary_expr ('g', l1));
	}
	append_expr (for_expr, tl);
	append_expr (for_expr, statement);
	append_expr (for_expr, continue_label);
	append_expr (for_expr, next);
	if (test) {
		append_expr (for_expr, l1);
		test = convert_bool (test, 1);
		if (test->type != ex_error) {
			backpatch (test->e.bool.true_list, tl);
			backpatch (test->e.bool.false_list, fl);
			append_expr (test->e.bool.e, fl);
			append_expr (for_expr, test);
		}
	} else {
		append_expr (for_expr, new_unary_expr ('g', tl));
		append_expr (for_expr, fl);
	}

	pr.source_line = line;
	pr.source_file = file;

	return for_expr;
}

expr_t *
build_state_expr (expr_t *frame, expr_t *think, expr_t *step)
{
	if (frame->type == ex_integer)
		convert_int (frame);
	else if (frame->type == ex_uinteger)
		convert_uint (frame);
	if (!type_assignable (&type_float, get_type (frame)))
		return error (frame, "invalid type for frame number");
	if (extract_type (think) != ev_func)
		return error (think, "invalid type for think");
	if (step) {
		if (step->type == ex_integer)
			convert_int (step);
		else if (step->type == ex_uinteger)
			convert_uint (step);
		if (!type_assignable (&type_float, get_type (step)))
			return error (step, "invalid type for frame number");
	}
	return new_state_expr (frame, think, step);
}

static int
is_indirect (expr_t *e)
{
	if (e->type == ex_expr && e->e.expr.op == '.')
		return 1;
	if (!(e->type == ex_uexpr && e->e.expr.op == '.'))
		return 0;
	e = e->e.expr.e1;
	if (e->type != ex_pointer
		|| !(POINTER_VAL (e->e.pointer) >= 0
			 && POINTER_VAL (e->e.pointer) < 65536)) {
		return 1;
	}
	return 0;
}

static inline int
is_lvalue (expr_t *e)
{
	if (e->type == ex_def || e->type == ex_temp)
		return 1;
	if (e->type == ex_expr && e->e.expr.op == '.')
		return 1;
	if (e->type == ex_uexpr && e->e.expr.op == '.')
		return 1;
	return 0;
}

expr_t *
assign_expr (expr_t *e1, expr_t *e2)
{
	int         op = '=';
	type_t     *t1, *t2, *type;
	expr_t     *e;

	if (e1->type == ex_error)
		return e1;
	if (e2->type == ex_error)
		return e2;

	if (options.traditional) {
		if (e2->type == ex_expr && !e2->paren
			&& (e2->e.expr.op == AND || e2->e.expr.op == OR)) {
			notice (e2, "precedence of `%s' and `%s' inverted for "
						"traditional code", get_op_string (op),
						get_op_string (e2->e.expr.op));
			e1 = assign_expr (e1, e2->e.expr.e1);
			e1->paren = 1;
			return binary_expr (e2->e.expr.op, e1, e2->e.expr.e2);
		}
	}

	if (e1->type == ex_def)
		def_initialized (e1->e.def);

	if (!is_lvalue (e1)) {
		if (options.traditional)
			warning (e1, "invalid lvalue in assignment");
		else
			return error (e1, "invalid lvalue in assignment");
	}
	t1 = get_type (e1);
	t2 = get_type (e2);
	if (!t1 || !t2) {
		error (e1, "internal error");
		abort ();
	}
	//XXX func = func ???
	if (t1->type != ev_pointer || !is_array (t2))
		check_initialized (e2);
	else {
		e2 = address_expr (e2, 0, t2->t.fldptr.type);	// FIXME
		t2 = get_type (e2);
	}
	if (e2->type == ex_bool)
		e2 = convert_from_bool (e2, t1);

	if (t1->type != ev_void && e2->type == ex_nil) {
		t2 = t1;
		convert_nil (e2, t2);
	}

	e2->rvalue = 1;

	if (!type_assignable (t1, t2)) {
		if (options.traditional) {
			if (t1->type == ev_func && t2->type == ev_func) {
				warning (e1, "assignment between disparate function types");
			} else if (t1->type == ev_float && t2->type == ev_vector) {
				warning (e1, "assignment of vector to float");
				e2 = binary_expr ('.', e2, new_name_expr ("x"));
			} else if (t1->type == ev_vector && t2->type == ev_float) {
				warning (e1, "assignment of float to vector");
				e1 = binary_expr ('.', e1, new_name_expr ("x"));
			} else {
				return type_mismatch (e1, e2, op);
			}
		} else {
			return type_mismatch (e1, e2, op);
		}
	}
	type = t1;
	if (is_indirect (e1) && is_indirect (e2)) {
		if (is_struct (get_type (e2))) {
			e1 = address_expr (e1, 0, 0);
			e2 = address_expr (e2, 0, 0);
			e = new_move_expr (e1, e2, get_type (e2));
		} else {
			expr_t     *temp = new_temp_def_expr (t1);

			e = new_block_expr ();
			append_expr (e, assign_expr (temp, e2));
			append_expr (e, assign_expr (e1, temp));
			e->e.block.result = temp;
		}
		return e;
	} else if (is_indirect (e1)) {
		if (is_struct (get_type (e1))) {
			e1 = address_expr (e1, 0, 0);
			return new_move_expr (e1, e2, get_type (e2));
		}
		if (e1->type == ex_expr) {
			if (get_type (e1->e.expr.e1) == &type_entity) {
				type = e1->e.expr.type;
				e1->e.expr.type = pointer_type (type);
				e1->e.expr.op = '&';
			}
			op = PAS;
		} else {
			e = e1->e.expr.e1;
			if (e->type != ex_pointer
				|| !(POINTER_VAL (e->e.pointer) > 0
					 && POINTER_VAL (e->e.pointer) < 65536)) {
				e1 = e;
				op = PAS;
			}
		}
	} else if (is_indirect (e2)) {
		if (is_struct (get_type (e1))) {
			e2 = address_expr (e2, 0, 0);
			e2->rvalue = 1;
			return new_move_expr (e1, e2, get_type (e2));
		}
		if (e2->type == ex_uexpr) {
			e = e2->e.expr.e1;
			if (e->type != ex_pointer
				|| !(POINTER_VAL (e->e.pointer) > 0
					 && POINTER_VAL (e->e.pointer) < 65536)) {
				if (e->type == ex_expr && e->e.expr.op == '&'
					&& e->e.expr.type->type == ev_pointer
					&& e->e.expr.e1->type < ex_nil) {
					e2 = e;
					e2->e.expr.op = '.';
					e2->e.expr.type = t2;
					e2->rvalue = 1;
				}
			}
		}
	}
	if (is_struct (get_type (e1))) {
		return new_move_expr (e1, e2, get_type (e2));
	}
	if (!type)
		error (e1, "internal error");

	e = new_binary_expr (op, e1, e2);
	e->e.expr.type = type;
	return e;
}

expr_t *
cast_expr (type_t *type, expr_t *e)
{
	expr_t    *c;
	type_t    *e_type;

	if (e->type == ex_error)
		return e;

	check_initialized (e);

	e_type = get_type (e);

	if (type == e_type)
		return e;

	if (!(type->type == ev_pointer
		  && (e_type->type == ev_pointer
			  || e_type == &type_integer //|| e_type == &type_uinteger
			  || is_array (e_type)))
		&& !(type->type == ev_func && e_type->type == ev_func)
		&& !(((type == &type_integer)
			  && (e_type == &type_float || e_type == &type_integer
				  || e_type->type == ev_pointer))
			 || (type == &type_float
				 && (e_type == &type_integer)))) {
		return cast_error (e, get_type (e), type);
	}
	if (is_array (e_type)) {
		return address_expr (e, 0, 0);
	}
	if (e->type == ex_uexpr && e->e.expr.op == '.') {
		e->e.expr.type = type;
		c = e;
	} else {
		c = new_unary_expr ('C', e);
		c->e.expr.type = type;
	}
	return c;
}

void
init_elements (def_t *def, expr_t *eles)
{
}

expr_t *
selector_expr (keywordarg_t *selector)
{
	dstring_t  *sel_id = dstring_newstr ();
	expr_t     *sel;
	def_t      *sel_def;
	int         index;

	selector = copy_keywordargs (selector);
	selector = (keywordarg_t *) reverse_params ((param_t *) selector);
	selector_name (sel_id, selector);
	index = selector_index (sel_id->str);
	index *= type_size (type_SEL.t.fldptr.type);
	sel_def = get_def (type_SEL.t.fldptr.type, "_OBJ_SELECTOR_TABLE", pr.scope,
					   st_extern);
	sel = new_def_expr (sel_def);
	dstring_delete (sel_id);
	return address_expr (sel, new_short_expr (index), 0);
}

expr_t *
protocol_expr (const char *protocol)
{
	return error (0, "not implemented");
}

expr_t *
encode_expr (type_t *type)
{
	dstring_t  *encoding = dstring_newstr ();
	expr_t     *e;

	encode_type (encoding, type);
	e = new_string_expr (encoding->str);
	free (encoding);
	return e;
}

expr_t *
super_expr (class_type_t *class_type)
{
	def_t      *super_d;
	expr_t     *super;
	expr_t     *e;
	expr_t     *super_block;
	class_t    *class;
	class_type_t _class_type;

	if (!class_type)
		return error (0, "`super' used outside of class implementation");

	class = extract_class (class_type);

	if (!class->super_class)
		return error (0, "%s has no super class", class->name);

	super_d = get_def (&type_Super, ".super", current_func->scope, st_local);
	def_initialized (super_d);
	super = new_def_expr (super_d);
	super_block = new_block_expr ();

	e = assign_expr (binary_expr ('.', super, new_name_expr ("self")),
								  new_name_expr ("self"));
	append_expr (super_block, e);

	_class_type.type = ct_class;
	_class_type.c.class = class;
	e = new_def_expr (class_def (&_class_type, 1));
	e = assign_expr (binary_expr ('.', super, new_name_expr ("class")),
					 binary_expr ('.', e, new_name_expr ("super_class")));
	append_expr (super_block, e);

	e = address_expr (super, 0, &type_void);
	super_block->e.block.result = e;
	return super_block;
}

expr_t *
message_expr (expr_t *receiver, keywordarg_t *message)
{
	expr_t     *args = 0, **a = &args;
	expr_t     *selector = selector_expr (message);
	expr_t     *call;
	keywordarg_t *m;
	int         self = 0, super = 0, class_msg = 0;
	type_t     *rec_type;
	class_t    *class;
	method_t   *method;

	if (receiver->type == ex_symbol
		&& strcmp (receiver->e.symbol->name, "super") == 0) {
		super = 1;

		receiver = super_expr (current_class);

		if (receiver->type == ex_error)
			return receiver;
		class = extract_class (current_class);
		rec_type = class->type;
	} else {
		if (receiver->type == ex_symbol) {
			if (strcmp (receiver->e.symbol->name, "self") == 0)
				self = 1;
			if (get_class (receiver->e.symbol, 0))
				class_msg = 1;
		}
		rec_type = get_type (receiver);

		if (receiver->type == ex_error)
			return receiver;

		if (rec_type->type != ev_pointer
			  || !is_class (rec_type->t.fldptr.type))
			return error (receiver, "not a class/object");
		if (self) {
			class = extract_class (current_class);
			if (rec_type == &type_Class)
				class_msg = 1;
		} else {
			class = rec_type->t.fldptr.type->t.class;
		}
	}

	method = class_message_response (class, class_msg, selector);
	if (method)
		rec_type = method->type->t.func.type;

	for (m = message; m; m = m->next) {
		*a = m->expr;
		while ((*a))
			a = &(*a)->next;
	}
	*a = selector;
	a = &(*a)->next;
	*a = receiver;

	if (method) {
		expr_t      *err;
		if ((err = method_check_params (method, args)))
			return err;
		call = build_function_call (send_message (super), method->type, args);
	} else {
		call = build_function_call (send_message (super), &type_IMP, args);
	}

	if (call->type == ex_error)
		return receiver;

	call->e.block.result = new_ret_expr (rec_type);
	return call;
}

expr_t *
sizeof_expr (expr_t *expr, struct type_s *type)
{
	if (!((!expr) ^ (!type))) {
		error (0, "internal error");
		abort ();
	}
	if (!type)
		type = get_type (expr);
	expr = new_integer_expr (type_size (type));
	return expr;
}

static void
report_function (expr_t *e)
{
	static function_t *last_func = (function_t *)-1L;
	static string_t last_file;
	string_t    file = pr.source_file;
	srcline_t  *srcline;

	if (e)
		file = e->file;

	if (file != last_file) {
		for (srcline = pr.srcline_stack; srcline; srcline = srcline->next)
			fprintf (stderr, "In file included from %s:%d:\n",
					 G_GETSTR (srcline->source_file), srcline->source_line);
	}
	last_file = file;
	if (current_func != last_func) {
		if (current_func) {
			fprintf (stderr, "%s: In function `%s':\n", G_GETSTR (file),
					 current_func->name);
		} else if (current_class) {
			fprintf (stderr, "%s: In class `%s':\n", G_GETSTR (file),
					 get_class_name (current_class, 1));
		} else {
			fprintf (stderr, "%s: At top level:\n", G_GETSTR (file));
		}
	}
	last_func = current_func;
}

static void
_warning (expr_t *e, const char *fmt, va_list args)
{
	string_t    file = pr.source_file;
	int         line = pr.source_line;

	report_function (e);
	if (options.warnings.promote) {
		options.warnings.promote = 0;	// want to do this only once
		fprintf (stderr, "%s: warnings treated as errors\n", "qfcc");
		pr.error_count++;
	}

	if (e) {
		file = e->file;
		line = e->line;
	}
	fprintf (stderr, "%s:%d: warning: ", G_GETSTR (file), line);
	vfprintf (stderr, fmt, args);
	fputs ("\n", stderr);
}

expr_t *
notice (expr_t *e, const char *fmt, ...)
{
	va_list     args;

	if (options.notices.silent)
		return e;

	va_start (args, fmt);
	if (options.notices.promote) {
		_warning (e, fmt, args);
	} else {
		string_t    file = pr.source_file;
		int         line = pr.source_line;

		report_function (e);
		if (e) {
			file = e->file;
			line = e->line;
		}
		fprintf (stderr, "%s:%d: notice: ", G_GETSTR (file), line);
		vfprintf (stderr, fmt, args);
		fputs ("\n", stderr);
	}
	va_end (args);
	return e;
}

expr_t *
warning (expr_t *e, const char *fmt, ...)
{
	va_list     args;

	va_start (args, fmt);
	_warning (e, fmt, args);
	va_end (args);
	return e;
}

static void
_error (expr_t *e, const char *err, const char *fmt, va_list args)
{
	string_t    file = pr.source_file;
	int         line = pr.source_line;

	report_function (e);

	if (e) {
		file = e->file;
		line = e->line;
	}
	fprintf (stderr, "%s:%d: %s%s", G_GETSTR (file), line, err,
			 fmt ? ": " : "");
	if (fmt)
		vfprintf (stderr, fmt, args);
	fputs ("\n", stderr);
	pr.error_count++;
}

void
internal_error (expr_t *e, const char *fmt, ...)
{
	va_list     args;

	va_start (args, fmt);
	_error (e, "internal error", fmt, args);
	va_end (args);
	abort ();
}

expr_t *
error (expr_t *e, const char *fmt, ...)
{
	va_list     args;

	va_start (args, fmt);
	_error (e, "error", fmt, args);
	va_end (args);

	if (!e)
		e = new_expr ();
	e->type = ex_error;
	return e;
}
