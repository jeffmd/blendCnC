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
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */

/** \file blender/blenkernel/intern/camera.c
 *  \ingroup bke
 */

#include <stdlib.h>
#include <stddef.h>

#include "DNA_camera_types.h"
#include "DNA_lamp_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_view3d_types.h"
#include "DNA_ID.h"

#include "BLI_math.h"
#include "BLI_rect.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BKE_camera.h"
#include "BKE_object.h"
#include "BKE_library.h"
#include "BKE_library_query.h"
#include "BKE_library_remap.h"
#include "BKE_main.h"
#include "BKE_scene.h"
#include "BKE_screen.h"

/****************************** Camera Datablock *****************************/

void BKE_camera_init(Camera *cam)
{
	BLI_assert(MEMCMP_STRUCT_OFS_IS_ZERO(cam, id));

	cam->lens = 35.0f;
	cam->sensor_x = DEFAULT_SENSOR_WIDTH;
	cam->sensor_y = DEFAULT_SENSOR_HEIGHT;
	cam->clipsta = 0.1f;
	cam->clipend = 100.0f;
	cam->drawsize = 0.5f;
	cam->ortho_scale = 6.0;
	cam->flag |= CAM_SHOWPASSEPARTOUT;
	cam->passepartalpha = 0.5f;

}

void *BKE_camera_add(Main *bmain, const char *name)
{
	Camera *cam;

	cam =  BKE_libblock_alloc(bmain, ID_CA, name, 0);

	BKE_camera_init(cam);

	return cam;
}

/**
 * Only copy internal data of Camera ID from source to already allocated/initialized destination.
 * You probably nerver want to use that directly, use id_copy or BKE_id_copy_ex for typical needs.
 *
 * WARNING! This function will not handle ID user count!
 *
 * \param flag: Copying options (see BKE_library.h's LIB_ID_COPY_... flags for more).
 */
void BKE_camera_copy_data(Main *UNUSED(bmain), Camera *UNUSED(cam_dst), const Camera *UNUSED(cam_src), const int UNUSED(flag))
{
	/* Nothing to do! */
}

Camera *BKE_camera_copy(Main *bmain, const Camera *cam)
{
	Camera *cam_copy;
	BKE_id_copy_ex(bmain, &cam->id, (ID **)&cam_copy, 0, false);
	return cam_copy;
}

void BKE_camera_make_local(Main *bmain, Camera *cam, const bool lib_local)
{
	BKE_id_make_local_generic(bmain, &cam->id, true, lib_local);
}

/** Free (or release) any data used by this camera (does not free the camera itself). */
void BKE_camera_free(Camera *ca)
{
}

/******************************** Camera Usage *******************************/

float BKE_camera_sensor_size(int sensor_fit, float sensor_x, float sensor_y)
{
	/* sensor size used to fit to. for auto, sensor_x is both x and y. */
	if (sensor_fit == CAMERA_SENSOR_FIT_VERT)
		return sensor_y;

	return sensor_x;
}

int BKE_camera_sensor_fit(int sensor_fit, float sizex, float sizey)
{
	if (sensor_fit == CAMERA_SENSOR_FIT_AUTO) {
		if (sizex >= sizey)
			return CAMERA_SENSOR_FIT_HOR;
		else
			return CAMERA_SENSOR_FIT_VERT;
	}

	return sensor_fit;
}

/******************************** Camera Params *******************************/

void BKE_camera_params_init(CameraParams *params)
{
	memset(params, 0, sizeof(CameraParams));

	/* defaults */
	params->sensor_x = DEFAULT_SENSOR_WIDTH;
	params->sensor_y = DEFAULT_SENSOR_HEIGHT;
	params->sensor_fit = CAMERA_SENSOR_FIT_AUTO;

	params->zoom = 1.0f;

	/* fallback for non camera objects */
	params->clipsta = 0.1f;
	params->clipend = 100.0f;
}

void BKE_camera_params_from_object(CameraParams *params, const Object *ob)
{
	if (!ob)
		return;

	if (ob->type == OB_CAMERA) {
		/* camera object */
		Camera *cam = ob->data;

		if (cam->type == CAM_ORTHO)
			params->is_ortho = true;
		params->lens = cam->lens;
		params->ortho_scale = cam->ortho_scale;

		params->shiftx = cam->shiftx;
		params->shifty = cam->shifty;

		params->sensor_x = cam->sensor_x;
		params->sensor_y = cam->sensor_y;
		params->sensor_fit = cam->sensor_fit;

		params->clipsta = cam->clipsta;
		params->clipend = cam->clipend;
	}
	else if (ob->type == OB_LAMP) {
		/* lamp object */
		Lamp *la = ob->data;
		float fac = cosf(la->spotsize * 0.5f);
		float phi = acosf(fac);

		params->lens = 16.0f * fac / sinf(phi);
		if (params->lens == 0.0f)
			params->lens = 35.0f;

		params->clipsta = la->clipsta;
		params->clipend = la->clipend;
	}
	else {
		params->lens = 35.0f;
	}
}

void BKE_camera_params_from_view3d(CameraParams *params, const View3D *v3d, const RegionView3D *rv3d)
{
	/* common */
	params->lens = v3d->lens;
	params->clipsta = v3d->near;
	params->clipend = v3d->far;

	if (rv3d->persp == RV3D_CAMOB) {
		/* camera view */
		BKE_camera_params_from_object(params, v3d->camera);

		params->zoom = BKE_screen_view3d_zoom_to_fac(rv3d->camzoom);

		params->offsetx = 2.0f * rv3d->camdx * params->zoom;
		params->offsety = 2.0f * rv3d->camdy * params->zoom;

		params->shiftx *= params->zoom;
		params->shifty *= params->zoom;

		params->zoom = CAMERA_PARAM_ZOOM_INIT_CAMOB / params->zoom;
	}
	else if (rv3d->persp == RV3D_ORTHO) {
		/* orthographic view */
		float sensor_size = BKE_camera_sensor_size(params->sensor_fit, params->sensor_x, params->sensor_y);
		params->clipend *= 0.5f;    // otherwise too extreme low zbuffer quality
		params->clipsta = -params->clipend;

		params->is_ortho = true;
		/* make sure any changes to this match ED_view3d_radius_to_dist_ortho() */
		params->ortho_scale = rv3d->dist * sensor_size / v3d->lens;
		params->zoom = CAMERA_PARAM_ZOOM_INIT_PERSP;
	}
	else {
		/* perspective view */
		params->zoom = CAMERA_PARAM_ZOOM_INIT_PERSP;
	}
}

void BKE_camera_params_compute_viewplane(CameraParams *params, int winx, int winy, float xasp, float yasp)
{
	rctf viewplane;
	float pixsize, viewfac, sensor_size, dx, dy;
	int sensor_fit;

	/* fields rendering */
	params->ycor = yasp / xasp;
	if (params->use_fields)
		params->ycor *= 2.0f;

	if (params->is_ortho) {
		/* orthographic camera */
		/* scale == 1.0 means exact 1 to 1 mapping */
		pixsize = params->ortho_scale;
	}
	else {
		/* perspective camera */
		sensor_size = BKE_camera_sensor_size(params->sensor_fit, params->sensor_x, params->sensor_y);
		pixsize = (sensor_size * params->clipsta) / params->lens;
	}

	/* determine sensor fit */
	sensor_fit = BKE_camera_sensor_fit(params->sensor_fit, xasp * winx, yasp * winy);

	if (sensor_fit == CAMERA_SENSOR_FIT_HOR)
		viewfac = winx;
	else
		viewfac = params->ycor * winy;

	pixsize /= viewfac;

	/* extra zoom factor */
	pixsize *= params->zoom;

	/* compute view plane:
	 * fully centered, zbuffer fills in jittered between -.5 and +.5 */
	viewplane.xmin = -0.5f * (float)winx;
	viewplane.ymin = -0.5f * params->ycor * (float)winy;
	viewplane.xmax =  0.5f * (float)winx;
	viewplane.ymax =  0.5f * params->ycor * (float)winy;

	/* lens shift and offset */
	dx = params->shiftx * viewfac + winx * params->offsetx;
	dy = params->shifty * viewfac + winy * params->offsety;

	viewplane.xmin += dx;
	viewplane.ymin += dy;
	viewplane.xmax += dx;
	viewplane.ymax += dy;

	/* fields offset */
	if (params->field_second) {
		if (params->field_odd) {
			viewplane.ymin -= 0.5f * params->ycor;
			viewplane.ymax -= 0.5f * params->ycor;
		}
		else {
			viewplane.ymin += 0.5f * params->ycor;
			viewplane.ymax += 0.5f * params->ycor;
		}
	}

	/* the window matrix is used for clipping, and not changed during OSA steps */
	/* using an offset of +0.5 here would give clip errors on edges */
	viewplane.xmin *= pixsize;
	viewplane.xmax *= pixsize;
	viewplane.ymin *= pixsize;
	viewplane.ymax *= pixsize;

	/* Used for rendering (offset by near-clip with perspective views), passed to RE_SetPixelSize.
	 * For viewport drawing 'RegionView3D.pixsize'. */
	params->viewdx = pixsize;
	params->viewdy = params->ycor * pixsize;
	params->viewplane = viewplane;
}

/* viewplane is assumed to be already computed */
void BKE_camera_params_compute_matrix(CameraParams *params)
{
	rctf viewplane = params->viewplane;

	/* compute projection matrix */
	if (params->is_ortho)
		orthographic_m4(params->winmat, viewplane.xmin, viewplane.xmax,
		                viewplane.ymin, viewplane.ymax, params->clipsta, params->clipend);
	else
		perspective_m4(params->winmat, viewplane.xmin, viewplane.xmax,
		               viewplane.ymin, viewplane.ymax, params->clipsta, params->clipend);
}

/***************************** Camera View Frame *****************************/

void BKE_camera_view_frame_ex(
        const Scene *scene, const Camera *camera,
        const float drawsize, const bool do_clip, const float scale[3],
        float r_asp[2], float r_shift[2], float *r_drawsize, float r_vec[4][3])
{
	float facx, facy;
	float depth;

	/* aspect correcton */
	r_asp[0] = 1.0f;
	r_asp[1] = 1.0f;

	if (camera->type == CAM_ORTHO) {
		facx = 0.5f * camera->ortho_scale * r_asp[0] * scale[0];
		facy = 0.5f * camera->ortho_scale * r_asp[1] * scale[1];
		r_shift[0] = camera->shiftx * camera->ortho_scale * scale[0];
		r_shift[1] = camera->shifty * camera->ortho_scale * scale[1];
		depth = do_clip ? -((camera->clipsta * scale[2]) + 0.1f) : -(drawsize * 2.0f) * scale[2];

		*r_drawsize = 0.5f * camera->ortho_scale;
	}
	else {
		/* that way it's always visible - clipsta+0.1 */
		float fac, scale_x, scale_y;
		float half_sensor = 0.5f * ((camera->sensor_fit == CAMERA_SENSOR_FIT_VERT) ?
		                            (camera->sensor_y) : (camera->sensor_x));


		if (do_clip) {
			/* fixed depth, variable size (avoids exceeding clipping range) */
			/* r_drawsize shouldn't be used in this case, set to dummy value */
			*r_drawsize = 1.0f;
			depth = -(camera->clipsta + 0.1f) * scale[2];
			fac = depth / (camera->lens / (-half_sensor));
			scale_x = scale[0] / scale[2];
			scale_y = scale[1] / scale[2];
		}
		else {
			/* fixed size, variable depth (stays a reasonable size in the 3D view) */
			*r_drawsize = drawsize / ((scale[0] + scale[1] + scale[2]) / 3.0f);
			depth = *r_drawsize * camera->lens / (-half_sensor) * scale[2];
			fac = *r_drawsize;
			scale_x = scale[0];
			scale_y = scale[1];
		}

		facx = fac * r_asp[0] * scale_x;
		facy = fac * r_asp[1] * scale_y;
		r_shift[0] = camera->shiftx * fac * 2.0f * scale_x;
		r_shift[1] = camera->shifty * fac * 2.0f * scale_y;
	}

	r_vec[0][0] = r_shift[0] + facx; r_vec[0][1] = r_shift[1] + facy; r_vec[0][2] = depth;
	r_vec[1][0] = r_shift[0] + facx; r_vec[1][1] = r_shift[1] - facy; r_vec[1][2] = depth;
	r_vec[2][0] = r_shift[0] - facx; r_vec[2][1] = r_shift[1] - facy; r_vec[2][2] = depth;
	r_vec[3][0] = r_shift[0] - facx; r_vec[3][1] = r_shift[1] + facy; r_vec[3][2] = depth;
}

void BKE_camera_view_frame(const Scene *scene, const Camera *camera, float r_vec[4][3])
{
	float dummy_asp[2];
	float dummy_shift[2];
	float dummy_drawsize;
	const float dummy_scale[3] = {1.0f, 1.0f, 1.0f};

	BKE_camera_view_frame_ex(scene, camera, 0.0, true, dummy_scale,
	                         dummy_asp, dummy_shift, &dummy_drawsize, r_vec);
}

#define CAMERA_VIEWFRAME_NUM_PLANES 4

typedef struct CameraViewFrameData {
	float plane_tx[CAMERA_VIEWFRAME_NUM_PLANES][4];  /* 4 planes */
	float normal_tx[CAMERA_VIEWFRAME_NUM_PLANES][3];
	float dist_vals_sq[CAMERA_VIEWFRAME_NUM_PLANES];  /* distance squared (signed) */
	unsigned int tot;

	/* Ortho camera only. */
	bool is_ortho;
	float camera_no[3];
	float dist_to_cam;

	/* Not used by callbacks... */
	float camera_rotmat[3][3];
} CameraViewFrameData;

static void camera_to_frame_view_cb(const float co[3], void *user_data)
{
	CameraViewFrameData *data = (CameraViewFrameData *)user_data;
	unsigned int i;

	for (i = 0; i < CAMERA_VIEWFRAME_NUM_PLANES; i++) {
		const float nd = dist_signed_squared_to_plane_v3(co, data->plane_tx[i]);
		CLAMP_MAX(data->dist_vals_sq[i], nd);
	}

	if (data->is_ortho) {
		const float d = dot_v3v3(data->camera_no, co);
		CLAMP_MAX(data->dist_to_cam, d);
	}

	data->tot++;
}

static void camera_frame_fit_data_init(
        const Scene *scene, const Object *ob,
        CameraParams *params, CameraViewFrameData *data)
{
	float camera_rotmat_transposed_inversed[4][4];
	unsigned int i;

	/* setup parameters */
	BKE_camera_params_init(params);
	BKE_camera_params_from_object(params, ob);

	BKE_camera_params_compute_viewplane(params, 1, 1, 1.0f, 1.0f);
	BKE_camera_params_compute_matrix(params);

	/* initialize callback data */
	copy_m3_m4(data->camera_rotmat, (float (*)[4])ob->obmat);
	normalize_m3(data->camera_rotmat);
	/* To transform a plane which is in its homogeneous representation (4d vector),
	 * we need the inverse of the transpose of the transform matrix... */
	copy_m4_m3(camera_rotmat_transposed_inversed, data->camera_rotmat);
	transpose_m4(camera_rotmat_transposed_inversed);
	invert_m4(camera_rotmat_transposed_inversed);

	/* Extract frustum planes from projection matrix. */
	planes_from_projmat(params->winmat,
	                    /*   left              right                 top              bottom        near  far */
	                    data->plane_tx[2], data->plane_tx[0], data->plane_tx[3], data->plane_tx[1], NULL, NULL);

	/* Rotate planes and get normals from them */
	for (i = 0; i < CAMERA_VIEWFRAME_NUM_PLANES; i++) {
		mul_m4_v4(camera_rotmat_transposed_inversed, data->plane_tx[i]);
		normalize_v3_v3(data->normal_tx[i], data->plane_tx[i]);
	}

	copy_v4_fl(data->dist_vals_sq, FLT_MAX);
	data->tot = 0;
	data->is_ortho = params->is_ortho;
	if (params->is_ortho) {
		/* we want (0, 0, -1) transformed by camera_rotmat, this is a quicker shortcut. */
		negate_v3_v3(data->camera_no, data->camera_rotmat[2]);
		data->dist_to_cam = FLT_MAX;
	}
}

static bool camera_frame_fit_calc_from_data(
        CameraParams *params, CameraViewFrameData *data, float r_co[3], float *r_scale)
{
	float plane_tx[CAMERA_VIEWFRAME_NUM_PLANES][4];
	unsigned int i;

	if (data->tot <= 1) {
		return false;
	}

	if (params->is_ortho) {
		const float *cam_axis_x = data->camera_rotmat[0];
		const float *cam_axis_y = data->camera_rotmat[1];
		const float *cam_axis_z = data->camera_rotmat[2];
		float dists[CAMERA_VIEWFRAME_NUM_PLANES];
		float scale_diff;

		/* apply the dist-from-plane's to the transformed plane points */
		for (i = 0; i < CAMERA_VIEWFRAME_NUM_PLANES; i++) {
			dists[i] = sqrtf_signed(data->dist_vals_sq[i]);
		}

		if ((dists[0] + dists[2]) > (dists[1] + dists[3])) {
			scale_diff = (dists[1] + dists[3]) *
			             (BLI_rctf_size_x(&params->viewplane) / BLI_rctf_size_y(&params->viewplane));
		}
		else {
			scale_diff = (dists[0] + dists[2]) *
			             (BLI_rctf_size_y(&params->viewplane) / BLI_rctf_size_x(&params->viewplane));
		}
		*r_scale = params->ortho_scale - scale_diff;

		zero_v3(r_co);
		madd_v3_v3fl(r_co, cam_axis_x, (dists[2] - dists[0]) * 0.5f + params->shiftx * scale_diff);
		madd_v3_v3fl(r_co, cam_axis_y, (dists[1] - dists[3]) * 0.5f + params->shifty * scale_diff);
		madd_v3_v3fl(r_co, cam_axis_z, -(data->dist_to_cam - 1.0f - params->clipsta));

		return true;
	}
	else {
		float plane_isect_1[3], plane_isect_1_no[3], plane_isect_1_other[3];
		float plane_isect_2[3], plane_isect_2_no[3], plane_isect_2_other[3];

		float plane_isect_pt_1[3], plane_isect_pt_2[3];

		/* apply the dist-from-plane's to the transformed plane points */
		for (i = 0; i < CAMERA_VIEWFRAME_NUM_PLANES; i++) {
			float co[3];
			mul_v3_v3fl(co, data->normal_tx[i], sqrtf_signed(data->dist_vals_sq[i]));
			plane_from_point_normal_v3(plane_tx[i], co, data->normal_tx[i]);
		}

		if ((!isect_plane_plane_v3(plane_tx[0], plane_tx[2], plane_isect_1, plane_isect_1_no)) ||
		    (!isect_plane_plane_v3(plane_tx[1], plane_tx[3], plane_isect_2, plane_isect_2_no)))
		{
			return false;
		}

		add_v3_v3v3(plane_isect_1_other, plane_isect_1, plane_isect_1_no);
		add_v3_v3v3(plane_isect_2_other, plane_isect_2, plane_isect_2_no);

		if (isect_line_line_v3(plane_isect_1, plane_isect_1_other,
		                       plane_isect_2, plane_isect_2_other,
		                       plane_isect_pt_1, plane_isect_pt_2) != 0)
		{
			float cam_plane_no[3];
			float plane_isect_delta[3];
			float plane_isect_delta_len;

			float shift_fac = BKE_camera_sensor_size(params->sensor_fit, params->sensor_x, params->sensor_y) /
			                  params->lens;

			/* we want (0, 0, -1) transformed by camera_rotmat, this is a quicker shortcut. */
			negate_v3_v3(cam_plane_no, data->camera_rotmat[2]);

			sub_v3_v3v3(plane_isect_delta, plane_isect_pt_2, plane_isect_pt_1);
			plane_isect_delta_len = len_v3(plane_isect_delta);

			if (dot_v3v3(plane_isect_delta, cam_plane_no) > 0.0f) {
				copy_v3_v3(r_co, plane_isect_pt_1);

				/* offset shift */
				normalize_v3(plane_isect_1_no);
				madd_v3_v3fl(r_co, plane_isect_1_no, params->shifty * plane_isect_delta_len * shift_fac);
			}
			else {
				copy_v3_v3(r_co, plane_isect_pt_2);

				/* offset shift */
				normalize_v3(plane_isect_2_no);
				madd_v3_v3fl(r_co, plane_isect_2_no, params->shiftx * plane_isect_delta_len * shift_fac);
			}

			return true;
		}
	}

	return false;
}

/* don't move the camera, just yield the fit location */
/* r_scale only valid/useful for ortho cameras */
bool BKE_camera_view_frame_fit_to_scene(
        Main *bmain, Scene *scene, struct View3D *v3d, Object *camera_ob, float r_co[3], float *r_scale)
{
	CameraParams params;
	CameraViewFrameData data_cb;

	/* just in case */
	*r_scale = 1.0f;

	camera_frame_fit_data_init(scene, camera_ob, &params, &data_cb);

	/* run callback on all visible points */
	BKE_scene_foreach_display_point(bmain, scene, v3d, BA_SELECT, camera_to_frame_view_cb, &data_cb);

	return camera_frame_fit_calc_from_data(&params, &data_cb, r_co, r_scale);
}

bool BKE_camera_view_frame_fit_to_coords(
        const Scene *scene, const float (*cos)[3], int num_cos, const Object *camera_ob,
        float r_co[3], float *r_scale)
{
	CameraParams params;
	CameraViewFrameData data_cb;

	/* just in case */
	*r_scale = 1.0f;

	camera_frame_fit_data_init(scene, camera_ob, &params, &data_cb);

	/* run callback on all given coordinates */
	while (num_cos--) {
		camera_to_frame_view_cb(cos[num_cos], &data_cb);
	}

	return camera_frame_fit_calc_from_data(&params, &data_cb, r_co, r_scale);
}

void BKE_camera_model_matrix(Object *camera, float r_modelmat[4][4])
{
	copy_m4_m4(r_modelmat, camera->obmat);
	normalize_m4(r_modelmat);
}

/* the view matrix is used by the viewport drawing, it is basically the inverted model matrix */
void BKE_camera_view_matrix(Object *camera, float r_viewmat[4][4])
{
	BKE_camera_model_matrix(camera, r_viewmat);
	invert_m4(r_viewmat);
}

