// Tuna addition, not part of upstream Box3D. See modules/box3d/README.md,
// "Local changes to thirdparty/box3d".
//
// A six degree of freedom joint. Each linear axis (frame A) and each angular axis
// (XYZ Euler angles of frame B relative to frame A) can independently be free,
// limited, locked (lower == upper), motorized and sprung. Rows are solved one at a
// time, the same way the limits of the other joints are.

#include "body.h"
#include "core.h"
#include "joint.h"
#include "math_internal.h"
#include "physics_world.h"
#include "solver.h"
#include "solver_set.h"

#include "box3d/box3d.h"

#include <float.h>
#include <math.h>

// Below this, the Euler angles are close enough to gimbal lock that the dual axes blow up.
#define B3_GENERIC_MIN_COS 0.1f

typedef struct b3GenericFrame
{
	b3Vec3 rB;
	// Anchor B relative to the center of A. The arm for the linear rows on body A.
	b3Vec3 armA;
	b3Vec3 axes[6];
	float values[6];
} b3GenericFrame;

static b3GenericFrame b3ComputeGenericFrame( const b3GenericJoint* joint, const b3BodyState* stateA, const b3BodyState* stateB )
{
	b3GenericFrame f;
	b3Vec3 rA = b3RotateVector( stateA->deltaRotation, joint->frameA.p );
	f.rB = b3RotateVector( stateB->deltaRotation, joint->frameB.p );
	b3Quat qA = b3MulQuat( stateA->deltaRotation, joint->frameA.q );
	b3Quat qB = b3MulQuat( stateB->deltaRotation, joint->frameB.q );

	b3Vec3 d = b3Add( b3Add( b3Sub( stateB->deltaPosition, stateA->deltaPosition ), b3Sub( f.rB, rA ) ), joint->deltaCenter );
	f.armA = b3Add( rA, d );

	b3Vec3 ax = b3RotateVector( qA, b3Vec3_axisX );
	b3Vec3 ay = b3RotateVector( qA, b3Vec3_axisY );
	b3Vec3 az = b3RotateVector( qA, b3Vec3_axisZ );
	b3Vec3 bx = b3RotateVector( qB, b3Vec3_axisX );
	b3Vec3 by = b3RotateVector( qB, b3Vec3_axisY );
	b3Vec3 bz = b3RotateVector( qB, b3Vec3_axisZ );

	f.axes[0] = ax;
	f.axes[1] = ay;
	f.axes[2] = az;
	for ( int i = 0; i < 3; ++i )
	{
		f.values[i] = b3Dot( d, f.axes[i] );
	}

	// Euler angles of inv(B) * A = Rx * Ry * Rz, negated so that the rate of each angle
	// is that of B relative to A.
	float s = b3ClampFloat( b3Dot( bx, az ), -1.0f, 1.0f );
	f.values[3] = -atan2f( -b3Dot( by, az ), b3Dot( bz, az ) );
	f.values[4] = -asinf( s );
	f.values[5] = -atan2f( -b3Dot( bx, ay ), b3Dot( bx, ax ) );

	// The rotation axes are bx, y and az. The Jacobians are their reciprocal basis.
	b3Vec3 y = b3Cross( az, bx );
	float c = b3Length( y );
	y = c > 1e-6f ? b3MulSV( 1.0f / c, y ) : ay;
	float invC = 1.0f / b3MaxFloat( c, B3_GENERIC_MIN_COS );
	f.axes[3] = b3MulSV( invC, b3Cross( y, az ) );
	f.axes[4] = y;
	f.axes[5] = b3MulSV( invC, b3Cross( bx, y ) );
	return f;
}

static float b3GenericInvMass( const b3JointSim* base, const b3GenericFrame* f, int row )
{
	b3Vec3 axis = f->axes[row];
	if ( row < 3 )
	{
		b3Vec3 sA = b3Cross( f->armA, axis );
		b3Vec3 sB = b3Cross( f->rB, axis );
		return base->invMassA + base->invMassB + b3Dot( sA, b3MulMV( base->invIA, sA ) ) + b3Dot( sB, b3MulMV( base->invIB, sB ) );
	}
	return b3Dot( axis, b3MulMV( base->invIA, axis ) ) + b3Dot( axis, b3MulMV( base->invIB, axis ) );
}

typedef struct b3GenericVelocity
{
	b3Vec3 vA, wA, vB, wB;
} b3GenericVelocity;

static float b3GenericSpeed( const b3GenericFrame* f, const b3GenericVelocity* v, int row )
{
	b3Vec3 axis = f->axes[row];
	if ( row < 3 )
	{
		b3Vec3 rel = b3Sub( b3Add( v->vB, b3Cross( v->wB, f->rB ) ), b3Add( v->vA, b3Cross( v->wA, f->armA ) ) );
		return b3Dot( axis, rel );
	}
	return b3Dot( axis, b3Sub( v->wB, v->wA ) );
}

static void b3GenericApply( const b3JointSim* base, const b3GenericFrame* f, b3GenericVelocity* v, int row, float impulse )
{
	b3Vec3 p = b3MulSV( impulse, f->axes[row] );
	if ( row < 3 )
	{
		v->vA = b3MulSub( v->vA, base->invMassA, p );
		v->wA = b3Sub( v->wA, b3MulMV( base->invIA, b3Cross( f->armA, p ) ) );
		v->vB = b3MulAdd( v->vB, base->invMassB, p );
		v->wB = b3Add( v->wB, b3MulMV( base->invIB, b3Cross( f->rB, p ) ) );
	}
	else
	{
		v->wA = b3Sub( v->wA, b3MulMV( base->invIA, p ) );
		v->wB = b3Add( v->wB, b3MulMV( base->invIB, p ) );
	}
}

static void b3GenericStore( b3BodyState* stateA, b3BodyState* stateB, const b3GenericVelocity* v )
{
	if ( stateA->flags & b3_dynamicFlag )
	{
		stateA->linearVelocity = v->vA;
		stateA->angularVelocity = v->wA;
	}

	if ( stateB->flags & b3_dynamicFlag )
	{
		stateB->linearVelocity = v->vB;
		stateB->angularVelocity = v->wB;
	}
}

void b3PrepareGenericJoint( b3JointSim* base, b3StepContext* context )
{
	B3_ASSERT( base->type == b3_genericJoint );

	b3World* world = context->world;
	b3Body* bodyA = b3Array_Get( world->bodies, base->bodyIdA );
	b3Body* bodyB = b3Array_Get( world->bodies, base->bodyIdB );
	B3_ASSERT( bodyA->setIndex == b3_awakeSet || bodyB->setIndex == b3_awakeSet );

	b3SolverSet* setA = b3Array_Get( world->solverSets, bodyA->setIndex );
	b3SolverSet* setB = b3Array_Get( world->solverSets, bodyB->setIndex );
	b3BodySim* simA = b3Array_Get( setA->bodySims, bodyA->localIndex );
	b3BodySim* simB = b3Array_Get( setB->bodySims, bodyB->localIndex );

	base->invMassA = simA->invMass;
	base->invMassB = simB->invMass;
	base->invIA = simA->invInertiaWorld;
	base->invIB = simB->invInertiaWorld;
	base->fixedRotation = b3Det( b3AddMM( base->invIA, base->invIB ) ) < 1000.0f * FLT_MIN;

	b3GenericJoint* joint = &base->genericJoint;
	joint->indexA = bodyA->setIndex == b3_awakeSet ? bodyA->localIndex : B3_NULL_INDEX;
	joint->indexB = bodyB->setIndex == b3_awakeSet ? bodyB->localIndex : B3_NULL_INDEX;

	joint->frameA.q = b3MulQuat( simA->transform.q, base->localFrameA.q );
	joint->frameA.p = b3RotateVector( simA->transform.q, b3Sub( base->localFrameA.p, simA->localCenter ) );
	joint->frameB.q = b3MulQuat( simB->transform.q, base->localFrameB.q );
	joint->frameB.p = b3RotateVector( simB->transform.q, b3Sub( base->localFrameB.p, simB->localCenter ) );
	joint->deltaCenter = b3SubPos( simB->center, simA->center );

	if ( context->enableWarmStarting == false )
	{
		for ( int i = 0; i < 6; ++i )
		{
			b3GenericRow* row = joint->rows + i;
			row->lowerImpulse = 0.0f;
			row->upperImpulse = 0.0f;
			row->motorImpulse = 0.0f;
			row->springImpulse = 0.0f;
		}
	}
}

void b3WarmStartGenericJoint( b3JointSim* base, b3StepContext* context )
{
	b3GenericJoint* joint = &base->genericJoint;
	b3BodyState dummyState = b3_identityBodyState;
	b3BodyState* stateA = joint->indexA == B3_NULL_INDEX ? &dummyState : context->states + joint->indexA;
	b3BodyState* stateB = joint->indexB == B3_NULL_INDEX ? &dummyState : context->states + joint->indexB;

	b3GenericFrame f = b3ComputeGenericFrame( joint, stateA, stateB );
	b3GenericVelocity v = { stateA->linearVelocity, stateA->angularVelocity, stateB->linearVelocity, stateB->angularVelocity };

	for ( int i = 0; i < 6; ++i )
	{
		b3GenericRow* row = joint->rows + i;
		b3GenericApply( base, &f, &v, i, row->lowerImpulse - row->upperImpulse + row->motorImpulse + row->springImpulse );
	}

	b3GenericStore( stateA, stateB, &v );
}

void b3SolveGenericJoint( b3JointSim* base, b3StepContext* context, bool useBias )
{
	b3GenericJoint* joint = &base->genericJoint;
	b3BodyState dummyState = b3_identityBodyState;
	b3BodyState* stateA = joint->indexA == B3_NULL_INDEX ? &dummyState : context->states + joint->indexA;
	b3BodyState* stateB = joint->indexB == B3_NULL_INDEX ? &dummyState : context->states + joint->indexB;

	b3GenericFrame f = b3ComputeGenericFrame( joint, stateA, stateB );
	b3GenericVelocity v = { stateA->linearVelocity, stateA->angularVelocity, stateB->linearVelocity, stateB->angularVelocity };

	float h = context->h;

	// Angular rows first, linear rows last: the position error is the more visible one.
	for ( int n = 0; n < 6; ++n )
	{
		int i = n < 3 ? n + 3 : n - 3;
		b3GenericRow* row = joint->rows + i;
		const b3GenericAxis* axis = &row->axis;
		if ( i >= 3 && base->fixedRotation )
		{
			continue;
		}

		float k = b3GenericInvMass( base, &f, i );
		if ( k <= 0.0f )
		{
			continue;
		}
		float mass = 1.0f / k;
		float value = f.values[i];

		if ( axis->enableSpring )
		{
			// Implicit spring in physical units, which is what Godot exposes.
			float denom = h * ( axis->damping + h * axis->stiffness );
			if ( denom > 0.0f )
			{
				float gamma = 1.0f / denom;
				float bias = ( value - axis->equilibrium ) * h * axis->stiffness * gamma;
				float cdot = b3GenericSpeed( &f, &v, i );
				float impulse = -( cdot + bias + gamma * row->springImpulse ) / ( k + gamma );
				row->springImpulse += impulse;
				b3GenericApply( base, &f, &v, i, impulse );
			}
		}

		if ( axis->enableMotor )
		{
			float impulse = -mass * ( b3GenericSpeed( &f, &v, i ) - axis->motorSpeed );
			float maxImpulse = h * axis->maxMotorForce;
			float oldImpulse = row->motorImpulse;
			row->motorImpulse = b3ClampFloat( oldImpulse + impulse, -maxImpulse, maxImpulse );
			b3GenericApply( base, &f, &v, i, row->motorImpulse - oldImpulse );
		}

		if ( axis->enableLimit == false || axis->lower > axis->upper )
		{
			continue;
		}

		if ( axis->lower == axis->upper )
		{
			// Locked: one equality row, accumulated in lowerImpulse.
			float bias = 0.0f;
			float massScale = 1.0f;
			float impulseScale = 0.0f;
			if ( useBias )
			{
				bias = base->constraintSoftness.biasRate * ( value - axis->lower );
				massScale = base->constraintSoftness.massScale;
				impulseScale = base->constraintSoftness.impulseScale;
			}
			float impulse = -massScale * mass * ( b3GenericSpeed( &f, &v, i ) + bias ) - impulseScale * row->lowerImpulse;
			row->lowerImpulse += impulse;
			b3GenericApply( base, &f, &v, i, impulse );
			continue;
		}

		for ( int side = 0; side < 2; ++side )
		{
			float sign = side == 0 ? 1.0f : -1.0f;
			float c = side == 0 ? value - axis->lower : axis->upper - value;
			float* accumulated = side == 0 ? &row->lowerImpulse : &row->upperImpulse;

			float bias = 0.0f;
			float massScale = 1.0f;
			float impulseScale = 0.0f;
			if ( c > 0.0f )
			{
				// speculation
				bias = c * context->inv_h;
			}
			else if ( useBias )
			{
				bias = base->constraintSoftness.biasRate * c;
				massScale = base->constraintSoftness.massScale;
				impulseScale = base->constraintSoftness.impulseScale;
			}

			float cdot = sign * b3GenericSpeed( &f, &v, i );
			float oldImpulse = *accumulated;
			float impulse = -massScale * mass * ( cdot + bias ) - impulseScale * oldImpulse;
			*accumulated = b3MaxFloat( oldImpulse + impulse, 0.0f );
			b3GenericApply( base, &f, &v, i, sign * ( *accumulated - oldImpulse ) );
		}
	}

	b3GenericStore( stateA, stateB, &v );
}

static b3Vec3 b3GenericReaction( const b3World* world, const b3GenericJoint* joint, int first )
{
	float s[3];
	for ( int i = 0; i < 3; ++i )
	{
		const b3GenericRow* row = joint->rows + first + i;
		s[i] = world->inv_h * ( row->lowerImpulse - row->upperImpulse + row->motorImpulse + row->springImpulse );
	}
	b3Vec3 local = { s[0], s[1], s[2] };
	return b3RotateVector( joint->frameA.q, local );
}

b3Vec3 b3GetGenericJointForce( b3World* world, b3JointSim* base )
{
	return b3GenericReaction( world, &base->genericJoint, 0 );
}

b3Vec3 b3GetGenericJointTorque( b3World* world, b3JointSim* base )
{
	return b3GenericReaction( world, &base->genericJoint, 3 );
}

void b3GenericJoint_SetAxis( b3JointId jointId, int index, b3GenericAxis axis )
{
	B3_ASSERT( 0 <= index && index < 6 );
	b3JointSim* base = b3GetJointSimCheckType( jointId, b3_genericJoint );
	b3GenericRow* row = base->genericJoint.rows + index;
	const b3GenericAxis* old = &row->axis;

	// Impulses belong to the constraint that produced them. Dropping them when that
	// constraint changes kind stops a stale one from being warm started forever.
	bool wasLocked = old->enableLimit && old->lower == old->upper;
	bool isLocked = axis.enableLimit && axis.lower == axis.upper;
	if ( old->enableLimit != axis.enableLimit || wasLocked != isLocked || axis.lower > axis.upper )
	{
		row->lowerImpulse = 0.0f;
		row->upperImpulse = 0.0f;
	}
	if ( axis.enableMotor == false )
	{
		row->motorImpulse = 0.0f;
	}
	if ( axis.enableSpring == false )
	{
		row->springImpulse = 0.0f;
	}
	row->axis = axis;
}
