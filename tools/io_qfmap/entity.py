# vim:ts=4:et
# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8 compliant>

import bpy

def default_brush_entity(entityclass):
    name = entityclass.name
    verts = [(-8, -8, -8),
             ( 8,  8, -8),
             (-8,  8,  8),
             ( 8, -8,  8)]
    faces = [(0, 2, 1),
             (0, 3, 2),
             (0, 1, 3),
             (1, 2, 3)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    return mesh

def entity_box(entityclass):
    name = entityclass.name
    size = entityclass.size
    color = entityclass.color
    if name in bpy.data.meshes:
        return bpy.data.meshes[name]
    verts = [(size[0][0], size[0][1], size[0][2]),
             (size[0][0], size[0][1], size[1][2]),
             (size[0][0], size[1][1], size[0][2]),
             (size[0][0], size[1][1], size[1][2]),
             (size[1][0], size[0][1], size[0][2]),
             (size[1][0], size[0][1], size[1][2]),
             (size[1][0], size[1][1], size[0][2]),
             (size[1][0], size[1][1], size[1][2])]
    faces = [(0, 1, 3, 2),
             (0, 2, 6, 4),
             (0, 4, 5, 1),
             (4, 6, 7, 5),
             (6, 2, 3, 7),
             (1, 5, 7, 3)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = color
    mat.use_raytrace = False
    mesh.materials.append(mat)
    return mesh

def add_entity(self, context, entclass):
    entity_class = context.scene.qfmap.entity_classes.entity_classes[entclass]
    context.user_preferences.edit.use_global_undo = False
    for obj in bpy.data.objects:
        obj.select = False
    if entity_class.size:
        mesh = entity_box(entity_class)
    else:
        mesh = default_brush_entity(entity_class)
    obj = bpy.data.objects.new(entity_class.name, mesh)
    obj.location = context.scene.cursor_location
    obj.select = True
    context.scene.objects.link(obj)
    bpy.context.scene.objects.active=obj
    context.user_preferences.edit.use_global_undo = True
    return {'FINISHED'}