/*
	sw32_ralias.c

	routines for setting up to draw alias models

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

#define NH_DEFINE
#include "namehack.h"

#include "QF/entity.h"
#include "QF/image.h"
#include "QF/render.h"
#include "QF/skin.h"
#include "QF/sys.h"

#include "d_ifacea.h"
#include "r_internal.h"

#include "stdlib.h"

#define LIGHT_MIN	5					// lowest light value we'll allow, to
										// avoid the need for inner-loop light
										// clamping

affinetridesc_t sw32_r_affinetridesc;

void       *sw32_acolormap;					// FIXME: should go away

trivertx_t *sw32_r_apverts;

// TODO: these probably will go away with optimized rasterization
static mdl_t      *pmdl;
vec3_t      sw32_r_plightvec;
int         sw32_r_ambientlight;
float       sw32_r_shadelight;
static aliashdr_t *paliashdr;
finalvert_t *sw32_pfinalverts;
auxvert_t  *sw32_pauxverts;
float sw32_ziscale;
static model_t *pmodel;

static vec3_t alias_forward, alias_right, alias_up;

static maliasskindesc_t *pskindesc;

int         sw32_r_amodels_drawn;
static int         a_skinwidth;
static int         r_anumverts;

float       sw32_aliastransform[3][4];

typedef struct {
	int         index0;
	int         index1;
} aedge_t;

static aedge_t aedges[12] = {
	{0, 1}, {1, 2}, {2, 3}, {3, 0},
	{4, 5}, {5, 6}, {6, 7}, {7, 4},
	{0, 5}, {1, 4}, {2, 7}, {3, 6}
};

qboolean
sw32_R_AliasCheckBBox (void)
{
	int         i, flags, frame, numv;
	aliashdr_t *pahdr;
	float       zi, basepts[8][3], v0, v1, frac;
	finalvert_t *pv0, *pv1, viewpts[16];
	auxvert_t  *pa0, *pa1, viewaux[16];
	maliasframedesc_t *pframedesc;
	qboolean    zclipped, zfullyclipped;
	unsigned int anyclip, allclip;
	int         minz;

	// expand, rotate, and translate points into worldspace
	currententity->visibility.trivial_accept = 0;
	pmodel = currententity->renderer.model;
	if (!(pahdr = pmodel->aliashdr))
		pahdr = Cache_Get (&pmodel->cache);
	pmdl = (mdl_t *) ((byte *) pahdr + pahdr->model);

	sw32_R_AliasSetUpTransform (0);

	// construct the base bounding box for this frame
	frame = currententity->animation.frame;
// TODO: don't repeat this check when drawing?
	if ((frame >= pmdl->numframes) || (frame < 0)) {
		Sys_MaskPrintf (SYS_dev, "No such frame %d %s\n", frame, pmodel->path);
		frame = 0;
	}

	pframedesc = &pahdr->frames[frame];

	// x worldspace coordinates
	basepts[0][0] = basepts[1][0] = basepts[2][0] = basepts[3][0] =
		(float) pframedesc->bboxmin.v[0];
	basepts[4][0] = basepts[5][0] = basepts[6][0] = basepts[7][0] =
		(float) pframedesc->bboxmax.v[0];

	// y worldspace coordinates
	basepts[0][1] = basepts[3][1] = basepts[5][1] = basepts[6][1] =
		(float) pframedesc->bboxmin.v[1];
	basepts[1][1] = basepts[2][1] = basepts[4][1] = basepts[7][1] =
		(float) pframedesc->bboxmax.v[1];

	// z worldspace coordinates
	basepts[0][2] = basepts[1][2] = basepts[4][2] = basepts[5][2] =
		(float) pframedesc->bboxmin.v[2];
	basepts[2][2] = basepts[3][2] = basepts[6][2] = basepts[7][2] =
		(float) pframedesc->bboxmax.v[2];

	zclipped = false;
	zfullyclipped = true;

	minz = 9999;
	for (i = 0; i < 8; i++) {
		sw32_R_AliasTransformVector (&basepts[i][0], &viewaux[i].fv[0]);

		if (viewaux[i].fv[2] < ALIAS_Z_CLIP_PLANE) {
			// we must clip points that are closer than the near clip plane
			viewpts[i].flags = ALIAS_Z_CLIP;
			zclipped = true;
		} else {
			if (viewaux[i].fv[2] < minz)
				minz = viewaux[i].fv[2];
			viewpts[i].flags = 0;
			zfullyclipped = false;
		}
	}

	if (zfullyclipped) {
		if (!pmodel->aliashdr)
			Cache_Release (&pmodel->cache);
		return false;					// everything was near-z-clipped
	}

	numv = 8;

	if (zclipped) {
		// organize points by edges, use edges to get new points (possible
		// trivial reject)
		for (i = 0; i < 12; i++) {
			// edge endpoints
			pv0 = &viewpts[aedges[i].index0];
			pv1 = &viewpts[aedges[i].index1];
			pa0 = &viewaux[aedges[i].index0];
			pa1 = &viewaux[aedges[i].index1];

			// if one end is clipped and the other isn't, make a new point
			if (pv0->flags ^ pv1->flags) {
				frac = (ALIAS_Z_CLIP_PLANE - pa0->fv[2]) /
					(pa1->fv[2] - pa0->fv[2]);
				viewaux[numv].fv[0] = pa0->fv[0] +
					(pa1->fv[0] - pa0->fv[0]) * frac;
				viewaux[numv].fv[1] = pa0->fv[1] +
					(pa1->fv[1] - pa0->fv[1]) * frac;
				viewaux[numv].fv[2] = ALIAS_Z_CLIP_PLANE;
				viewpts[numv].flags = 0;
				numv++;
			}
		}
	}
	// project the vertices that remain after clipping
	anyclip = 0;
	allclip = ALIAS_XY_CLIP_MASK;

// TODO: probably should do this loop in ASM, especially if we use floats
	for (i = 0; i < numv; i++) {
		// we don't need to bother with vertices that were z-clipped
		if (viewpts[i].flags & ALIAS_Z_CLIP)
			continue;

		zi = 1.0 / viewaux[i].fv[2];

		// FIXME: do with chop mode in ASM, or convert to float
		v0 = (viewaux[i].fv[0] * sw32_xscale * zi) + sw32_xcenter;
		v1 = (viewaux[i].fv[1] * sw32_yscale * zi) + sw32_ycenter;

		flags = 0;

		if (v0 < r_refdef.fvrectx)
			flags |= ALIAS_LEFT_CLIP;
		if (v1 < r_refdef.fvrecty)
			flags |= ALIAS_TOP_CLIP;
		if (v0 > r_refdef.fvrectright)
			flags |= ALIAS_RIGHT_CLIP;
		if (v1 > r_refdef.fvrectbottom)
			flags |= ALIAS_BOTTOM_CLIP;

		anyclip |= flags;
		allclip &= flags;
	}

	if (allclip) {
		if (!pmodel->aliashdr)
			Cache_Release (&pmodel->cache);
		return false;					// trivial reject off one side
	}

	currententity->visibility.trivial_accept = !anyclip & !zclipped;

	if (currententity->visibility.trivial_accept) {
		if (minz > (sw32_r_aliastransition + (pmdl->size * sw32_r_resfudge))) {
			currententity->visibility.trivial_accept |= 2;
		}
	}

	if (!pmodel->aliashdr)
		Cache_Release (&pmodel->cache);
	return true;
}


void
sw32_R_AliasTransformVector (vec3_t in, vec3_t out)
{
	out[0] = DotProduct (in, sw32_aliastransform[0])
		+ sw32_aliastransform[0][3];
	out[1] = DotProduct (in, sw32_aliastransform[1])
		+ sw32_aliastransform[1][3];
	out[2] = DotProduct (in, sw32_aliastransform[2])
		+ sw32_aliastransform[2][3];
}


void
sw32_R_AliasClipAndProjectFinalVert (finalvert_t *fv, auxvert_t *av)
{
	if (av->fv[2] < ALIAS_Z_CLIP_PLANE) {
		fv->flags |= ALIAS_Z_CLIP;
		return;
	}

	sw32_R_AliasProjectFinalVert (fv, av);

	if (fv->v[0] < r_refdef.aliasvrect.x)
		fv->flags |= ALIAS_LEFT_CLIP;
	if (fv->v[1] < r_refdef.aliasvrect.y)
		fv->flags |= ALIAS_TOP_CLIP;
	if (fv->v[0] > r_refdef.aliasvrectright)
		fv->flags |= ALIAS_RIGHT_CLIP;
	if (fv->v[1] > r_refdef.aliasvrectbottom)
		fv->flags |= ALIAS_BOTTOM_CLIP;
}

static void
R_AliasTransformFinalVert16 (auxvert_t *av, trivertx_t *pverts)
{
	trivertx_t  * pextra;
	float       vextra[3];

	pextra = pverts + pmdl->numverts;
	vextra[0] = pverts->v[0] + pextra->v[0] / (float)256;
	vextra[1] = pverts->v[1] + pextra->v[1] / (float)256;
	vextra[2] = pverts->v[2] + pextra->v[2] / (float)256;
	av->fv[0] = DotProduct (vextra, sw32_aliastransform[0]) +
		sw32_aliastransform[0][3];
	av->fv[1] = DotProduct (vextra, sw32_aliastransform[1]) +
		sw32_aliastransform[1][3];
	av->fv[2] = DotProduct (vextra, sw32_aliastransform[2]) +
		sw32_aliastransform[2][3];
}

static void
R_AliasTransformFinalVert8 (auxvert_t *av, trivertx_t *pverts)
{
	av->fv[0] = DotProduct (pverts->v, sw32_aliastransform[0]) +
		sw32_aliastransform[0][3];
	av->fv[1] = DotProduct (pverts->v, sw32_aliastransform[1]) +
		sw32_aliastransform[1][3];
	av->fv[2] = DotProduct (pverts->v, sw32_aliastransform[2]) +
		sw32_aliastransform[2][3];
}

/*
	R_AliasPreparePoints

	General clipped case
*/
static void
R_AliasPreparePoints (void)
{
	int         i;
	stvert_t   *pstverts;
	finalvert_t *fv;
	auxvert_t  *av;
	mtriangle_t *ptri;
	finalvert_t *pfv[3];

	pstverts = (stvert_t *) ((byte *) paliashdr + paliashdr->stverts);
	r_anumverts = pmdl->numverts;
	fv = pfinalverts;
	av = sw32_pauxverts;

	if (pmdl->ident == HEADER_MDL16) {
		for (i = 0; i < r_anumverts; i++, fv++, av++, sw32_r_apverts++,
				pstverts++) {
			R_AliasTransformFinalVert16 (av, sw32_r_apverts);
			sw32_R_AliasTransformFinalVert (fv, sw32_r_apverts, pstverts);
			R_AliasClipAndProjectFinalVert (fv, av);
		}
	}
	else {
		for (i = 0; i < r_anumverts; i++, fv++, av++, sw32_r_apverts++,
				pstverts++) {
			R_AliasTransformFinalVert8 (av, sw32_r_apverts);
			sw32_R_AliasTransformFinalVert (fv, sw32_r_apverts, pstverts);
			R_AliasClipAndProjectFinalVert (fv, av);
		}
	}

	// clip and draw all triangles
	sw32_r_affinetridesc.numtriangles = 1;

	ptri = (mtriangle_t *) ((byte *) paliashdr + paliashdr->triangles);
	for (i = 0; i < pmdl->numtris; i++, ptri++) {
		pfv[0] = &pfinalverts[ptri->vertindex[0]];
		pfv[1] = &pfinalverts[ptri->vertindex[1]];
		pfv[2] = &pfinalverts[ptri->vertindex[2]];

		if (pfv[0]->flags & pfv[1]->flags & pfv[2]->
			flags & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP))
			continue;					// completely clipped

		if (!((pfv[0]->flags | pfv[1]->flags | pfv[2]->flags) &
			  (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP))) {	// totally unclipped
			sw32_r_affinetridesc.pfinalverts = pfinalverts;
			sw32_r_affinetridesc.ptriangles = ptri;
			sw32_D_PolysetDraw ();
		} else {						// partially clipped
			sw32_R_AliasClipTriangle (ptri);
		}
	}
}


void
sw32_R_AliasSetUpTransform (int trivial_accept)
{
	int         i;
	float       rotationmatrix[3][4], t2matrix[3][4];
	static float tmatrix[3][4];
	static float viewmatrix[3][4];

	mat4f_t     mat;
	Transform_GetWorldMatrix (currententity->transform, mat);
	VectorCopy (mat[0], alias_forward);
	VectorNegate (mat[1], alias_right);
	VectorCopy (mat[2], alias_up);

	tmatrix[0][0] = pmdl->scale[0];
	tmatrix[1][1] = pmdl->scale[1];
	tmatrix[2][2] = pmdl->scale[2];

	tmatrix[0][3] = pmdl->scale_origin[0];
	tmatrix[1][3] = pmdl->scale_origin[1];
	tmatrix[2][3] = pmdl->scale_origin[2];

// TODO: can do this with simple matrix rearrangement

	for (i = 0; i < 3; i++) {
		t2matrix[i][0] = alias_forward[i];
		t2matrix[i][1] = -alias_right[i];
		t2matrix[i][2] = alias_up[i];
	}

	t2matrix[0][3] = -modelorg[0];
	t2matrix[1][3] = -modelorg[1];
	t2matrix[2][3] = -modelorg[2];

// FIXME: can do more efficiently than full concatenation
	R_ConcatTransforms (t2matrix, tmatrix, rotationmatrix);

// TODO: should be global, set when vright, etc., set
	VectorCopy (vright, viewmatrix[0]);
	VectorCopy (vup, viewmatrix[1]);
	VectorNegate (viewmatrix[1], viewmatrix[1]);
	VectorCopy (vpn, viewmatrix[2]);

//	viewmatrix[0][3] = 0;
//	viewmatrix[1][3] = 0;
//	viewmatrix[2][3] = 0;

	R_ConcatTransforms (viewmatrix, rotationmatrix, sw32_aliastransform);

// do the scaling up of x and y to screen coordinates as part of the transform
// for the unclipped case (it would mess up clipping in the clipped case).
// Also scale down z, so 1/z is scaled 31 bits for free, and scale down x and y
// correspondingly so the projected x and y come out right
// FIXME: make this work for clipped case too?
	if (trivial_accept) {
		for (i = 0; i < 4; i++) {
			sw32_aliastransform[0][i] *= sw32_aliasxscale *
				(1.0 / ((float) 0x8000 * 0x10000));
			sw32_aliastransform[1][i] *= sw32_aliasyscale *
				(1.0 / ((float) 0x8000 * 0x10000));
			sw32_aliastransform[2][i] *= 1.0 / ((float) 0x8000 * 0x10000);
		}
	}
}

/*
sw32_R_AliasTransformFinalVert

now this function just copies the texture coordinates and calculates lighting
actual 3D transform is done by R_AliasTransformFinalVert8/16 functions above
*/
void
sw32_R_AliasTransformFinalVert (finalvert_t *fv, trivertx_t *pverts,
								stvert_t *pstverts)
{
	int         temp;
	float       lightcos, *plightnormal;

	fv->v[2] = pstverts->s;
	fv->v[3] = pstverts->t;

	fv->flags = pstverts->onseam;

	// lighting
	// LordHavoc: flipped lightcos so it is + for bright, not -
	plightnormal = r_avertexnormals[pverts->lightnormalindex];
	lightcos = -DotProduct (plightnormal, sw32_r_plightvec);
	temp = sw32_r_ambientlight;

	if (lightcos > 0) {
		temp += (int) (sw32_r_shadelight * lightcos);

		// clamp; because we limited the minimum ambient and shading light,
		// we don't have to clamp low light, just bright
		if (temp < 0)
			temp = 0;
	}

	fv->v[4] = temp;
}

void
sw32_R_AliasTransformAndProjectFinalVerts (finalvert_t *fv, stvert_t *pstverts)
{
	int         i, temp;
	float       lightcos, *plightnormal, zi;
	trivertx_t *pverts;

	pverts = sw32_r_apverts;

	for (i = 0; i < r_anumverts; i++, fv++, pverts++, pstverts++) {
		// transform and project
		zi = 1.0 / (DotProduct (pverts->v, sw32_aliastransform[2]) +
					sw32_aliastransform[2][3]);

		// x, y, and z are scaled down by 1/2**31 in the transform, so 1/z is
		// scaled up by 1/2**31, and the scaling cancels out for x and y in
		// the projection
		fv->v[5] = zi;

		fv->v[0] = ((DotProduct (pverts->v, sw32_aliastransform[0]) +
					 sw32_aliastransform[0][3]) * zi) + sw32_aliasxcenter;
		fv->v[1] = ((DotProduct (pverts->v, sw32_aliastransform[1]) +
					 sw32_aliastransform[1][3]) * zi) + sw32_aliasycenter;

		fv->v[2] = pstverts->s;
		fv->v[3] = pstverts->t;
		fv->flags = pstverts->onseam;

		// lighting
		// LordHavoc: flipped lightcos so it is + for bright, not -
		plightnormal = r_avertexnormals[pverts->lightnormalindex];
		lightcos = -DotProduct (plightnormal, sw32_r_plightvec);
		temp = sw32_r_ambientlight;

		if (lightcos > 0) {
			temp += (int) (sw32_r_shadelight * lightcos);

			// clamp; because we limited the minimum ambient and shading
			// light, we don't have to clamp low light, just bright
			if (temp < 0)
				temp = 0;
		}

		fv->v[4] = temp;
	}
}

void
sw32_R_AliasProjectFinalVert (finalvert_t *fv, auxvert_t *av)
{
	float       zi;

	// project points
	zi = 1.0 / av->fv[2];

	fv->v[5] = zi * sw32_ziscale;

	fv->v[0] = (av->fv[0] * sw32_aliasxscale * zi) + sw32_aliasxcenter;
	fv->v[1] = (av->fv[1] * sw32_aliasyscale * zi) + sw32_aliasycenter;
}


static void
R_AliasPrepareUnclippedPoints (void)
{
	stvert_t   *pstverts;

	pstverts = (stvert_t *) ((byte *) paliashdr + paliashdr->stverts);
	r_anumverts = pmdl->numverts;

	sw32_R_AliasTransformAndProjectFinalVerts (pfinalverts, pstverts);

	sw32_r_affinetridesc.pfinalverts = pfinalverts;
	sw32_r_affinetridesc.ptriangles = (mtriangle_t *)
		((byte *) paliashdr + paliashdr->triangles);
	sw32_r_affinetridesc.numtriangles = pmdl->numtris;

	sw32_D_PolysetDraw ();
}


static void
R_AliasSetupSkin (void)
{
	int         skinnum;

	skinnum = currententity->renderer.skinnum;
	if ((skinnum >= pmdl->numskins) || (skinnum < 0)) {
		Sys_MaskPrintf (SYS_dev, "R_AliasSetupSkin: no such skin # %d\n",
						skinnum);
		skinnum = 0;
	}

	pskindesc = R_AliasGetSkindesc (skinnum, paliashdr);
	a_skinwidth = pmdl->skinwidth;

	sw32_r_affinetridesc.pskin = (void *) ((byte *) paliashdr + pskindesc->skin);
	sw32_r_affinetridesc.skinwidth = a_skinwidth;
	sw32_r_affinetridesc.seamfixupX16 = (a_skinwidth >> 1) << 16;
	sw32_r_affinetridesc.skinheight = pmdl->skinheight;

	sw32_acolormap = vid.colormap8;
	if (currententity->renderer.skin) {
		tex_t      *base;

		base = currententity->renderer.skin->texels;
		if (base) {
			sw32_r_affinetridesc.pskin = base->data;
			sw32_r_affinetridesc.skinwidth = base->width;
			sw32_r_affinetridesc.skinheight = base->height;
		}
		sw32_acolormap = currententity->renderer.skin->colormap;
	}
}

static void
R_AliasSetupLighting (alight_t *plighting)
{
	// guarantee that no vertex will ever be lit below LIGHT_MIN, so we don't
	// have to clamp off the bottom
	sw32_r_ambientlight = plighting->ambientlight;

	if (sw32_r_ambientlight < LIGHT_MIN)
		sw32_r_ambientlight = LIGHT_MIN;

	sw32_r_ambientlight = (/*255 -*/ sw32_r_ambientlight) << VID_CBITS;

//	if (sw32_r_ambientlight < LIGHT_MIN)
//		sw32_r_ambientlight = LIGHT_MIN;

	sw32_r_shadelight = plighting->shadelight;

	if (sw32_r_shadelight < 0)
		sw32_r_shadelight = 0;

	sw32_r_shadelight *= VID_GRADES;

	// rotate the lighting vector into the model's frame of reference
	sw32_r_plightvec[0] = DotProduct (plighting->plightvec, alias_forward);
	sw32_r_plightvec[1] = -DotProduct (plighting->plightvec, alias_right);
	sw32_r_plightvec[2] = DotProduct (plighting->plightvec, alias_up);
}


/*
	R_AliasSetupFrame

	set sw32_r_apverts
*/
static void
R_AliasSetupFrame (void)
{
	maliasframedesc_t *frame;

	frame = R_AliasGetFramedesc (currententity->animation.frame, paliashdr);
	sw32_r_apverts = (trivertx_t *) ((byte *) paliashdr + frame->frame);
}


void
sw32_R_AliasDrawModel (alight_t *plighting)
{
	int         size;
	finalvert_t *finalverts;

	sw32_r_amodels_drawn++;

	if (!(paliashdr = currententity->renderer.model->aliashdr))
		paliashdr = Cache_Get (&currententity->renderer.model->cache);
	pmdl = (mdl_t *) ((byte *) paliashdr + paliashdr->model);

	size = (CACHE_SIZE - 1)
		   + sizeof (finalvert_t) * (pmdl->numverts + 1)
		   + sizeof (auxvert_t) * pmdl->numverts;
	finalverts = (finalvert_t *) Hunk_TempAlloc (size);
	if (!finalverts)
		Sys_Error ("R_AliasDrawModel: out of memory");

	// cache align
	pfinalverts = (finalvert_t *)
		(((intptr_t) &finalverts[0] + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
	sw32_pauxverts = (auxvert_t *) &pfinalverts[pmdl->numverts + 1];

	R_AliasSetupSkin ();
	sw32_R_AliasSetUpTransform (currententity->visibility.trivial_accept);
	R_AliasSetupLighting (plighting);
	R_AliasSetupFrame ();

	if (!sw32_acolormap)
		sw32_acolormap = vid.colormap8;
	if (sw32_acolormap == &vid.colormap8 && sw32_r_pixbytes != 1)
	{
		if (sw32_r_pixbytes == 2)
			sw32_acolormap = vid.colormap16;
		else if (sw32_r_pixbytes == 4)
			sw32_acolormap = vid.colormap32;
		else
			Sys_Error("R_AliasDrawmodel: unsupported r_pixbytes %i",
					  sw32_r_pixbytes);
	}

	if (currententity != vr_data.view_model)
		sw32_ziscale = (float) 0x8000 *(float) 0x10000;
	else
		sw32_ziscale = (float) 0x8000 *(float) 0x10000 *3.0;

	if (currententity->visibility.trivial_accept) {
		R_AliasPrepareUnclippedPoints ();
	} else {
		R_AliasPreparePoints ();
	}

	if (!currententity->renderer.model->aliashdr) {
		Cache_Release (&currententity->renderer.model->cache);
	}
}
