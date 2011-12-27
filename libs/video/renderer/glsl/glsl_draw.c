/*
	glsl_draw.c

	2D drawing support for GLSL

	Copyright (C) 2011 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2011/12/23

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

static __attribute__ ((used)) const char rcsid[] = "$Id$";

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "QF/cvar.h"
#include "QF/draw.h"
#include "QF/dstring.h"
#include "QF/quakefs.h"
#include "QF/sys.h"
#include "QF/vid.h"

#include "QF/GLSL/defines.h"
#include "QF/GLSL/funcs.h"
#include "QF/GLSL/qf_textures.h"
#include "QF/GLSL/qf_vid.h"

#include "gl_draw.h"
#include "r_cvar.h"
#include "r_local.h"
#include "r_shared.h"

typedef struct {
	int         texnum;
} glpic_t;


static const char quakeicon_vert[] =
#include "quakeico.vc"
;

static const char quaketext_vert[] =
#include "quaketxt.vc"
;

static const char quake2d_frag[] =
#include "quake2d.fc"
;

static float proj_matrix[16];

static struct {
	int         program;
	shaderparam_t charmap;
	shaderparam_t palette;
	shaderparam_t matrix;
	shaderparam_t vertex;
	shaderparam_t dchar;
} quake_text = {
	0,
	{"texture", 1},
	{"palette", 1},
	{"mvp_mat", 1},
	{"vertex", 0},
	{"char", 0},
};

static struct {
	int         program;
	shaderparam_t icon;
	shaderparam_t palette;
	shaderparam_t matrix;
	shaderparam_t vertex;
} quake_icon = {
	0,
	{"texture", 1},
	{"palette", 1},
	{"mvp_mat", 1},
	{"vertex", 0},
};

VISIBLE byte *draw_chars;
static dstring_t *char_queue;
static int  char_texture;
static int  conback_texture;
static qpic_t *crosshair_pic;

static qpic_t *
make_glpic (const char *name, qpic_t *p)
{
	qpic_t     *pic = 0;
	glpic_t    *gl;

	if (p) {
		// FIXME is alignment ok?
		pic = malloc (sizeof (qpic_t) + sizeof (glpic_t));
		*pic = *p;
		gl = (glpic_t *) pic->data;
		gl->texnum = GL_LoadQuakeTexture (name, p->width, p->height, p->data);
	}
	return pic;
}

static void
make_quad (qpic_t *pic, int x, int y, int srcx, int srcy,
		   int width, int height, float verts[6][4])
{
	float       sl, sh, tl, th;

	sl = (float) srcx / (float) pic->width;
	sh = sl + (float) width / (float) pic->width;
	tl = (float) srcy / (float) pic->height;
	th = tl + (float) height / (float) pic->height;

	verts[0][0] = x;
	verts[0][1] = y;
	verts[0][2] = sl;
	verts[0][3] = tl;

	verts[1][0] = x + width;
	verts[1][1] = y;
	verts[1][2] = sh;
	verts[1][3] = tl;

	verts[2][0] = x + width;
	verts[2][1] = y + height;
	verts[2][2] = sh;
	verts[2][3] = th;

	verts[3][0] = x;
	verts[3][1] = y;
	verts[3][2] = sl;
	verts[3][3] = tl;

	verts[4][0] = x + width;
	verts[4][1] = y + height;
	verts[4][2] = sh;
	verts[4][3] = th;

	verts[5][0] = x;
	verts[5][1] = y + height;
	verts[5][2] = sl;
	verts[5][3] = th;
}

static void
draw_pic (int x, int y, qpic_t *pic, int srcx, int srcy, int width, int height)
{
	glpic_t    *gl;
	float       verts[6][4];

	make_quad (pic, x, y, srcx, srcy, width, height, verts);
	gl = (glpic_t *) pic->data;

	qfglUseProgram (quake_icon.program);
	qfglEnableVertexAttribArray (quake_icon.vertex.location);

	qfglUniformMatrix4fv (quake_icon.matrix.location, 1, false, proj_matrix);

	qfglUniform1i (quake_icon.icon.location, 0);
	qfglActiveTexture (GL_TEXTURE0 + 0);
	qfglEnable (GL_TEXTURE_2D);
	qfglBindTexture (GL_TEXTURE_2D, gl->texnum);

	qfglUniform1i (quake_icon.palette.location, 1);
	qfglActiveTexture (GL_TEXTURE0 + 1);
	qfglEnable (GL_TEXTURE_2D);
	qfglBindTexture (GL_TEXTURE_2D, glsl_palette);

	qfglVertexAttribPointer (quake_icon.vertex.location, 4, GL_FLOAT,
							 0, 0, verts);

	qfglDrawArrays (GL_TRIANGLES, 0, 6);

	qfglDisableVertexAttribArray (quake_icon.vertex.location);
	char_queue->size = 0;
}

VISIBLE qpic_t *
Draw_PicFromWad (const char *name)
{
	return make_glpic (name, W_GetLumpName (name));
}

VISIBLE qpic_t *
Draw_CachePic (const char *path, qboolean alpha)
{
	qpic_t     *p, *pic;

	if (strlen (path) < 4 || strcmp (path + strlen (path) - 4, ".lmp")
		|| !(p = (qpic_t *) QFS_LoadFile (path, 0))) {
		//FIXME load a null texture
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	}

	pic = make_glpic (path, p);
	free (p);
	return pic;
}

VISIBLE void
Draw_TextBox (int x, int y, int width, int lines, byte alpha)
{
}

VISIBLE void
Draw_Init (void)
{
	int         i;
	int         frag, vert;
	qpic_t     *pic;

	char_queue = dstring_new ();
	vert = GL_CompileShader ("quaketxt.vert", quaketext_vert, GL_VERTEX_SHADER);
	frag = GL_CompileShader ("quake2d.frag", quake2d_frag, GL_FRAGMENT_SHADER);
	quake_text.program = GL_LinkProgram ("quaketxt", vert, frag);
	GL_ResolveShaderParam (quake_text.program, &quake_text.charmap);
	GL_ResolveShaderParam (quake_text.program, &quake_text.palette);
	GL_ResolveShaderParam (quake_text.program, &quake_text.matrix);
	GL_ResolveShaderParam (quake_text.program, &quake_text.vertex);
	GL_ResolveShaderParam (quake_text.program, &quake_text.dchar);

	vert = GL_CompileShader ("quakeico.vert", quakeicon_vert, GL_VERTEX_SHADER);
	quake_icon.program = GL_LinkProgram ("quakeico", vert, frag);
	GL_ResolveShaderParam (quake_icon.program, &quake_icon.icon);
	GL_ResolveShaderParam (quake_icon.program, &quake_icon.palette);
	GL_ResolveShaderParam (quake_icon.program, &quake_icon.matrix);
	GL_ResolveShaderParam (quake_icon.program, &quake_icon.vertex);

	draw_chars = W_GetLumpName ("conchars");
	for (i = 0; i < 256 * 64; i++)
		if (draw_chars[i] == 0)
			draw_chars[i] = 255;		// proper transparent color

	char_texture = GL_LoadQuakeTexture ("conchars", 128, 128, draw_chars);

	pic = (qpic_t *) QFS_LoadFile ("gfx/conback.lmp", 0);
	if (pic) {
		SwapPic (pic);
		conback_texture = GL_LoadQuakeTexture ("conback",
											   pic->width, pic->height,
											   pic->data);
		free (pic);
	}

	pic = Draw_CrosshairPic ();
	crosshair_pic = make_glpic ("crosshair", pic);
	free (pic);
}

static inline void
queue_character (int x, int y, byte chr)
{
	unsigned short *v;
	unsigned    i, c;
	const int   size = 5 * 2 * 6;

	char_queue->size += size;
	dstring_adjust (char_queue);
	v = (unsigned short *) (char_queue->str + char_queue->size - size);
	c = 0x738;
	for (i = 0; i < 6; i++, c >>= 2) {
		*v++ = x;
		*v++ = y;
		*v++ = c & 1;
		*v++ = (c >> 1) & 1;
		*v++ = chr;
	}
}

static void
flush_text (void)
{
	qfglUseProgram (quake_text.program);
	qfglEnableVertexAttribArray (quake_text.vertex.location);
	qfglEnableVertexAttribArray (quake_text.dchar.location);

	qfglUniformMatrix4fv (quake_text.matrix.location, 1, false, proj_matrix);

	qfglUniform1i (quake_text.charmap.location, 0);
	qfglActiveTexture (GL_TEXTURE0 + 0);
	qfglEnable (GL_TEXTURE_2D);
	qfglBindTexture (GL_TEXTURE_2D, char_texture);

	qfglUniform1i (quake_text.palette.location, 1);
	qfglActiveTexture (GL_TEXTURE0 + 1);
	qfglEnable (GL_TEXTURE_2D);
	qfglBindTexture (GL_TEXTURE_2D, glsl_palette);

	qfglVertexAttribPointer (quake_text.vertex.location, 4, GL_UNSIGNED_SHORT,
							 0, 10, char_queue->str);
	qfglVertexAttribPointer (quake_text.dchar.location, 1, GL_UNSIGNED_SHORT,
							 0, 10, char_queue->str + 8);

	qfglDrawArrays (GL_TRIANGLES, 0, char_queue->size / 10);

	qfglDisableVertexAttribArray (quake_text.dchar.location);
	qfglDisableVertexAttribArray (quake_text.vertex.location);
	char_queue->size = 0;
}

VISIBLE void
Draw_Character (int x, int y, unsigned int chr)
{
	chr &= 255;

	if (chr == 32)
		return;							// space
	if (y <= -8)
		return;							// totally off screen

	queue_character (x, y, chr);
}

VISIBLE void
Draw_String (int x, int y, const char *str)
{
	byte        chr;

	if (!str || !str[0])
		return;							// totally off screen
	if (y <= -8)
		return;							// totally off screen

	while (*str) {
		if ((chr = *str++) != 32)
			queue_character (x, y, chr);
		x += 8;
	}
}

VISIBLE void
Draw_nString (int x, int y, const char *str, int count)
{
	byte        chr;

	if (!str || !str[0])
		return;							// totally off screen
	if (y <= -8)
		return;							// totally off screen

	while (count-- && *str) {
		if ((chr = *str++) != 32)
			queue_character (x, y, chr);
		x += 8;
	}
}

void
Draw_AltString (int x, int y, const char *str)
{
	byte        chr;

	if (!str || !str[0])
		return;
	if (y <= -8)
		return;							// totally off screen

	while (*str) {
		if ((chr = *str++ | 0x80) != (0x80 | 32)) {		// Don't render spaces
			queue_character (x, y, chr);
		}
		x += 8;
	}
}

static void
crosshair_1 (int x, int y)
{
	Draw_Character (x - 4, y - 4, '+');
}

//FIXME these should use an index to select the region.
static void
crosshair_2 (int x, int y)
{
	draw_pic (x, y, crosshair_pic,
			  0, 0, CROSSHAIR_WIDTH, CROSSHAIR_HEIGHT);
}

static void
crosshair_3 (int x, int y)
{
	draw_pic (x, y, crosshair_pic,
			  CROSSHAIR_WIDTH, 0,
			  CROSSHAIR_WIDTH, CROSSHAIR_HEIGHT);
}

static void
crosshair_4 (int x, int y)
{
	draw_pic (x, y, crosshair_pic,
			  0, CROSSHAIR_HEIGHT,
			  CROSSHAIR_WIDTH, CROSSHAIR_HEIGHT);
}

static void
crosshair_5 (int x, int y)
{
	draw_pic (x, y, crosshair_pic,
			  CROSSHAIR_WIDTH, CROSSHAIR_HEIGHT,
			  CROSSHAIR_WIDTH, CROSSHAIR_HEIGHT);
}

static void (*crosshair_func[]) (int x, int y) = {
	crosshair_1,
	crosshair_2,
	crosshair_3,
	crosshair_4,
	crosshair_5,
};

VISIBLE void
Draw_Crosshair (void)
{
	int         x, y;
	size_t      ch;

	ch = crosshair->int_val - 1;
	if (ch >= sizeof (crosshair_func) / sizeof (crosshair_func[0]))
		return;

	x = vid.conwidth / 2 + cl_crossx->int_val;
	y = vid.conheight / 2 + cl_crossy->int_val;

	crosshair_func[ch] (x, y);
}

void
Draw_CrosshairAt (int ch, int x, int y)
{
	unsigned    c = ch - 1;

	if (c >= sizeof (crosshair_func) / sizeof (crosshair_func[0]))
		return;

	crosshair_func[c] (x, y);
}

VISIBLE void
Draw_Pic (int x, int y, qpic_t *pic)
{
	draw_pic (x, y, pic, 0, 0, pic->width, pic->height);
}

VISIBLE void
Draw_SubPic (int x, int y, qpic_t *pic, int srcx, int srcy, int width,
			 int height)
{
	draw_pic (x, y, pic, srcx, srcy, width, height);
}

VISIBLE void
Draw_ConsoleBackground (int lines, byte alpha)
{
	float       ofs = (vid.conheight - lines) / (float) vid.conheight;
	float       verts[][4] = {
		{           0,     0, 0, ofs},
		{vid.conwidth,     0, 1, ofs},
		{vid.conwidth, lines, 1,   1},
		{           0,     0, 0, ofs},
		{vid.conwidth, lines, 1,   1},
		{           0, lines, 0,   1},
	};

	qfglUseProgram (quake_icon.program);
	qfglEnableVertexAttribArray (quake_icon.vertex.location);

	qfglUniformMatrix4fv (quake_icon.matrix.location, 1, false, proj_matrix);

	qfglUniform1i (quake_icon.icon.location, 0);
	qfglActiveTexture (GL_TEXTURE0 + 0);
	qfglEnable (GL_TEXTURE_2D);
	qfglBindTexture (GL_TEXTURE_2D, conback_texture);

	qfglUniform1i (quake_icon.palette.location, 1);
	qfglActiveTexture (GL_TEXTURE0 + 1);
	qfglEnable (GL_TEXTURE_2D);
	qfglBindTexture (GL_TEXTURE_2D, glsl_palette);

	qfglVertexAttribPointer (quake_icon.vertex.location, 4, GL_FLOAT,
							 0, 0, verts);

	qfglDrawArrays (GL_TRIANGLES, 0, 6);

	qfglDisableVertexAttribArray (quake_icon.vertex.location);
	char_queue->size = 0;
}

VISIBLE void
Draw_TileClear (int x, int y, int w, int h)
{
}

VISIBLE void
Draw_Fill (int x, int y, int w, int h, int c)
{
}

VISIBLE void
Draw_FadeScreen (void)
{
}

static void
ortho_mat (float *proj, float xmin, float xmax, float ymin, float ymax,
		   float znear, float zfar)
{
	proj[0] = 2/(xmax-xmin);
	proj[4] = 0;
	proj[8] = 0;
	proj[12] = -(xmax+xmin)/(xmax-xmin);

	proj[1] = 0;
	proj[5] = 2/(ymax-ymin);
	proj[9] = 0;
	proj[13] = -(ymax+ymin)/(ymax-ymin);

	proj[2] = 0;
	proj[6] = 0;
	proj[10] = -2/(zfar-znear);
	proj[14] = -(zfar+znear)/(zfar-znear);

	proj[3] = 0;
	proj[7] = 0;
	proj[11] = 0;
	proj[15] = 1;
}

static void
set_2d (int width, int height)
{
	qfglViewport (0, 0, vid.width, vid.height);

	qfglDisable (GL_DEPTH_TEST);
	qfglDisable (GL_CULL_FACE);

	ortho_mat (proj_matrix, 0, width, height, 0, -99999, 99999);
}

void
GL_Set2D (void)
{
    set_2d (vid.width, vid.height);
}

void
GL_Set2DScaled (void)
{
    set_2d (vid.conwidth, vid.conheight);
}

void
GL_DrawReset (void)
{
	char_queue->size = 0;
}

void
GL_FlushText (void)
{
	if (char_queue->size)
		flush_text ();
}
