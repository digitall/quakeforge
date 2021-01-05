/*
	shader.c

	Vulkan shader manager

	Copyright (C) 2020      Bill Currie <bill@taniwha.org>

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

#ifdef HAVE_MATH_H
# include <math.h>
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "QF/alloc.h"
#include "QF/cvar.h"
#include "QF/dstring.h"
#include "QF/hash.h"
#include "QF/qfplist.h"
#include "QF/quakefs.h"
#include "QF/sys.h"
#include "QF/Vulkan/qf_vid.h"
#include "QF/Vulkan/device.h"
#include "QF/Vulkan/image.h"
#include "QF/Vulkan/instance.h"
#include "QF/Vulkan/renderpass.h"
#include "QF/Vulkan/shader.h"

#include "vid_vulkan.h"

static
#include "libs/video/renderer/vulkan/passthrough.vert.spvc"
static
#include "libs/video/renderer/vulkan/pushcolor.frag.spvc"

typedef struct shaderdata_s {
	const char *name;
	const uint32_t *data;
	size_t      size;
} shaderdata_t;

static shaderdata_t builtin_shaders[] = {
	{ "passthrough.vert", passthrough_vert, sizeof (passthrough_vert) },
	{ "pushcolor.frag", pushcolor_frag, sizeof (pushcolor_frag) },
	{}
};

#define BUILTIN "$builtin/"
#define BUILTIN_SIZE (sizeof (BUILTIN) - 1)
#define SHADER "$shader/"
#define SHADER_SIZE (sizeof (SHADER) - 1)

VkShaderModule
QFV_CreateShaderModule (qfv_device_t *device, const char *shader_path)
{
	VkDevice    dev = device->dev;
	qfv_devfuncs_t *dfunc = device->funcs;

	shaderdata_t _data = {};
	shaderdata_t *data = 0;
	dstring_t  *path = 0;
	QFile      *file = 0;
	VkShaderModule shader = 0;

	if (strncmp (shader_path, BUILTIN, BUILTIN_SIZE) == 0) {
		const char *name = shader_path + BUILTIN_SIZE;

		for (int i = 0; builtin_shaders[i].name; i++) {
			if (strcmp (builtin_shaders[i].name, name) == 0) {
				data = &builtin_shaders[i];
				break;
			}
		}
	} else if (strncmp (shader_path, SHADER, SHADER_SIZE)) {
		path = dstring_new ();
		dsprintf (path, "%s/%s", FS_SHADERPATH, shader_path + SHADER_SIZE);
		file = Qopen (path->str, "rbz");
	} else {
		file = QFS_FOpenFile (shader_path);
	}

	if (file) {
		_data.size = Qfilesize (file);
		_data.data = malloc (_data.size);
		Qread (file, (void *) _data.data, _data.size);
		Qclose (file);
		data = &_data;
	}

	if (data) {
		Sys_MaskPrintf (SYS_VULKAN,
						"QFV_CreateShaderModule: creating shader module %s\n",
						shader_path);
		VkShaderModuleCreateInfo createInfo = {
			VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, 0,
			0, data->size, data->data
		};

		dfunc->vkCreateShaderModule (dev, &createInfo, 0, &shader);
	} else {
		Sys_MaskPrintf (SYS_VULKAN,
						"QFV_CreateShaderModule: could not find shader %s\n",
						shader_path);
	}

	if (path) {
		dstring_delete (path);
	}
	if (_data.data) {
		free ((void *) _data.data);
	}
	return shader;
}

void
QFV_DestroyShaderModule (qfv_device_t *device, VkShaderModule module)
{
	VkDevice    dev = device->dev;
	qfv_devfuncs_t *dfunc = device->funcs;

	dfunc->vkDestroyShaderModule (dev, module, 0);
}