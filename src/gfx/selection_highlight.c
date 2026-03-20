#include "gfx/selection_highlight.h"

#include <math.h>
#include <stddef.h>

#include "game/raycast.h"
#include "raylib.h"

typedef struct SelectionFaceAxes {
  int normal_axis;
  int axis_u;
  int axis_v;
  int normal_direction;
} SelectionFaceAxes;

static float selection_axis_min(Vector3 mins, int axis) {
  if (axis == 0) {
    return mins.x;
  }
  if (axis == 1) {
    return mins.y;
  }
  return mins.z;
}

static float selection_axis_max(Vector3 maxs, int axis) {
  if (axis == 0) {
    return maxs.x;
  }
  if (axis == 1) {
    return maxs.y;
  }
  return maxs.z;
}

static void selection_set_axis(Vector3 *point, int axis, float value) {
  if (axis == 0) {
    point->x = value;
  } else if (axis == 1) {
    point->y = value;
  } else {
    point->z = value;
  }
}

static Vector3 selection_make_point(int normal_axis, float normal_coordinate, int axis_u, float u,
                                    int axis_v, float v) {
  Vector3 point = {0.0f, 0.0f, 0.0f};
  selection_set_axis(&point, normal_axis, normal_coordinate);
  selection_set_axis(&point, axis_u, u);
  selection_set_axis(&point, axis_v, v);
  return point;
}

static void selection_emit_quad(int normal_axis, float normal_coordinate, int axis_u, float u0,
                                float u1, int axis_v, float v0, float v1, Vector3 desired_normal,
                                Color color) {
  Vector3 p0 = selection_make_point(normal_axis, normal_coordinate, axis_u, u0, axis_v, v0);
  Vector3 p1 = selection_make_point(normal_axis, normal_coordinate, axis_u, u1, axis_v, v0);
  Vector3 p2 = selection_make_point(normal_axis, normal_coordinate, axis_u, u1, axis_v, v1);
  Vector3 p3 = selection_make_point(normal_axis, normal_coordinate, axis_u, u0, axis_v, v1);

  Vector3 e1 = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
  Vector3 e2 = {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
  Vector3 face_normal = {
      e1.y * e2.z - e1.z * e2.y,
      e1.z * e2.x - e1.x * e2.z,
      e1.x * e2.y - e1.y * e2.x,
  };

  float normal_alignment = face_normal.x * desired_normal.x + face_normal.y * desired_normal.y +
                           face_normal.z * desired_normal.z;
  if (normal_alignment >= 0.0f) {
    DrawTriangle3D(p0, p1, p2, color);
    DrawTriangle3D(p0, p2, p3, color);
  } else {
    DrawTriangle3D(p0, p2, p1, color);
    DrawTriangle3D(p0, p3, p2, color);
  }
}

static void draw_selection_face_brackets(Vector3 mins, Vector3 maxs, SelectionFaceAxes axes,
                                         float normal_offset, Color color) {
  float face_coordinate = (axes.normal_direction < 0)
                              ? (selection_axis_min(mins, axes.normal_axis) - normal_offset)
                              : (selection_axis_max(maxs, axes.normal_axis) + normal_offset);

  float u0 = selection_axis_min(mins, axes.axis_u);
  float u1 = selection_axis_max(maxs, axes.axis_u);
  float v0 = selection_axis_min(mins, axes.axis_v);
  float v1 = selection_axis_max(maxs, axes.axis_v);

  float u_size = u1 - u0;
  float v_size = v1 - v0;
  if (u_size <= 0.0f || v_size <= 0.0f) {
    return;
  }

  float min_size = fminf(u_size, v_size);
  float thickness = fmaxf(min_size * 0.07f, 0.015f);
  thickness = fminf(thickness, u_size * 0.45f);
  thickness = fminf(thickness, v_size * 0.45f);

  float segment_u = fmaxf(u_size * 0.2f, thickness * 1.5f);
  float segment_v = fmaxf(v_size * 0.2f, thickness * 1.5f);
  segment_u = fminf(segment_u, u_size * 0.5f);
  segment_v = fminf(segment_v, v_size * 0.5f);

  Vector3 desired_normal = {0.0f, 0.0f, 0.0f};
  selection_set_axis(&desired_normal, axes.normal_axis, (float)axes.normal_direction);

  selection_emit_quad(axes.normal_axis, face_coordinate, axes.axis_u, u0, u0 + segment_u,
                      axes.axis_v, v0, v0 + thickness, desired_normal, color);
  selection_emit_quad(axes.normal_axis, face_coordinate, axes.axis_u, u0, u0 + thickness,
                      axes.axis_v, v0 + thickness, v0 + segment_v, desired_normal, color);

  selection_emit_quad(axes.normal_axis, face_coordinate, axes.axis_u, u1 - segment_u, u1,
                      axes.axis_v, v0, v0 + thickness, desired_normal, color);
  selection_emit_quad(axes.normal_axis, face_coordinate, axes.axis_u, u1 - thickness, u1,
                      axes.axis_v, v0 + thickness, v0 + segment_v, desired_normal, color);

  selection_emit_quad(axes.normal_axis, face_coordinate, axes.axis_u, u0, u0 + segment_u,
                      axes.axis_v, v1 - thickness, v1, desired_normal, color);
  selection_emit_quad(axes.normal_axis, face_coordinate, axes.axis_u, u0, u0 + thickness,
                      axes.axis_v, v1 - segment_v, v1 - thickness, desired_normal, color);

  selection_emit_quad(axes.normal_axis, face_coordinate, axes.axis_u, u1 - segment_u, u1,
                      axes.axis_v, v1 - thickness, v1, desired_normal, color);
  selection_emit_quad(axes.normal_axis, face_coordinate, axes.axis_u, u1 - thickness, u1,
                      axes.axis_v, v1 - segment_v, v1 - thickness, desired_normal, color);
}

void SelectionHighlight_Draw(const VoxelRaycastHit *hit) {
  if (hit == NULL || !hit->hit) {
    return;
  }

  Vector3 mins = {(float)hit->block_x, (float)hit->block_y, (float)hit->block_z};
  Vector3 maxs = {mins.x + 1.0f, mins.y + 1.0f, mins.z + 1.0f};

  static const SelectionFaceAxes k_selection_faces[] = {
      {1, 0, 2, -1}, /* Bottom */
      {1, 0, 2, 1},  /* Top */
      {2, 0, 1, -1}, /* North (-Z) */
      {2, 0, 1, 1},  /* South (+Z) */
      {0, 2, 1, -1}, /* West (-X) */
      {0, 2, 1, 1},  /* East (+X) */
  };

  Color bracket_color = (Color){255, 255, 255, 230};
  const float normal_offset = 0.012f;

  for (size_t i = 0; i < sizeof(k_selection_faces) / sizeof(k_selection_faces[0]); i++) {
    draw_selection_face_brackets(mins, maxs, k_selection_faces[i], normal_offset, bracket_color);
  }
}
