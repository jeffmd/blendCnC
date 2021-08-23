/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __RNA_ACCESS_H__
#define __RNA_ACCESS_H__

/** \file RNA_access.h
 *  \ingroup RNA
 */

#include <stdarg.h>

#include "RNA_types.h"

#include "BLI_compiler_attrs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ID;
struct ListBase;
struct Main;
struct ReportList;
struct Scene;
struct bContext;

/* Types */

extern BlenderRNA BLENDER_RNA;
extern StructRNA RNA_Addon;
extern StructRNA RNA_AddonPreferences;
extern StructRNA RNA_AdjustmentSequence;
extern StructRNA RNA_AnyType;
extern StructRNA RNA_Area;
extern StructRNA RNA_AreaLamp;
extern StructRNA RNA_ArrayModifier;
extern StructRNA RNA_BackgroundImage;
extern StructRNA RNA_BevelModifier;
extern StructRNA RNA_SplinePoint;
extern StructRNA RNA_BezierSplinePoint;
extern StructRNA RNA_BlendData;
extern StructRNA RNA_BlendTexture;
extern StructRNA RNA_BlenderRNA;
extern StructRNA RNA_BooleanModifier;
extern StructRNA RNA_BoolProperty;
extern StructRNA RNA_BuildModifier;
extern StructRNA RNA_MeshCacheModifier;
extern StructRNA RNA_MeshSequenceCacheModifier;
extern StructRNA RNA_CacheFile;
extern StructRNA RNA_Camera;
extern StructRNA RNA_CastModifier;
extern StructRNA RNA_CloudsTexture;
extern StructRNA RNA_CollectionProperty;
extern StructRNA RNA_CollisionModifier;
extern StructRNA RNA_ColorManagedInputColorspaceSettings;
extern StructRNA RNA_ColorManagedSequencerColorspaceSettings;
extern StructRNA RNA_ColorManagedDisplaySettings;
extern StructRNA RNA_ColorManagedViewSettings;
extern StructRNA RNA_ColorRamp;
extern StructRNA RNA_ColorRampElement;
extern StructRNA RNA_ColorSequence;
extern StructRNA RNA_ColorMixSequence;
extern StructRNA RNA_ConsoleLine;
extern StructRNA RNA_Context;
extern StructRNA RNA_CopyLocationConstraint;
extern StructRNA RNA_CopyRotationConstraint;
extern StructRNA RNA_CopyScaleConstraint;
extern StructRNA RNA_CopyTransformsConstraint;
extern StructRNA RNA_Curve;
extern StructRNA RNA_CurveMap;
extern StructRNA RNA_CurveMapPoint;
extern StructRNA RNA_CurveMapping;
extern StructRNA RNA_CurveModifier;
extern StructRNA RNA_CurvePoint;
extern StructRNA RNA_DataTransferModifier;
extern StructRNA RNA_DecimateModifier;
extern StructRNA RNA_CorrectiveSmoothModifier;
extern StructRNA RNA_DisplaceModifier;
extern StructRNA RNA_DisplaySafeAreas;
extern StructRNA RNA_DistortedNoiseTexture;
extern StructRNA RNA_EdgeSplitModifier;
extern StructRNA RNA_EnumProperty;
extern StructRNA RNA_EnumPropertyItem;
extern StructRNA RNA_EnvironmentMap;
extern StructRNA RNA_EnvironmentMapTexture;
extern StructRNA RNA_Event;
extern StructRNA RNA_FieldSettings;
extern StructRNA RNA_FileBrowserFSMenuEntry;
extern StructRNA RNA_FileSelectParams;
extern StructRNA RNA_FloatProperty;
extern StructRNA RNA_FloorConstraint;
extern StructRNA RNA_Function;
extern StructRNA RNA_Group;
extern StructRNA RNA_Header;
extern StructRNA RNA_HemiLamp;
extern StructRNA RNA_Histogram;
extern StructRNA RNA_HookModifier;
extern StructRNA RNA_ID;
extern StructRNA RNA_Image;
extern StructRNA RNA_ImagePreview;
extern StructRNA RNA_ImageTexture;
extern StructRNA RNA_ImageUser;
extern StructRNA RNA_IntProperty;
extern StructRNA RNA_Itasc;
extern StructRNA RNA_KeyConfig;
extern StructRNA RNA_KeyMap;
extern StructRNA RNA_KeyMapItem;
extern StructRNA RNA_KeyMapItems;
extern StructRNA RNA_KeyboardSensor;
extern StructRNA RNA_KinematicConstraint;
extern StructRNA RNA_Lamp;
extern StructRNA RNA_LampSkySettings;
extern StructRNA RNA_LampTextureSlot;
extern StructRNA RNA_LaplacianDeformModifier;
extern StructRNA RNA_LaplacianSmoothModifier;
extern StructRNA RNA_Lattice;
extern StructRNA RNA_LatticeModifier;
extern StructRNA RNA_LatticePoint;
extern StructRNA RNA_Library;
extern StructRNA RNA_LimitDistanceConstraint;
extern StructRNA RNA_LimitLocationConstraint;
extern StructRNA RNA_LimitRotationConstraint;
extern StructRNA RNA_LimitScaleConstraint;
extern StructRNA RNA_LockedTrackConstraint;
extern StructRNA RNA_Macro;
extern StructRNA RNA_MagicTexture;
extern StructRNA RNA_MarbleTexture;
extern StructRNA RNA_Material;
extern StructRNA RNA_MaterialHalo;
extern StructRNA RNA_MaterialPhysics;
extern StructRNA RNA_MaterialRaytraceMirror;
extern StructRNA RNA_MaterialRaytraceTransparency;
extern StructRNA RNA_MaterialSlot;
extern StructRNA RNA_MaterialStrand;
extern StructRNA RNA_MaterialSubsurfaceScattering;
extern StructRNA RNA_MaterialTextureSlot;
extern StructRNA RNA_MaterialVolume;
extern StructRNA RNA_Menu;
extern StructRNA RNA_Mesh;
extern StructRNA RNA_MeshColor;
extern StructRNA RNA_MeshColorLayer;
extern StructRNA RNA_MeshLoopColorLayer;
extern StructRNA RNA_MeshDeformModifier;
extern StructRNA RNA_MeshEdge;
extern StructRNA RNA_MeshPolygon;
extern StructRNA RNA_MeshTessFace;
extern StructRNA RNA_MeshLoop;
extern StructRNA RNA_MeshFloatProperty;
extern StructRNA RNA_MeshFloatPropertyLayer;
extern StructRNA RNA_MeshIntProperty;
extern StructRNA RNA_MeshIntPropertyLayer;
extern StructRNA RNA_MeshSkinVertexLayer;
extern StructRNA RNA_MeshSkinVertex;
extern StructRNA RNA_MeshSticky;
extern StructRNA RNA_MeshStringProperty;
extern StructRNA RNA_MeshStringPropertyLayer;
extern StructRNA RNA_MeshTextureFace;
extern StructRNA RNA_MeshTextureFaceLayer;
extern StructRNA RNA_MeshTexturePoly;
extern StructRNA RNA_MeshTexturePolyLayer;
extern StructRNA RNA_MeshVertex;
extern StructRNA RNA_MirrorModifier;
extern StructRNA RNA_Modifier;
extern StructRNA RNA_MusgraveTexture;
extern StructRNA RNA_NoiseTexture;
extern StructRNA RNA_Object;
extern StructRNA RNA_ObjectBase;
extern StructRNA RNA_Operator;
extern StructRNA RNA_OperatorFileListElement;
extern StructRNA RNA_OperatorMousePath;
extern StructRNA RNA_OperatorProperties;
extern StructRNA RNA_OperatorStrokeElement;
extern StructRNA RNA_OperatorMacro;
extern StructRNA RNA_PackedFile;
extern StructRNA RNA_Panel;
extern StructRNA RNA_PivotConstraint;
extern StructRNA RNA_PointLamp;
extern StructRNA RNA_PointerProperty;
extern StructRNA RNA_Property;
extern StructRNA RNA_PropertyGroup;
extern StructRNA RNA_PropertyGroupItem;
extern StructRNA RNA_Region;
extern StructRNA RNA_RigidBodyWorld;
extern StructRNA RNA_RigidBodyObject;
extern StructRNA RNA_RigidBodyJointConstraint;
extern StructRNA RNA_Scene;
extern StructRNA RNA_SceneObjects;
extern StructRNA RNA_Scopes;
extern StructRNA RNA_Screen;
extern StructRNA RNA_ScrewModifier;
extern StructRNA RNA_NormalEditModifier;
extern StructRNA RNA_ShrinkwrapConstraint;
extern StructRNA RNA_ShrinkwrapModifier;
extern StructRNA RNA_SimpleDeformModifier;
extern StructRNA RNA_SmoothModifier;
extern StructRNA RNA_SolidifyModifier;
extern StructRNA RNA_Space;
extern StructRNA RNA_SpaceConsole;
extern StructRNA RNA_SpaceFileBrowser;
extern StructRNA RNA_SpaceImageEditor;
extern StructRNA RNA_SpaceInfo;
extern StructRNA RNA_SpaceOutliner;
extern StructRNA RNA_SpaceProperties;
extern StructRNA RNA_SpaceTextEditor;
extern StructRNA RNA_SpaceUserPreferences;
extern StructRNA RNA_SpaceView3D;
extern StructRNA RNA_Spline;
extern StructRNA RNA_SpotLamp;
extern StructRNA RNA_StretchToConstraint;
extern StructRNA RNA_StringProperty;
extern StructRNA RNA_Struct;
extern StructRNA RNA_StucciTexture;
extern StructRNA RNA_SubsurfModifier;
extern StructRNA RNA_SunLamp;
extern StructRNA RNA_SurfaceCurve;
extern StructRNA RNA_SurfaceDeformModifier;
extern StructRNA RNA_SurfaceModifier;
extern StructRNA RNA_TexMapping;
extern StructRNA RNA_Text;
extern StructRNA RNA_TextBox;
extern StructRNA RNA_TextCharacterFormat;
extern StructRNA RNA_TextCurve;
extern StructRNA RNA_TextLine;
extern StructRNA RNA_Texture;
extern StructRNA RNA_TextureSlot;
extern StructRNA RNA_Theme;
extern StructRNA RNA_ThemeConsole;
extern StructRNA RNA_ThemeFileBrowser;
extern StructRNA RNA_ThemeFontStyle;
extern StructRNA RNA_ThemeImageEditor;
extern StructRNA RNA_ThemeInfo;
extern StructRNA RNA_ThemeOutliner;
extern StructRNA RNA_ThemeProperties;
extern StructRNA RNA_ThemeSpaceGeneric;
extern StructRNA RNA_ThemeSpaceGradient;
extern StructRNA RNA_ThemeSpaceListGeneric;
extern StructRNA RNA_ThemeStyle;
extern StructRNA RNA_ThemeTextEditor;
extern StructRNA RNA_ThemeUserInterface;
extern StructRNA RNA_ThemeUserPreferences;
extern StructRNA RNA_ThemeView3D;
extern StructRNA RNA_ThemeWidgetColors;
extern StructRNA RNA_ThemeWidgetStateColors;
extern StructRNA RNA_Timer;
extern StructRNA RNA_ToolSettings;
extern StructRNA RNA_UILayout;
extern StructRNA RNA_UIList;
extern StructRNA RNA_UIPieMenu;
extern StructRNA RNA_UIPopupMenu;
extern StructRNA RNA_UnitSettings;
extern StructRNA RNA_UnknownType;
extern StructRNA RNA_UserPreferences;
extern StructRNA RNA_UserPreferencesEdit;
extern StructRNA RNA_UserPreferencesFilePaths;
extern StructRNA RNA_UserPreferencesInput;
extern StructRNA RNA_UserPreferencesSystem;
extern StructRNA RNA_UserPreferencesView;
extern StructRNA RNA_UserPreferencesWalkNavigation;
extern StructRNA RNA_UserSolidLight;
extern StructRNA RNA_VectorFont;
extern StructRNA RNA_VertexGroup;
extern StructRNA RNA_VertexGroupElement;
extern StructRNA RNA_VoronoiTexture;
extern StructRNA RNA_VoxelData;
extern StructRNA RNA_VoxelDataTexture;
extern StructRNA RNA_WarpModifier;
extern StructRNA RNA_WaveModifier;
extern StructRNA RNA_VertexWeightEditModifier;
extern StructRNA RNA_VertexWeightMixModifier;
extern StructRNA RNA_VertexWeightProximityModifier;
extern StructRNA RNA_Window;
extern StructRNA RNA_WindowManager;
extern StructRNA RNA_WireframeModifier;
extern StructRNA RNA_WoodTexture;
extern StructRNA RNA_World;
extern StructRNA RNA_WorldAmbientOcclusion;
extern StructRNA RNA_WorldLighting;
extern StructRNA RNA_WorldMistSettings;
extern StructRNA RNA_WorldTextureSlot;

/* Pointer
 *
 * These functions will fill in RNA pointers, this can be done in three ways:
 * - a pointer Main is created by just passing the data pointer
 * - a pointer to a datablock can be created with the type and id data pointer
 * - a pointer to data contained in a datablock can be created with the id type
 *   and id data pointer, and the data type and pointer to the struct itself.
 *
 * There is also a way to get a pointer with the information about all structs.
 */

void RNA_main_pointer_create(struct Main *main, PointerRNA *r_ptr);
void RNA_id_pointer_create(struct ID *id, PointerRNA *r_ptr);
void RNA_pointer_create(struct ID *id, StructRNA *type, void *data, PointerRNA *r_ptr);
bool RNA_pointer_is_null(const PointerRNA *ptr);

bool RNA_path_resolved_create(
        PointerRNA *ptr, struct PropertyRNA *prop,
        const int prop_index,
        PathResolvedRNA *r_anim_rna);

void RNA_blender_rna_pointer_create(PointerRNA *r_ptr);
void RNA_pointer_recast(PointerRNA *ptr, PointerRNA *r_ptr);

extern const PointerRNA PointerRNA_NULL;

/* Structs */

StructRNA *RNA_struct_find(const char *identifier);

const char *RNA_struct_identifier(const StructRNA *type);
const char *RNA_struct_ui_name(const StructRNA *type);
const char *RNA_struct_ui_name_raw(const StructRNA *type);
const char *RNA_struct_ui_description(const StructRNA *type);
const char *RNA_struct_ui_description_raw(const StructRNA *type);
const char *RNA_struct_translation_context(const StructRNA *type);
int RNA_struct_ui_icon(const StructRNA *type);

PropertyRNA *RNA_struct_name_property(const StructRNA *type);
const EnumPropertyItem *RNA_struct_property_tag_defines(const StructRNA *type);
PropertyRNA *RNA_struct_iterator_property(StructRNA *type);
StructRNA *RNA_struct_base(StructRNA *type);
const StructRNA *RNA_struct_base_child_of(const StructRNA *type, const StructRNA *parent_type);

bool RNA_struct_is_ID(const StructRNA *type);
bool RNA_struct_is_a(const StructRNA *type, const StructRNA *srna);

bool RNA_struct_undo_check(const StructRNA *type);

StructRegisterFunc RNA_struct_register(StructRNA *type);
StructUnregisterFunc RNA_struct_unregister(StructRNA *type);
void **RNA_struct_instance(PointerRNA *ptr);

void *RNA_struct_py_type_get(StructRNA *srna);
void RNA_struct_py_type_set(StructRNA *srna, void *py_type);

void *RNA_struct_blender_type_get(StructRNA *srna);
void RNA_struct_blender_type_set(StructRNA *srna, void *blender_type);

struct IDProperty *RNA_struct_idprops(PointerRNA *ptr, bool create);
bool RNA_struct_idprops_check(StructRNA *srna);
bool RNA_struct_idprops_register_check(const StructRNA *type);
bool RNA_struct_idprops_datablock_allowed(const StructRNA *type);
bool RNA_struct_idprops_contains_datablock(const StructRNA *type);
bool RNA_struct_idprops_unset(PointerRNA *ptr, const char *identifier);

PropertyRNA *RNA_struct_find_property(PointerRNA *ptr, const char *identifier);
bool RNA_struct_contains_property(PointerRNA *ptr, PropertyRNA *prop_test);
unsigned int RNA_struct_count_properties(StructRNA *srna);

/* lower level functions for access to type properties */
const struct ListBase *RNA_struct_type_properties(StructRNA *srna);
PropertyRNA *RNA_struct_type_find_property(StructRNA *srna, const char *identifier);

FunctionRNA *RNA_struct_find_function(StructRNA *srna, const char *identifier);
const struct ListBase *RNA_struct_type_functions(StructRNA *srna);

char *RNA_struct_name_get_alloc(PointerRNA *ptr, char *fixedbuf, int fixedlen, int *r_len);

bool RNA_struct_available_or_report(struct ReportList *reports, const char *identifier);
bool RNA_struct_bl_idname_ok_or_report(struct ReportList *reports, const char *identifier, const char *sep);

/* Properties
 *
 * Access to struct properties. All this works with RNA pointers rather than
 * direct pointers to the data. */

/* Property Information */

const char *RNA_property_identifier(const PropertyRNA *prop);
const char *RNA_property_description(PropertyRNA *prop);

PropertyType RNA_property_type(PropertyRNA *prop);
PropertySubType RNA_property_subtype(PropertyRNA *prop);
PropertyUnit RNA_property_unit(PropertyRNA *prop);
int RNA_property_flag(PropertyRNA *prop);
int RNA_property_tags(PropertyRNA *prop);
bool RNA_property_builtin(PropertyRNA *prop);
void *RNA_property_py_data_get(PropertyRNA *prop);

int  RNA_property_array_length(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_array_check(PropertyRNA *prop);
int  RNA_property_multi_array_length(PointerRNA *ptr, PropertyRNA *prop, int dimension);
int  RNA_property_array_dimension(PointerRNA *ptr, PropertyRNA *prop, int length[]);
char RNA_property_array_item_char(PropertyRNA *prop, int index);
int  RNA_property_array_item_index(PropertyRNA *prop, char name);

int RNA_property_string_maxlength(PropertyRNA *prop);

const char *RNA_property_ui_name(PropertyRNA *prop);
const char *RNA_property_ui_name_raw(PropertyRNA *prop);
const char *RNA_property_ui_description(PropertyRNA *prop);
const char *RNA_property_ui_description_raw(PropertyRNA *prop);
const char *RNA_property_translation_context(PropertyRNA *prop);
int RNA_property_ui_icon(PropertyRNA *prop);

/* Dynamic Property Information */

void RNA_property_int_range(PointerRNA *ptr, PropertyRNA *prop, int *hardmin, int *hardmax);
void RNA_property_int_ui_range(PointerRNA *ptr, PropertyRNA *prop, int *softmin, int *softmax, int *step);

void RNA_property_float_range(PointerRNA *ptr, PropertyRNA *prop, float *hardmin, float *hardmax);
void RNA_property_float_ui_range(PointerRNA *ptr, PropertyRNA *prop, float *softmin, float *softmax, float *step, float *precision);

int RNA_property_float_clamp(PointerRNA *ptr, PropertyRNA *prop, float *value);
int RNA_property_int_clamp(PointerRNA *ptr, PropertyRNA *prop, int *value);

bool RNA_enum_identifier(const EnumPropertyItem *item, const int value, const char **identifier);
int  RNA_enum_bitflag_identifiers(const EnumPropertyItem *item, const int value, const char **identifier);
bool RNA_enum_name(const EnumPropertyItem *item, const int value, const char **r_name);
bool RNA_enum_description(const EnumPropertyItem *item, const int value, const char **description);
int  RNA_enum_from_value(const EnumPropertyItem *item, const int value);
int  RNA_enum_from_identifier(const EnumPropertyItem *item, const char *identifier);
unsigned int RNA_enum_items_count(const EnumPropertyItem *item);

void RNA_property_enum_items_ex(
        struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, const bool use_static,
        const EnumPropertyItem **r_item, int *r_totitem, bool *r_free);
void RNA_property_enum_items(
        struct bContext *C, PointerRNA *ptr, PropertyRNA *prop,
        const EnumPropertyItem **r_item, int *r_totitem, bool *r_free);
void RNA_property_enum_items_gettexted(
        struct bContext *C, PointerRNA *ptr, PropertyRNA *prop,
        const EnumPropertyItem **r_item, int *r_totitem, bool *r_free);
void RNA_property_enum_items_gettexted_all(
        struct bContext *C, PointerRNA *ptr, PropertyRNA *prop,
        const EnumPropertyItem **r_item, int *r_totitem, bool *r_free);
bool RNA_property_enum_value(struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, const char *identifier, int *r_value);
bool RNA_property_enum_identifier(struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, const int value, const char **identifier);
bool RNA_property_enum_name(struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, const int value, const char **name);
bool RNA_property_enum_name_gettexted(struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, const int value, const char **name);

bool RNA_property_enum_item_from_value(
        struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, const int value,
        EnumPropertyItem *r_item);
bool RNA_property_enum_item_from_value_gettexted(
        struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, const int value,
        EnumPropertyItem *r_item);

int RNA_property_enum_bitflag_identifiers(struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, const int value, const char **identifier);

StructRNA *RNA_property_pointer_type(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_pointer_poll(PointerRNA *ptr, PropertyRNA *prop, PointerRNA *value);

bool RNA_property_editable(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_editable_info(PointerRNA *ptr, PropertyRNA *prop, const char **r_info);
bool RNA_property_editable_index(PointerRNA *ptr, PropertyRNA *prop, int index);
bool RNA_property_editable_flag(PointerRNA *ptr, PropertyRNA *prop); /* without lib check, only checks the flag */
bool RNA_property_animated(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_path_from_ID_check(PointerRNA *ptr, PropertyRNA *prop); /* slow, use with care */

void RNA_property_update(struct bContext *C, PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_update_main(struct Main *bmain, struct Scene *scene, PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_update_check(struct PropertyRNA *prop);

void RNA_property_update_cache_add(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_update_cache_flush(struct Main *bmain, struct Scene *scene);
void RNA_property_update_cache_free(void);

/* Property Data */

bool RNA_property_boolean_get(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_boolean_set(PointerRNA *ptr, PropertyRNA *prop, bool value);
void RNA_property_boolean_get_array(PointerRNA *ptr, PropertyRNA *prop, bool *values);
bool RNA_property_boolean_get_index(PointerRNA *ptr, PropertyRNA *prop, int index);
void RNA_property_boolean_set_array(PointerRNA *ptr, PropertyRNA *prop, const bool *values);
void RNA_property_boolean_set_index(PointerRNA *ptr, PropertyRNA *prop, int index, bool value);
bool RNA_property_boolean_get_default(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_boolean_get_default_array(PointerRNA *ptr, PropertyRNA *prop, bool *values);
bool RNA_property_boolean_get_default_index(PointerRNA *ptr, PropertyRNA *prop, int index);

int RNA_property_int_get(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_int_set(PointerRNA *ptr, PropertyRNA *prop, int value);
void RNA_property_int_get_array(PointerRNA *ptr, PropertyRNA *prop, int *values);
void RNA_property_int_get_array_range(PointerRNA *ptr, PropertyRNA *prop, int values[2]);
int RNA_property_int_get_index(PointerRNA *ptr, PropertyRNA *prop, int index);
void RNA_property_int_set_array(PointerRNA *ptr, PropertyRNA *prop, const int *values);
void RNA_property_int_set_index(PointerRNA *ptr, PropertyRNA *prop, int index, int value);
int RNA_property_int_get_default(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_int_get_default_array(PointerRNA *ptr, PropertyRNA *prop, int *values);
int RNA_property_int_get_default_index(PointerRNA *ptr, PropertyRNA *prop, int index);

float RNA_property_float_get(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_float_set(PointerRNA *ptr, PropertyRNA *prop, float value);
void RNA_property_float_get_array(PointerRNA *ptr, PropertyRNA *prop, float *values);
void RNA_property_float_get_array_range(PointerRNA *ptr, PropertyRNA *prop, float values[2]);
float RNA_property_float_get_index(PointerRNA *ptr, PropertyRNA *prop, int index);
void RNA_property_float_set_array(PointerRNA *ptr, PropertyRNA *prop, const float *values);
void RNA_property_float_set_index(PointerRNA *ptr, PropertyRNA *prop, int index, float value);
float RNA_property_float_get_default(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_float_get_default_array(PointerRNA *ptr, PropertyRNA *prop, float *values);
float RNA_property_float_get_default_index(PointerRNA *ptr, PropertyRNA *prop, int index);

void RNA_property_string_get(PointerRNA *ptr, PropertyRNA *prop, char *value);
char *RNA_property_string_get_alloc(PointerRNA *ptr, PropertyRNA *prop, char *fixedbuf, int fixedlen, int *r_len);
void RNA_property_string_set(PointerRNA *ptr, PropertyRNA *prop, const char *value);
void RNA_property_string_set_bytes(PointerRNA *ptr, PropertyRNA *prop, const char *value, int len);
int RNA_property_string_length(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_string_get_default(PointerRNA *ptr, PropertyRNA *prop, char *value);
char *RNA_property_string_get_default_alloc(PointerRNA *ptr, PropertyRNA *prop, char *fixedbuf, int fixedlen);
int RNA_property_string_default_length(PointerRNA *ptr, PropertyRNA *prop);

int RNA_property_enum_get(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_enum_set(PointerRNA *ptr, PropertyRNA *prop, int value);
int RNA_property_enum_get_default(PointerRNA *ptr, PropertyRNA *prop);
void *RNA_property_enum_py_data_get(PropertyRNA *prop);
int RNA_property_enum_step(const struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, int from_value, int step);

PointerRNA RNA_property_pointer_get(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_pointer_set(PointerRNA *ptr, PropertyRNA *prop, PointerRNA ptr_value);
PointerRNA RNA_property_pointer_get_default(PointerRNA *ptr, PropertyRNA *prop);

void RNA_property_collection_begin(PointerRNA *ptr, PropertyRNA *prop, CollectionPropertyIterator *iter);
void RNA_property_collection_next(CollectionPropertyIterator *iter);
void RNA_property_collection_skip(CollectionPropertyIterator *iter, int num);
void RNA_property_collection_end(CollectionPropertyIterator *iter);
int RNA_property_collection_length(PointerRNA *ptr, PropertyRNA *prop);
int RNA_property_collection_lookup_index(PointerRNA *ptr, PropertyRNA *prop, PointerRNA *t_ptr);
int RNA_property_collection_lookup_int(PointerRNA *ptr, PropertyRNA *prop, int key, PointerRNA *r_ptr);
int RNA_property_collection_lookup_string(PointerRNA *ptr, PropertyRNA *prop, const char *key, PointerRNA *r_ptr);
int RNA_property_collection_assign_int(PointerRNA *ptr, PropertyRNA *prop, const int key, const PointerRNA *assign_ptr);
bool RNA_property_collection_type_get(PointerRNA *ptr, PropertyRNA *prop, PointerRNA *r_ptr);

/* efficient functions to set properties for arrays */
int RNA_property_collection_raw_array(PointerRNA *ptr, PropertyRNA *prop, PropertyRNA *itemprop, RawArray *array);
int RNA_property_collection_raw_get(struct ReportList *reports, PointerRNA *ptr, PropertyRNA *prop, const char *propname, void *array, RawPropertyType type, int len);
int RNA_property_collection_raw_set(struct ReportList *reports, PointerRNA *ptr, PropertyRNA *prop, const char *propname, void *array, RawPropertyType type, int len);
int RNA_raw_type_sizeof(RawPropertyType type);
RawPropertyType RNA_property_raw_type(PropertyRNA *prop);


/* to create ID property groups */
void RNA_property_pointer_add(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_pointer_remove(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_collection_add(PointerRNA *ptr, PropertyRNA *prop, PointerRNA *r_ptr);
bool RNA_property_collection_remove(PointerRNA *ptr, PropertyRNA *prop, int key);
void RNA_property_collection_clear(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_collection_move(PointerRNA *ptr, PropertyRNA *prop, int key, int pos);

/* copy/reset */
bool RNA_property_copy(PointerRNA *ptr, PointerRNA *fromptr, PropertyRNA *prop, int index);
bool RNA_property_reset(PointerRNA *ptr, PropertyRNA *prop, int index);

/* Path
 *
 * Experimental method to refer to structs and properties with a string,
 * using a syntax like: scenes[0].objects["Cube"].data.verts[7].co
 *
 * This provides a way to refer to RNA data while being detached from any
 * particular pointers, which is useful in a number of applications, like
 * UI code or Actions, though efficiency is a concern. */

char *RNA_path_append(const char *path, PointerRNA *ptr, PropertyRNA *prop,
                      int intkey, const char *strkey);
char *RNA_path_back(const char *path);

/* path_resolve() variants only ensure that a valid pointer (and optionally property) exist */
bool RNA_path_resolve(PointerRNA *ptr, const char *path,
                     PointerRNA *r_ptr, PropertyRNA **r_prop);

bool RNA_path_resolve_full(PointerRNA *ptr, const char *path,
                           PointerRNA *r_ptr, PropertyRNA **r_prop, int *r_index);

/* path_resolve_property() variants ensure that pointer + property both exist */
bool RNA_path_resolve_property(PointerRNA *ptr, const char *path,
                               PointerRNA *r_ptr, PropertyRNA **r_prop);

bool RNA_path_resolve_property_full(PointerRNA *ptr, const char *path,
                                    PointerRNA *r_ptr, PropertyRNA **r_prop, int *r_index);

typedef struct PropertyElemRNA PropertyElemRNA;
struct PropertyElemRNA {
	PropertyElemRNA *next, *prev;
	PointerRNA ptr;
	PropertyRNA *prop;
	int index;
};
bool RNA_path_resolve_elements(PointerRNA *ptr, const char *path, struct ListBase *r_elements);

char *RNA_path_from_ID_to_struct(PointerRNA *ptr);
char *RNA_path_from_ID_to_property(PointerRNA *ptr, PropertyRNA *prop);
char *RNA_path_from_ID_to_property_index(PointerRNA *ptr, PropertyRNA *prop, int array_dim, int index);

char *RNA_path_resolve_from_type_to_property(
        struct PointerRNA *ptr, struct PropertyRNA *prop,
        const struct StructRNA *type);

char *RNA_path_full_ID_py(struct ID *id);
char *RNA_path_full_struct_py(struct PointerRNA *ptr);
char *RNA_path_full_property_py_ex(PointerRNA *ptr, PropertyRNA *prop, int index, bool use_fallback);
char *RNA_path_full_property_py(struct PointerRNA *ptr, struct PropertyRNA *prop, int index);
char *RNA_path_struct_property_py(struct PointerRNA *ptr, struct PropertyRNA *prop, int index);
char *RNA_path_property_py(struct PointerRNA *ptr, struct PropertyRNA *prop, int index);

/* Quick name based property access
 *
 * These are just an easier way to access property values without having to
 * call RNA_struct_find_property. The names have to exist as RNA properties
 * for the type in the pointer, if they do not exist an error will be printed.
 *
 * There is no support for pointers and collections here yet, these can be
 * added when ID properties support them. */

bool RNA_boolean_get(PointerRNA *ptr, const char *name);
void RNA_boolean_set(PointerRNA *ptr, const char *name, bool value);
void RNA_boolean_get_array(PointerRNA *ptr, const char *name, bool *values);
void RNA_boolean_set_array(PointerRNA *ptr, const char *name, const bool *values);

int  RNA_int_get(PointerRNA *ptr, const char *name);
void RNA_int_set(PointerRNA *ptr, const char *name, int value);
void RNA_int_get_array(PointerRNA *ptr, const char *name, int *values);
void RNA_int_set_array(PointerRNA *ptr, const char *name, const int *values);

float RNA_float_get(PointerRNA *ptr, const char *name);
void  RNA_float_set(PointerRNA *ptr, const char *name, float value);
void  RNA_float_get_array(PointerRNA *ptr, const char *name, float *values);
void  RNA_float_set_array(PointerRNA *ptr, const char *name, const float *values);

int  RNA_enum_get(PointerRNA *ptr, const char *name);
void RNA_enum_set(PointerRNA *ptr, const char *name, int value);
void RNA_enum_set_identifier(struct bContext *C, PointerRNA *ptr, const char *name, const char *id);
bool RNA_enum_is_equal(struct bContext *C, PointerRNA *ptr, const char *name, const char *enumname);

/* lower level functions that don't use a PointerRNA */
bool RNA_enum_value_from_id(const EnumPropertyItem *item, const char *identifier, int *r_value);
bool RNA_enum_id_from_value(const EnumPropertyItem *item, int value, const char **r_identifier);
bool RNA_enum_icon_from_value(const EnumPropertyItem *item, int value, int *r_icon);
bool RNA_enum_name_from_value(const EnumPropertyItem *item, int value, const char **r_name);

void  RNA_string_get(PointerRNA *ptr, const char *name, char *value);
char *RNA_string_get_alloc(PointerRNA *ptr, const char *name, char *fixedbuf, int fixedlen);
int   RNA_string_length(PointerRNA *ptr, const char *name);
void  RNA_string_set(PointerRNA *ptr, const char *name, const char *value);

/**
 * Retrieve the named property from PointerRNA.
 */
PointerRNA RNA_pointer_get(PointerRNA *ptr, const char *name);
/* Set the property name of PointerRNA ptr to ptr_value */
void RNA_pointer_set(PointerRNA *ptr, const char *name, PointerRNA ptr_value);
void RNA_pointer_add(PointerRNA *ptr, const char *name);

void RNA_collection_begin(PointerRNA *ptr, const char *name, CollectionPropertyIterator *iter);
int  RNA_collection_length(PointerRNA *ptr, const char *name);
void RNA_collection_add(PointerRNA *ptr, const char *name, PointerRNA *r_value);
void RNA_collection_clear(PointerRNA *ptr, const char *name);

#define RNA_BEGIN(sptr, itemptr, propname)                                    \
	{                                                                         \
		CollectionPropertyIterator rna_macro_iter;                            \
		for (RNA_collection_begin(sptr, propname, &rna_macro_iter);           \
		     rna_macro_iter.valid;                                            \
		     RNA_property_collection_next(&rna_macro_iter))                   \
		{                                                                     \
			PointerRNA itemptr = rna_macro_iter.ptr;

#define RNA_END                                                               \
		}                                                                     \
		RNA_property_collection_end(&rna_macro_iter);                         \
	} ((void)0)

#define RNA_PROP_BEGIN(sptr, itemptr, prop)                                   \
	{                                                                         \
		CollectionPropertyIterator rna_macro_iter;                            \
		for (RNA_property_collection_begin(sptr, prop, &rna_macro_iter);      \
		     rna_macro_iter.valid;                                            \
		     RNA_property_collection_next(&rna_macro_iter))                   \
		{                                                                     \
			PointerRNA itemptr = rna_macro_iter.ptr;

#define RNA_PROP_END                                                          \
		}                                                                     \
		RNA_property_collection_end(&rna_macro_iter);                         \
	}

#define RNA_STRUCT_BEGIN(sptr, prop)                                          \
	{                                                                         \
		CollectionPropertyIterator rna_macro_iter;                            \
		for (RNA_property_collection_begin(                                   \
		             sptr,                                                    \
		             RNA_struct_iterator_property((sptr)->type),              \
		             &rna_macro_iter);                                        \
		     rna_macro_iter.valid;                                            \
		     RNA_property_collection_next(&rna_macro_iter))                   \
		{                                                                     \
			PropertyRNA *prop = (PropertyRNA *)rna_macro_iter.ptr.data;

#define RNA_STRUCT_END                                                        \
		}                                                                     \
		RNA_property_collection_end(&rna_macro_iter);                         \
	}

/* check if the idproperty exists, for operators */
bool RNA_property_is_set_ex(PointerRNA *ptr, PropertyRNA *prop, bool use_ghost);
bool RNA_property_is_set(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_unset(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_struct_property_is_set_ex(PointerRNA *ptr, const char *identifier, bool use_ghost);
bool RNA_struct_property_is_set(PointerRNA *ptr, const char *identifier);
bool RNA_property_is_idprop(const PropertyRNA *prop);
bool RNA_property_is_unlink(PropertyRNA *prop);
void RNA_struct_property_unset(PointerRNA *ptr, const char *identifier);

/* python compatible string representation of this property, (must be freed!) */
char *RNA_property_as_string(struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, int index, int max_prop_length);
char *RNA_pointer_as_string_id(struct bContext *C, PointerRNA *ptr);
char *RNA_pointer_as_string(struct bContext *C, PointerRNA *ptr, PropertyRNA *prop_ptr, PointerRNA *ptr_prop);
char *RNA_pointer_as_string_keywords_ex(struct bContext *C, PointerRNA *ptr,
                                        const bool skip_optional_value, const bool all_args, const bool nested_args,
                                        const int max_prop_length,
                                        PropertyRNA *iterprop);
char *RNA_pointer_as_string_keywords(struct bContext *C, PointerRNA *ptr,
                                     const bool skip_optional_value, const bool all_args, const bool nested_args,
                                     const int max_prop_length);
char *RNA_function_as_string_keywords(struct bContext *C, FunctionRNA *func,
                                      const bool as_function, const bool all_args,
                                      const int max_prop_length);

/* Function */

const char *RNA_function_identifier(FunctionRNA *func);
const char *RNA_function_ui_description(FunctionRNA *func);
const char *RNA_function_ui_description_raw(FunctionRNA *func);
int RNA_function_flag(FunctionRNA *func);
int RNA_function_defined(FunctionRNA *func);

PropertyRNA *RNA_function_get_parameter(PointerRNA *ptr, FunctionRNA *func, int index);
PropertyRNA *RNA_function_find_parameter(PointerRNA *ptr, FunctionRNA *func, const char *identifier);
const struct ListBase *RNA_function_defined_parameters(FunctionRNA *func);

/* Utility */

int RNA_parameter_flag(PropertyRNA *prop);

ParameterList *RNA_parameter_list_create(ParameterList *parms, PointerRNA *ptr, FunctionRNA *func);
void RNA_parameter_list_free(ParameterList *parms);
int  RNA_parameter_list_size(ParameterList *parms);
int  RNA_parameter_list_arg_count(ParameterList *parms);
int  RNA_parameter_list_ret_count(ParameterList *parms);

void RNA_parameter_list_begin(ParameterList *parms, ParameterIterator *iter);
void RNA_parameter_list_next(ParameterIterator *iter);
void RNA_parameter_list_end(ParameterIterator *iter);

void RNA_parameter_get(ParameterList *parms, PropertyRNA *parm, void **value);
void RNA_parameter_get_lookup(ParameterList *parms, const char *identifier, void **value);
void RNA_parameter_set(ParameterList *parms, PropertyRNA *parm, const void *value);
void RNA_parameter_set_lookup(ParameterList *parms, const char *identifier, const void *value);
/* Only for PROP_DYNAMIC properties! */
int RNA_parameter_dynamic_length_get(ParameterList *parms, PropertyRNA *parm);
int RNA_parameter_dynamic_length_get_data(ParameterList *parms, PropertyRNA *parm, void *data);
void RNA_parameter_dynamic_length_set(ParameterList *parms, PropertyRNA *parm, int length);
void RNA_parameter_dynamic_length_set_data(ParameterList *parms, PropertyRNA *parm, void *data, int length);

int RNA_function_call(struct bContext *C, struct ReportList *reports, PointerRNA *ptr,
                      FunctionRNA *func, ParameterList *parms);
int RNA_function_call_lookup(struct bContext *C, struct ReportList *reports, PointerRNA *ptr,
                             const char *identifier, ParameterList *parms);

int RNA_function_call_direct(struct bContext *C, struct ReportList *reports, PointerRNA *ptr,
                             FunctionRNA *func, const char *format, ...) ATTR_PRINTF_FORMAT(5, 6);
int RNA_function_call_direct_lookup(struct bContext *C, struct ReportList *reports, PointerRNA *ptr,
                                    const char *identifier, const char *format, ...) ATTR_PRINTF_FORMAT(5, 6);
int RNA_function_call_direct_va(struct bContext *C, struct ReportList *reports, PointerRNA *ptr,
                                FunctionRNA *func, const char *format, va_list args);
int RNA_function_call_direct_va_lookup(struct bContext *C, struct ReportList *reports, PointerRNA *ptr,
                                       const char *identifier, const char *format, va_list args);

const char *RNA_translate_ui_text(
        const char *text, const char *text_ctxt, struct StructRNA *type, struct PropertyRNA *prop, int translate);

/* ID */

short RNA_type_to_ID_code(const StructRNA *type);
StructRNA *ID_code_to_RNA_type(short idcode);


#define RNA_POINTER_INVALIDATE(ptr) {                                         \
	/* this is checked for validity */                                        \
	(ptr)->type =                                                             \
	/* should not be needed but prevent bad pointer access, just in case */   \
	(ptr)->id.data = NULL;                                                    \
} (void)0

/* macro which inserts the function name */
#if defined __GNUC__
#  define RNA_warning(format, args ...) _RNA_warning("%s: " format "\n", __func__, ##args)
#else
#  define RNA_warning(format, ...) _RNA_warning("%s: " format "\n", __FUNCTION__, __VA_ARGS__)
#endif

void _RNA_warning(const char *format, ...) ATTR_PRINTF_FORMAT(1, 2);

/* Equals test (skips pointers and collections)
 * is_strict false assumes uninitialized properties are equal */

typedef enum eRNAEqualsMode {
	RNA_EQ_STRICT,          /* set/unset ignored */
	RNA_EQ_UNSET_MATCH_ANY, /* unset property matches anything */
	RNA_EQ_UNSET_MATCH_NONE /* unset property never matches set property */
} eRNAEqualsMode;

bool RNA_property_equals(struct PointerRNA *a, struct PointerRNA *b, struct PropertyRNA *prop, eRNAEqualsMode mode);
bool RNA_struct_equals(struct PointerRNA *a, struct PointerRNA *b, eRNAEqualsMode mode);

#ifdef __cplusplus
}
#endif

#endif /* __RNA_ACCESS_H__ */
