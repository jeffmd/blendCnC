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
from bpy.types import Panel, Menu
from rna_prop_ui import PropertyPanel


class ObjectButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "object"


class OBJECT_PT_context_object(ObjectButtonsPanel, Panel):
    bl_label = ""
    bl_options = {'HIDE_HEADER'}

    def draw(self, context):
        layout = self.layout
        space = context.space_data

        if space.use_pin_id:
            layout.template_ID(space, "pin_id")
        else:
            row = layout.row()
            row.template_ID(context.scene.objects, "active", filter='AVAILABLE')


class OBJECT_PT_transform(ObjectButtonsPanel, Panel):
    bl_label = "Transform"

    def draw(self, context):
        layout = self.layout

        ob = context.object

        row = layout.row()

        row.column().prop(ob, "location")
        if ob.rotation_mode == 'QUATERNION':
            row.column().prop(ob, "rotation_quaternion", text="Rotation")
        elif ob.rotation_mode == 'AXIS_ANGLE':
            # row.column().label(text="Rotation")
            #row.column().prop(pchan, "rotation_angle", text="Angle")
            #row.column().prop(pchan, "rotation_axis", text="Axis")
            row.column().prop(ob, "rotation_axis_angle", text="Rotation")
        else:
            row.column().prop(ob, "rotation_euler", text="Rotation")

        row.column().prop(ob, "scale")

        layout.prop(ob, "rotation_mode")


class OBJECT_PT_delta_transform(ObjectButtonsPanel, Panel):
    bl_label = "Delta Transform"
    bl_options = {'DEFAULT_CLOSED'}

    def draw(self, context):
        layout = self.layout

        ob = context.object

        row = layout.row()

        row.column().prop(ob, "delta_location")
        if ob.rotation_mode == 'QUATERNION':
            row.column().prop(ob, "delta_rotation_quaternion", text="Rotation")
        elif ob.rotation_mode == 'AXIS_ANGLE':
            # row.column().label(text="Rotation")
            #row.column().prop(pchan, "delta_rotation_angle", text="Angle")
            #row.column().prop(pchan, "delta_rotation_axis", text="Axis")
            #row.column().prop(ob, "delta_rotation_axis_angle", text="Rotation")
            row.column().label(text="Not for Axis-Angle")
        else:
            row.column().prop(ob, "delta_rotation_euler", text="Delta Rotation")

        row.column().prop(ob, "delta_scale")


class OBJECT_PT_transform_locks(ObjectButtonsPanel, Panel):
    bl_label = "Transform Locks"
    bl_options = {'DEFAULT_CLOSED'}

    def draw(self, context):
        layout = self.layout

        ob = context.object

        split = layout.split(percentage=0.1)

        col = split.column(align=True)
        col.label(text="")
        col.label(text="X:")
        col.label(text="Y:")
        col.label(text="Z:")

        split.column().prop(ob, "lock_location", text="Location")
        split.column().prop(ob, "lock_rotation", text="Rotation")
        split.column().prop(ob, "lock_scale", text="Scale")

        if ob.rotation_mode in {'QUATERNION', 'AXIS_ANGLE'}:
            row = layout.row()
            row.prop(ob, "lock_rotations_4d", text="Lock Rotation")

            sub = row.row()
            sub.active = ob.lock_rotations_4d
            sub.prop(ob, "lock_rotation_w", text="W")


class OBJECT_PT_relations(ObjectButtonsPanel, Panel):
    bl_label = "Relations"

    def draw(self, context):
        layout = self.layout

        ob = context.object

        split = layout.split()

        col = split.column()
        col.prop(ob, "layers")
        col.separator()
        col.prop(ob, "pass_index")

        col = split.column()
        col.label(text="Parent:")
        col.prop(ob, "parent", text="")

        sub = col.column()
        sub.prop(ob, "parent_type", text="")
        parent = ob.parent
        sub.active = (parent is not None)


class OBJECT_PT_relations_extras(ObjectButtonsPanel, Panel):
    bl_label = "Relations Extras"
    bl_options = {'DEFAULT_CLOSED'}

    def draw(self, context):
        layout = self.layout

        ob = context.object

        split = layout.split()

        layout.prop(ob, "use_extra_recalc_object")
        layout.prop(ob, "use_extra_recalc_data")


class GROUP_MT_specials(Menu):
    bl_label = "Group Specials"

    def draw(self, context):
        layout = self.layout

        layout.operator("object.group_unlink", icon='X')
        layout.operator("object.grouped_select")
        layout.operator("object.dupli_offset_from_cursor")


class OBJECT_PT_groups(ObjectButtonsPanel, Panel):
    bl_label = "Groups"

    def draw(self, context):
        layout = self.layout

        obj = context.object

        row = layout.row(align=True)
        if bpy.data.groups:
            row.operator("object.group_link", text="Add to Group")
        else:
            row.operator("object.group_add", text="Add to Group")
        row.operator("object.group_add", text="", icon='ZOOMIN')

        obj_name = obj.name
        for group in bpy.data.groups:
            # XXX this is slow and stupid!, we need 2 checks, one that's fast
            # and another that we can be sure its not a name collision
            # from linked library data
            group_objects = group.objects
            if obj_name in group.objects and obj in group_objects[:]:
                col = layout.column(align=True)

                col.context_pointer_set("group", group)

                row = col.box().row()
                row.prop(group, "name", text="")
                row.operator("object.group_remove", text="", icon='X', emboss=False)
                row.menu("GROUP_MT_specials", icon='DOWNARROW_HLT', text="")

                split = col.box().split()

                col = split.column()
                col.prop(group, "layers", text="Dupli Visibility")

                col = split.column()
                col.prop(group, "dupli_offset", text="")


class OBJECT_PT_display(ObjectButtonsPanel, Panel):
    bl_label = "Display"

    def draw(self, context):
        layout = self.layout

        obj = context.object
        obj_type = obj.type
        is_geometry = (obj_type in {'MESH', 'CURVE', 'SURFACE', 'FONT'})
        is_wire = (obj_type in {'CAMERA', 'EMPTY'})
        is_empty_image = (obj_type == 'EMPTY' and obj.empty_draw_type == 'IMAGE')

        split = layout.split()

        col = split.column()
        col.prop(obj, "show_name", text="Name")
        col.prop(obj, "show_axis", text="Axis")
        # Makes no sense for cameras, etc.!
        if is_geometry:
            col.prop(obj, "show_wire", text="Wire")
        if obj_type == 'MESH':
            col.prop(obj, "show_all_edges")

        col = split.column()
        row = col.row()
        row.prop(obj, "show_bounds", text="Bounds")
        sub = row.row()
        sub.active = obj.show_bounds
        sub.prop(obj, "draw_bounds_type", text="")

        if is_geometry:
            col.prop(obj, "show_texture_space", text="Texture Space")
        col.prop(obj, "show_x_ray", text="X-Ray")
        if obj_type == 'MESH' or is_empty_image:
            col.prop(obj, "show_transparent", text="Transparency")

        split = layout.split()

        col = split.column()
        if is_wire:
            # wire objects only use the max. draw type for duplis
            col.active = False

        col.label(text="Maximum Draw Type:")
        col.prop(obj, "draw_type", text="")

        col = split.column()
        if is_geometry or is_empty_image:
            # Only useful with object having faces/materials...
            col.label(text="Object Color:")
            col.prop(obj, "color", text="")

class OBJECT_PT_custom_props(ObjectButtonsPanel, PropertyPanel, Panel):
    COMPAT_ENGINES = {'BLENDER_RENDER'}
    _context_path = "object"
    _property_type = bpy.types.Object


classes = (
    OBJECT_PT_context_object,
    OBJECT_PT_transform,
    OBJECT_PT_delta_transform,
    OBJECT_PT_transform_locks,
    OBJECT_PT_relations,
    OBJECT_PT_relations_extras,
    GROUP_MT_specials,
    OBJECT_PT_groups,
    OBJECT_PT_display,
    OBJECT_PT_custom_props,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
