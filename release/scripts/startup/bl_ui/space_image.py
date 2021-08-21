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
import math
from bpy.types import Header, Menu, Panel
from bpy.app.translations import pgettext_iface as iface_

class IMAGE_MT_view(Menu):
    bl_label = "View"

    def draw(self, context):
        layout = self.layout

        sima = context.space_data
        tool_settings = context.tool_settings

        layout.operator("image.properties", icon='MENU_PANEL')
        layout.operator("image.toolshelf", icon='MENU_PANEL')

        layout.separator()

        layout.prop(sima, "use_realtime_update")

        layout.prop(uv, "show_other_objects")
        layout.prop(uv, "show_metadata")
        layout.separator()

        layout.operator("image.view_zoom_in")
        layout.operator("image.view_zoom_out")

        layout.separator()

        ratios = ((1, 8), (1, 4), (1, 2), (1, 1), (2, 1), (4, 1), (8, 1))

        for a, b in ratios:
            layout.operator(
                "image.view_zoom_ratio",
                text=iface_(f"Zoom {a:d}:{b:d}"),
                translate=False,
            ).ratio = a / b

        layout.separator()

        layout.operator("image.view_all")
        layout.operator("image.view_all", text="View Fit").fit_view = True

        layout.separator()

        layout.operator("screen.area_dupli")
        layout.operator("screen.screen_full_area")
        layout.operator("screen.screen_full_area", text="Toggle Fullscreen Area").use_hide_panels = True

class IMAGE_MT_image(Menu):
    bl_label = "Image"

    def draw(self, context):
        layout = self.layout

        sima = context.space_data
        ima = sima.image

        layout.operator("image.new")
        layout.operator("image.open")

        layout.operator("image.save_dirty", text="Save All Images")

        if ima:
            if not show_render:
                layout.operator("image.replace")
                layout.operator("image.reload")

            layout.operator("image.save")
            layout.operator("image.save_as")
            layout.operator("image.save_as", text="Save a Copy").copy = True

            layout.operator("image.external_edit", "Edit Externally")

            layout.separator()

            layout.menu("IMAGE_MT_image_invert")

            if not ima.packed_file:
                layout.separator()
                layout.operator("image.pack")

                # only for dirty && specific image types, perhaps
                # this could be done in operator poll too
                if ima.is_dirty:
                    if ima.source in {'FILE', 'GENERATED'} and ima.type != 'OPEN_EXR_MULTILAYER':
                        if ima.packed_file:
                            layout.separator()
                        layout.operator("image.pack", text="Pack As PNG").as_png = True


class IMAGE_MT_image_invert(Menu):
    bl_label = "Invert"

    def draw(self, context):
        layout = self.layout

        props = layout.operator("image.invert", text="Invert Image Colors")
        props.invert_r = True
        props.invert_g = True
        props.invert_b = True

        layout.separator()

        layout.operator("image.invert", text="Invert Red Channel").invert_r = True
        layout.operator("image.invert", text="Invert Green Channel").invert_g = True
        layout.operator("image.invert", text="Invert Blue Channel").invert_b = True
        layout.operator("image.invert", text="Invert Alpha Channel").invert_a = True


class IMAGE_HT_header(Header):
    bl_space_type = 'IMAGE_EDITOR'

    def draw(self, context):
        layout = self.layout

        sima = context.space_data
        ima = sima.image
        iuser = sima.image_user
        tool_settings = context.tool_settings
        mode = sima.mode

        row = layout.row(align=True)
        row.template_header()

        layout.template_ID(sima, "image", new="image.new", open="image.open")
        layout.prop(sima, "use_image_pin", text="")

        layout.prop(sima, "mode", text="")

        layout.prop(sima, "pivot_point", icon_only=True)

        if ima:
            # layers
            layout.template_image_layers(ima, iuser)

            # draw options
            row = layout.row(align=True)
            row.prop(sima, "draw_channels", text="", expand=True)

            row = layout.row(align=True)
            if ima.type == 'COMPOSITE':
                row.operator("image.record_composite", icon='REC')
            if ima.type == 'COMPOSITE' and ima.source in {'MOVIE', 'SEQUENCE'}:
                row.operator("image.play_composite", icon='PLAY')

class MASK_MT_editor_menus(Menu):
    bl_idname = "MASK_MT_editor_menus"
    bl_label = ""

    def draw(self, context):
        self.draw_menus(self.layout, context)

    @staticmethod
    def draw_menus(layout, context):
        sima = context.space_data
        ima = sima.image

        show_uvedit = sima.show_uvedit
        show_maskedit = sima.show_maskedit
        show_paint = sima.show_paint

        layout.menu("IMAGE_MT_view")

        if show_uvedit:
            layout.menu("IMAGE_MT_select")
        if show_maskedit:
            layout.menu("MASK_MT_select")
        if show_paint:
            layout.menu("IMAGE_MT_brush")

        if ima and ima.is_dirty:
            layout.menu("IMAGE_MT_image", text="Image*")
        else:
            layout.menu("IMAGE_MT_image", text="Image")

        if show_uvedit:
            layout.menu("IMAGE_MT_uvs")
        if show_maskedit:
            layout.menu("MASK_MT_mask")


class IMAGE_PT_image_properties(Panel):
    bl_space_type = 'IMAGE_EDITOR'
    bl_region_type = 'UI'
    bl_label = "Image"

    @classmethod
    def poll(cls, context):
        sima = context.space_data
        return (sima.image)

    def draw(self, context):
        layout = self.layout

        sima = context.space_data
        iuser = sima.image_user

        layout.template_image(sima, "image", iuser, multiview=True)


class IMAGE_PT_game_properties(Panel):
    bl_space_type = 'IMAGE_EDITOR'
    bl_region_type = 'UI'
    bl_label = "Game Properties"

    @classmethod
    def poll(cls, context):
        sima = context.space_data
        # display even when not in game mode because these settings effect the 3d view
        return (sima and sima.image and not sima.show_maskedit)  # and (rd.engine == 'BLENDER_GAME')

    def draw(self, context):
        layout = self.layout

        sima = context.space_data
        ima = sima.image

        split = layout.split()
        col = split.column()
        col.prop(ima, "use_animation")
        sub = col.column(align=True)
        sub.active = ima.use_animation
        sub.prop(ima, "frame_start", text="Start")
        sub.prop(ima, "frame_end", text="End")
        sub.prop(ima, "fps", text="Speed")

        col = split.column()
        col.prop(ima, "use_tiles")
        sub = col.column(align=True)
        sub.active = ima.use_tiles or ima.use_animation
        sub.prop(ima, "tiles_x", text="X")
        sub.prop(ima, "tiles_y", text="Y")

        split = layout.split()
        col = split.column()
        col.label(text="Clamp:")
        col.prop(ima, "use_clamp_x", text="X")
        col.prop(ima, "use_clamp_y", text="Y")

        col = split.column()
        col.label(text="Mapping:")
        col.prop(ima, "mapping", expand=True)


class IMAGE_PT_view_properties(Panel):
    bl_space_type = 'IMAGE_EDITOR'
    bl_region_type = 'UI'
    bl_label = "Display"

    @classmethod
    def poll(cls, context):
        sima = context.space_data
        return (sima and (sima.image or sima.show_uvedit))

    def draw(self, context):
        layout = self.layout

        sima = context.space_data
        ima = sima.image

        split = layout.split()

        col = split.column()
        if ima:
            col.prop(ima, "display_aspect", text="Aspect Ratio")

            col = split.column()
            col.label(text="Coordinates:")
            col.prop(sima, "show_repeat", text="Repeat")
            if show_uvedit:
                col.prop(uvedit, "show_normalized_coords", text="Normalized")

class ImageScopesPanel:
    @classmethod
    def poll(cls, context):
        sima = context.space_data
        if not (sima and sima.image):
            return False
        # scopes are not updated in paint modes, hide
        if sima.mode == 'PAINT':
            return False
        ob = context.active_object
        if ob and ob.mode in {'TEXTURE_PAINT', 'EDIT'}:
            return False
        return True

class IMAGE_PT_view_histogram(ImageScopesPanel, Panel):
    bl_space_type = 'IMAGE_EDITOR'
    bl_region_type = 'TOOLS'
    bl_label = "Histogram"
    bl_category = "Scopes"

    def draw(self, context):
        layout = self.layout

        sima = context.space_data
        hist = sima.scopes.histogram

        layout.template_histogram(sima.scopes, "histogram")
        row = layout.row(align=True)
        row.prop(hist, "mode", expand=True)
        row.prop(hist, "show_line", text="")


class IMAGE_PT_view_waveform(ImageScopesPanel, Panel):
    bl_space_type = 'IMAGE_EDITOR'
    bl_region_type = 'TOOLS'
    bl_label = "Waveform"
    bl_category = "Scopes"

    def draw(self, context):
        layout = self.layout

        sima = context.space_data

        layout.template_waveform(sima, "scopes")
        row = layout.split(percentage=0.75)
        row.prop(sima.scopes, "waveform_alpha")
        row.prop(sima.scopes, "waveform_mode", text="")


class IMAGE_PT_view_vectorscope(ImageScopesPanel, Panel):
    bl_space_type = 'IMAGE_EDITOR'
    bl_region_type = 'TOOLS'
    bl_label = "Vectorscope"
    bl_category = "Scopes"

    def draw(self, context):
        layout = self.layout

        sima = context.space_data
        layout.template_vectorscope(sima, "scopes")
        layout.prop(sima.scopes, "vectorscope_alpha")


class IMAGE_PT_sample_line(ImageScopesPanel, Panel):
    bl_space_type = 'IMAGE_EDITOR'
    bl_region_type = 'TOOLS'
    bl_label = "Sample Line"
    bl_category = "Scopes"

    def draw(self, context):
        layout = self.layout

        sima = context.space_data
        hist = sima.sample_histogram

        layout.operator("image.sample_line")
        layout.template_histogram(sima, "sample_histogram")
        row = layout.row(align=True)
        row.prop(hist, "mode", expand=True)
        row.prop(hist, "show_line", text="")


class IMAGE_PT_scope_sample(ImageScopesPanel, Panel):
    bl_space_type = 'IMAGE_EDITOR'
    bl_region_type = 'TOOLS'
    bl_label = "Scope Samples"
    bl_category = "Scopes"

    def draw(self, context):
        layout = self.layout

        sima = context.space_data

        row = layout.row()
        row.prop(sima.scopes, "use_full_resolution")
        sub = row.row()
        sub.active = not sima.scopes.use_full_resolution
        sub.prop(sima.scopes, "accuracy")


classes = (
    IMAGE_MT_view,
    IMAGE_MT_image,
    IMAGE_MT_image_invert,
    IMAGE_HT_header,
    IMAGE_PT_image_properties,
    IMAGE_PT_view_properties,
    IMAGE_PT_view_histogram,
    IMAGE_PT_view_waveform,
    IMAGE_PT_view_vectorscope,
    IMAGE_PT_sample_line,
    IMAGE_PT_scope_sample,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
