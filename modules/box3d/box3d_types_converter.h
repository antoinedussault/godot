#ifndef BOX3D_TYPES_CONVERTER_H
#define BOX3D_TYPES_CONVERTER_H

#include "core/math/basis.h"
#include "core/math/quat.h"
#include "core/math/transform.h"
#include "core/math/vector3.h"

#include "box3d/box3d.h"
#include "box3d/math_functions.h"

// Godot <-> Box3D value conversions.
//
// Both engines are Y-up right-handed with the same quaternion convention, so
// these are plain component copies — no basis swizzle, no handedness flip.
//
// b3Pos is a distinct double-precision type only when Box3D is built with
// BOX3D_DOUBLE_PRECISION (its "large world" mode, off by default in
// thirdparty/box3d/include/box3d/config.h). In the default single-precision
// build b3Pos is a typedef of b3Vec3, so the position helpers below must not be
// separate overloads or they would redeclare the same signature.

_FORCE_INLINE_ b3Vec3 b3_from_godot(const Vector3 &p_v) {
	b3Vec3 v;
	v.x = (float)p_v.x;
	v.y = (float)p_v.y;
	v.z = (float)p_v.z;
	return v;
}

_FORCE_INLINE_ Vector3 b3_to_godot(const b3Vec3 &p_v) {
	return Vector3(p_v.x, p_v.y, p_v.z);
}

_FORCE_INLINE_ b3Quat b3_quat_from_godot(const Quat &p_q) {
	b3Quat q;
	q.v.x = (float)p_q.x;
	q.v.y = (float)p_q.y;
	q.v.z = (float)p_q.z;
	q.s = (float)p_q.w;
	return q;
}

_FORCE_INLINE_ Quat b3_quat_to_godot(const b3Quat &p_q) {
	return Quat(p_q.v.x, p_q.v.y, p_q.v.z, p_q.s);
}

#if defined(BOX3D_DOUBLE_PRECISION)

_FORCE_INLINE_ b3Pos b3_pos_from_godot(const Vector3 &p_v) {
	b3Pos p;
	p.x = p_v.x;
	p.y = p_v.y;
	p.z = p_v.z;
	return p;
}

_FORCE_INLINE_ Vector3 b3_pos_to_godot(const b3Pos &p_p) {
	return Vector3(p_p.x, p_p.y, p_p.z);
}

#else

_FORCE_INLINE_ b3Pos b3_pos_from_godot(const Vector3 &p_v) {
	return b3_from_godot(p_v);
}

_FORCE_INLINE_ Vector3 b3_pos_to_godot(const b3Pos &p_p) {
	return b3_to_godot(p_p);
}

#endif

// Godot Transforms carry scale and shear in the basis; Box3D transforms are
// strictly rigid. Scale is dropped here, which is correct — Godot's physics
// nodes bake scale into the shape's own extents, not into the body transform.
_FORCE_INLINE_ Transform b3_transform_to_godot(const b3WorldTransform &p_t) {
	Transform t;
	t.basis = Basis(b3_quat_to_godot(p_t.q));
	t.origin = b3_pos_to_godot(p_t.p);
	return t;
}

#endif // BOX3D_TYPES_CONVERTER_H
