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
from bpy.types import Menu, Panel, UIList
from rna_prop_ui import PropertyPanel
from bpy.app.translations import pgettext_iface as iface_


def active_node_mat(mat):
    # TODO, 2.4x has a pipeline section, for 2.5 we need to communicate
    # which settings from node-materials are used
    if mat is not None:
        return mat

    return None


def check_material(mat):
    if mat is not None:
        return True
    return False

def simple_material(mat):
    if (mat is not None):
        return True
    return False

class MATERIAL_MT_specials(Menu):
    bl_label = "Material Specials"

    def draw(self, context):
        layout = self.layout

        layout.operator("object.material_slot_copy", icon='COPY_ID')
        layout.operator("material.copy", icon='COPYDOWN')
        layout.operator("material.paste", icon='PASTEDOWN')


class MATERIAL_UL_matslots(UIList):

    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        # assert(isinstance(item, bpy.types.MaterialSlot)
        # ob = data
        slot = item
        ma = slot.material
        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            if ma:
                layout.prop(ma, "name", text="", emboss=False, icon_value=icon)
            else:
                layout.label(text="", icon_value=icon)

        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)


class MaterialButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "material"

    @classmethod
    def poll(cls, context):
        return context.material


class MATERIAL_PT_context_material(MaterialButtonsPanel, Panel):
    bl_label = ""
    bl_options = {'HIDE_HEADER'}

    @classmethod
    def poll(cls, context):
        # An exception, don't call the parent poll func because
        # this manages materials for all engine types

        return (context.material or context.object)

    def draw(self, context):
        layout = self.layout

        mat = context.material
        ob = context.object
        slot = context.material_slot
        space = context.space_data

        if ob:
            is_sortable = (len(ob.material_slots) > 1)

            rows = 1
            if is_sortable:
                rows = 4

            row = layout.row()

            row.template_list("MATERIAL_UL_matslots", "", ob, "material_slots", ob, "active_material_index", rows=rows)

            col = row.column(align=True)
            # col.operator("object.material_slot_add", icon='ZOOMIN', text="")
            # col.operator("object.material_slot_remove", icon='ZOOMOUT', text="")

            col.menu("MATERIAL_MT_specials", icon='DOWNARROW_HLT', text="")

            if is_sortable:
                col.separator()

                col.operator("object.material_slot_move", icon='TRIA_UP', text="").direction = 'UP'
                col.operator("object.material_slot_move", icon='TRIA_DOWN', text="").direction = 'DOWN'

            if ob.mode == 'EDIT':
                row = layout.row(align=True)
                row.operator("object.material_slot_assign", text="Assign")
                row.operator("object.material_slot_select", text="Select")
                row.operator("object.material_slot_deselect", text="Deselect")

        split = layout.split(percentage=0.65)

        if ob:
            split.template_ID(ob, "active_material", new="material.new")
            row = split.row()

            if slot:
                row.prop(slot, "link", text="")
            else:
                row.label()
        elif mat:
            split.template_ID(space, "pin_id")
            split.separator()

        if mat:
            layout.row().prop(mat, "type", expand=True)


class MATERIAL_PT_preview(MaterialButtonsPanel, Panel):
    bl_label = "Preview"

    def draw(self, context):
        self.layout.template_preview(context.material)


class MATERIAL_PT_pipeline(MaterialButtonsPanel, Panel):
    bl_label = "Render Pipeline Options"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        mat = context.material
        return mat and (not simple_material(mat)) and (mat.type in {'SURFACE', 'WIRE', 'VOLUME'})

    def draw(self, context):
        layout = self. layout

        mat = context.material
        mat_type = mat.type in {'SURFACE', 'WIRE'}

        row = layout.row()
        row.active = mat_type
        row.prop(mat, "use_transparency")
        sub = row.column()
        sub.prop(mat, "offset_z")

        sub.active = mat_type and mat.use_transparency and mat.transparency_method == 'Z_TRANSPARENCY'

        row = layout.row()
        row.active = mat.use_transparency or not mat_type
        row.prop(mat, "transparency_method", expand=True)

        layout.separator()

        split = layout.split()
        col = split.column()

        col.prop(mat, "use_raytrace")
        col.prop(mat, "use_full_oversampling")
        sub = col.column()
        sub.active = mat_type
        sub.prop(mat, "use_sky")
        sub.prop(mat, "invert_z")
        col.prop(mat, "pass_index")

        col = split.column()
        col.active = mat_type

        col.prop(mat, "use_cast_shadows", text="Cast")
        col.prop(mat, "use_cast_shadows_only", text="Cast Only")
        col.prop(mat, "use_cast_buffer_shadows")
        sub = col.column()
        sub.active = mat.use_cast_buffer_shadows
        sub.prop(mat, "shadow_cast_alpha", text="Casting Alpha")
        col.prop(mat, "use_cast_approximate")


class MATERIAL_PT_diffuse(MaterialButtonsPanel, Panel):
    bl_label = "Diffuse"

    @classmethod
    def poll(cls, context):
        mat = context.material
        return check_material(mat) and (mat.type in {'SURFACE', 'WIRE'})

    def draw(self, context):
        layout = self.layout

        mat = active_node_mat(context.material)

        split = layout.split()

        col = split.column()
        col.prop(mat, "diffuse_color", text="")
        sub = col.column()
        sub.active = (not mat.use_shadeless)
        sub.prop(mat, "diffuse_intensity", text="Intensity")

        col = split.column()
        col.active = (not mat.use_shadeless)
        col.prop(mat, "diffuse_shader", text="")
        col.prop(mat, "use_diffuse_ramp", text="Ramp")

        col = layout.column()
        col.active = (not mat.use_shadeless)
        if mat.diffuse_shader == 'OREN_NAYAR':
            col.prop(mat, "roughness")
        elif mat.diffuse_shader == 'MINNAERT':
            col.prop(mat, "darkness")
        elif mat.diffuse_shader == 'TOON':
            row = col.row()
            row.prop(mat, "diffuse_toon_size", text="Size")
            row.prop(mat, "diffuse_toon_smooth", text="Smooth")
        elif mat.diffuse_shader == 'FRESNEL':
            row = col.row()
            row.prop(mat, "diffuse_fresnel", text="Fresnel")
            row.prop(mat, "diffuse_fresnel_factor", text="Factor")

        if mat.use_diffuse_ramp:
            col = layout.column()
            col.active = (not mat.use_shadeless)
            col.separator()
            col.template_color_ramp(mat, "diffuse_ramp", expand=True)
            col.separator()

            row = col.row()
            row.prop(mat, "diffuse_ramp_input", text="Input")
            row.prop(mat, "diffuse_ramp_blend", text="Blend")

            col.prop(mat, "diffuse_ramp_factor", text="Factor")


class MATERIAL_PT_specular(MaterialButtonsPanel, Panel):
    bl_label = "Specular"

    @classmethod
    def poll(cls, context):
        mat = context.material
        return check_material(mat) and (mat.type in {'SURFACE', 'WIRE'})

    def draw(self, context):
        layout = self.layout

        mat = active_node_mat(context.material)

        layout.active = (not mat.use_shadeless)

        split = layout.split()

        col = split.column()
        col.prop(mat, "specular_color", text="")
        col.prop(mat, "specular_intensity", text="Intensity")

        col = split.column()
        col.prop(mat, "specular_shader", text="")
        col.prop(mat, "use_specular_ramp", text="Ramp")

        col = layout.column()
        if mat.specular_shader in {'COOKTORR', 'PHONG'}:
            col.prop(mat, "specular_hardness", text="Hardness")
        elif mat.specular_shader == 'BLINN':
            row = col.row()
            row.prop(mat, "specular_hardness", text="Hardness")
            row.prop(mat, "specular_ior", text="IOR")
        elif mat.specular_shader == 'WARDISO':
            col.prop(mat, "specular_slope", text="Slope")
        elif mat.specular_shader == 'TOON':
            row = col.row()
            row.prop(mat, "specular_toon_size", text="Size")
            row.prop(mat, "specular_toon_smooth", text="Smooth")

        if mat.use_specular_ramp:
            layout.separator()
            layout.template_color_ramp(mat, "specular_ramp", expand=True)
            layout.separator()

            row = layout.row()
            row.prop(mat, "specular_ramp_input", text="Input")
            row.prop(mat, "specular_ramp_blend", text="Blend")

            layout.prop(mat, "specular_ramp_factor", text="Factor")


class MATERIAL_PT_shading(MaterialButtonsPanel, Panel):
    bl_label = "Shading"

    @classmethod
    def poll(cls, context):
        mat = context.material
        return check_material(mat) and (mat.type in {'SURFACE', 'WIRE'})

    def draw(self, context):
        layout = self.layout

        mat = active_node_mat(context.material)

        if mat.type in {'SURFACE', 'WIRE'}:
            split = layout.split()

            col = split.column()
            sub = col.column()
            sub.active = not mat.use_shadeless
            sub.prop(mat, "emit")
            sub.prop(mat, "ambient")
            sub = col.column()
            sub.prop(mat, "translucency")

            col = split.column()
            col.prop(mat, "use_shadeless")
            sub = col.column()
            sub.active = not mat.use_shadeless
            sub.prop(mat, "use_tangent_shading")
            sub.prop(mat, "use_cubic")


class MATERIAL_PT_transp(MaterialButtonsPanel, Panel):
    bl_label = "Transparency"

    @classmethod
    def poll(cls, context):
        mat = context.material
        return check_material(mat) and (mat.type in {'SURFACE', 'WIRE'})

    def draw_header(self, context):
        mat = context.material

        if simple_material(mat):
            self.layout.prop(mat, "use_transparency", text="")

    def draw(self, context):
        layout = self.layout

        base_mat = context.material
        mat = active_node_mat(context.material)
        rayt = mat.raytrace_transparency

        if simple_material(base_mat):
            row = layout.row()
            row.active = mat.use_transparency
            row.prop(mat, "transparency_method", expand=True)

        split = layout.split()
        split.active = base_mat.use_transparency

        col = split.column()
        col.prop(mat, "alpha")
        row = col.row()
        row.active = (base_mat.transparency_method != 'MASK') and (not mat.use_shadeless)
        row.prop(mat, "specular_alpha", text="Specular")

        col = split.column()
        col.active = (not mat.use_shadeless)
        col.prop(rayt, "fresnel")
        sub = col.column()
        sub.active = (rayt.fresnel > 0.0)
        sub.prop(rayt, "fresnel_factor", text="Blend")

        if base_mat.transparency_method == 'RAYTRACE':
            layout.separator()
            split = layout.split()
            split.active = base_mat.use_transparency

            col = split.column()
            col.prop(rayt, "ior")
            col.prop(rayt, "filter")
            col.prop(rayt, "falloff")
            col.prop(rayt, "depth_max")
            col.prop(rayt, "depth")

            col = split.column()
            col.label(text="Gloss:")
            col.prop(rayt, "gloss_factor", text="Amount")
            sub = col.column()
            sub.active = rayt.gloss_factor < 1.0
            sub.prop(rayt, "gloss_threshold", text="Threshold")
            sub.prop(rayt, "gloss_samples", text="Samples")


class MATERIAL_PT_mirror(MaterialButtonsPanel, Panel):
    bl_label = "Mirror"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        mat = context.material
        return check_material(mat) and (mat.type in {'SURFACE', 'WIRE'})

    def draw_header(self, context):
        raym = active_node_mat(context.material).raytrace_mirror

        self.layout.prop(raym, "use", text="")

    def draw(self, context):
        layout = self.layout

        mat = active_node_mat(context.material)
        raym = mat.raytrace_mirror

        layout.active = raym.use

        split = layout.split()

        col = split.column()
        col.prop(raym, "reflect_factor")
        col.prop(mat, "mirror_color", text="")

        col = split.column()
        col.prop(raym, "fresnel")
        sub = col.column()
        sub.active = (raym.fresnel > 0.0)
        sub.prop(raym, "fresnel_factor", text="Blend")

        split = layout.split()

        col = split.column()
        col.separator()
        col.prop(raym, "depth")
        col.prop(raym, "distance", text="Max Dist")
        col.separator()
        sub = col.split(percentage=0.4)
        sub.active = (raym.distance > 0.0)
        sub.label(text="Fade To:")
        sub.prop(raym, "fade_to", text="")

        col = split.column()
        col.label(text="Gloss:")
        col.prop(raym, "gloss_factor", text="Amount")
        sub = col.column()
        sub.active = (raym.gloss_factor < 1.0)
        sub.prop(raym, "gloss_threshold", text="Threshold")
        sub.prop(raym, "gloss_samples", text="Samples")
        sub.prop(raym, "gloss_anisotropic", text="Anisotropic")


class MATERIAL_PT_physics(MaterialButtonsPanel, Panel):
    bl_label = "Physics"

    def draw_header(self, context):
        game = context.material.game_settings
        self.layout.prop(game, "physics", text="")

    @classmethod
    def poll(cls, context):
        return context.material

    def draw(self, context):
        layout = self.layout
        layout.active = context.material.game_settings.physics

        phys = context.material.physics  # don't use node material

        split = layout.split()
        row = split.row()
        row.prop(phys, "friction")
        row.prop(phys, "elasticity", slider=True)

        row = layout.row()
        row.label(text="Force Field:")

        row = layout.row()
        row.prop(phys, "fh_force")
        row.prop(phys, "fh_damping", slider=True)

        row = layout.row()
        row.prop(phys, "fh_distance")
        row.prop(phys, "use_fh_normal")


class MATERIAL_PT_options(MaterialButtonsPanel, Panel):
    bl_label = "Options"

    @classmethod
    def poll(cls, context):
        mat = context.material
        return check_material(mat) and (mat.type in {'SURFACE', 'WIRE'})

    def draw(self, context):
        layout = self.layout

        base_mat = context.material
        mat = active_node_mat(base_mat)

        split = layout.split()

        col = split.column()
        if simple_material(base_mat):
            col.prop(mat, "use_raytrace")
            col.prop(mat, "use_full_oversampling")
            col.prop(mat, "use_sky")
        col.prop(mat, "use_mist")
        if simple_material(base_mat):
            col.prop(mat, "invert_z")
            sub = col.row()
            sub.prop(mat, "offset_z")
            sub.active = mat.use_transparency and mat.transparency_method == 'Z_TRANSPARENCY'
        sub = col.column(align=True)
        sub.label(text="Light Group:")
        sub.prop(mat, "light_group", text="")
        row = sub.row(align=True)
        row.active = bool(mat.light_group)
        row.prop(mat, "use_light_group_exclusive", text="Exclusive")
        row.prop(mat, "use_light_group_local", text="Local")

        col = split.column()
        col.prop(mat, "use_face_texture")
        sub = col.column()
        sub.active = mat.use_face_texture
        sub.prop(mat, "use_face_texture_alpha")
        col.separator()
        col.prop(mat, "use_vertex_color_paint")
        col.prop(mat, "use_vertex_color_light")
        col.prop(mat, "use_object_color")
        col.prop(mat, "use_uv_project")
        if simple_material(base_mat):
            col.prop(mat, "pass_index")

class MATERIAL_PT_transp_game(MaterialButtonsPanel, Panel):
    bl_label = "Transparency"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        mat = context.material
        return check_material(mat)

    def draw_header(self, context):
        mat = context.material

        if simple_material(mat):
            self.layout.prop(mat, "use_transparency", text="")

    def draw(self, context):
        layout = self.layout
        base_mat = context.material
        mat = active_node_mat(base_mat)

        layout.active = mat.use_transparency

        if simple_material(base_mat):
            row = layout.row()
            row.prop(mat, "transparency_method", expand=True)

        layout.prop(mat, "alpha")
        layout.prop(mat, "specular_alpha", text="Specular")


class VolumeButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "material"

    @classmethod
    def poll(cls, context):
        mat = context.material
        return mat and (mat.type == 'VOLUME')

class MATERIAL_PT_custom_props(MaterialButtonsPanel, PropertyPanel, Panel):
    _context_path = "material"
    _property_type = bpy.types.Material


classes = (
    MATERIAL_MT_specials,
    MATERIAL_UL_matslots,
    MATERIAL_PT_context_material,
    MATERIAL_PT_preview,
    MATERIAL_PT_pipeline,
    MATERIAL_PT_diffuse,
    MATERIAL_PT_specular,
    MATERIAL_PT_shading,
    MATERIAL_PT_transp,
    MATERIAL_PT_mirror,
    MATERIAL_PT_physics,
    MATERIAL_PT_options,
    MATERIAL_PT_custom_props,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
