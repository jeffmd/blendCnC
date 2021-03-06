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

#ifndef __BKE_CAMERA_H__
#define __BKE_CAMERA_H__

/** \file BKE_camera.h
 *  \ingroup bke
 *  \brief Camera datablock and utility functions.
 */
#ifdef __cplusplus
extern "C" {
#endif

#include "DNA_vec_types.h"

struct Camera;
struct GPUFXSettings;
struct Main;
struct Object;
struct RegionView3D;
struct Scene;
struct View3D;
struct rctf;

/* Camera Datablock */

void BKE_camera_init(struct Camera *cam);
void *BKE_camera_add(struct Main *bmain, const char *name);
void BKE_camera_copy_data(struct Main *bmain, struct Camera *cam_dst, const struct Camera *cam_src, const int flag);
struct Camera *BKE_camera_copy(struct Main *bmain, const struct Camera *cam);
void BKE_camera_make_local(struct Main *bmain, struct Camera *cam, const bool lib_local);
void BKE_camera_free(struct Camera *ca);

/* Camera Usage */

int BKE_camera_sensor_fit(int sensor_fit, float sizex, float sizey);
float BKE_camera_sensor_size(int sensor_fit, float sensor_x, float sensor_y);

/* Camera Parameters:
 *
 * Intermediate struct for storing camera parameters from various sources,
 * to unify computation of viewplane, window matrix, ... */

typedef struct CameraParams {
	/* lens */
	bool is_ortho;
	float lens;
	float ortho_scale;
	float zoom;

	float shiftx;
	float shifty;
	float offsetx;
	float offsety;

	/* sensor */
	float sensor_x;
	float sensor_y;
	int sensor_fit;

	/* clipping */
	float clipsta;
	float clipend;

	/* fields */
	int use_fields;
	int field_second;
	int field_odd;

	/* computed viewplane */
	float ycor;
	float viewdx;
	float viewdy;
	rctf viewplane;

	/* computed matrix */
	float winmat[4][4];
} CameraParams;

/* values for CameraParams.zoom, need to be taken into account for some operations */
#define CAMERA_PARAM_ZOOM_INIT_CAMOB 1.0f
#define CAMERA_PARAM_ZOOM_INIT_PERSP 2.0f

void BKE_camera_params_init(CameraParams *params);
void BKE_camera_params_from_object(CameraParams *params, const struct Object *camera);
void BKE_camera_params_from_view3d(CameraParams *params, const struct View3D *v3d, const struct RegionView3D *rv3d);

void BKE_camera_params_compute_viewplane(CameraParams *params, int winx, int winy, float aspx, float aspy);
void BKE_camera_params_compute_matrix(CameraParams *params);

/* Camera View Frame */

void BKE_camera_view_frame_ex(
        const struct Scene *scene, const struct Camera *camera,
        const float drawsize, const bool do_clip, const float scale[3],
        float r_asp[2], float r_shift[2], float *r_drawsize, float r_vec[4][3]);
void BKE_camera_view_frame(
        const struct Scene *scene, const struct Camera *camera,
        float r_vec[4][3]);

bool BKE_camera_view_frame_fit_to_scene(
        struct Main *bmain, struct Scene *scene, struct View3D *v3d, struct Object *camera_ob,
        float r_co[3], float *r_scale);
bool BKE_camera_view_frame_fit_to_coords(
        const struct Scene *scene,
        const float (*cos)[3], int num_cos,
        const struct Object *camera_ob,
        float r_co[3], float *r_scale);

void BKE_camera_model_matrix(struct Object *camera, float r_modelmat[4][4]);
void BKE_camera_view_matrix(struct Object *camera, float r_viewmat[4][4]);

#ifdef __cplusplus
}
#endif

#endif
