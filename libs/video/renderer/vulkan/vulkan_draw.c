/*
	vulkan_draw.c

	2D drawing support for Vulkan

	Copyright (C) 2021 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2021/1/10

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

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "QF/cvar.h"
#include "QF/draw.h"
#include "QF/dstring.h"
#include "QF/hash.h"
#include "QF/quakefs.h"
#include "QF/render.h"
#include "QF/sys.h"
#include "QF/vid.h"

#include "compat.h"
#include "QF/Vulkan/qf_draw.h"
#include "QF/Vulkan/qf_vid.h"
#include "QF/Vulkan/barrier.h"
#include "QF/Vulkan/buffer.h"
#include "QF/Vulkan/command.h"
#include "QF/Vulkan/descriptor.h"
#include "QF/Vulkan/device.h"
#include "QF/Vulkan/image.h"
#include "QF/Vulkan/staging.h"
#include "QF/Vulkan/texture.h"

#include "r_internal.h"
#include "vid_vulkan.h"
#include "vkparse.h"

typedef struct {
	float       xy[2];
	float       st[2];
	float       color[4];
} drawvert_t;

#define MAX_QUADS (65536)
#define VERTS_PER_QUAD (4)
#define INDS_PER_QUAD (5)	// one per vert plus primitive reset

//FIXME move into a context struct
VkSampler       conchars_sampler;
scrap_t        *draw_scrap;
qfv_stagebuf_t *draw_stage;
qpic_t         *conchars;

VkBuffer        quad_vert_buffer;
VkBuffer        quad_ind_buffer;
VkDeviceMemory  quad_memory;
drawvert_t     *quad_verts;
uint32_t       *quad_inds;
uint32_t        num_quads;

VkPipeline		twod_pipeline;
VkPipelineLayout twod_layout;
size_t          draw_cmdBuffer;

static void
create_quad_buffers (vulkan_ctx_t *ctx)
{
	qfv_device_t *device = ctx->device;
	qfv_devfuncs_t *dfunc = device->funcs;

	//FIXME quad_inds can be a completely separate buffer that is
	//pre-initialized to draw 2-tri triangle strips as the actual indices will
	//never change
	size_t      vert_size = MAX_QUADS * VERTS_PER_QUAD * sizeof (drawvert_t);
	size_t      ind_size = MAX_QUADS * INDS_PER_QUAD * sizeof (uint32_t);

	quad_vert_buffer = QFV_CreateBuffer (device, vert_size,
										 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	quad_ind_buffer = QFV_CreateBuffer (device, ind_size,
										VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	quad_memory = QFV_AllocBufferMemory (device, quad_vert_buffer,
										 VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
										 vert_size + ind_size, 0);
	QFV_BindBufferMemory (device, quad_vert_buffer, quad_memory, 0);
	QFV_BindBufferMemory (device, quad_ind_buffer, quad_memory, vert_size);
	void       *data;
	dfunc->vkMapMemory (device->dev, quad_memory, 0, vert_size + ind_size,
						0, &data);
	quad_verts = data;
	quad_inds = (uint32_t *) (quad_verts + MAX_QUADS * VERTS_PER_QUAD);
	// pre-initialize quad_inds as the indices will never change
	uint32_t   *ind = quad_inds;
	for (int i = 0; i < MAX_QUADS; i++) {
		for (int j = 0; j < VERTS_PER_QUAD; j++) {
			*ind++ = i * VERTS_PER_QUAD + j;
		}
		// mark end of primitive
		*ind++ = -1;
	}
	VkMappedMemoryRange range = {
		VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, 0,
		quad_memory, vert_size, ind_size,
	};
	dfunc->vkFlushMappedMemoryRanges (device->dev, 1, &range);
}

static void
destroy_quad_buffers (vulkan_ctx_t *ctx)
{
	qfv_device_t *device = ctx->device;
	qfv_devfuncs_t *dfunc = device->funcs;

	dfunc->vkUnmapMemory (device->dev, quad_memory);
	dfunc->vkFreeMemory (device->dev, quad_memory, 0);
	dfunc->vkDestroyBuffer (device->dev, quad_vert_buffer, 0);
	dfunc->vkDestroyBuffer (device->dev, quad_ind_buffer, 0);
}

static void
flush_draw_scrap (vulkan_ctx_t *ctx)
{
	qfv_device_t *device = ctx->device;
	qfv_devfuncs_t *dfunc = device->funcs;
	VkCommandBuffer cmd = ctx->cmdbuffer;

	dfunc->vkWaitForFences (device->dev, 1, &ctx->fence, VK_TRUE, ~0ull);
	dfunc->vkResetCommandBuffer (cmd, 0);
	VkCommandBufferBeginInfo beginInfo = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 0,
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, 0,
	};
	dfunc->vkBeginCommandBuffer (cmd, &beginInfo);
	QFV_ScrapFlush (draw_scrap, draw_stage, cmd);
	dfunc->vkEndCommandBuffer (cmd);

	VkSubmitInfo submitInfo = {
		VK_STRUCTURE_TYPE_SUBMIT_INFO, 0,
		0, 0, 0,
		1, &cmd,
		0, 0,
	};
	dfunc->vkResetFences (device->dev, 1, &ctx->fence);
	dfunc->vkQueueSubmit (device->queue.queue, 1, &submitInfo, ctx->fence);
	dfunc->vkWaitForFences (device->dev, 1, &ctx->fence, VK_TRUE, ~0ull);//FIXME
}

static qpic_t *
pic_data (const char *name, int w, int h, const byte *data, vulkan_ctx_t *ctx)
{
	qpic_t     *pic;
	subpic_t   *subpic;
	byte       *picdata;

	pic = malloc (field_offset (qpic_t, data[sizeof (subpic_t)]));
	pic->width = w;
	pic->height = h;

	subpic = QFV_ScrapSubpic (draw_scrap, w, h);
	*(subpic_t **) pic->data = subpic;

	picdata = QFV_SubpicBatch (subpic, draw_stage);
	size_t size = w * h;
	for (size_t i = 0; i < size; i++) {
		byte        pix = *data++;
		byte       *col = vid.palette + pix * 3;
		*picdata++ = *col++;
		*picdata++ = *col++;
		*picdata++ = *col++;
		*picdata++ = (pix == 255) - 1;
	}
	return pic;
}

qpic_t *
Vulkan_Draw_MakePic (int width, int height, const byte *data,
					 vulkan_ctx_t *ctx)
{
	return pic_data (0, width, height, data, ctx);
}

void
Vulkan_Draw_DestroyPic (qpic_t *pic, vulkan_ctx_t *ctx)
{
}

qpic_t *
Vulkan_Draw_PicFromWad (const char *name, vulkan_ctx_t *ctx)
{
	qpic_t     *wadpic = W_GetLumpName (name);

	if (!wadpic) {
		return 0;
	}
	return pic_data (name, wadpic->width, wadpic->height, wadpic->data, ctx);
}

qpic_t *
Vulkan_Draw_CachePic (const char *path, qboolean alpha, vulkan_ctx_t *ctx)
{
	return pic_data (0, 1, 1, (const byte *)"", ctx);
}

void
Vulkan_Draw_UncachePic (const char *path, vulkan_ctx_t *ctx)
{
}

void
Vulkan_Draw_TextBox (int x, int y, int width, int lines, byte alpha,
					 vulkan_ctx_t *ctx)
{
}

void
Vulkan_Draw_Shutdown (vulkan_ctx_t *ctx)
{
	qfv_device_t *device = ctx->device;
	qfv_devfuncs_t *dfunc = device->funcs;

	destroy_quad_buffers (ctx);

	dfunc->vkDestroyPipeline (device->dev, twod_pipeline, 0);
	QFV_DestroyScrap (draw_scrap);
	QFV_DestroyStagingBuffer (draw_stage);
}

void
Vulkan_Draw_Init (vulkan_ctx_t *ctx)
{
	qfv_device_t *device = ctx->device;
	qfv_devfuncs_t *dfunc = device->funcs;

	create_quad_buffers (ctx);
	draw_scrap = QFV_CreateScrap (device, 2048, QFV_RGBA);
	draw_stage = QFV_CreateStagingBuffer (device, 4 * 1024 * 1024);
	conchars_sampler = QFV_GetSampler (ctx, "quakepic");

	qpic_t     *charspic = Draw_Font8x8Pic ();

	conchars = pic_data (0, charspic->width, charspic->height, charspic->data, ctx);

	flush_draw_scrap (ctx);

	twod_pipeline = Vulkan_CreatePipeline (ctx, "twod");

	twod_layout = QFV_GetPipelineLayout (ctx, "twod");

	__auto_type layouts = QFV_AllocDescriptorSetLayoutSet (ctx->framebuffers.size, alloca);
	for (size_t i = 0; i < layouts->size; i++) {
		layouts->a[i] = QFV_GetDescriptorSetLayout (ctx, "twod");
	}
	__auto_type pool = QFV_GetDescriptorPool (ctx, "twod");

	VkDescriptorBufferInfo bufferInfo = {
		ctx->matrices.buffer_2d, 0, VK_WHOLE_SIZE
	};
	VkDescriptorImageInfo imageInfo = {
		conchars_sampler,
		QFV_ScrapImageView (draw_scrap),
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	__auto_type cmdBuffers
		= QFV_AllocateCommandBuffers (device, ctx->cmdpool, 1,
									  ctx->framebuffers.size);

	__auto_type sets = QFV_AllocateDescriptorSet (device, pool, layouts);
	for (size_t i = 0; i < ctx->framebuffers.size; i++) {
		__auto_type frame = &ctx->framebuffers.a[i];
		frame->twodDescriptors = sets->a[i];

		VkWriteDescriptorSet write[] = {
			{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0,
			  frame->twodDescriptors, 0, 0, 1,
			  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			  0, &bufferInfo, 0 },
			{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0,
			  frame->twodDescriptors, 1, 0, 1,
			  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  &imageInfo, 0, 0 },
		};
		dfunc->vkUpdateDescriptorSets (device->dev, 2, write, 0, 0);
		draw_cmdBuffer = frame->subCommand->size;
		DARRAY_APPEND (frame->subCommand, cmdBuffers->a[i]);
	}
	free (sets);
	free (cmdBuffers);
}

static inline void
draw_pic (float x, float y, int w, int h, qpic_t *pic,
		  int srcx, int srcy, int srcw, int srch,
		  float *color)
{
	if (num_quads + VERTS_PER_QUAD > MAX_QUADS) {
		return;
	}

	drawvert_t *verts = quad_verts + num_quads * VERTS_PER_QUAD;
	num_quads += VERTS_PER_QUAD;

	subpic_t   *subpic = *(subpic_t **) pic->data;
	srcx += subpic->rect->x;
	srcy += subpic->rect->y;

	float size = subpic->size;
	float sl = (srcx + 0.03125) * size;
	float sr = (srcx + srcw - 0.03125) * size;
	float st = (srcy + 0.03125) * size;
	float sb = (srcy + srch - 0.03125) * size;

	verts[0].xy[0] = x;
	verts[0].xy[1] = y;
	verts[0].st[0] = sl;
	verts[0].st[1] = st;
	QuatCopy (color, verts[0].color);

	verts[1].xy[0] = x;
	verts[1].xy[1] = y + h;
	verts[1].st[0] = sl;
	verts[1].st[1] = sb;
	QuatCopy (color, verts[1].color);

	verts[2].xy[0] = x + w;
	verts[2].xy[1] = y;
	verts[2].st[0] = sr;
	verts[2].st[1] = st;
	QuatCopy (color, verts[2].color);

	verts[3].xy[0] = x + w;
	verts[3].xy[1] = y + h;
	verts[3].st[0] = sr;
	verts[3].st[1] = sb;
	QuatCopy (color, verts[3].color);
}

static inline void
queue_character (int x, int y, byte chr, vulkan_ctx_t *ctx)
{
	quat_t      color = {1, 1, 1, 1};
	int         cx, cy;
	cx = chr % 16;
	cy = chr / 16;

	draw_pic (x, y, 8, 8, conchars, cx * 8, cy * 8, 8, 8, color);
}

void
Vulkan_Draw_Character (int x, int y, unsigned int chr, vulkan_ctx_t *ctx)
{
	if (chr == ' ') {
		return;
	}
	if (y <= -8 || y >= vid.conheight) {
		return;
	}
	if (x <= -8 || x >= vid.conwidth) {
		return;
	}
	queue_character (x, y, chr, ctx);
}

void
Vulkan_Draw_String (int x, int y, const char *str, vulkan_ctx_t *ctx)
{
	byte        chr;

	if (!str || !str[0]) {
		return;
	}
	if (y <= -8 || y >= vid.conheight) {
		return;
	}
	while (*str) {
		if ((chr = *str++) != ' ' && x >= -8 && x < vid.conwidth) {
			queue_character (x, y, chr, ctx);
		}
		x += 8;
	}
}

void
Vulkan_Draw_nString (int x, int y, const char *str, int count,
					 vulkan_ctx_t *ctx)
{
	byte        chr;

	if (!str || !str[0]) {
		return;
	}
	if (y <= -8 || y >= vid.conheight) {
		return;
	}
	while (count-- > 0 && *str) {
		if ((chr = *str++) != ' ' && x >= -8 && x < vid.conwidth) {
			queue_character (x, y, chr, ctx);
		}
		x += 8;
	}
}

void
Vulkan_Draw_AltString (int x, int y, const char *str, vulkan_ctx_t *ctx)
{
	byte        chr;

	if (!str || !str[0]) {
		return;
	}
	if (y <= -8 || y >= vid.conheight) {
		return;
	}
	while (*str) {
		if ((chr = *str++ | 0x80) != (' ' | 0x80)
			&& x >= -8 && x < vid.conwidth) {
			queue_character (x, y, chr, ctx);
		}
		x += 8;
	}
}

void
Vulkan_Draw_CrosshairAt (int ch, int x, int y, vulkan_ctx_t *ctx)
{
}

void
Vulkan_Draw_Crosshair (vulkan_ctx_t *ctx)
{
}

void
Vulkan_Draw_Pic (int x, int y, qpic_t *pic, vulkan_ctx_t *ctx)
{
	static quat_t color = { 1, 1, 1, 1};
	draw_pic (x, y, pic->width, pic->height, pic,
			  0, 0, pic->width, pic->height, color);
}

void
Vulkan_Draw_Picf (float x, float y, qpic_t *pic, vulkan_ctx_t *ctx)
{
	static quat_t color = { 1, 1, 1, 1};
	draw_pic (x, y, pic->width, pic->height, pic,
			  0, 0, pic->width, pic->height, color);
}

void
Vulkan_Draw_SubPic (int x, int y, qpic_t *pic,
					int srcx, int srcy, int width, int height,
					vulkan_ctx_t *ctx)
{
}

void
Vulkan_Draw_ConsoleBackground (int lines, byte alpha, vulkan_ctx_t *ctx)
{
}

void
Vulkan_Draw_TileClear (int x, int y, int w, int h, vulkan_ctx_t *ctx)
{
}

void
Vulkan_Draw_Fill (int x, int y, int w, int h, int c, vulkan_ctx_t *ctx)
{
}

void
Vulkan_Draw_FadeScreen (vulkan_ctx_t *ctx)
{
}

void
Vulkan_Set2D (vulkan_ctx_t *ctx)
{
}

void
Vulkan_Set2DScaled (vulkan_ctx_t *ctx)
{
}

void
Vulkan_End2D (vulkan_ctx_t *ctx)
{
}

void
Vulkan_DrawReset (vulkan_ctx_t *ctx)
{
}

void
Vulkan_FlushText (vulkan_ctx_t *ctx)
{
	if (draw_stage->offset) {
		flush_draw_scrap (ctx);
	}
	qfv_device_t *device = ctx->device;
	qfv_devfuncs_t *dfunc = device->funcs;
	__auto_type frame = &ctx->framebuffers.a[ctx->curFrame];
	VkCommandBuffer cmd = frame->subCommand->a[draw_cmdBuffer];

	VkMappedMemoryRange range = {
		VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, 0,
		quad_memory, 0, num_quads * VERTS_PER_QUAD * sizeof (drawvert_t),
	};
	dfunc->vkFlushMappedMemoryRanges (device->dev, 1, &range);

	dfunc->vkResetCommandBuffer (cmd, 0);
	VkCommandBufferInheritanceInfo inherit = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO, 0,
		ctx->renderpass.renderpass, 0,
		frame->framebuffer,
		0, 0, 0
	};
	VkCommandBufferBeginInfo beginInfo = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 0,
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		| VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT, &inherit,
	};
	dfunc->vkBeginCommandBuffer (cmd, &beginInfo);

	dfunc->vkCmdBindPipeline (cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
							  twod_pipeline);
	VkViewport viewport = {0, 0, vid.width, vid.height, 0, 1};
	VkRect2D scissor = { {0, 0}, {vid.width, vid.height} };
	dfunc->vkCmdSetViewport (cmd, 0, 1, &viewport);
	dfunc->vkCmdSetScissor (cmd, 0, 1, &scissor);
	VkDeviceSize offsets[] = {0};
	dfunc->vkCmdBindVertexBuffers (cmd, 0, 1, &quad_vert_buffer, offsets);
	dfunc->vkCmdBindIndexBuffer (cmd, quad_ind_buffer, 0,
								 VK_INDEX_TYPE_UINT32);
	VkDescriptorSet set = frame->twodDescriptors;
	VkPipelineLayout layout = twod_layout;
	dfunc->vkCmdBindDescriptorSets (cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
									layout, 0, 1, &set, 0, 0);
	dfunc->vkCmdDrawIndexed (cmd, num_quads * INDS_PER_QUAD, 1, 0, 0, 0);

	dfunc->vkEndCommandBuffer (cmd);

	num_quads = 0;
}

void
Vulkan_Draw_BlendScreen (quat_t color, vulkan_ctx_t *ctx)
{
}
