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

from struct import unpack

from mathutils import Vector

class MDL:
    class Skin:
        def __init__(self):
            pass
        def read(self, mdl, sub=0):
            self.width, self.height = mdl.skinwidth, mdl.skinheight
            if sub:
                self.type = 0
                self.read_pixels(mdl)
                return self
            self.type = mdl.read_int()
            if self.type:
                # skin group
                num = mdl.read_int()
                self.times = mdl.read_float(num)
                self.skins = []
                for i in range(num):
                    self.skins.append(MDL.Skin().read(mdl, 1))
                    num -= 1
                return self
            self.read_pixels(mdl)
            return self

        def read_pixels(self, mdl):
            size = self.width * self.height
            self.pixels = mdl.read_bytes(size)

    class STVert:
        def __init__(self):
            pass
        def read(self, mdl):
            self.onseam = mdl.read_int()
            self.s, self.t = mdl.read_int(2)
            return self

    class Tri:
        def __init__(self):
            pass
        def read(self, mdl):
            self.facesfront = mdl.read_int()
            self.verts = mdl.read_int(3)
            return self

    class Frame:
        def __init__(self):
            pass
        def read(self, mdl, numverts, sub=0):
            if sub:
                self.type = 0
            else:
                self.type = mdl.read_int()
            if self.type:
                num = mdl.read_int()
                self.read_bounds(mdl)
                self.times = mdl.read_float(num)
                self.frames = []
                for i in range(num):
                    self.frames.append(MDL.Frame().read(mdl, numverts, 1))
                return self
            self.read_bounds(mdl)
            self.read_name(mdl)
            self.read_verts(mdl, numverts)
            return self

        def read_name(self, mdl):
            if mdl.version == 6:
                name = mdl.read_string(16)
            else:
                name = ""
            if "\0" in name:
                name = name[:name.index("\0")]
            self.name = name

        def read_bounds(self, mdl):
            self.mins = mdl.read_byte(4)[:3]    #discard normal index
            self.maxs = mdl.read_byte(4)[:3]    #discard normal index

        def read_verts(self, mdl, num):
            self.verts = []
            for i in range(num):
                self.verts.append(MDL.Vert().read(mdl))

    class Vert:
        def __init__(self):
            pass
        def read(self, mdl):
            self.r = mdl.read_byte(3)
            self.ni = mdl.read_byte()
            return self

    def read_byte(self, count=1):
        size = 1 * count
        data = self.file.read(size)
        data = unpack("<%dB" % count, data)
        if count == 1:
            return data[0]
        return data

    def read_int(self, count=1):
        size = 4 * count
        data = self.file.read(size)
        data = unpack("<%di" % count, data)
        if count == 1:
            return data[0]
        return data

    def read_float(self, count=1):
        size = 4 * count
        data = self.file.read(size)
        data = unpack("<%df" % count, data)
        if count == 1:
            return data[0]
        return data

    def read_bytes(self, size):
        return self.file.read(size)

    def read_string(self, size):
        data = self.file.read(size)
        s = ""
        for c in data:
            s = s + chr(c)
        return s

    def __init__(self):
        pass
    def read(self, filepath):
        self.file = open(filepath, "rb")
        self.name = filepath.split('/')[-1]
        self.name = self.name.split('.')[0]
        self.ident = self.read_string(4)
        self.version = self.read_int()
        if self.ident not in ["IDPO", "MD16"] or self.version not in [3, 6]:
            return None
        self.scale = Vector(self.read_float(3))
        self.scale_origin = Vector(self.read_float(3))
        self.boundingradius = self.read_float()
        self.eyeposition = Vector(self.read_float(3))
        numskins = self.read_int()
        self.skinwidth, self.skinheight = self.read_int(2)
        numverts, numtris, numframes = self.read_int(3)
        self.synctype = self.read_int()
        if self.version == 6:
            self.flags = self.read_int()
            self.size = self.read_float()
        # read in the skin data
        self.skins = []
        for i in range(numskins):
            self.skins.append(MDL.Skin().read(self))
        #read in the st verts (uv map)
        self.stverts = []
        for i in range(numverts):
            self.stverts.append (MDL.STVert().read(self))
        #read in the tris
        self.tris = []
        for i in range(numtris):
            self.tris.append(MDL.Tri().read(self))
        #read in the frames
        self.frames = []
        for i in range(numframes):
            self.frames.append(MDL.Frame().read(self, numverts))
        return self