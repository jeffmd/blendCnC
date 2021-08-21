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

class View3DPanel:
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'TOOLS'


# **************** standard tool clusters ******************

# ********** default tools for object-mode ****************


class VIEW3D_PT_tools_transform(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "objectmode"
    bl_label = "Transform"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.operator("transform.translate")
        col.operator("transform.rotate")
        col.operator("transform.resize", text="Scale")

        col = layout.column(align=True)
        col.operator("transform.mirror", text="Mirror")


class VIEW3D_PT_tools_object(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "objectmode"
    bl_label = "Edit"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.operator("object.duplicate_move", text="Duplicate")
        col.operator("object.duplicate_move_linked", text="Duplicate Linked")

        col.operator("object.delete")

        obj = context.active_object
        if obj:
            obj_type = obj.type

            if obj_type in {'MESH', 'CURVE', 'SURFACE'}:
                col = layout.column(align=True)
                col.operator("object.join")

            if obj_type in {'MESH', 'CURVE', 'SURFACE', 'FONT', 'LATTICE'}:
                col = layout.column(align=True)
                col.operator_menu_enum("object.origin_set", "type", text="Set Origin")

            if obj_type in {'MESH', 'CURVE', 'SURFACE'}:
                col = layout.column(align=True)
                col.label(text="Shading:")
                row = col.row(align=True)
                row.operator("object.shade_smooth", text="Smooth")
                row.operator("object.shade_flat", text="Flat")

            if obj_type == 'MESH':
                col = layout.column(align=True)
                col.label(text="Data Transfer:")
                row = col.row(align=True)
                row.operator("object.data_transfer", text="Data")
                row.operator("object.datalayout_transfer", text="Data Layout")


class VIEW3D_PT_tools_add_object(View3DPanel, Panel):
    bl_category = "Create"
    bl_context = "objectmode"
    bl_label = "Add Primitive"

    @staticmethod
    def draw_add_mesh(layout, label=False):
        if label:
            layout.label(text="Primitives:")
        layout.operator("mesh.primitive_plane_add", text="Plane", icon='MESH_PLANE')
        layout.operator("mesh.primitive_cube_add", text="Cube", icon='MESH_CUBE')
        layout.operator("mesh.primitive_circle_add", text="Circle", icon='MESH_CIRCLE')
        layout.operator("mesh.primitive_uv_sphere_add", text="UV Sphere", icon='MESH_UVSPHERE')
        layout.operator("mesh.primitive_ico_sphere_add", text="Ico Sphere", icon='MESH_ICOSPHERE')
        layout.operator("mesh.primitive_cylinder_add", text="Cylinder", icon='MESH_CYLINDER')
        layout.operator("mesh.primitive_cone_add", text="Cone", icon='MESH_CONE')
        layout.operator("mesh.primitive_torus_add", text="Torus", icon='MESH_TORUS')

        if label:
            layout.label(text="Special:")
        else:
            layout.separator()
        layout.operator("mesh.primitive_grid_add", text="Grid", icon='MESH_GRID')
        layout.operator("mesh.primitive_monkey_add", text="Monkey", icon='MESH_MONKEY')

    @staticmethod
    def draw_add_curve(layout, label=False):

        if label:
            layout.label(text="Bezier:")
        layout.operator("curve.primitive_bezier_curve_add", text="Bezier", icon='CURVE_BEZCURVE')
        layout.operator("curve.primitive_bezier_circle_add", text="Circle", icon='CURVE_BEZCIRCLE')

        if label:
            layout.label(text="Nurbs:")
        else:
            layout.separator()
        layout.operator("curve.primitive_nurbs_curve_add", text="Nurbs Curve", icon='CURVE_NCURVE')
        layout.operator("curve.primitive_nurbs_circle_add", text="Nurbs Circle", icon='CURVE_NCIRCLE')
        layout.operator("curve.primitive_nurbs_path_add", text="Path", icon='CURVE_PATH')

        layout.separator()

        layout.operator("curve.draw", icon='LINE_DATA')

    @staticmethod
    def draw_add_surface(layout):
        layout.operator("surface.primitive_nurbs_surface_curve_add", text="Nurbs Curve", icon='SURFACE_NCURVE')
        layout.operator("surface.primitive_nurbs_surface_circle_add", text="Nurbs Circle", icon='SURFACE_NCIRCLE')
        layout.operator("surface.primitive_nurbs_surface_surface_add", text="Nurbs Surface", icon='SURFACE_NSURFACE')
        layout.operator("surface.primitive_nurbs_surface_cylinder_add", text="Nurbs Cylinder", icon='SURFACE_NCYLINDER')
        layout.operator("surface.primitive_nurbs_surface_sphere_add", text="Nurbs Sphere", icon='SURFACE_NSPHERE')
        layout.operator("surface.primitive_nurbs_surface_torus_add", text="Nurbs Torus", icon='SURFACE_NTORUS')

    @staticmethod
    def draw_add_mball(layout):
        layout.operator_enum("object.metaball_add", "type")

    @staticmethod
    def draw_add_lamp(layout):
        layout.operator_enum("object.lamp_add", "type")

    @staticmethod
    def draw_add_other(layout):
        layout.operator("object.text_add", text="Text", icon='OUTLINER_OB_FONT')
        layout.operator("object.armature_add", text="Armature", icon='OUTLINER_OB_ARMATURE')
        layout.operator("object.add", text="Lattice", icon='OUTLINER_OB_LATTICE').type = 'LATTICE'
        layout.operator("object.empty_add", text="Empty", icon='OUTLINER_OB_EMPTY').type = 'PLAIN_AXES'
        layout.operator("object.speaker_add", text="Speaker", icon='OUTLINER_OB_SPEAKER')
        layout.operator("object.camera_add", text="Camera", icon='OUTLINER_OB_CAMERA')

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.label(text="Mesh:")
        self.draw_add_mesh(col)

        col = layout.column(align=True)
        col.label(text="Curve:")
        self.draw_add_curve(col)

        # not used here:
        # draw_add_surface
        # draw_add_mball

        col = layout.column(align=True)
        col.label(text="Lamp:")
        self.draw_add_lamp(col)

        col = layout.column(align=True)
        col.label(text="Other:")
        self.draw_add_other(col)


class VIEW3D_PT_tools_relations(View3DPanel, Panel):
    bl_category = "Relations"
    bl_context = "objectmode"
    bl_label = "Relations"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)

        col.label(text="Group:")
        col.operator("group.create", text="New Group")
        col.operator("group.objects_add_active", text="Add to Active")
        col.operator("group.objects_remove", text="Remove from Group")

        col.separator()

        col.label(text="Parent:")
        row = col.row(align=True)
        row.operator("object.parent_set", text="Set")
        row.operator("object.parent_clear", text="Clear")

        col.separator()

        col.label(text="Object Data:")
        col.operator("object.make_links_data")
        col.operator("object.make_single_user")

        col.separator()

        col.label(text="Linked Objects:")
        col.operator("object.make_local")
        col.operator("object.proxy_make")


class VIEW3D_PT_tools_animation(View3DPanel, Panel):
    bl_category = "Animation"
    bl_context = "objectmode"
    bl_label = "Animation"

    def draw(self, context):
        layout = self.layout

        ob = context.active_object
        mpath = ob.motion_path if ob else None

        draw_keyframing_tools(context, layout)

        col = layout.column(align=True)
        col.label(text="Motion Paths:")
        if mpath:
            row = col.row(align=True)
            row.operator("object.paths_update", text="Update")
            row.operator("object.paths_clear", text="", icon='X')
        else:
            col.operator("object.paths_calculate", text="Calculate")

        col.separator()

        col.label(text="Action:")
        col.operator("nla.bake", text="Bake Action")


class VIEW3D_PT_tools_rigid_body(View3DPanel, Panel):
    bl_category = "Physics"
    bl_context = "objectmode"
    bl_label = "Rigid Body Tools"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.label(text="Add/Remove:")
        row = col.row(align=True)
        row.operator("rigidbody.objects_add", text="Add Active").type = 'ACTIVE'
        row.operator("rigidbody.objects_add", text="Add Passive").type = 'PASSIVE'
        row = col.row(align=True)
        row.operator("rigidbody.objects_remove", text="Remove")

        col = layout.column(align=True)
        col.label(text="Object Tools:")
        col.operator("rigidbody.shape_change", text="Change Shape")
        col.operator("rigidbody.mass_calculate", text="Calculate Mass")
        col.operator("rigidbody.object_settings_copy", text="Copy from Active")
        col.operator("object.visual_transform_apply", text="Apply Transformation")
        col.operator("rigidbody.bake_to_keyframes", text="Bake To Keyframes")
        col.label(text="Constraints:")
        col.operator("rigidbody.connect", text="Connect")


# ********** default tools for editmode_mesh ****************

class VIEW3D_PT_tools_transform_mesh(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "mesh_edit"
    bl_label = "Transform"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.operator("transform.translate")
        col.operator("transform.rotate")
        col.operator("transform.resize", text="Scale")
        col.operator("transform.shrink_fatten", text="Shrink/Fatten")
        col.operator("transform.push_pull", text="Push/Pull")


class VIEW3D_PT_tools_meshedit(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "mesh_edit"
    bl_label = "Mesh Tools"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.label(text="Deform:")
        row = col.row(align=True)
        row.operator("transform.edge_slide", text="Slide Edge")
        row.operator("transform.vert_slide", text="Vertex")
        col.operator("mesh.noise")
        col.operator("mesh.vertices_smooth")
        col.operator("transform.vertex_random")

        col = layout.column(align=True)
        col.label(text="Add:")

        col.menu("VIEW3D_MT_edit_mesh_extrude")
        col.operator("view3d.edit_mesh_extrude_move_normal", text="Extrude Region")
        col.operator("view3d.edit_mesh_extrude_individual_move", text="Extrude Individual")
        col.operator("mesh.inset", text="Inset Faces")
        col.operator("mesh.edge_face_add")
        col.operator("mesh.subdivide")
        col.operator("mesh.loopcut_slide")
        col.operator("mesh.offset_edge_loops_slide")
        col.operator("mesh.duplicate_move", text="Duplicate")
        row = col.row(align=True)
        row.operator("mesh.spin")
        row.operator("mesh.screw")

        row = col.row(align=True)
        props = row.operator("mesh.knife_tool", text="Knife")
        props.use_occlude_geometry = True
        props.only_selected = False
        props = row.operator("mesh.knife_tool", text="Select")
        props.use_occlude_geometry = False
        props.only_selected = True
        col.operator("mesh.knife_project")
        col.operator("mesh.bisect")

        col = layout.column(align=True)
        col.label(text="Remove:")
        col.menu("VIEW3D_MT_edit_mesh_delete")
        col.operator_menu_enum("mesh.merge", "type")
        col.operator("mesh.remove_doubles")


class VIEW3D_PT_tools_meshweight(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "mesh_edit"
    bl_label = "Weight Tools"
    bl_options = {'DEFAULT_CLOSED'}

    # Used for Weight-Paint mode and Edit-Mode
    @staticmethod
    def draw_generic(layout):
        col = layout.column()
        col.operator("object.vertex_group_normalize_all", text="Normalize All")
        col.operator("object.vertex_group_normalize", text="Normalize")
        col.operator("object.vertex_group_mirror", text="Mirror")
        col.operator("object.vertex_group_invert", text="Invert")
        col.operator("object.vertex_group_clean", text="Clean")
        col.operator("object.vertex_group_quantize", text="Quantize")
        col.operator("object.vertex_group_levels", text="Levels")
        col.operator("object.vertex_group_smooth", text="Smooth")
        col.operator("object.vertex_group_limit_total", text="Limit Total")
        col.operator("object.vertex_group_fix", text="Fix Deforms")

    def draw(self, context):
        layout = self.layout
        self.draw_generic(layout)


class VIEW3D_PT_tools_add_mesh_edit(View3DPanel, Panel):
    bl_category = "Create"
    bl_context = "mesh_edit"
    bl_label = "Add Meshes"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)

        VIEW3D_PT_tools_add_object.draw_add_mesh(col, label=True)


class VIEW3D_PT_tools_shading(View3DPanel, Panel):
    bl_category = "Shading / UVs"
    bl_context = "mesh_edit"
    bl_label = "Shading"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.label(text="Faces:")
        row = col.row(align=True)
        row.operator("mesh.faces_shade_smooth", text="Smooth")
        row.operator("mesh.faces_shade_flat", text="Flat")
        col.label(text="Edges:")
        row = col.row(align=True)
        row.operator("mesh.mark_sharp", text="Smooth").clear = True
        row.operator("mesh.mark_sharp", text="Sharp")
        col.label(text="Vertices:")
        row = col.row(align=True)
        props = row.operator("mesh.mark_sharp", text="Smooth")
        props.use_verts = True
        props.clear = True
        row.operator("mesh.mark_sharp", text="Sharp").use_verts = True

        col = layout.column(align=True)
        col.label(text="Normals:")
        col.operator("mesh.normals_make_consistent", text="Recalculate")
        col.operator("mesh.flip_normals", text="Flip Direction")
        col.operator("mesh.set_normals_from_faces", text="Set From Faces")


class VIEW3D_PT_tools_uvs(View3DPanel, Panel):
    bl_category = "Shading / UVs"
    bl_context = "mesh_edit"
    bl_label = "UVs"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.label(text="UV Mapping:")
        col.menu("VIEW3D_MT_uv_map", text="Unwrap")
        col.operator("mesh.mark_seam").clear = False
        col.operator("mesh.mark_seam", text="Clear Seam").clear = True


class VIEW3D_PT_tools_meshedit_options(View3DPanel, Panel):
    bl_category = "Options"
    bl_context = "mesh_edit"
    bl_label = "Mesh Options"

    @classmethod
    def poll(cls, context):
        return context.active_object

    def draw(self, context):
        layout = self.layout

        ob = context.active_object

        tool_settings = context.tool_settings
        mesh = ob.data

        col = layout.column(align=True)
        col.prop(mesh, "use_mirror_x")

        row = col.row(align=True)
        row.active = ob.data.use_mirror_x
        row.prop(mesh, "use_mirror_topology")

        col = layout.column(align=True)
        col.label("Edge Select Mode:")
        col.prop(tool_settings, "edge_path_mode", text="")
        col.prop(tool_settings, "edge_path_live_unwrap")
        col.label("Double Threshold:")
        col.prop(tool_settings, "double_threshold", text="")

        if mesh.show_weight:
            col.label("Show Zero Weights:")
            col.row().prop(tool_settings, "vertex_group_user", expand=True)

# ********** default tools for editmode_curve ****************


class VIEW3D_PT_tools_transform_curve(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "curve_edit"
    bl_label = "Transform"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.operator("transform.translate")
        col.operator("transform.rotate")
        col.operator("transform.resize", text="Scale")

        col = layout.column(align=True)
        col.operator("transform.tilt", text="Tilt")
        col.operator("transform.transform", text="Shrink/Fatten").mode = 'CURVE_SHRINKFATTEN'


class VIEW3D_PT_tools_curveedit(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "curve_edit"
    bl_label = "Curve Tools"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.label(text="Curve:")
        col.operator("curve.duplicate_move", text="Duplicate")
        col.operator("curve.delete")
        col.operator("curve.cyclic_toggle")
        col.operator("curve.switch_direction")
        col.operator("curve.spline_type_set")
        col.operator("curve.radius_set")

        col = layout.column(align=True)
        col.label(text="Handles:")
        row = col.row(align=True)
        row.operator("curve.handle_type_set", text="Auto").type = 'AUTOMATIC'
        row.operator("curve.handle_type_set", text="Vector").type = 'VECTOR'
        row = col.row(align=True)
        row.operator("curve.handle_type_set", text="Align").type = 'ALIGNED'
        row.operator("curve.handle_type_set", text="Free").type = 'FREE_ALIGN'

        col = layout.column(align=True)
        col.operator("curve.normals_make_consistent")

        col = layout.column(align=True)
        col.label(text="Modeling:")
        col.operator("curve.extrude_move", text="Extrude")
        col.operator("curve.subdivide")
        col.operator("curve.smooth")
        col.operator("transform.vertex_random")


class VIEW3D_PT_tools_add_curve_edit(View3DPanel, Panel):
    bl_category = "Create"
    bl_context = "curve_edit"
    bl_label = "Add Curves"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)

        VIEW3D_PT_tools_add_object.draw_add_curve(col, label=True)


class VIEW3D_PT_tools_curveedit_options_stroke(View3DPanel, Panel):
    bl_category = "Options"
    bl_context = "curve_edit"
    bl_label = "Curve Stroke"

    def draw(self, context):
        layout = self.layout

        tool_settings = context.tool_settings
        cps = tool_settings.curve_paint_settings

        col = layout.column()

        col.prop(cps, "curve_type")

        if cps.curve_type == 'BEZIER':
            col.label("Bezier Options:")
            col.prop(cps, "error_threshold")
            col.prop(cps, "fit_method")
            col.prop(cps, "use_corners_detect")

            col = layout.column()
            col.active = cps.use_corners_detect
            col.prop(cps, "corner_angle")

        col.label("Pressure Radius:")
        row = layout.row(align=True)
        rowsub = row.row(align=True)
        rowsub.prop(cps, "radius_min", text="Min")
        rowsub.prop(cps, "radius_max", text="Max")

        row.prop(cps, "use_pressure_radius", text="", icon_only=True)

        col = layout.column()
        col.label("Taper Radius:")
        row = layout.row(align=True)
        row.prop(cps, "radius_taper_start", text="Start")
        row.prop(cps, "radius_taper_end", text="End")

        col = layout.column()
        col.label("Projection Depth:")
        row = layout.row(align=True)
        row.prop(cps, "depth_mode", expand=True)

        col = layout.column()
        if cps.depth_mode == 'SURFACE':
            col.prop(cps, "surface_offset")
            col.prop(cps, "use_offset_absolute")
            col.prop(cps, "use_stroke_endpoints")
            if cps.use_stroke_endpoints:
                colsub = layout.column(align=True)
                colsub.prop(cps, "surface_plane", expand=True)


# ********** default tools for editmode_surface ****************

class VIEW3D_PT_tools_transform_surface(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "surface_edit"
    bl_label = "Transform"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.operator("transform.translate")
        col.operator("transform.rotate")
        col.operator("transform.resize", text="Scale")


class VIEW3D_PT_tools_surfaceedit(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "surface_edit"
    bl_label = "Surface Tools"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.label(text="Curve:")
        col.operator("curve.duplicate_move", text="Duplicate")
        col.operator("curve.delete")
        col.operator("curve.cyclic_toggle")
        col.operator("curve.switch_direction")

        col = layout.column(align=True)
        col.label(text="Modeling:")
        col.operator("curve.extrude", text="Extrude")
        col.operator("curve.spin")
        col.operator("curve.subdivide")

        col = layout.column(align=True)
        col.label(text="Deform:")
        col.operator("transform.vertex_random")


class VIEW3D_PT_tools_add_surface_edit(View3DPanel, Panel):
    bl_category = "Create"
    bl_context = "surface_edit"
    bl_label = "Add Surfaces"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)

        VIEW3D_PT_tools_add_object.draw_add_surface(col)


# ********** default tools for editmode_text ****************


class VIEW3D_PT_tools_textedit(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "text_edit"
    bl_label = "Text Tools"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.label(text="Set Case:")
        col.operator("font.case_set", text="To Upper").case = 'UPPER'
        col.operator("font.case_set", text="To Lower").case = 'LOWER'

        col = layout.column(align=True)
        col.label(text="Style:")
        col.operator("font.style_toggle", text="Bold").style = 'BOLD'
        col.operator("font.style_toggle", text="Italic").style = 'ITALIC'
        col.operator("font.style_toggle", text="Underline").style = 'UNDERLINE'


# ********** default tools for editmode_armature ****************


class VIEW3D_PT_tools_armatureedit_transform(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "armature_edit"
    bl_label = "Transform"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.operator("transform.translate")
        col.operator("transform.rotate")
        col.operator("transform.resize", text="Scale")


class VIEW3D_PT_tools_armatureedit(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "armature_edit"
    bl_label = "Armature Tools"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.label(text="Bones:")
        col.operator("armature.bone_primitive_add", text="Add")
        col.operator("armature.duplicate_move", text="Duplicate")
        col.operator("armature.delete", text="Delete")

        col = layout.column(align=True)
        col.label(text="Modeling:")
        col.operator("armature.extrude_move")
        col.operator("armature.subdivide", text="Subdivide")

        col = layout.column(align=True)
        col.label(text="Deform:")
        col.operator("transform.vertex_random")


class VIEW3D_PT_tools_armatureedit_options(View3DPanel, Panel):
    bl_category = "Options"
    bl_context = "armature_edit"
    bl_label = "Armature Options"

    def draw(self, context):
        arm = context.active_object.data

        self.layout.prop(arm, "use_mirror_x")


# ********** default tools for editmode_mball ****************


class VIEW3D_PT_tools_mballedit(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "mball_edit"
    bl_label = "Meta Tools"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.label(text="Transform:")
        col.operator("transform.translate")
        col.operator("transform.rotate")
        col.operator("transform.resize", text="Scale")

        col = layout.column(align=True)
        col.label(text="Deform:")
        col.operator("transform.vertex_random")


class VIEW3D_PT_tools_add_mball_edit(View3DPanel, Panel):
    bl_category = "Create"
    bl_context = "mball_edit"
    bl_label = "Add Metaball"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)

        VIEW3D_PT_tools_add_object.draw_add_mball(col)


# ********** default tools for editmode_lattice ****************


class VIEW3D_PT_tools_latticeedit(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "lattice_edit"
    bl_label = "Lattice Tools"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.label(text="Transform:")
        col.operator("transform.translate")
        col.operator("transform.rotate")
        col.operator("transform.resize", text="Scale")

        col = layout.column(align=True)
        col.operator("lattice.make_regular")

        col = layout.column(align=True)
        col.label(text="Deform:")
        col.operator("transform.vertex_random")


# ********** default tools for pose-mode ****************


class VIEW3D_PT_tools_posemode(View3DPanel, Panel):
    bl_category = "Tools"
    bl_context = "posemode"
    bl_label = "Pose Tools"

    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.label(text="Transform:")
        col.operator("transform.translate")
        col.operator("transform.rotate")
        col.operator("transform.resize", text="Scale")

        col = layout.column(align=True)
        col.label(text="In-Between:")
        row = col.row(align=True)
        row.operator("pose.push", text="Push")
        row.operator("pose.relax", text="Relax")
        col.operator("pose.breakdown", text="Breakdowner")

        col = layout.column(align=True)
        col.label(text="Pose:")
        row = col.row(align=True)
        row.operator("pose.copy", text="Copy")
        row.operator("pose.paste", text="Paste")

        row = layout.row(align=True)
        row.operator("pose.propagate", text="Propagate")
        row.menu("VIEW3D_MT_pose_propagate", icon='TRIA_RIGHT', text="")

        col = layout.column(align=True)
        col.operator("poselib.pose_add", text="Add To Library")

        draw_keyframing_tools(context, layout)

        ob = context.object
        avs = ob.pose.animation_visualization

        col = layout.column(align=True)
        col.label(text="Motion Paths:")
        if avs.motion_path.has_motion_paths:
            row = col.row(align=True)
            row.operator("pose.paths_update", text="Update")
            row.operator("pose.paths_clear", text="", icon='X')
        else:
            col.operator("pose.paths_calculate", text="Calculate")


class VIEW3D_PT_tools_posemode_options(View3DPanel, Panel):
    bl_category = "Options"
    bl_context = "posemode"
    bl_label = "Pose Options"

    def draw(self, context):
        arm = context.active_object.data

        self.layout.prop(arm, "use_auto_ik")

# ********** default tools for paint modes ****************


# Note: moved here so that it's always in last position in 'Tools' panels!
class VIEW3D_PT_tools_history(View3DPanel, Panel):
    bl_category = "Tools"
    # No bl_context, we are always available!
    bl_label = "History"
    bl_options = {'DEFAULT_CLOSED'}

    def draw(self, context):
        layout = self.layout
        obj = context.object

        col = layout.column(align=True)
        row = col.row(align=True)
        row.operator("ed.undo")
        row.operator("ed.redo")
        if obj is None:
            col.operator("ed.undo_history")

        col = layout.column(align=True)
        col.label(text="Repeat:")
        col.operator("screen.repeat_last")
        col.operator("screen.repeat_history", text="History...")


classes = (
    VIEW3D_PT_tools_transform,
    VIEW3D_PT_tools_object,
    VIEW3D_PT_tools_add_object,
    VIEW3D_PT_tools_relations,
    VIEW3D_PT_tools_rigid_body,
    VIEW3D_PT_tools_transform_mesh,
    VIEW3D_PT_tools_meshedit,
    VIEW3D_PT_tools_meshweight,
    VIEW3D_PT_tools_add_mesh_edit,
    VIEW3D_PT_tools_shading,
    VIEW3D_PT_tools_meshedit_options,
    VIEW3D_PT_tools_transform_curve,
    VIEW3D_PT_tools_curveedit,
    VIEW3D_PT_tools_add_curve_edit,
    VIEW3D_PT_tools_curveedit_options_stroke,
    VIEW3D_PT_tools_transform_surface,
    VIEW3D_PT_tools_surfaceedit,
    VIEW3D_PT_tools_add_surface_edit,
    VIEW3D_PT_tools_textedit,
    VIEW3D_PT_tools_latticeedit,
    VIEW3D_PT_tools_history,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
