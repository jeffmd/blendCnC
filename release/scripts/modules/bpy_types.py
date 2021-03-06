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

# <pep8-80 compliant>

from _bpy import types as bpy_types
import _bpy

StructRNA = bpy_types.bpy_struct
StructMetaPropGroup = bpy_types.bpy_struct_meta_idprop
# StructRNA = bpy_types.Struct

bpy_types.BlendDataLibraries.load = _bpy._library_load
bpy_types.BlendDataLibraries.write = _bpy._library_write
bpy_types.BlendData.user_map = _bpy._rna_id_collection_user_map


class Context(StructRNA):
    __slots__ = ()

    def copy(self):
        from types import BuiltinMethodType
        new_context = {}
        generic_attrs = (
            *StructRNA.__dict__.keys(),
            "bl_rna", "rna_type", "copy",
        )
        for attr in dir(self):
            if not (attr.startswith("_") or attr in generic_attrs):
                value = getattr(self, attr)
                if type(value) != BuiltinMethodType:
                    new_context[attr] = value

        return new_context


class Library(bpy_types.ID):
    __slots__ = ()

    @property
    def users_id(self):
        """ID data blocks which use this library"""
        import bpy

        # See: readblenentry.c, IDTYPE_FLAGS_ISLINKABLE,
        # we could make this an attribute in rna.
        attr_links = ("cameras",
                      "curves", "groups", "images",
                      "lamps", "materials", 
                      "meshes", "objects", "scenes",
                      "textures", "texts",
                      "fonts", "worlds")

        return tuple(id_block
                     for attr in attr_links
                     for id_block in getattr(bpy.data, attr)
                     if id_block.library == self)


class Texture(bpy_types.ID):
    __slots__ = ()

    @property
    def users_material(self):
        """Materials that use this texture"""
        import bpy
        return tuple(mat for mat in bpy.data.materials
                     if self in [slot.texture
                                 for slot in mat.texture_slots
                                 if slot]
                     )

    @property
    def users_object_modifier(self):
        """Object modifiers that use this texture"""
        import bpy
        return tuple(obj for obj in bpy.data.objects if
                     self in [mod.texture
                              for mod in obj.modifiers
                              if mod.type == 'DISPLACE']
                     )


class Group(bpy_types.ID):
    __slots__ = ()

    @property
    def users_dupli_group(self):
        """The dupli group this group is used in"""
        import bpy
        return tuple(obj for obj in bpy.data.objects
                     if self == obj.dupli_group)


class Object(bpy_types.ID):
    __slots__ = ()

    @property
    def children(self):
        """All the children of this object"""
        import bpy
        return tuple(child for child in bpy.data.objects
                     if child.parent == self)

    @property
    def users_group(self):
        """The groups this object is in"""
        import bpy
        return tuple(group for group in bpy.data.groups
                     if self in group.objects[:])

    @property
    def users_scene(self):
        """The scenes this object is in"""
        import bpy
        return tuple(scene for scene in bpy.data.scenes
                     if self in scene.objects[:])


class WindowManager(bpy_types.ID):
    __slots__ = ()

    def popup_menu(self, draw_func, title="", icon='NONE'):
        import bpy
        popup = self.popmenu_begin__internal(title, icon)

        try:
            draw_func(popup, bpy.context)
        finally:
            self.popmenu_end__internal(popup)

    def popup_menu_pie(self, event, draw_func, title="", icon='NONE'):
        import bpy
        pie = self.piemenu_begin__internal(title, icon, event)

        if pie:
            try:
                draw_func(pie, bpy.context)
            finally:
                self.piemenu_end__internal(pie)


def ord_ind(i1, i2):
    if i1 < i2:
        return i1, i2
    return i2, i1


class Mesh(bpy_types.ID):
    __slots__ = ()

    def from_pydata(self, vertices, edges, faces):
        """
        Make a mesh from a list of vertices/edges/faces
        Until we have a nicer way to make geometry, use this.

        :arg vertices:

           float triplets each representing (X, Y, Z)
           eg: [(0.0, 1.0, 0.5), ...].

        :type vertices: iterable object
        :arg edges:

           int pairs, each pair contains two indices to the
           *vertices* argument. eg: [(1, 2), ...]

        :type edges: iterable object
        :arg faces:

           iterator of faces, each faces contains three or more indices to
           the *vertices* argument. eg: [(5, 6, 8, 9), (1, 2, 3), ...]

        :type faces: iterable object

        .. warning::

           Invalid mesh data
           *(out of range indices, edges with matching indices,
           2 sided faces... etc)* are **not** prevented.
           If the data used for mesh creation isn't known to be valid,
           run :class:`Mesh.validate` after this function.
        """
        from itertools import chain, islice, accumulate

        face_lengths = tuple(map(len, faces))

        self.vertices.add(len(vertices))
        self.edges.add(len(edges))
        self.loops.add(sum(face_lengths))
        self.polygons.add(len(faces))

        self.vertices.foreach_set("co", tuple(chain.from_iterable(vertices)))
        self.edges.foreach_set("vertices", tuple(chain.from_iterable(edges)))

        vertex_indices = tuple(chain.from_iterable(faces))
        loop_starts = tuple(islice(chain([0], accumulate(face_lengths)), len(faces)))

        self.polygons.foreach_set("loop_total", face_lengths)
        self.polygons.foreach_set("loop_start", loop_starts)
        self.polygons.foreach_set("vertices", vertex_indices)

        # if no edges - calculate them
        if faces and (not edges):
            self.update(calc_edges=True)

    @property
    def edge_keys(self):
        return [ed.key for ed in self.edges]


class MeshEdge(StructRNA):
    __slots__ = ()

    @property
    def key(self):
        return ord_ind(*tuple(self.vertices))


class MeshTessFace(StructRNA):
    __slots__ = ()

    @property
    def center(self):
        """The midpoint of the face."""
        face_verts = self.vertices[:]
        mesh_verts = self.id_data.vertices
        if len(face_verts) == 3:
            return (mesh_verts[face_verts[0]].co +
                    mesh_verts[face_verts[1]].co +
                    mesh_verts[face_verts[2]].co
                    ) / 3.0
        else:
            return (mesh_verts[face_verts[0]].co +
                    mesh_verts[face_verts[1]].co +
                    mesh_verts[face_verts[2]].co +
                    mesh_verts[face_verts[3]].co
                    ) / 4.0

    @property
    def edge_keys(self):
        verts = self.vertices[:]
        if len(verts) == 3:
            return (ord_ind(verts[0], verts[1]),
                    ord_ind(verts[1], verts[2]),
                    ord_ind(verts[2], verts[0]),
                    )
        else:
            return (ord_ind(verts[0], verts[1]),
                    ord_ind(verts[1], verts[2]),
                    ord_ind(verts[2], verts[3]),
                    ord_ind(verts[3], verts[0]),
                    )


class MeshPolygon(StructRNA):
    __slots__ = ()

    @property
    def edge_keys(self):
        verts = self.vertices[:]
        vlen = len(self.vertices)
        return [ord_ind(verts[i], verts[(i + 1) % vlen]) for i in range(vlen)]

    @property
    def loop_indices(self):
        start = self.loop_start
        end = start + self.loop_total
        return range(start, end)


class Text(bpy_types.ID):
    __slots__ = ()

    def as_string(self):
        """Return the text as a string."""
        return "\n".join(line.body for line in self.lines)

    def from_string(self, string):
        """Replace text with this string."""
        self.clear()
        self.write(string)

# values are module: [(cls, path, line), ...]
TypeMap = {}


class RNAMeta(type):

    def __new__(cls, name, bases, classdict, **args):
        result = type.__new__(cls, name, bases, classdict)
        if bases and bases[0] is not StructRNA:
            from _weakref import ref as ref
            module = result.__module__

            # first part of packages only
            if "." in module:
                module = module[:module.index(".")]

            TypeMap.setdefault(module, []).append(ref(result))

        return result

    @property
    def is_registered(cls):
        return "bl_rna" in cls.__dict__


class OrderedDictMini(dict):

    def __init__(self, *args):
        self.order = []
        dict.__init__(self, args)

    def __setitem__(self, key, val):
        dict.__setitem__(self, key, val)
        if key not in self.order:
            self.order.append(key)

    def __delitem__(self, key):
        dict.__delitem__(self, key)
        self.order.remove(key)


class RNAMetaPropGroup(StructMetaPropGroup, RNAMeta):
    pass


class OrderedMeta(RNAMeta):

    def __init__(cls, name, bases, attributes):
        if attributes.__class__ is OrderedDictMini:
            cls.order = attributes.order

    def __prepare__(name, bases, **kwargs):
        return OrderedDictMini()  # collections.OrderedDict()


# Only defined so operators members can be used by accessing self.order
# with doc generation 'self.properties.bl_rna.properties' can fail
class Operator(StructRNA, metaclass=OrderedMeta):
    __slots__ = ()

    def __getattribute__(self, attr):
        properties = StructRNA.path_resolve(self, "properties")
        bl_rna = getattr(properties, "bl_rna", None)
        if (bl_rna is not None) and (attr in bl_rna.properties):
            return getattr(properties, attr)
        return super().__getattribute__(attr)

    def __setattr__(self, attr, value):
        properties = StructRNA.path_resolve(self, "properties")
        bl_rna = getattr(properties, "bl_rna", None)
        if (bl_rna is not None) and (attr in bl_rna.properties):
            return setattr(properties, attr, value)
        return super().__setattr__(attr, value)

    def __delattr__(self, attr):
        properties = StructRNA.path_resolve(self, "properties")
        bl_rna = getattr(properties, "bl_rna", None)
        if (bl_rna is not None) and (attr in bl_rna.properties):
            return delattr(properties, attr)
        return super().__delattr__(attr)

    def as_keywords(self, ignore=()):
        """Return a copy of the properties as a dictionary"""
        ignore = ignore + ("rna_type",)
        return {attr: getattr(self, attr)
                for attr in self.properties.rna_type.properties.keys()
                if attr not in ignore}


class Macro(StructRNA, metaclass=OrderedMeta):
    # bpy_types is imported before ops is defined
    # so we have to do a local import on each run
    __slots__ = ()

    @classmethod
    def define(self, opname):
        from _bpy import ops
        return ops.macro_define(self, opname)


class PropertyGroup(StructRNA, metaclass=RNAMetaPropGroup):
    __slots__ = ()


class AddonPreferences(StructRNA, metaclass=RNAMeta):
    __slots__ = ()


class _GenericUI:
    __slots__ = ()

    @classmethod
    def _dyn_ui_initialize(cls):
        draw_funcs = getattr(cls.draw, "_draw_funcs", None)

        if draw_funcs is None:

            def draw_ls(self, context):
                # ensure menus always get default context
                operator_context_default = self.layout.operator_context

                for func in draw_ls._draw_funcs:
                    # so bad menu functions don't stop
                    # the entire menu from drawing
                    try:
                        func(self, context)
                    except:
                        import traceback
                        traceback.print_exc()

                    self.layout.operator_context = operator_context_default

            draw_funcs = draw_ls._draw_funcs = [cls.draw]
            cls.draw = draw_ls

        return draw_funcs

    @classmethod
    def is_extended(cls):
        return bool(getattr(cls.draw, "_draw_funcs", None))

    @classmethod
    def append(cls, draw_func):
        """
        Append a draw function to this menu,
        takes the same arguments as the menus draw function
        """
        draw_funcs = cls._dyn_ui_initialize()
        draw_funcs.append(draw_func)

    @classmethod
    def prepend(cls, draw_func):
        """
        Prepend a draw function to this menu, takes the same arguments as
        the menus draw function
        """
        draw_funcs = cls._dyn_ui_initialize()
        draw_funcs.insert(0, draw_func)

    @classmethod
    def remove(cls, draw_func):
        """Remove a draw function that has been added to this menu"""
        draw_funcs = cls._dyn_ui_initialize()
        try:
            draw_funcs.remove(draw_func)
        except ValueError:
            pass


class Panel(StructRNA, _GenericUI, metaclass=RNAMeta):
    __slots__ = ()


class UIList(StructRNA, _GenericUI, metaclass=RNAMeta):
    __slots__ = ()


class Header(StructRNA, _GenericUI, metaclass=RNAMeta):
    __slots__ = ()


class Menu(StructRNA, _GenericUI, metaclass=RNAMeta):
    __slots__ = ()

    def path_menu(self, searchpaths, operator, *,
                  props_default=None, prop_filepath="filepath",
                  filter_ext=None, filter_path=None, display_name=None):
        """
        Populate a menu from a list of paths.

        :arg searchpaths: Paths to scan.
        :type searchpaths: sequence of strings.
        :arg operator: The operator id to use with each file.
        :type operator: string
        :arg prop_filepath: Optional operator filepath property (defaults to "filepath").
        :type prop_filepath: string
        :arg props_default: Properties to assign to each operator.
        :type props_default: dict
        :arg filter_ext: Optional callback that takes the file extensions.

           Returning false excludes the file from the list.

        :type filter_ext: Callable that takes a string and returns a bool.
        :arg display_name: Optional callback that takes the full path, returns the name to display.
        :type display_name: Callable that takes a string and returns a string.
        """

        layout = self.layout

        import os
        import bpy.utils

        layout = self.layout

        if not searchpaths:
            layout.label("* Missing Paths *")

        # collect paths
        files = []
        for directory in searchpaths:
            files.extend(
                [(f, os.path.join(directory, f))
                 for f in os.listdir(directory)
                 if (not f.startswith("."))
                 if ((filter_ext is None) or
                     (filter_ext(os.path.splitext(f)[1])))
                 if ((filter_path is None) or
                     (filter_path(f)))
                 ])

        files.sort()

        for f, filepath in files:
            # Intentionally pass the full path to 'display_name' callback,
            # since the callback may want to use part a directory in the name.
            props = layout.operator(
                operator,
                text=display_name(filepath) if display_name else bpy.path.display_name(f),
                translate=False,
            )

            if props_default is not None:
                for attr, value in props_default.items():
                    setattr(props, attr, value)

            setattr(props, prop_filepath, filepath)
            if operator == "script.execute_preset":
                props.menu_idname = self.bl_idname

    def draw_preset(self, context):
        """
        Define these on the subclass:
        - preset_operator (string)
        - preset_subdir (string)

        Optionally:
        - preset_extensions (set of strings)
        - preset_operator_defaults (dict of keyword args)
        """
        import bpy
        ext_valid = getattr(self, "preset_extensions", {".py", ".xml"})
        props_default = getattr(self, "preset_operator_defaults", None)
        self.path_menu(bpy.utils.preset_paths(self.preset_subdir),
                       self.preset_operator,
                       props_default=props_default,
                       filter_ext=lambda ext: ext.lower() in ext_valid)

    @classmethod
    def draw_collapsible(cls, context, layout):
        # helper function for (optionally) collapsed header menus
        # only usable within headers
        if context.area.show_menus:
            cls.draw_menus(layout, context)
        else:
            layout.menu(cls.__name__, icon='COLLAPSEMENU')

