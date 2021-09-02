# blender CAM __init__.py (c) 2012 Vilem Novak
#
# ***** BEGIN GPL LICENSE BLOCK *****
#
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ***** END GPL LICENCE BLOCK *****

import bpy, bgl,blf 
import mathutils
import math, time
from mathutils import *
from bpy_extras.object_utils import object_data_add
from bpy.props import *
import bl_operators
from bpy.types import Menu, Operator, UIList, AddonPreferences
from cam.nc import getPostProcessorMenuItems


#from . import patterns
#from . import chunk_operations
from cam import ui, ops, utils, simple, polygon_utils_cam#, post_processors
import numpy

from shapely import geometry as sgeometry
from bpy.app.handlers import persistent
import subprocess,os, sys, threading
import pickle
#from .utils import *

bl_info = {
	"name": "CAM - gcode generation tools",
	"author": "Vilem Novak, Jeff Doyle",
	"version": (0, 10, 0),
	"blender": (2, 78, 6),
	"location": "Properties > cnc",
	"description": "Generate machining paths for CNC",
	"warning": "there is no warranty for the gcode produced by this addon",
	"wiki_url": "blendercam.blogspot.com",
	"tracker_url": "",
	"support": 'OFFICIAL',
	"category": "CNC"}
  
  
PRECISION=5


def updateMachine(self,context):
	print('update machine ')
	utils.addMachineAreaObject()
	
def updateMaterial(self,context):
	print('update material')
	utils.addMaterialAreaObject()

def updateHideOtherPaths(self, context):
	scene = context.scene
	uiset = scene.cam_ui_settings

	for _ao in scene.cam_operations:
		if _ao.path_object_name in bpy.data.objects:
			path_obj = bpy.data.objects[_ao.path_object_name]
			if uiset.hide_other_toolpaths == True:
				_ao.path_hidden = path_obj.hide
			else:
				path_obj.hide = False
			
	updateOperation(self, context)
	
def updateOperation(self, context):
	scene = context.scene
	uiset = scene.cam_ui_settings
	ao = scene.cam_operations[scene.cam_active_operation]

	bpy.ops.object.select_all(action='DESELECT')

	if ao.path_object_name in bpy.data.objects:
		path_obj = bpy.data.objects[ao.path_object_name]
	else:
		path_obj = None
		
	for _ao in scene.cam_operations:
		if _ao.path_object_name in bpy.data.objects:
			other_obj = bpy.data.objects[_ao.path_object_name]
			if other_obj != path_obj:
				if uiset.hide_other_toolpaths == True:
					other_obj.hide = True
				else:
					if _ao.path_hidden == True:
						other_obj.hide = True
						_ao.path_hidden = False
				
	# try highlighting the object in the 3d view and make it active
	if uiset.select_opobject == True:
		if ao.geometry_source=='OBJECT':
			if ao.object_name in bpy.data.objects:
				ob = bpy.data.objects[ao.object_name]
				simple.activate(ob)
		elif ao.geometry_source=='GROUP':
			if ao.group_name in bpy.data.groups:
				group = bpy.data.groups[ao.group_name]
				for obj in group.objects:
					obj.select = True

	# highlight the cutting path if it exists
	if path_obj is not None:
		if uiset.select_opobject == True:
			path_obj.select = True
		else:
			# make path active since operation object is not selected
			simple.activate(path_obj)
		# Show object if it was hidden
		if uiset.hide_other_toolpaths == False:
			# keep track if path was manually hidden
			ao.path_hidden = path_obj.hide
		path_obj.hide = False
		
	
	
	
class CamAddonPreferences(AddonPreferences):
    # this must match the addon name, use '__package__'
    # when defining this in a submodule of a python package.
    bl_idname = __package__

    experimental = BoolProperty(
            name="Show experimental features",
            default=False,
            )

    def draw(self, context):
        layout = self.layout
        layout.label(text="Use experimental features when you want to help development of Blender CAM:")
        
        layout.prop(self, "experimental")


class machineSettings(bpy.types.PropertyGroup):
	'''stores all data for machines'''
	#name = StringProperty(name="Machine Name", default="Machine")
	post_processor = EnumProperty(name='Post processor',
		items=getPostProcessorMenuItems(),
		description='Post processor',
		default='MACH3')
	#units = EnumProperty(name='Units', items = (('IMPERIAL', ''))
	#position definitions:
	use_position_definitions = BoolProperty(name="Use position definitions",description="Define own positions for op start, toolchange, ending position", default=False)
	starting_position = FloatVectorProperty(name = 'Start position', default=(0,0,0), unit='LENGTH', precision=PRECISION,subtype="XYZ", update = updateMachine)
	mtc_position = FloatVectorProperty(name = 'MTC position', default=(0,0,0), unit='LENGTH', precision=PRECISION,subtype="XYZ", update = updateMachine)
	ending_position = FloatVectorProperty(name = 'End position', default=(0,0,0), unit='LENGTH', precision=PRECISION,subtype="XYZ", update = updateMachine)
	
	working_area = FloatVectorProperty(name = 'Work Area', default=(0.500,0.500,0.100), unit='LENGTH', precision=PRECISION,subtype="XYZ",update = updateMachine)
	feedrate_min = FloatProperty(name="Feedrate minimum /min", default=0.0, min=0.00001, max=320000,precision=PRECISION, unit='LENGTH')
	feedrate_max = FloatProperty(name="Feedrate maximum /min", default=2, min=0.00001, max=320000,precision=PRECISION, unit='LENGTH')
	feedrate_default = FloatProperty(name="Feedrate default /min", default=1.5, min=0.00001, max=320000,precision=PRECISION, unit='LENGTH')
	#UNSUPPORTED:
	spindle_min = FloatProperty(name="Spindle speed minimum RPM", default=5000, min=0.00001, max=320000,precision=1)
	spindle_max = FloatProperty(name="Spindle speed maximum RPM", default=30000, min=0.00001, max=320000,precision=1)
	spindle_default = FloatProperty(name="Spindle speed default RPM", default=15000, min=0.00001, max=320000,precision=1)
	spindle_start_time = FloatProperty(name="Spindle start delay seconds", description = 'Wait for the spindle to start spinning before starting the feeds , in seconds', default=0, min=0.0000, max=320000,precision=1)
	
	axis4 = BoolProperty(name="#4th axis",description="Machine has 4th axis", default=0)
	axis5 = BoolProperty(name="#5th axis",description="Machine has 5th axis", default=0)
	
	eval_splitting = BoolProperty(name="Split files",description="split gcode file with large number of operations", default=True)#split large files
	split_limit = IntProperty(name="Operations per file", description="Split files with larger number of operations than this", min=1000, max=20000000, default=800000)
	'''rotary_axis1 = EnumProperty(name='Axis 1',
		items=(
			('X', 'X', 'x'),
			('Y', 'Y', 'y'),
			('Z', 'Z', 'z')),
		description='Number 1 rotational axis',
		default='X', update = updateOffsetImage)
	'''
	collet_size=FloatProperty(name="#Collet size", description="Collet size for collision detection",default=33, min=0.00001, max=320000,precision=PRECISION , unit="LENGTH")
	#exporter_start = StringProperty(name="exporter start", default="%")

    #post processor options

	output_block_numbers = BoolProperty(name = "output block numbers", description = "output block numbers ie N10 at start of line", default = False)
	start_block_number = IntProperty(name = "start block number", description = "the starting block number ie 10", default = 10)
	block_number_increment = IntProperty(name = "block number increment", description = "how much the block number should increment for the next line", default = 10)
	output_tool_definitions = BoolProperty(name = "output tool definitions", description = "output tool definitions", default = True)
	output_tool_change = BoolProperty(name = "output tool change commands", description = "output tool change commands ie: Tn M06", default = True)
	output_g43_on_tool_change = BoolProperty(name = "output G43 on tool change", description = "output G43 on tool change line", default = False)


class uiSettings(bpy.types.PropertyGroup):
	'''stores ui settings'''

	select_opobject = BoolProperty(
		name="Select operation object",
		description="make operation object/group active"
			    " when CAM operation is selected",
		default=False, update=updateOperation)
		
	hide_other_toolpaths = BoolProperty(
		name="Hide other toolpaths",
		description="Hide all other tool paths except toolpath"
			    " assotiated with the selected CAM operation",
		default=False, update=updateHideOtherPaths)


class PackObjectsSettings(bpy.types.PropertyGroup):
	'''stores all data for pack object settings'''
	#name = StringProperty(name="Machine Name", default="Machine")
	sheet_fill_direction = EnumProperty(name='Fill direction',
		items=[('X','X','Fills sheet in X axis direction'),
				('Y','Y','Fills sheet in Y axis direction')],
		description='Fill direction of the packer algorithm',
		default='Y')
	sheet_x = FloatProperty(name="X size", description="Sheet size", min=0.001, max=10, default=0.5, precision=PRECISION, unit="LENGTH")
	sheet_y = FloatProperty(name="Y size", description="Sheet size", min=0.001, max=10, default=0.5, precision=PRECISION, unit="LENGTH")
	distance = FloatProperty(name="Minimum distance", description="minimum distance between objects(should be at least cutter diameter!)", min=0.001, max=10, default=0.01, precision=PRECISION, unit="LENGTH")
	rotate = BoolProperty(name="enable rotation",description="Enable rotation of elements", default=True)

class SliceObjectsSettings(bpy.types.PropertyGroup):
	'''stores all data for slice object settings'''
	#name = StringProperty(name="Machine Name", default="Machine")
	
	slice_distance = FloatProperty(name="Slicing distance", description="slices distance in z, should be most often thickness of plywood sheet.", min=0.001, max=10, default=0.005, precision=PRECISION, unit="LENGTH")
	indexes = BoolProperty(name="add indexes",description="adds index text of layer + index", default=True)

	
def operationValid(self,context):
	o=self
	o.changed=True
	o.valid=True
	invalidmsg = "Operation has no valid data input\n"
	o.warnings=""
	o=bpy.context.scene.cam_operations[bpy.context.scene.cam_active_operation]
	if o.geometry_source=='OBJECT':
		if not o.object_name in bpy.data.objects :
			o.valid=False;
			o.warnings= invalidmsg
	if o.geometry_source=='GROUP':
		if not o.group_name in bpy.data.groups:
			o.valid=False;
			o.warnings=invalidmsg
		elif len(bpy.data.groups[o.group_name].objects)==0: 
			o.valid=False;
			o.warnings=invalidmsg
		
	if o.geometry_source=='IMAGE':
		if not o.source_image_name in bpy.data.images:
			o.valid=False
			o.warnings=invalidmsg

		o.use_exact=False
	o.update_offsetimage_tag=True
	o.update_zbufferimage_tag=True
	print('validity ')
	#print(o.valid)
	
def updateOperationValid(self, context):
	operationValid(self, context)
	updateOperation(self, context)
	
#Update functions start here
def updateChipload(self,context):
	'''this is very simple computation of chip size, could be very much improved'''
	print('update chipload ')
	o=self;
	#self.changed=True
	#Old chipload
	o.chipload = ((o.feedrate/(o.spindle_rpm*o.cutter_flutes)))
	###New chipload with chip thining compensation.
	# I have tried to combine these 2 formulas to compinsate for the phenomenon of chip thinning when cutting at less than 50% cutter engagement with cylindrical end mills.
	# formula 1 Nominal Chipload is " feedrate mm/minute = spindle rpm x chipload x cutter diameter mm x cutter_flutes "
	# formula 2 (.5*(cutter diameter mm devided by dist_between_paths)) devided by square root of ((cutter diameter mm devided by dist_between_paths)-1) x Nominal Chipload
	# Nominal Chipload = what you find in end mill data sheats recomended chip load at %50 cutter engagment. I have no programming or math back ground.
	# I am sure there is a better way to do this. I dont get consistent result and I am not sure if there is something wrong with the units going into the formula, my math or my lack of underestanding of python or programming in genereal. Hopefuly some one can have a look at this and with any luck we will be one tiny step on the way to a slightly better chipload calculating function.

	#self.chipload = ((0.5*(o.cutter_diameter/o.dist_between_paths))/(math.sqrt((o.feedrate*1000)/(o.spindle_rpm*o.cutter_diameter*o.cutter_flutes)*(o.cutter_diameter/o.dist_between_paths)-1)))
	print (o.chipload)
	
	
	
	
def updateOffsetImage(self,context):
	'''refresh offset image tag for rerendering'''
	updateChipload(self,context)
	print('update offset')
	self.changed=True
	self.update_offsetimage_tag=True

def updateZbufferImage(self,context):
	'''changes tags so offset and zbuffer images get updated on calculation time.'''
	#print('updatezbuf')
	#print(self,context)
	self.changed=True
	self.update_zbufferimage_tag=True
	self.update_offsetimage_tag=True
	utils.getOperationSources(self)
	#utils.checkMemoryLimit(self)

def updateStrategy(o,context):
	''''''
	o.changed=True
	print('update strategy')
	if o.machine_axes=='5' or (o.machine_axes=='4' and o.strategy4axis=='INDEXED'):#INDEXED 4 AXIS DOESN'T EXIST NOW...
		utils.addOrientationObject(o)
	else:
		utils.removeOrientationObject(o)
	updateExact(o,context)

def updateCutout(o,context):
	pass;
	#if o.outlines_count>1:
	#	o.use_bridges=False
		
	
def updateExact(o,context):
	print('update exact ')
	o.changed=True
	o.update_zbufferimage_tag=True
	o.update_offsetimage_tag=True
	if o.use_exact and (o.strategy=='WATERLINE' or o.strategy=='POCKET' or o.inverse):
		o.use_exact=False
		
def updateOpencamlib(o,context):
	print('update opencamlib ')
	o.changed=True
	
def updateBridges(o,context):
	print('update bridges ')
	o.changed=True
	#utils.setupBridges(o)
	
def updateRest(o,context):
	#print('update rest ')
	#if o.use_layers:
		#o.parallel_step_back = False
	o.changed=True

def updateStepover(o, context):
	newval = o.stepover_perc / 100.0 * o.cutter_diameter
	if abs(newval - o.dist_between_paths) > 0.000001:
		o.dist_between_paths  = newval
		o.changed = True

def updateToolpathDist(o, context):
	newval = o.dist_between_paths / o.cutter_diameter * 100.0
	if abs(newval - o.stepover_perc) > 0.000001:
		o.stepover_perc = newval
		o.changed = True

def updatePlungeFeedrateVal(o, context):
	newval = o.plunge_feedrate_perc * o.feedrate / 100.0
	if abs(newval - o.plunge_feedrate_val) > 0.000001:
		o.plunge_feedrate_val = newval
		o.changed = True

def updatePlungeFeedratePerc(o, context):
	newval = o.plunge_feedrate_val / o.feedrate * 100.0
	if abs(newval - o.plunge_feedrate_perc) > 0.000001:
		o.plunge_feedrate_perc = newval
		o.changed = True
		
def updateFeeds(o, context):
	updatePlungeFeedrateVal(o, context)
	updateChipload(o, context)

def updateCutter(o, context):
	updateStepover(o, context)
	updateOffsetImage(o, context)
		
def getStrategyList(scene, context):
	use_experimental=bpy.context.user_preferences.addons['cam'].preferences.experimental
	items =[
			('CUTOUT','Profile(Cutout)', 'Cut the silhouete with offset'),
			('POCKET','Pocket', 'Pocket operation'),
			('DRILL','Drill', 'Drill operation'),
			('PARALLEL','Parallel', 'Parallel lines on any angle'),
			('CROSS','Cross', 'Cross paths'),
			('BLOCK','Block', 'Block path'),
			('SPIRAL','Spiral', 'Spiral path'),
			('CIRCLES','Circles', 'Circles path'),
			('OUTLINEFILL','Outline Fill', 'Detect outline and fill it with paths as pocket. Then sample these paths on the 3d surface'),
			('CARVE','Carve', 'Pocket operation'),
			('MEDIAL_AXIS','Medial axis - vcarve', 'Medial axis, must be used with V or ball cutter, for engraving various width shapes with a single stroke '),
			]
	if use_experimental:
		items.extend(
			[('WATERLINE','Waterline - EXPERIMENTAL', 'Waterline paths - constant z'),
			('CURVE','Curve to Path - EXPERIMENTAL', 'Curve object gets converted directly to path'),
			('PENCIL','Pencil - EXPERIMENTAL', 'Pencil operation - detects negative corners in the model and mills only those.'),
			('CRAZY','Crazy path - EXPERIMENTAL', 'Crazy paths - dont even think about using this!'),
			('PROJECTED_CURVE','Projected curve - EXPERIMENTAL', 'project 1 curve towards other curve'),
			('F_ENGRAVE', 'F-Engrave - EXPERIMENTAL', 'engrave or v-carve operation using f-engrave')
			])
	return items
	
def getMaxCutdepth(self):
	return self.max_cutdepthValue
		
class camOperation(bpy.types.PropertyGroup):
	
	name = StringProperty(name="Operation Name", description="A unique name for the operation",  default="Operation", update = updateRest)
	filename = StringProperty(name="File name", description="a unique file name to be used for the exported g-code", default="Operation", update = updateRest)
	auto_export = BoolProperty(name="Auto export",description="export g-code files immediately after path calculation", default=True)
	object_name = StringProperty(name='Object', description='object handled by this operation', update=updateOperationValid)
	group_name = StringProperty(name='Group', description='Object group handled by this operation', update=updateOperationValid)
	curve_object = StringProperty(name='Curve source', description='curve which will be sampled along the 3d object', update=operationValid)
	curve_object1 = StringProperty(name='Curve target', description='curve which will serve as attractor for the cutter when the cutter follows the curve', update=operationValid)
	source_image_name = StringProperty(name='image_source', description='image source', update=operationValid)
	geometry_source = EnumProperty(name='Source of data',
		items=(
			('OBJECT','object', 'mesh, curve, text'),('GROUP','Group of objects', 'objects that have been that have been put in a Group'),('IMAGE','Image', 'an image that has been loaded')),
		description='Geometry source used when sampling',
		default='OBJECT', update=updateOperationValid)
	cutter_type = EnumProperty(name='Cutter',
		items=(
			('END', 'End', 'end - flat cutter'),
			('BALLNOSE', 'Ballnose', 'ballnose cutter'),
			('VCARVE', 'V-carve', 'v carve cutter'),
			('BALL', 'Sphere', 'Sphere cutter'),
			('CUSTOM', 'Custom-EXPERIMENTAL', 'modeled cutter - must be a fully enclosed mesh, not well tested yet.')),
		description='Type of cutter/tool bit used for the operation',
		default='END', update = updateZbufferImage)
	cutter_object_name = StringProperty(name='Cutter object', description='object used as custom cutter for this operation', update=updateZbufferImage)

	machine_axes = EnumProperty(name='Number of axes',
		items=(
			('3', '3 axis', 'x, y, and z axis'),
			('4', '#4 axis - EXPERIMENTAL', 'x,y,z, and A axis'),
			('5', '#5 axis - EXPERIMENTAL', 'x,y,z, A and B axis')),
		description='How many axes will be used for the operation',
		default='3', update = updateStrategy)
	strategy = EnumProperty(name='Strategy',
		items=getStrategyList,
		description='Strategy',
		update = updateStrategy)
		
	strategy4axis = EnumProperty(name='4 axis Strategy',
		items=(
			('PARALLELR','Parallel around 1st rotary axis', 'Parallel lines around first rotary axis'),
			('PARALLEL','Parallel along 1st rotary axis', 'Parallel lines along first rotary axis'),
			('HELIX','Helix around 1st rotary axis', 'Helix around rotary axis'),
			('INDEXED','Indexed 3-axis','all 3 axis strategies, just applied to the 4th axis'),
			('CROSS','Cross', 'Cross paths')),
		description='#Strategy',
		default='PARALLEL',
		update = updateStrategy)
	strategy5axis = EnumProperty(name='Strategy',
		items=(
			('INDEXED','Indexed 3-axis','all 3 axis strategies, just rotated by 4+5th axes'),
			),
		description='5 axis Strategy',
		default='INDEXED',
		update = updateStrategy)
		
	#active_orientation = IntProperty(name="active orientation",description="active orientation", default=0,min=0, max=32000, update = updateRest)
	rotary_axis_1 = EnumProperty(name='Rotary axis',
		items=(
			('X','X', ''),
			('Y','Y', ''),
			('Z','Z', ''),
			),
		description='Around which axis rotates the first rotary axis',
		default='X',
		update = updateStrategy)
	rotary_axis_2 = EnumProperty(name='Rotary axis 2',
		items=(
			('X','X', ''),
			('Y','Y', ''),
			('Z','Z', ''),
			),
		description='Around which axis rotates the second rotary axis',
		default='Z',
		update = updateStrategy)
	
	skin = FloatProperty(name="Skin", description="Thicknes of material to leave when roughing", min=0.0, max=1.0, default=0.0,precision=PRECISION, unit="LENGTH", update = updateOffsetImage)
	inverse = BoolProperty(name="Inverse milling",description="Male to female model conversion", default=False, update = updateOffsetImage)
	array = BoolProperty(name="Use array",description="Create a repetitive array for producing the same thing manytimes", default=False, update = updateRest)
	array_x_count = IntProperty(name="X count",description="number of times the operation is repeated on the x axis", default=1,min=1, max=32000, update = updateRest)
	array_y_count = IntProperty(name="Y count",description="number of times the operation is repeated on the y axis", default=1,min=1, max=32000, update = updateRest)
	array_x_distance = FloatProperty(name="X distance", description="distance between operation origins on x axis", min=0.00001, max=1.0, default=0.01,precision=PRECISION, unit="LENGTH", update = updateRest)
	array_y_distance = FloatProperty(name="Y distance", description="distance between operation origins on y axis", min=0.00001, max=1.0, default=0.01,precision=PRECISION, unit="LENGTH", update = updateRest)
	
	
	# pocket options
	pocket_option = EnumProperty(name='Start Position', items=(('INSIDE', 'Inside', 'path starts at center of pocket area'), ('OUTSIDE', 'Outside', 'path starts at the outside perimeter of the pocket area'), ('MIDDLE', 'Middle', 'path starts in the middle of the pocket area')), description='Pocket starting position', default='MIDDLE', update=updateRest)
	
	#Cutout	   
	cut_type = EnumProperty(name='Cut',items=(('OUTSIDE', 'Outside', 'cut on the outside of the curve'),('INSIDE', 'Inside', 'cut on the inside of the curve'),('ONLINE', 'On line', 'cut exactly on the curve')),description='on which side of the curve to cut',default='OUTSIDE', update = updateRest)  
	#render_all = BoolProperty(name="Use all geometry",description="use also other objects in the scene", default=True)#replaced with groups support
	outlines_count = IntProperty(name="Outlines count`EXPERIMENTAL",description="the number of repeat outline cuts to perform", default=1,min=1, max=32, update = updateCutout)
	
	
	#cutter
	cutter_id = IntProperty(name="Tool number", description="For machines which support tool change based on tool id", min=0, max=10000, default=1, update = updateRest)
	cutter_diameter = FloatProperty(name="Cutter diameter", description="Cutter diameter = 2x cutter radius", min=0.000001, max=10, default=0.003, precision=PRECISION, unit="LENGTH", update = updateCutter)
	cutter_length = FloatProperty(name="Cutter length", description="length of the cutter from tip to end of flute", min=0.0, max=10.0, default=0.025, precision=PRECISION, unit="LENGTH",  update = updateOffsetImage)
	cutter_flutes = IntProperty(name="Cutter flutes", description="the number of cutter flutes, used for chip load calculation", min=1, max=20, default=2, update = updateChipload)
	cutter_tip_angle = FloatProperty(name="Cutter v-carve angle", description="Cutter v-carve angle", min=0.0, max=180.0, default=60.0,precision=PRECISION,	 update = updateOffsetImage)
	cutter_description = StringProperty(name="Tool Description", default="", update = updateOffsetImage)
	
	#steps
	dist_between_paths = FloatProperty(name="Distance between toolpaths", description="step over distance between tool paths", default=0.001, min=0.00001, max=32,precision=PRECISION, unit="LENGTH", update = updateToolpathDist)
	stepover_perc = FloatProperty(name="% of tool diameter", description="step over distance expressed as percentage of tool diameter", default=40.0, min=0.00, max=100 ,precision=1, subtype='PERCENTAGE', update = updateStepover)
	dist_along_paths = FloatProperty(name="Distance along toolpaths", description="shorter distance gives better sampling accuracy but increases operation processing time", default=0.0002, min=0.00001, max=32,precision=PRECISION, unit="LENGTH", update = updateRest)
	parallel_angle = FloatProperty(name="Angle of paths", default=0, min=-360, max=360 , precision=0, subtype="ANGLE" , unit="ROTATION" , update = updateRest)
	
	#carve only
	carve_depth = FloatProperty(name="Carve depth", description="maximum depth to carve", default=0.001, min=-.100, max=32,precision=PRECISION, unit="LENGTH", update = updateRest)
	#drill only
	drill_type = EnumProperty(name='Holes on',items=(('MIDDLE_SYMETRIC', 'Middle of symetric curves', 'drill a hole at the center of each curve'),('MIDDLE_ALL', 'Middle of all curve parts', 'drill a hole at the center of the curves'),('ALL_POINTS', 'All points in curve', 'drill a hole at each point in the curve')),description='Strategy to detect holes to drill',default='MIDDLE_SYMETRIC', update = updateRest)	
	#waterline only
	slice_detail = FloatProperty(name="Distance betwen slices", default=0.001, min=0.00001, max=32,precision=PRECISION, unit="LENGTH", update = updateRest)
	waterline_fill = BoolProperty(name="Fill areas between slices",description="Fill areas between slices in waterline mode", default=True, update = updateRest)
	waterline_project = BoolProperty(name="Project paths",description="Project paths in areas between slices", default=True, update = updateRest)
	
	#movement and ramps
	use_layers = BoolProperty(name="Use Layers",description="Use layers for roughing", default=True, update = updateRest)
	stepdown = FloatProperty(name="Step down", description="depth of cut in a layer/pass", default=0.01, min=0.00001, max=32,precision=PRECISION, unit="LENGTH", update = updateRest)
	first_down = BoolProperty(name="First down",description="First go down on a contour, then go to the next one", default=False, update = updateRest)
	ramp = BoolProperty(name="Ramp in - EXPERIMENTAL",description="Ramps down the whole contour, so the cutline looks like helix", default=False, update = updateRest)
	ramp_out = BoolProperty(name="Ramp out - EXPERIMENTAL",description="Ramp out to not leave mark on surface", default=False, update = updateRest)
	ramp_in_angle = FloatProperty(name="Ramp in angle", default=math.pi/6, min=0, max=math.pi*0.4999 , precision=1, subtype="ANGLE" , unit="ROTATION" , update = updateRest)
	ramp_out_angle = FloatProperty(name="Ramp out angle", default=math.pi/6, min=0, max=math.pi*0.4999 , precision=1, subtype="ANGLE" , unit="ROTATION" , update = updateRest)
	helix_enter = BoolProperty(name="Helix enter - EXPERIMENTAL",description="Enter material in helix", default=False, update = updateRest)
	#helix_angle =	FloatProperty(name="Helix ramp angle", default=3*math.pi/180, min=0.00001, max=math.pi*0.4999,precision=1, subtype="ANGLE" , unit="ROTATION" , update = updateRest)
	helix_diameter = FloatProperty(name = 'Helix diameter % of cutter D', default=90,min=10, max=100, precision=1,subtype='PERCENTAGE', update = updateRest)
	retract_tangential = BoolProperty(name="Retract tangential - EXPERIMENTAL",description="Retract from material in circular motion", default=False, update = updateRest)
	retract_radius = FloatProperty(name = 'Retract arc radius', default=0.001,min=0.000001, max=100, precision=PRECISION, unit="LENGTH", update = updateRest)
	retract_height = FloatProperty(name = 'Retract arc height', default=0.001,min=0.00000, max=100, precision=PRECISION, unit="LENGTH", update = updateRest)
	
	minz_from_ob = BoolProperty(name="Depth from object",description="Operation ending depth from object", default=True, update = updateRest)
	minz = FloatProperty(name="Operation depth end", default=-0.01, min=-3, max=3,precision=PRECISION, unit="LENGTH", update = updateRest)#this is input minz. True minimum z can be something else, depending on material e.t.c.
	start_type = EnumProperty(name='Start type',
		items=(
			('ZLEVEL','Z level', 'Starts on a given Z level'),
			('OPERATIONRESULT','Rest milling', 'For rest milling, operations have to be put in chain for this to work well.'),
			),
		description='Starting depth',
		default='ZLEVEL',
		update = updateStrategy)
		
	maxz = FloatProperty(name="Operation depth start", description='operation starting depth', default=0, min=-3, max=10,precision=PRECISION, unit="LENGTH", update = updateRest)#EXPERIMENTAL
	#######################################################
	######Image related
	####################################################
	source_image_scale_z = FloatProperty(name="Image source depth scale", default=0.01, min=-1, max=1,precision=PRECISION, unit="LENGTH",  update = updateZbufferImage)
	source_image_size_x = FloatProperty(name="Image source x size", default=0.1, min=-10, max=10,precision=PRECISION, unit="LENGTH",  update = updateZbufferImage)
	source_image_offset = FloatVectorProperty(name = 'Image offset', default=(0,0,0), unit='LENGTH', precision=PRECISION,subtype="XYZ",	 update = updateZbufferImage)
	source_image_crop = BoolProperty(name="Crop source image",description="Crop source image - the position of the sub-rectangle is relative to the whole image, so it can be used for e.g. finishing just a part of an image", default=False,	update = updateZbufferImage)
	source_image_crop_start_x = FloatProperty(name = 'crop start x', default=0,min=0, max=100, precision=PRECISION,subtype='PERCENTAGE',  update = updateZbufferImage)
	source_image_crop_start_y = FloatProperty(name = 'crop start y', default=0,min=0, max=100, precision=PRECISION,subtype='PERCENTAGE',  update = updateZbufferImage)
	source_image_crop_end_x = FloatProperty(name = 'crop end x', default=100,min=0, max=100, precision=PRECISION,subtype='PERCENTAGE',  update = updateZbufferImage)
	source_image_crop_end_y = FloatProperty(name = 'crop end y', default=100,min=0, max=100, precision=PRECISION,subtype='PERCENTAGE',  update = updateZbufferImage)
	
	#########################################################
	#Toolpath and area related
	#####################################################
	protect_vertical = BoolProperty(name="Protect vertical",description="The path goes only vertically next to steep areas", default=True)
	protect_vertical_limit = FloatProperty(name="Verticality limit", description="What angle is allready considered vertical", default=math.pi/45, min=0, max=math.pi*0.5 , precision=0, subtype="ANGLE" , unit="ROTATION" , update = updateRest)
		
	ambient_behaviour = EnumProperty(name='Ambient',items=(('ALL', 'All', 'all ambient inclusive'),('AROUND', 'Around', 'ambiant around object defined by radius')   ),description='handling ambient surfaces',default='ALL', update = updateZbufferImage)
	

	ambient_radius = FloatProperty(name="Ambient radius", description="Radius around the part which will be milled if ambient is set to Around", min=0.0, max=100.0, default=0.01, precision=PRECISION, unit="LENGTH", update = updateRest)
	#ambient_cutter = EnumProperty(name='Borders',items=(('EXTRAFORCUTTER', 'Extra for cutter', "Extra space for cutter is cut around the segment"),('ONBORDER', "Cutter on edge", "Cutter goes exactly on edge of ambient with it's middle") ,('INSIDE', "Inside segment", 'Cutter stays within segment')	 ),description='handling of ambient and cutter size',default='INSIDE')
	use_limit_curve = BoolProperty(name="Use limit curve",description="A curve limits the operation area", default=False, update = updateRest)
	ambient_cutter_restrict = BoolProperty(name="Cutter stays in ambient limits",description="Cutter doesn't get out from ambient limits otherwise goes on the border exactly", default=True, update = updateRest)#restricts cutter inside ambient only
	limit_curve = StringProperty(name='Limit curve', description='curve used to limit the area of the operation', update = updateRest)
	
	
	#feeds
	feedrate = FloatProperty(name="Feedrate", description="Feedrate expressed in units/minute", min=0.00005, max=50.0, default=1.0,precision=PRECISION, unit="LENGTH", update = updateFeeds)
	plunge_feedrate_perc = FloatProperty(name="Plunge % feedrate", description="% of feedrate", min=0.1, max=100.0, default=50.0,precision=1, subtype='PERCENTAGE', update = updatePlungeFeedrateVal)
	plunge_feedrate_val = FloatProperty(name="Plunge feedrate", description="feedrate to use when tool moves downward", min=0.000001, max=50.0, default=1.0,precision=PRECISION, unit='LENGTH', update = updatePlungeFeedratePerc)
	plunge_angle =	FloatProperty(name="Plunge angle", description="What angle is allready considered to plunge", default=math.pi/6, min=0, max=math.pi*0.5 , precision=0, subtype="ANGLE" , unit="ROTATION" , update = updateRest)
	spindle_rpm = FloatProperty(name="Spindle RPM", description="Spindle rotation speed", min=100, max=60000, default=12000, update = updateChipload)
	#movement parallel_step_back 
	movement_type = EnumProperty(name='Movement type',items=(('CONVENTIONAL','Conventional / Up milling', 'cutter rotates against the direction of the feed'),('CLIMB', 'Climb / Down milling', 'cutter rotates with the direction of the feed'),('MEANDER', 'Meander / Zig Zag' , 'cutting is done both with and against the rotation of the spindle')	 ),description='movement type', default='CLIMB', update = updateRest)
	spindle_rotation_direction = EnumProperty(name='Spindle rotation', items=(('CW','Clockwise', 'tool bit spins clockwise'),('CCW', 'Counter clockwise', 'tool bit spins counter clock wise')),description='Spindle rotation direction',default='CW', update = updateRest)
	free_movement_height = FloatProperty(name="Free movement height", default=0.01, min=0.0000, max=32,precision=PRECISION, unit="LENGTH", update = updateRest)
	movement_insideout = EnumProperty(name='Direction', items=(('INSIDEOUT','Inside out', 'Path starts at the center/inside and works its way to the outside perimeter of the work area'),('OUTSIDEIN', 'Outside in', 'Path starts at the outside perimeter or the work area and works its way to the inside')),description='approach to the piece',default='INSIDEOUT', update = updateRest)
	parallel_step_back =  BoolProperty(name="Parallel step back", description='For roughing and finishing in one pass: mills material in climb mode, then steps back and goes between 2 last chunks back', default=False, update = updateRest)
	stay_low = BoolProperty(name="Stay low if possible", default=True, update = updateRest)
	merge_dist = FloatProperty(name="Merge distance - EXPERIMENTAL", default=0.0, min=0.0000, max=0.1,precision=PRECISION, unit="LENGTH", update = updateRest)
	#optimization and performance
	circle_detail = IntProperty(name="Detail of circles used for curve offsets", default=64, min=12, max=512, update = updateRest)
	use_exact = BoolProperty(name="Use exact mode",description="Uses Bullet physics engine to sample 3D mesh. Exact mode allows greater precision, but is slower with complex meshes", default=True, update = updateExact)
	exact_subdivide_edges = BoolProperty(name="Auto subdivide long edges",description="This can avoid some collision issues when importing CAD models", default=False, update = updateExact)
	use_opencamlib = BoolProperty(name="Use OpenCAMLib",description="Use OpenCAMLib to sample paths or get waterline shape", default=False, update = updateOpencamlib)
	pixsize = FloatProperty(name="sampling pixel size", description="the physical size of a pixel in the image. The smaller the pixel size is the larger the image will be and processing will be slower", default=0.0001, min=0.00001, max=0.1,precision=PRECISION, unit="LENGTH", update = updateZbufferImage)
	simulation_detail = FloatProperty(name="Simulation sampling raster detail", default=0.0002, min=0.00001, max=0.01,precision=PRECISION, unit="LENGTH", update = updateRest)
	do_simulation_feedrate = BoolProperty(name="Adjust feedrates with simulation EXPERIMENTAL",description="Adjust feedrates with simulation", default=False, update = updateRest)
	
	imgres_limit = IntProperty(name="Maximum resolution in megapixels", default=16, min=1, max=512,description="This property limits total memory usage and prevents crashes. Increase it if you know what are doing.", update = updateZbufferImage)
	optimize = BoolProperty(name="Reduce path points",description="Optimize the path by reducing the number of points that make up the path", default=True, update = updateRest)
	optimize_threshold = FloatProperty(name="Reduction threshold", description="the smallest deviation length between 3 path points before reduction occurs. Smaller values means more path points and improves curve fitting but increases gcode file size", default=0.000002, min=0.0000001, max=1, precision=PRECISION, unit="LENGTH", update = updateRest)
	
	dont_merge = BoolProperty(name="Dont merge outlines when cutting",description="this is usefull when you want to cut around everything", default=False, update = updateRest)
	
	pencil_threshold = FloatProperty(name="Pencil threshold", default=0.00002, min=0.00000001, max=1,precision=PRECISION, unit="LENGTH", update = updateRest)
	crazy_threshold1 = FloatProperty(name="min engagement", default=0.02, min=0.00000001, max=100,precision=PRECISION, update = updateRest)
	crazy_threshold5 = FloatProperty(name="optimal engagement", default=0.3, min=0.00000001, max=100,precision=PRECISION, update = updateRest)
	crazy_threshold2 = FloatProperty(name="max engagement", default=0.5, min=0.00000001, max=100,precision=PRECISION, update = updateRest)
	crazy_threshold3 = FloatProperty(name="max angle", default=2, min=0.00000001, max=100,precision=PRECISION, update = updateRest)
	crazy_threshold4 = FloatProperty(name="test angle step", default=0.05, min=0.00000001, max=100,precision=PRECISION, update = updateRest)
	####
	#calculations
	duration = FloatProperty(name="Estimated time", default=0.01, min=0.0000, max=3200000000,precision=PRECISION, unit="TIME")
	max_cutdepth = FloatProperty(name="maximum depth of cut", default=0.01, min=-100.0000, max=100,precision=PRECISION, unit="LENGTH", get=getMaxCutdepth)
	max_cutdepthValue = FloatProperty(name="maximum depth of cut value", default=0.01, min=-100.0000, max=100,precision=PRECISION, unit="LENGTH")

	#chip_rate
	#bridges
	use_bridges =  BoolProperty(name="Use bridges",description="use bridges in cutout", default=False, update = updateBridges)
	bridges_width = FloatProperty(name = 'width of bridges', default=0.002, unit='LENGTH', precision=PRECISION, update = updateBridges)
	bridges_height = FloatProperty(name = 'height of bridges', description="Height from the bottom of the cutting operation", default=0.0005, unit='LENGTH', precision=PRECISION, update = updateBridges)
	bridges_group_name = StringProperty(name='Bridges Group', description='Group of curves used as bridges', update=operationValid)
	use_bridge_modifiers = BoolProperty(name = "use bridge modifiers", description = "include bridge curve modifiers using render level when calculating operation, does not effect original bridge data", default = True, update=updateBridges)

	'''commented this - auto bridges will be generated, but not as a setting of the operation
	bridges_placement = EnumProperty(name='Bridge placement',
		items=(
			('AUTO','Automatic', 'Automatic bridges with a set distance'),
			('MANUAL','Manual', 'Manual placement of bridges'),
			),
		description='Bridge placement',
		default='AUTO',
		update = updateStrategy)
	
	bridges_per_curve = IntProperty(name="minimum bridges per curve", description="", default=4, min=1, max=512, update = updateBridges)
	bridges_max_distance = FloatProperty(name = 'Maximum distance between bridges', default=0.08, unit='LENGTH', precision=PRECISION, update = updateBridges)
	'''
	use_modifiers = BoolProperty(name = "use mesh modifiers", description = "include mesh modifiers using render level when calculating operation, does not effect original mesh", default = True, update=operationValid)
	#optimisation panel
	
	#material settings
	material_from_model = BoolProperty(name="Estimate from model",description="Estimate material size from model", default=True, update = updateMaterial)
	material_radius_around_model = FloatProperty(name="radius around model",description="How much to add to model size on all sides", default=0.0, unit='LENGTH', precision=PRECISION, update = updateMaterial)
	material_origin = FloatVectorProperty(name = 'Material origin', default=(0,0,0), unit='LENGTH', precision=PRECISION,subtype="XYZ", update = updateMaterial)
	material_size = FloatVectorProperty(name = 'Material size', default=(0.200,0.200,0.100), unit='LENGTH', precision=PRECISION,subtype="XYZ", update = updateMaterial)
	min = FloatVectorProperty(name = 'Operation minimum', default=(0,0,0), unit='LENGTH', precision=PRECISION,subtype="XYZ")
	max = FloatVectorProperty(name = 'Operation maximum', default=(0,0,0), unit='LENGTH', precision=PRECISION,subtype="XYZ")
	warnings = StringProperty(name='warnings', description='warnings', default='', update = updateRest)
	chipload = FloatProperty(name="chipload",description="Calculated chipload", default=0.0, unit='LENGTH', precision=10)

	#g-code options for operation
	output_header = BoolProperty(name = "output g-code header", description = "output user defined g-code command header at start of operation", default = False)
	gcode_header = StringProperty(name = "g-code header", description = "g-code commands at start of operation. Use ; for line breaks", default = "G53 G0")
	output_trailer = BoolProperty(name = "output g-code trailer", description = "output user defined g-code command trailer at end of operation", default = False)
	gcode_trailer = StringProperty(name = "g-code trailer", description = "g-code commands at end of operation. Use ; for line breaks", default = "M02")
	
		
	#internal properties
	###########################################
	#testing = IntProperty(name="developer testing ", description="This is just for script authors for help in coding, keep 0", default=0, min=0, max=512)
	offset_image = numpy.array([],dtype=float)
	zbuffer_image = numpy.array([],dtype=float)
	
	silhouete = sgeometry.Polygon()
	ambient = sgeometry.Polygon()
	operation_limit = sgeometry.Polygon()
	borderwidth = 50
	object = None
	path_object_name = StringProperty(name='Path object', description='actual cnc path')
	path_hidden = BoolProperty(name="Path hidden",description="True if the path was hidden before selection", default=False)
	#####update and tags and related
	changed = BoolProperty(name="True if any of the operation settings has changed",description="mark for update", default=False)
	update_zbufferimage_tag = BoolProperty(name="mark zbuffer image for update",description="mark for update", default=True)
	update_offsetimage_tag = BoolProperty(name="mark offset image for update",description="mark for update", default=True)
	update_silhouete_tag = BoolProperty(name="mark silhouete image for update",description="mark for update", default=True)
	update_ambient_tag = BoolProperty(name="mark ambient polygon for update",description="mark for update", default=True)
	update_bullet_collision_tag = BoolProperty(name="mark bullet collisionworld for update",description="mark for update", default=True)
	
	
	valid = BoolProperty(name="Valid",description="True if operation is ok for calculation", default=True);
	changedata = StringProperty(name='changedata', description='change data for checking if stuff changed.')
	###############process related data
	computing = BoolProperty(name="Computing right now",description="", default=False)
	pid = IntProperty(name="process id", description="Background process id", default=-1)
	outtext = StringProperty(name='outtext', description='outtext', default='')
	

class opReference(bpy.types.PropertyGroup):#this type is defined just to hold reference to operations for chains
	name = StringProperty(name="Operation name", default="Operation")
	computing = False;#for UiList display
	
class camChain(bpy.types.PropertyGroup):#chain is just a set of operations which get connected on export into 1 file.
	index = IntProperty(name="index", description="index in the hard-defined camChains", default=-1)
	active_operation = IntProperty(name="active operation", description="active operation in chain", default=-1)
	name = StringProperty(name="Chain Name", description="a unique name for the chain", default="Chain")
	filename = StringProperty(name="File name", description="a unique file name to use when the chain is exported to a file", default="Chain")#filename of 
	valid = BoolProperty(name="Valid",description="True if whole chain is ok for calculation", default=True);
	computing = BoolProperty(name="Computing right now",description="", default=False)
	operations= CollectionProperty(type=opReference)#this is to hold just operation names.
			
@bpy.app.handlers.persistent
def check_operations_on_load(context):
	'''checks any broken computations on load and reset them.'''
	s=bpy.context.scene
	for o in s.cam_operations:
		if o.computing:
			o.computing=False



class CAM_CUTTER_presets(Menu):
	bl_label = "Cutter presets"
	preset_subdir = "cam_cutters"
	preset_operator = "script.execute_preset"
	draw = Menu.draw_preset

class CAM_MACHINE_presets(Menu):
	bl_label = "Machine presets"
	preset_subdir = "cam_machines"
	preset_operator = "script.execute_preset"
	draw = Menu.draw_preset
	
class AddPresetCamCutter(bl_operators.presets.AddPresetBase, Operator):
	'''Add a Cutter Preset'''
	bl_idname = "render.cam_preset_cutter_add"
	bl_label = "Add Cutter Preset"
	preset_menu = "CAM_CUTTER_presets"
	
	preset_defines = [
		"d = bpy.context.scene.cam_operations[bpy.context.scene.cam_active_operation]"
	]

	preset_values = [
		"d.cutter_id",
		"d.cutter_type",
		"d.cutter_diameter",
		"d.cutter_length",
		"d.cutter_flutes",
		"d.cutter_tip_angle",
		"d.cutter_description",
	]

	preset_subdir = "cam_cutters"

class CAM_OPERATION_presets(Menu):
	bl_label = "Operation presets"
	preset_subdir = "cam_operations"
	preset_operator = "script.execute_preset"
	draw = Menu.draw_preset
		
class AddPresetCamOperation(bl_operators.presets.AddPresetBase, Operator):
	'''Add an Operation Preset'''
	bl_idname = "render.cam_preset_operation_add"
	bl_label = "Add Operation Preset"
	preset_menu = "CAM_OPERATION_presets"
	
	preset_defines = [
		"o = bpy.context.scene.cam_operations[bpy.context.scene.cam_active_operation]"
	]
	'''
	d1=dir(bpy.types.machineSettings.bl_rna)

	d=[]
	for prop in d1:
		if (prop[:2]!='__' 
			and prop!='bl_rna'
			and prop!='translation_context'
			and prop!='base'
			and prop!='description'
			and prop!='identifier'
			and prop!='name'
			and prop!='name_property'):
				d.append(prop)
	'''
	preset_values = ['o.use_layers', 'o.duration', 'o.chipload', 'o.material_from_model',
		'o.stay_low', 'o.carve_depth', 'o.dist_along_paths', 'o.source_image_crop_end_x',
		'o.source_image_crop_end_y', 'o.material_size', 'o.material_radius_around_model',
		'o.use_limit_curve', 'o.cut_type', 'o.use_exact','o.exact_subdivide_edges',
		'o.minz_from_ob', 'o.free_movement_height', 'o.source_image_crop_start_x',
		'o.movement_insideout', 'o.spindle_rotation_direction', 'o.skin',
		'o.source_image_crop_start_y', 'o.movement_type', 'o.source_image_crop',
		'o.limit_curve', 'o.spindle_rpm', 'o.ambient_behaviour', 'o.cutter_type',
		'o.source_image_scale_z', 'o.cutter_diameter', 'o.source_image_size_x',
		'o.curve_object', 'o.curve_object1', 'o.cutter_flutes', 'o.ambient_radius',
		'o.simulation_detail', 'o.update_offsetimage_tag', 'o.dist_between_paths',
		'o.stepover_perc', 'o.max', 'o.min', 'o.pixsize', 'o.slice_detail',
		'o.parallel_step_back', 'o.drill_type', 'o.source_image_name', 'o.dont_merge',
		'o.update_silhouete_tag', 'o.material_origin', 'o.inverse', 'o.waterline_fill',
		'o.source_image_offset', 'o.circle_detail', 'o.strategy', 'o.update_zbufferimage_tag',
		'o.stepdown', 'o.feedrate', 'o.cutter_tip_angle', 'o.cutter_id', 'o.path_object_name',
		'o.pencil_threshold', 'o.geometry_source', 'o.optimize_threshold',
		'o.protect_vertical', 'o.plunge_feedrate_perc', 'o.plunge_feedrate_val', 'o.minz', 'o.warnings', 'o.object_name',
		'o.optimize', 'o.parallel_angle', 'o.cutter_length', 'o.output_header',
		'o.gcode_header', 'o.output_trailer', 'o.gcode_trailer', 'o.use_modifiers']

	preset_subdir = "cam_operations"   
	 
class AddPresetCamMachine(bl_operators.presets.AddPresetBase, Operator):
	'''Add a Cam Machine Preset'''
	bl_idname = "render.cam_preset_machine_add"
	bl_label = "Add Machine Preset"
	preset_menu = "CAM_MACHINE_presets"

	preset_defines = [
		"d = bpy.context.scene.cam_machine",
		"s = bpy.context.scene.unit_settings"
	]
	preset_values = [
		"d.post_processor",
		"s.system",
		"d.use_position_definitions",
		"d.starting_position",
		"d.mtc_position",
		"d.ending_position",
		"d.working_area",
		"d.feedrate_min",
		"d.feedrate_max",
		"d.feedrate_default",
		"d.spindle_min",
		"d.spindle_max",
		"d.spindle_default",
		"d.axis4",
		"d.axis5",
		"d.collet_size",
		"d.output_tool_change",
		"d.output_block_numbers",
		"d.output_tool_definitions",
		"d.output_g43_on_tool_change",
	]

	preset_subdir = "cam_machines"
		
		
classes = (
	ui.CAM_UL_operations,
	#ui.CAM_UL_orientations,
	ui.CAM_UL_chains,
	camOperation,
	opReference,
	camChain,
	machineSettings,
	uiSettings,
	CamAddonPreferences,
	
	ui.CAM_CHAINS_Panel,
	ui.CAM_OPERATIONS_Panel,
	ui.CAM_INFO_Panel,
	ui.CAM_MATERIAL_Panel,
	ui.CAM_OPERATION_PROPERTIES_Panel,
	ui.CAM_OPTIMISATION_Panel,
	ui.CAM_AREA_Panel,
	ui.CAM_MOVEMENT_Panel,
	ui.CAM_FEEDRATE_Panel,
	ui.CAM_CUTTER_Panel,
	ui.CAM_GCODE_Panel,
	ui.CAM_MACHINE_Panel,
	ui.CAM_UISETTINGS_Panel,
	ui.CAM_PACK_Panel,
	ui.CAM_SLICE_Panel,
	ui.VIEW3D_PT_tools_curvetools,
	
	ops.PathsBackground,
	ops.KillPathsBackground,
	ops.CalculatePath,
	ops.CalculateChain,
	ops.ExportChain,
	ops.PathsAll,
	ops.PathExport,
	ops.CAMPositionObject,
	ops.CAMSimulate,
	ops.CAMSimulateChain,
	ops.CamChainAdd,
	ops.CamChainRemove,
	ops.CamChainOperationAdd,
	ops.CamChainOperationRemove,
	ops.CamChainOperationUp,
	ops.CamChainOperationDown,
	
	ops.CamOperationAdd,
	ops.CamOperationCopy,
	ops.CamOperationRemove,
	ops.CamOperationMove,
	#bridges related
	ops.CamBridgesAdd,
	#5 axis ops
	ops.CamOrientationAdd,
	#shape packing
	ops.CamPackObjects,
	ops.CamSliceObjects,
	#other tools
	ops.CamCurveBoolean,
	ops.CamOffsetSilhouete,
	ops.CamObjectSilhouete,
	ops.CamCurveIntarsion,
	ops.CamCurveOvercuts,
	ops.CamCurveOvercutsB,
	ops.CamCurveRemoveDoubles,
	ops.CamMeshGetPockets,
	
	CAM_CUTTER_presets,
	CAM_OPERATION_presets,
	CAM_MACHINE_presets,
	AddPresetCamCutter,
	AddPresetCamOperation,
	AddPresetCamMachine,
	#CamBackgroundMonitor
	#pack module:
	PackObjectsSettings,
	SliceObjectsSettings,
	
)
	
def register():
	for cls in classes:
		bpy.utils.register_class(cls)
	
	s = bpy.types.Scene
	
	s.cam_chains = CollectionProperty(type=camChain)
	s.cam_active_chain = IntProperty(name="CAM Active Chain", description="The selected chain")

	s.cam_operations = CollectionProperty(type=camOperation)
	
	s.cam_active_operation = IntProperty(name="CAM Active Operation", description="The selected operation", update=updateOperation)
	s.cam_machine = PointerProperty(type=machineSettings)
	s.cam_ui_settings = PointerProperty(type=uiSettings)
	
	s.cam_text = StringProperty()
	bpy.app.handlers.scene_update_pre.append(ops.timer_update)
	bpy.app.handlers.load_post.append(check_operations_on_load)
	#bpy.types.INFO_HT_header.append(header_info)
	
	s.cam_pack = PointerProperty(type=PackObjectsSettings)
	
	s.cam_slice = PointerProperty(type=SliceObjectsSettings)

def unregister():
	for cls in classes:
		bpy.utils.unregister_class(cls)

	s = bpy.types.Scene
	del s.cam_operations
	#cam chains are defined hardly now.
	del s.cam_chains
	del s.cam_active_operation
	del s.cam_machine
	del s.cam_ui_settings
	
	bpy.app.handlers.scene_update_pre.remove(ops.timer_update)
	#bpy.types.INFO_HT_header.remove(header_info)

if __name__ == "__main__":
	register()
