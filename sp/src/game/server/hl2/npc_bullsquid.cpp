//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements the bullsquid
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "game.h"
#include "ai_default.h"
#include "ai_schedule.h"
#include "ai_hull.h"
#include "ai_navigator.h"
#include "ai_motor.h"
#include "ai_squad.h"
#include "npc_bullsquid.h"
#include "npcevent.h"
#include "soundent.h"
#include "activitylist.h"
#include "weapon_brickbat.h"
#include "npc_headcrab.h"
#include "player.h"
#include "gamerules.h"		// For g_pGameRules
#include "ammodef.h"
#include "grenade_spit.h"
#include "grenade_brickbat.h"
#include "entitylist.h"
#include "shake.h"
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"
#include "movevars_shared.h"
#include "hl2/grenade_frag.h"
#include "ai_hint.h"
#include "ai_senses.h"
#include "hl2/grenade_ar2.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define		SQUID_SPRINT_DIST	256 // how close the squid has to get before starting to sprint and refusing to swerve

ConVar sk_bullsquid_health( "sk_bullsquid_health", "0" );
ConVar sk_bullsquid_dmg_bite( "sk_bullsquid_dmg_bite", "0" );
ConVar sk_bullsquid_dmg_whip( "sk_bullsquid_dmg_whip", "0" );
ConVar sk_bullsquid_spit_speed("sk_bullsquid_spit_speed", "0");
ConVar g_debug_bullsquid("g_debug_bullsquid", "0");
ConVar sk_bullsquid_spit_size("sk_bullsquid_spit_size", "6");

//=========================================================
// monster-specific schedule types
//=========================================================
enum
{
	SCHED_SQUID_HURTHOP = LAST_SHARED_SCHEDULE + 1,
	SCHED_SQUID_SEECRAB,
	SCHED_SQUID_EAT,
	SCHED_SQUID_SNIFF_AND_EAT,
	SCHED_SQUID_WALLOW,
};

//=========================================================
// monster-specific tasks
//=========================================================
enum 
{
	TASK_SQUID_HOPTURN = LAST_SHARED_TASK + 1,
	TASK_SQUID_EAT,
};

//-----------------------------------------------------------------------------
// Squid Conditions
//-----------------------------------------------------------------------------
enum
{
	COND_SQUID_SMELL_FOOD	= LAST_SHARED_CONDITION + 1,
};


//=========================================================
// Interactions
//=========================================================
int	g_interactionBullsquidThrow		= 0;

//=========================================================
// Monster's Anim Events Go Here
//=========================================================
#define		BSQUID_AE_SPIT		( 1 )
#define		BSQUID_AE_BITE		( 2 )
#define		BSQUID_AE_BLINK		( 3 )
#define		BSQUID_AE_ROAR		( 4 )
#define		BSQUID_AE_HOP		( 5 )
#define		BSQUID_AE_THROW		( 6 )
#define		BSQUID_AE_WHIP_SND	( 7 )

LINK_ENTITY_TO_CLASS( npc_bullsquid, CNPC_Bullsquid );

int ACT_SQUID_EXCITED;
int ACT_SQUID_EAT;
int ACT_SQUID_DETECT_SCENT;
int ACT_SQUID_INSPECT_FLOOR;


//---------------------------------------------------------
// Save/Restore
//---------------------------------------------------------
BEGIN_DATADESC( CNPC_Bullsquid )

	DEFINE_FIELD( m_fCanThreatDisplay,	FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flLastHurtTime,		FIELD_TIME ),
	DEFINE_FIELD( m_flNextSpitTime,		FIELD_TIME ),
	DEFINE_FIELD( m_flHungryTime,		FIELD_TIME ),
	DEFINE_FIELD( m_nextSquidSoundTime,	FIELD_TIME ),
	DEFINE_FIELD(m_vecSaveSpitVelocity, FIELD_VECTOR),

END_DATADESC()


//=========================================================
// Spawn
//=========================================================
void CNPC_Bullsquid::Spawn()
{
	Precache( );

	SetModel( "models/bullsquid.mdl");
	SetHullType(HULL_WIDE_SHORT);
	SetHullSizeNormal();

	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_NOT_STANDABLE );
	SetMoveType( MOVETYPE_STEP );
	SetBloodColor(BLOOD_COLOR_GREEN);
	
	SetHealth(sk_bullsquid_health.GetInt());
	m_flFieldOfView		= 0.2;// indicates the width of this monster's forward view cone ( as a dotproduct result )
	m_NPCState			= NPC_STATE_NONE;
	
	CapabilitiesClear();
	CapabilitiesAdd( bits_CAP_MOVE_GROUND | bits_CAP_INNATE_RANGE_ATTACK1 | bits_CAP_INNATE_MELEE_ATTACK1 | bits_CAP_INNATE_MELEE_ATTACK2 );
	
	m_fCanThreatDisplay	= TRUE;
	m_flNextSpitTime = gpGlobals->curtime;

	NPCInit();

	m_flDistTooFar		= 784;
	BaseClass::Spawn();
}

//=========================================================
// Precache - precaches all resources this monster needs
//=========================================================
void CNPC_Bullsquid::Precache()
{
	PrecacheModel( "models/bullsquid.mdl" );

	UTIL_PrecacheOther( "grenade_spit" );
	UTIL_PrecacheOther("npc_grenade_frag");
	UTIL_PrecacheOther("grenade_ar2");

	PrecacheScriptSound( "NPC_Bullsquid.Idle" );
	PrecacheScriptSound( "NPC_Bullsquid.Pain" );
	PrecacheScriptSound( "NPC_Bullsquid.Alert" );
	PrecacheScriptSound( "NPC_Bullsquid.Death" );
	PrecacheScriptSound( "NPC_Bullsquid.Attack1" );
	PrecacheScriptSound( "NPC_Bullsquid.Growl" );
	PrecacheScriptSound( "NPC_Bullsquid.TailWhip");
	PrecacheScriptSound("NPC_Antlion.PoisonShoot");
	PrecacheScriptSound("NPC_Antlion.PoisonBall");

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: Indicates this monster's place in the relationship table.
// Output : 
//-----------------------------------------------------------------------------
Class_T	CNPC_Bullsquid::Classify( void )
{
	return CLASS_BULLSQUID; 
}

//=========================================================
// IdleSound 
//=========================================================
#define SQUID_ATTN_IDLE	(float)1.5
void CNPC_Bullsquid::IdleSound( void )
{
	EmitSound( "NPC_Bullsquid.Idle" );
}

//=========================================================
// PainSound 
//=========================================================
void CNPC_Bullsquid::PainSound( const CTakeDamageInfo &info )
{
	if (IsOnFire())
		return;

	EmitSound( "NPC_Bullsquid.Pain" );
}

//=========================================================
// AlertSound
//=========================================================
void CNPC_Bullsquid::AlertSound( void )
{
	EmitSound( "NPC_Bullsquid.Alert" );
}

//=========================================================
// DeathSound
//=========================================================
void CNPC_Bullsquid::DeathSound( const CTakeDamageInfo &info )
{
	if (IsOnFire())
		return;

	EmitSound( "NPC_Bullsquid.Death" );
}

//=========================================================
// AttackSound
//=========================================================
void CNPC_Bullsquid::AttackSound( void )
{
	EmitSound( "NPC_Bullsquid.Attack1" );
}

//=========================================================
// GrowlSound
//=========================================================
void CNPC_Bullsquid::GrowlSound( void )
{
	if (gpGlobals->curtime >= m_nextSquidSoundTime)
	{
		EmitSound( "NPC_Bullsquid.Growl" );
		m_nextSquidSoundTime	= gpGlobals->curtime + random->RandomInt(1.5,3.0);
	}
}


//=========================================================
// SetYawSpeed - allows each sequence to have a different
// turn rate associated with it.
//=========================================================
float CNPC_Bullsquid::MaxYawSpeed( void )
{
	float flYS = 0;

	switch ( GetActivity() )
	{
	case	ACT_WALK:			flYS = 90;	break;
	case	ACT_RUN:			flYS = 90;	break;
	case	ACT_IDLE:			flYS = 90;	break;
	case	ACT_RANGE_ATTACK1:	flYS = 90;	break;
	default:
		flYS = 90;
		break;
	}

	return flYS;
}

bool CNPC_Bullsquid::SeenEnemyWithinTime(float flTime)
{
	float flLastSeenTime = GetEnemies()->LastTimeSeen(GetEnemy());
	return (flLastSeenTime != 0.0f && (gpGlobals->curtime - flLastSeenTime) < flTime);
}

//-----------------------------------------------------------------------------
// Purpose: Test whether this antlion can hit the target
//-----------------------------------------------------------------------------
bool CNPC_Bullsquid::InnateWeaponLOSCondition(const Vector &ownerPos, const Vector &targetPos, bool bSetConditions)
{
	if (GetNextAttack() > gpGlobals->curtime)
		return false;

	// If we can see the enemy, or we've seen them in the last few seconds just try to lob in there
	if (SeenEnemyWithinTime(3.0f))
	{
		Vector vSpitPos;
		GetAttachment("mouth", vSpitPos);

		return GetSpitVector(vSpitPos, targetPos, &m_vecSaveSpitVelocity);
	}

	return BaseClass::InnateWeaponLOSCondition(ownerPos, targetPos, bSetConditions);
}

//
//	FIXME: Create this in a better fashion!
//

Vector BullsquidVecCheckThrowTolerance(CBaseEntity *pEdict, const Vector &vecSpot1, Vector vecSpot2, float flSpeed, float flTolerance)
{
	flSpeed = MAX(1.0f, flSpeed);

	float flGravity = GetCurrentGravity();

	Vector vecGrenadeVel = (vecSpot2 - vecSpot1);

	// throw at a constant time
	float time = vecGrenadeVel.Length() / flSpeed;
	vecGrenadeVel = vecGrenadeVel * (1.0 / time);

	// adjust upward toss to compensate for gravity loss
	vecGrenadeVel.z += flGravity * time * 0.5;

	Vector vecApex = vecSpot1 + (vecSpot2 - vecSpot1) * 0.5;
	vecApex.z += 0.5 * flGravity * (time * 0.5) * (time * 0.5);

	trace_t tr;
	UTIL_TraceLine(vecSpot1, vecApex, MASK_SOLID, pEdict, COLLISION_GROUP_NONE, &tr);
	if (tr.fraction != 1.0)
	{
		// fail!
		if (g_debug_bullsquid.GetBool())
		{
			NDebugOverlay::Line(vecSpot1, vecApex, 255, 0, 0, true, 5.0);
		}

		return vec3_origin;
	}

	if (g_debug_bullsquid.GetBool())
	{
		NDebugOverlay::Line(vecSpot1, vecApex, 0, 255, 0, true, 5.0);
	}

	UTIL_TraceLine(vecApex, vecSpot2, MASK_SOLID_BRUSHONLY, pEdict, COLLISION_GROUP_NONE, &tr);
	if (tr.fraction != 1.0)
	{
		bool bFail = true;

		// Didn't make it all the way there, but check if we're within our tolerance range
		if (flTolerance > 0.0f)
		{
			float flNearness = (tr.endpos - vecSpot2).LengthSqr();
			if (flNearness < Square(flTolerance))
			{
				if (g_debug_bullsquid.GetBool())
				{
					NDebugOverlay::Sphere(tr.endpos, vec3_angle, flTolerance, 0, 255, 0, 0, true, 5.0);
				}

				bFail = false;
			}
		}

		if (bFail)
		{
			if (g_debug_bullsquid.GetBool())
			{
				NDebugOverlay::Line(vecApex, vecSpot2, 255, 0, 0, true, 5.0);
				NDebugOverlay::Sphere(tr.endpos, vec3_angle, flTolerance, 255, 0, 0, 0, true, 5.0);
			}
			return vec3_origin;
		}
	}

	if (g_debug_bullsquid.GetBool())
	{
		NDebugOverlay::Line(vecApex, vecSpot2, 0, 255, 0, true, 5.0);
	}

	return vecGrenadeVel;
}

//-----------------------------------------------------------------------------
// Purpose: Get a toss direction that will properly lob spit to hit a target
// Input  : &vecStartPos - Where the spit will start from
//			&vecTarget - Where the spit is meant to land
//			*vecOut - The resulting vector to lob the spit
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_Bullsquid::GetSpitVector(const Vector &vecStartPos, const Vector &vecTarget, Vector *vecOut)
{
	// antlion workers exist only in episodic.
	// Try the most direct route
	Vector vecToss = BullsquidVecCheckThrowTolerance(this, vecStartPos, vecTarget, sk_bullsquid_spit_speed.GetFloat(), (10.0f*12.0f));

	// If this failed then try a little faster (flattens the arc)
	if (vecToss == vec3_origin)
	{
		vecToss = BullsquidVecCheckThrowTolerance(this, vecStartPos, vecTarget, sk_bullsquid_spit_speed.GetFloat() * 1.5f, (10.0f*12.0f));
		if (vecToss == vec3_origin)
			return false;
	}

	// Save out the result
	if (vecOut)
	{
		*vecOut = vecToss;
	}

	return true;
}

#define COMBINE_GRENADE_TIMER		3.5
extern ConVar sk_npc_dmg_smg1_grenade;

void CNPC_Bullsquid::CreateDefaultProjectile(const Vector &vSpitPos, Vector &vecToss, float flVelocity, int i)
{
	CGrenadeSpit* pGrenade = (CGrenadeSpit*)CreateEntityByName("grenade_spit");
	pGrenade->SetAbsOrigin(vSpitPos);
	pGrenade->SetAbsAngles(vec3_angle);
	DispatchSpawn(pGrenade);
	pGrenade->SetThrower(this);
	pGrenade->SetOwnerEntity(this);

	if (i == 0)
	{
		pGrenade->SetSpitSize(SPIT_LARGE);
		pGrenade->SetAbsVelocity(vecToss * flVelocity);
	}
	else
	{
		pGrenade->SetAbsVelocity((vecToss + RandomVector(-0.035f, 0.035f)) * flVelocity);
		pGrenade->SetSpitSize(random->RandomInt(SPIT_SMALL, SPIT_MEDIUM));
	}

	// Tumble through the air
	pGrenade->SetLocalAngularVelocity(QAngle(random->RandomFloat(-250, -500),
		random->RandomFloat(-250, -500),
		random->RandomFloat(-250, -500)));
}

void CNPC_Bullsquid::CreateSMGGrenadeProjectile(const Vector& vSpitPos, Vector &vecToss, float flVelocity, int i)
{
	//Create the grenade
	CGrenadeAR2* pGrenade = (CGrenadeAR2*)Create("grenade_ar2", vSpitPos, vec3_angle, this);
	pGrenade->SetMoveType(MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_BOUNCE);
	pGrenade->SetThrower(this);
	pGrenade->SetGravity(0.5);
	pGrenade->SetDamage(sk_npc_dmg_smg1_grenade.GetFloat());

	if (i == 0)
	{
		pGrenade->SetAbsVelocity(vecToss * flVelocity);
	}
	else
	{
		pGrenade->SetAbsVelocity((vecToss + RandomVector(-0.25f, 0.25f)) * flVelocity);
	}

	// Tumble through the air
	pGrenade->SetLocalAngularVelocity(QAngle(random->RandomFloat(-250, -500),
		random->RandomFloat(-250, -500),
		random->RandomFloat(-250, -500)));
}

void CNPC_Bullsquid::CreateFragGrenadeProjectile(const Vector& vSpitPos, Vector &vecToss, float flVelocity)
{
	Vector vecSpin;
	vecSpin.x = random->RandomFloat(-1000.0, 1000.0);
	vecSpin.y = random->RandomFloat(-1000.0, 1000.0);
	vecSpin.z = random->RandomFloat(-1000.0, 1000.0);

	Vector forward, up, vecThrow;

	GetVectors(&forward, NULL, &up);
	vecThrow = forward * flVelocity * 750 + up * 175;
	bool bFireGrenade = ((random->RandomInt(0, 1) == 1 && g_pGameRules->GetSkillLevel() >= SKILL_HARD) ? true : false);
	Fraggrenade_Create(vSpitPos, vec3_angle, vecThrow, vecSpin, this, COMBINE_GRENADE_TIMER, true, bFireGrenade);
}

//=========================================================
// HandleAnimEvent - catches the monster-specific messages
// that occur when tagged animation frames are played.
//=========================================================
void CNPC_Bullsquid::HandleAnimEvent( animevent_t *pEvent )
{
	switch( pEvent->event )
	{
		case BSQUID_AE_SPIT:
		{
			if (GetEnemy() != NULL)
			{
				Vector vSpitPos;
				GetAttachment("mouth", vSpitPos);
				vSpitPos.z += 40.0f;
				Vector	vTarget;

				// If our enemy is looking at us and far enough away, lead him
				if (HasCondition(COND_ENEMY_FACING_ME) && UTIL_DistApprox(GetAbsOrigin(), GetEnemy()->GetAbsOrigin()) > (40 * 12))
				{
					UTIL_PredictedPosition(GetEnemy(), 0.5f, &vTarget);
					vTarget.z = GetEnemy()->GetAbsOrigin().z;
				}
				else
				{
					// Otherwise he can't see us and he won't be able to dodge
					vTarget = GetEnemy()->BodyTarget(vSpitPos, true);
				}

				vTarget[2] += random->RandomFloat(0.0f, 32.0f);

				// Try and spit at our target
				Vector vecToss;
				if (GetSpitVector(vSpitPos, vTarget, &vecToss) == false)
				{
					DevMsg("GetSpitVector( vSpitPos, vTarget, &vecToss ) == false\n");
					// Now try where they were
					if (GetSpitVector(vSpitPos, m_vSavePosition, &vecToss) == false)
					{
						DevMsg("GetSpitVector( vSpitPos, m_vSavePosition, &vecToss ) == false\n");
						// Failing that, just shoot with the old velocity we calculated initially!
						vecToss = m_vecSaveSpitVelocity;
					}
				}

				// Find what our vertical theta is to estimate the time we'll impact the ground
				Vector vecToTarget = (vTarget - vSpitPos);
				VectorNormalize(vecToTarget);
				float flVelocity = VectorNormalize(vecToss);
				float flCosTheta = DotProduct(vecToTarget, vecToss);
				float flTime = (vSpitPos - vTarget).Length2D() / (flVelocity * flCosTheta);

				// Emit a sound where this is going to hit so that targets get a chance to act correctly
				CSoundEnt::InsertSound(SOUND_DANGER, vTarget, (15 * 12), flTime, this);

				// Don't fire again until this volley would have hit the ground (with some lag behind it)
				SetNextAttack(gpGlobals->curtime + flTime + random->RandomFloat(0.5f, 2.0f));

				int nShots = sk_bullsquid_spit_size.GetInt();

				if (m_pAttributes != NULL)
				{
					int shots = m_pAttributes->GetInt("spit_size");

					if (shots > 0)
					{
						nShots = shots;
					}
				}

				for (int i = 0; i < nShots; i++)
				{
					if (m_pAttributes)
					{
						int projInt = m_pAttributes->GetInt("new_projectile");

						switch (projInt)
						{
						case 1:
							CreateFragGrenadeProjectile(vSpitPos, vecToss, flVelocity);
							break;
						case 2:
							CreateSMGGrenadeProjectile(vSpitPos, vecToss, flVelocity, i);
							break;
						default:
							CreateDefaultProjectile(vSpitPos, vecToss, flVelocity, i);
							break;
						}
					}
					else
					{
						CreateDefaultProjectile(vSpitPos, vecToss, flVelocity, i);
					}
				}

				for (int i = 0; i < 8; i++)
				{
					DispatchParticleEffect("smod_drip_y", vSpitPos + RandomVector(-12.0f, 12.0f), RandomAngle(0, 360));
				}

				EmitSound("NPC_Antlion.PoisonShoot");
			}
		}
		break;

		case BSQUID_AE_BITE:
		{
			if (GetEnemy() != NULL)
			{
				// SOUND HERE!
				CBaseEntity* pHurt = CheckTraceHullAttack(70, Vector(-16, -16, -16), Vector(16, 16, 16), sk_bullsquid_dmg_bite.GetFloat(), DMG_SLASH);
				if (pHurt)
				{
					//don't kick striders, only deliver damage.
					if (!FClassnameIs(pHurt, "npc_strider"))
					{
						Vector forward, up;
						AngleVectors(GetAbsAngles(), &forward, NULL, &up);
						pHurt->ApplyAbsVelocityImpulse(100 * (up - forward));
						pHurt->SetGroundEntity(NULL);
					}
				}
			}
		}
		break;

		case BSQUID_AE_WHIP_SND:
		{
			if (GetEnemy() != NULL)
			{
				CBaseEntity* pHurt = CheckTraceHullAttack(70, Vector(-16, -16, -16), Vector(16, 16, 16), sk_bullsquid_dmg_whip.GetFloat(), DMG_SLASH | DMG_ALWAYSGIB);
				if (pHurt)
				{
					//don't kick striders, only deliver damage.
					if (!FClassnameIs(pHurt, "npc_strider"))
					{
						Vector right, up;
						AngleVectors(GetAbsAngles(), NULL, &right, &up);

						if (pHurt->GetFlags() & (FL_NPC | FL_CLIENT))
							pHurt->ViewPunch(QAngle(20, 0, -20));

						pHurt->ApplyAbsVelocityImpulse(100 * (up + 2 * right));
					}
				}
				EmitSound("NPC_Bullsquid.TailWhip");
			}
			break;
		}

/*
		case BSQUID_AE_TAILWHIP:
		{
			CBaseEntity *pHurt = CheckTraceHullAttack( 70, Vector(-16,-16,-16), Vector(16,16,16), sk_bullsquid_dmg_whip.GetFloat(), DMG_SLASH | DMG_ALWAYSGIB );
			if ( pHurt ) 
			{
				Vector right, up;
				AngleVectors( GetAbsAngles(), NULL, &right, &up );

				if ( pHurt->GetFlags() & ( FL_NPC | FL_CLIENT ) )
					 pHurt->ViewPunch( QAngle( 20, 0, -20 ) );
			
				pHurt->ApplyAbsVelocityImpulse( 100 * (up+2*right) );
			}
		}
		break;
*/

		case BSQUID_AE_BLINK:
		{
			// close eye. 
			m_nSkin = 1;
		}
		break;

		case BSQUID_AE_HOP:
		{
			float flGravity = GetCurrentGravity();

			// throw the squid up into the air on this frame.
			if ( GetFlags() & FL_ONGROUND )
			{
				SetGroundEntity( NULL );
			}

			// jump 40 inches into the air
			Vector vecVel = GetAbsVelocity();
			vecVel.z += sqrt( flGravity * 2.0 * 40 );
			SetAbsVelocity( vecVel );
		}
		break;

		case BSQUID_AE_THROW:
			{
				if (GetEnemy() != NULL)
				{
					// squid throws its prey IF the prey is a client. 
					CBaseEntity *pHurt = CheckTraceHullAttack( 70, Vector(-16,-16,-16), Vector(16,16,16), 0, 0 );


					if (pHurt)
					{
						pHurt->ViewPunch(QAngle(20, 0, -20));

						// screeshake transforms the viewmodel as well as the viewangle. No problems with seeing the ends of the viewmodels.
						UTIL_ScreenShake(pHurt->GetAbsOrigin(), 25.0, 1.5, 0.7, 2, SHAKE_START);

						// If the player, throw him around
						if (pHurt->IsPlayer())
						{
							Vector forward, up;
							AngleVectors(GetLocalAngles(), &forward, NULL, &up);
							pHurt->ApplyAbsVelocityImpulse(forward * 300 + up * 300);
						}
						// If not the player see if has bullsquid throw interatcion
						else
						{
							CBaseCombatCharacter* pVictim = ToBaseCombatCharacter(pHurt);
							if (pVictim)
							{
								if (!FClassnameIs(pVictim, "npc_strider"))
								{
									if (pVictim->DispatchInteraction(g_interactionBullsquidThrow, NULL, this))
									{
										Vector forward, up;
										AngleVectors(GetLocalAngles(), &forward, NULL, &up);
										pVictim->ApplyAbsVelocityImpulse(forward * 300 + up * 250);
									}
								}
							}
						}
					}
				}
			}
		break;

		default:
			BaseClass::HandleAnimEvent( pEvent );
	}
}

int CNPC_Bullsquid::RangeAttack1Conditions( float flDot, float flDist )
{
	if ( IsMoving() && flDist >= 512 )
	{
		// squid will far too far behind if he stops running to spit at this distance from the enemy.
		return ( COND_NONE );
	}

	if ( flDist > 85 && flDist <= 784 && flDot >= 0.5 && gpGlobals->curtime >= m_flNextSpitTime )
	{
		if ( GetEnemy() != NULL )
		{
			if ( fabs( GetAbsOrigin().z - GetEnemy()->GetAbsOrigin().z ) > 256 )
			{
				// don't try to spit at someone up really high or down really low.
				return( COND_NONE );
			}
		}

		if ( IsMoving() )
		{
			// don't spit again for a long time, resume chasing enemy.
			m_flNextSpitTime = gpGlobals->curtime + 5;
		}
		else
		{
			// not moving, so spit again pretty soon.
			m_flNextSpitTime = gpGlobals->curtime + 0.5;
		}

		return( COND_CAN_RANGE_ATTACK1 );
	}

	return( COND_NONE );
}

//=========================================================
// MeleeAttack2Conditions - bullsquid is a big guy, so has a longer
// melee range than most monsters. This is the tailwhip attack
//=========================================================
int CNPC_Bullsquid::MeleeAttack1Conditions( float flDot, float flDist )
{
	if ( GetEnemy()->m_iHealth <= sk_bullsquid_dmg_whip.GetFloat() && flDist <= 85 && flDot >= 0.7 )
	{
		return ( COND_CAN_MELEE_ATTACK1 );
	}
	
	return( COND_NONE );
}

//=========================================================
// MeleeAttack2Conditions - bullsquid is a big guy, so has a longer
// melee range than most monsters. This is the bite attack.
// this attack will not be performed if the tailwhip attack
// is valid.
//=========================================================
int CNPC_Bullsquid::MeleeAttack2Conditions( float flDot, float flDist )
{
	if ( flDist <= 85 && flDot >= 0.7 && !HasCondition( COND_CAN_MELEE_ATTACK1 ) )		// The player & bullsquid can be as much as their bboxes 
		 return ( COND_CAN_MELEE_ATTACK2 );
	
	return( COND_NONE );
}

bool CNPC_Bullsquid::FValidateHintType( CAI_Hint *pHint )
{
	if ( pHint->HintType() == HINT_HL1_WORLD_HUMAN_BLOOD )
		 return true;

	DevMsg( "Couldn't validate hint type" );

	return false;
}

void CNPC_Bullsquid::RemoveIgnoredConditions( void )
{
	if ( m_flHungryTime > gpGlobals->curtime )
		 ClearCondition( COND_SQUID_SMELL_FOOD );

	if ( gpGlobals->curtime - m_flLastHurtTime <= 20 )
	{
		// haven't been hurt in 20 seconds, so let the squid care about stink. 
		ClearCondition( COND_SMELL );
	}

	if ( GetEnemy() != NULL )
	{
		// ( Unless after a tasty headcrab, yumm ^_^ )
		if (FClassnameIs(GetEnemy(), "npc_headcrab") ||
			FClassnameIs(GetEnemy(), "npc_headcrab_poison") ||
			FClassnameIs(GetEnemy(), "npc_headcrab_black") ||
			FClassnameIs(GetEnemy(), "npc_headcrab_fast"))
		{
			ClearCondition(COND_SMELL);
		}
	}
}

Disposition_t CNPC_Bullsquid::IRelationType( CBaseEntity *pTarget )
{
	if ( gpGlobals->curtime - m_flLastHurtTime < 5 && (FClassnameIs(pTarget, "npc_headcrab") ||
		FClassnameIs(pTarget, "npc_headcrab_poison") ||
		FClassnameIs(pTarget, "npc_headcrab_black") ||
		FClassnameIs(pTarget, "npc_headcrab_fast")))
	{
		// if squid has been hurt in the last 5 seconds, and is getting relationship for a headcrab, 
		// tell squid to disregard crab. 
		return D_NU;
	}

	return BaseClass::IRelationType( pTarget );
}

//=========================================================
// TakeDamage - overridden for bullsquid so we can keep track
// of how much time has passed since it was last injured
//=========================================================
int CNPC_Bullsquid::OnTakeDamage_Alive( const CTakeDamageInfo &inputInfo )
{

#if 0 //Fix later.

	float flDist;
	Vector vecApex, vOffset;

	// if the squid is running, has an enemy, was hurt by the enemy, hasn't been hurt in the last 3 seconds, and isn't too close to the enemy,
	// it will swerve. (whew).
	if ( GetEnemy() != NULL && IsMoving() && pevAttacker == GetEnemy() && gpGlobals->curtime - m_flLastHurtTime > 3 )
	{
		flDist = ( GetAbsOrigin() - GetEnemy()->GetAbsOrigin() ).Length2D();
		
		if ( flDist > SQUID_SPRINT_DIST )
		{
			AI_Waypoint_t*	pRoute = GetNavigator()->GetPath()->Route();

			if ( pRoute )
			{
				flDist = ( GetAbsOrigin() - pRoute[ pRoute->iNodeID ].vecLocation ).Length2D();// reusing flDist. 

				if ( GetNavigator()->GetPath()->BuildTriangulationRoute( GetAbsOrigin(), pRoute[ pRoute->iNodeID ].vecLocation, flDist * 0.5, GetEnemy(), &vecApex, &vOffset, NAV_GROUND ) )
				{
					GetNavigator()->PrependWaypoint( vecApex, bits_WP_TO_DETOUR | bits_WP_DONT_SIMPLIFY );
				}
			}
		}
	}
#endif

	if ((inputInfo.GetInflictor()->ClassMatches(GetClassname()) || inputInfo.GetInflictor() == this || inputInfo.GetAttacker() == this) && !(inputInfo.GetDamageType() == DMG_GENERIC))
	{
		return 0;
	}

	if ( !FClassnameIs( inputInfo.GetAttacker(), "npc_headcrab" ) || 
		!FClassnameIs(inputInfo.GetAttacker(), "npc_headcrab_poison") || 
		!FClassnameIs(inputInfo.GetAttacker(), "npc_headcrab_black") || 
		!FClassnameIs(inputInfo.GetAttacker(), "npc_headcrab_fast") )
	{
		// don't forget about headcrabs if it was a headcrab that hurt the squid.
		m_flLastHurtTime = gpGlobals->curtime;
	}

	return BaseClass::OnTakeDamage_Alive( inputInfo );
}

void CNPC_Bullsquid::Event_Killed(const CTakeDamageInfo& info)
{
	m_nSkin = 1;
	BaseClass::Event_Killed(info);
}

//=========================================================
// GetSoundInterests - returns a bit mask indicating which types
// of sounds this monster regards. In the base class implementation,
// monsters care about all sounds, but no scents.
//=========================================================
int CNPC_Bullsquid::GetSoundInterests( void )
{
	return	SOUND_WORLD	|
			SOUND_COMBAT	|
		    SOUND_CARCASS	|
			SOUND_MEAT		|
			SOUND_GARBAGE	|
			SOUND_PLAYER;
}

//=========================================================
// OnListened - monsters dig through the active sound list for
// any sounds that may interest them. (smells, too!)
//=========================================================
void CNPC_Bullsquid::OnListened( void )
{
	AISoundIter_t iter;
	
	CSound *pCurrentSound;

	static int conditionsToClear[] = 
	{
		COND_SQUID_SMELL_FOOD,
	};

	ClearConditions( conditionsToClear, ARRAYSIZE( conditionsToClear ) );
	
	pCurrentSound = GetSenses()->GetFirstHeardSound( &iter );
	
	while ( pCurrentSound )
	{
		// the npc cares about this sound, and it's close enough to hear.
		int condition = COND_NONE;
		
		if ( !pCurrentSound->FIsSound() )
		{
			// if not a sound, must be a smell - determine if it's just a scent, or if it's a food scent
			if ( pCurrentSound->m_iType & ( SOUND_MEAT | SOUND_CARCASS ) )
			{
				// the detected scent is a food item
				condition = COND_SQUID_SMELL_FOOD;
			}
		}
		
		if ( condition != COND_NONE )
			SetCondition( condition );

		pCurrentSound = GetSenses()->GetNextHeardSound( &iter );
	}

	BaseClass::OnListened();
}

//========================================================
// RunAI - overridden for bullsquid because there are things
// that need to be checked every think.
//========================================================
void CNPC_Bullsquid::RunAI( void )
{
	// first, do base class stuff
	BaseClass::RunAI();

	if ( m_nSkin != 0 )
	{
		// close eye if it was open.
		m_nSkin = 0; 
	}

	if ( random->RandomInt( 0,39 ) == 0 )
	{
		m_nSkin = 1;
	}

	if ( GetEnemy() != NULL && GetActivity() == ACT_RUN )
	{
		// chasing enemy. Sprint for last bit
		if ( (GetAbsOrigin() - GetEnemy()->GetAbsOrigin()).Length2D() < SQUID_SPRINT_DIST )
		{
			m_flPlaybackRate = 1.25;
		}
	}

}

//=========================================================
// GetSchedule 
//=========================================================
int CNPC_Bullsquid::SelectSchedule( void )
{
	switch	( m_NPCState )
	{
	case NPC_STATE_IDLE:
		{
			return SCHED_PATROL_WALK_LOOP;
		}
		break;
	case NPC_STATE_ALERT:
		{
			if ( HasCondition( COND_LIGHT_DAMAGE ) || HasCondition( COND_HEAVY_DAMAGE ) )
			{
				return SCHED_SQUID_HURTHOP;
			}

			if ( HasCondition( COND_SQUID_SMELL_FOOD ) )
			{
				CSound		*pSound;

				pSound = GetBestScent();
				
				if ( pSound && (!FInViewCone( pSound->GetSoundOrigin() ) || !FVisible( pSound->GetSoundOrigin() )) )
				{
					// scent is behind or occluded
					return SCHED_SQUID_SNIFF_AND_EAT;
				}

				// food is right out in the open. Just go get it.
				return SCHED_SQUID_EAT;
			}

			if ( HasCondition( COND_SMELL ) )
			{
				// there's something stinky. 
				CSound		*pSound;

				pSound = GetBestScent();
				if ( pSound )
					return SCHED_SQUID_WALLOW;
			}

			return SCHED_PATROL_WALK_LOOP;

			break;
		}
	case NPC_STATE_COMBAT:
		{
// dead enemy
			if ( HasCondition( COND_ENEMY_DEAD ) )
			{
				// call base class, all code to handle dead enemies is centralized there.
				return BaseClass::SelectSchedule();
			}

			if ( HasCondition( COND_NEW_ENEMY ) )
			{
				if ( m_fCanThreatDisplay && IRelationType( GetEnemy() ) == D_HT && FClassnameIs( GetEnemy(), "monster_headcrab" ) )
				{
					// this means squid sees a headcrab!
					m_fCanThreatDisplay = FALSE;// only do the headcrab dance once per lifetime.
					return SCHED_SQUID_SEECRAB;
				}
				else
				{
					return SCHED_WAKE_ANGRY;
				}
			}

			if ( HasCondition( COND_SQUID_SMELL_FOOD ) )
			{
				CSound		*pSound;

				pSound = GetBestScent();
				
				if ( pSound && (!FInViewCone( pSound->GetSoundOrigin() ) || !FVisible( pSound->GetSoundOrigin() )) )
				{
					// scent is behind or occluded
					return SCHED_SQUID_SNIFF_AND_EAT;
				}

				// food is right out in the open. Just go get it.
				return SCHED_SQUID_EAT;
			}

			if ( HasCondition( COND_CAN_RANGE_ATTACK1 ) )
			{
				return SCHED_RANGE_ATTACK1;
			}

			if ( HasCondition( COND_CAN_MELEE_ATTACK1 ) )
			{
				return SCHED_MELEE_ATTACK1;
			}

			if ( HasCondition( COND_CAN_MELEE_ATTACK2 ) )
			{
				return SCHED_MELEE_ATTACK2;
			}
			
			return SCHED_CHASE_ENEMY;

			break;
		}
	}

	return BaseClass::SelectSchedule();
}

//=========================================================
// FInViewCone - returns true is the passed vector is in
// the caller's forward view cone. The dot product is performed
// in 2d, making the view cone infinitely tall. 
//=========================================================
bool CNPC_Bullsquid::FInViewCone( Vector pOrigin )
{
	Vector los = ( pOrigin - GetAbsOrigin() );

	// do this in 2D
	los.z = 0;
	VectorNormalize( los );

	Vector facingDir = EyeDirection2D( );

	float flDot = DotProduct( los, facingDir );

	if ( flDot > m_flFieldOfView )
		return true;

	return false;
}

//=========================================================
// Start task - selects the correct activity and performs
// any necessary calculations to start the next task on the
// schedule.  OVERRIDDEN for bullsquid because it needs to
// know explicitly when the last attempt to chase the enemy
// failed, since that impacts its attack choices.
//=========================================================
void CNPC_Bullsquid::StartTask( const Task_t *pTask )
{
	switch ( pTask->iTask )
	{
	case TASK_MELEE_ATTACK2:
		{
			if (GetEnemy())
			{
				GrowlSound();

				m_flLastAttackTime = gpGlobals->curtime;

				BaseClass::StartTask( pTask );
			}
			break;
		}
	case TASK_SQUID_HOPTURN:
		{
			SetActivity( ACT_HOP );
			
			if ( GetEnemy() )
			{
				Vector	vecFacing = ( GetEnemy()->GetAbsOrigin() - GetAbsOrigin() );
				VectorNormalize( vecFacing );

				GetMotor()->SetIdealYaw( vecFacing );
			}

			break;
		}
	case TASK_SQUID_EAT:
		{
			m_flHungryTime = gpGlobals->curtime + pTask->flTaskData;
			TaskComplete();
			break;
		}
	default:
		{
			BaseClass::StartTask( pTask );
			break;
		}
	}
}

//=========================================================
// RunTask
//=========================================================
void CNPC_Bullsquid::RunTask( const Task_t *pTask )
{
	switch ( pTask->iTask )
	{
	case TASK_SQUID_HOPTURN:
		{
			if ( GetEnemy() )
			{
				Vector	vecFacing = ( GetEnemy()->GetAbsOrigin() - GetAbsOrigin() );
				VectorNormalize( vecFacing );
				GetMotor()->SetIdealYaw( vecFacing );
			}

			if ( IsSequenceFinished() )
			{
				TaskComplete(); 
			}
			break;
		}
	default:
		{
			BaseClass::RunTask( pTask );
			break;
		}
	}
}

//=========================================================
// GetIdealState - Overridden for Bullsquid to deal with
// the feature that makes it lose interest in headcrabs for 
// a while if something injures it. 
//=========================================================
NPC_STATE CNPC_Bullsquid::SelectIdealState( void )
{
	// If no schedule conditions, the new ideal state is probably the reason we're in here.
	switch ( m_NPCState )
	{
		case NPC_STATE_COMBAT:
		{
			// COMBAT goes to ALERT upon death of enemy
			if ( GetEnemy() != NULL && ( HasCondition( COND_LIGHT_DAMAGE ) || HasCondition( COND_HEAVY_DAMAGE ) ) && FClassnameIs( GetEnemy(), "monster_headcrab" ) )
			{
				// if the squid has a headcrab enemy and something hurts it, it's going to forget about the crab for a while.
				SetEnemy( NULL );
				return NPC_STATE_ALERT;
			}
			break;
		}
	}

	return BaseClass::SelectIdealState();
}


//------------------------------------------------------------------------------
//
// Schedules
//
//------------------------------------------------------------------------------

AI_BEGIN_CUSTOM_NPC( npc_bullsquid, CNPC_Bullsquid )

	DECLARE_TASK( TASK_SQUID_HOPTURN )
	DECLARE_TASK( TASK_SQUID_EAT )

	DECLARE_CONDITION( COND_SQUID_SMELL_FOOD )

	DECLARE_ACTIVITY( ACT_SQUID_EXCITED )
	DECLARE_ACTIVITY( ACT_SQUID_EAT )
	DECLARE_ACTIVITY( ACT_SQUID_DETECT_SCENT )
	DECLARE_ACTIVITY( ACT_SQUID_INSPECT_FLOOR )

	DECLARE_INTERACTION( g_interactionBullsquidThrow )

	//=========================================================
	// > SCHED_SQUID_HURTHOP
	//=========================================================
	DEFINE_SCHEDULE
	(
		SCHED_SQUID_HURTHOP,
	
		"	Tasks"
		"		TASK_STOP_MOVING			0"
		"		TASK_SOUND_WAKE				0"
		"		TASK_SQUID_HOPTURN			0"
		"		TASK_FACE_ENEMY				0"
		"	"
		"	Interrupts"
	)
	
	//=========================================================
	// > SCHED_SQUID_SEECRAB
	//=========================================================
	DEFINE_SCHEDULE
	(
		SCHED_SQUID_SEECRAB,
	
		"	Tasks"
		"		TASK_STOP_MOVING			0"
		"		TASK_SOUND_WAKE				0"
		"		TASK_PLAY_SEQUENCE			ACTIVITY:ACT_SQUID_EXCITED"
		"		TASK_FACE_ENEMY				0"
		"	"
		"	Interrupts"
		"		COND_LIGHT_DAMAGE"
		"		COND_HEAVY_DAMAGE"
	)
	
	//=========================================================
	// > SCHED_SQUID_EAT
	//=========================================================
	DEFINE_SCHEDULE
	(
		SCHED_SQUID_EAT,
	
		"	Tasks"
		"		TASK_STOP_MOVING					0"
		"		TASK_SQUID_EAT						10"
		"		TASK_STORE_LASTPOSITION				0"
		"		TASK_GET_PATH_TO_BESTSCENT			0"
		"		TASK_WALK_PATH						0"
		"		TASK_WAIT_FOR_MOVEMENT				0"
		"		TASK_PLAY_SEQUENCE					ACTIVITY:ACT_SQUID_EAT"
		"		TASK_PLAY_SEQUENCE					ACTIVITY:ACT_SQUID_EAT"
		"		TASK_PLAY_SEQUENCE					ACTIVITY:ACT_SQUID_EAT"
		"		TASK_SQUID_EAT						50"
		"		TASK_GET_PATH_TO_LASTPOSITION		0"
		"		TASK_WALK_PATH						0"
		"		TASK_WAIT_FOR_MOVEMENT				0"
		"		TASK_CLEAR_LASTPOSITION				0"
		"	"
		"	Interrupts"
		"		COND_LIGHT_DAMAGE"
		"		COND_HEAVY_DAMAGE"
		"		COND_NEW_ENEMY"
		"		COND_SMELL"
	)
	
	//=========================================================
	// > SCHED_SQUID_SNIFF_AND_EAT
	//=========================================================
	DEFINE_SCHEDULE
	(
		SCHED_SQUID_SNIFF_AND_EAT,
	
		"	Tasks"
		"		TASK_STOP_MOVING					0"
		"		TASK_SQUID_EAT						10"
		"		TASK_PLAY_SEQUENCE					ACTIVITY:ACT_SQUID_DETECT_SCENT"
		"		TASK_STORE_LASTPOSITION				0"
		"		TASK_GET_PATH_TO_BESTSCENT			0"
		"		TASK_WALK_PATH						0"
		"		TASK_WAIT_FOR_MOVEMENT				0"
		"		TASK_PLAY_SEQUENCE					ACTIVITY:ACT_SQUID_EAT"
		"		TASK_PLAY_SEQUENCE					ACTIVITY:ACT_SQUID_EAT"
		"		TASK_PLAY_SEQUENCE					ACTIVITY:ACT_SQUID_EAT"
		"		TASK_SQUID_EAT						50"
		"		TASK_GET_PATH_TO_LASTPOSITION		0"
		"		TASK_WALK_PATH						0"
		"		TASK_WAIT_FOR_MOVEMENT				0"
		"		TASK_CLEAR_LASTPOSITION				0"
		"	"
		"	Interrupts"
		"		COND_LIGHT_DAMAGE"
		"		COND_HEAVY_DAMAGE"
		"		COND_NEW_ENEMY"
		"		COND_SMELL"
	)
	
	//=========================================================
	// > SCHED_SQUID_WALLOW
	//=========================================================
	DEFINE_SCHEDULE
	(
		SCHED_SQUID_WALLOW,
	
		"	Tasks"
		"		TASK_STOP_MOVING				0"
		"		TASK_SQUID_EAT					10"
		"		TASK_STORE_LASTPOSITION			0"
		"		TASK_GET_PATH_TO_BESTSCENT		0"
		"		TASK_WALK_PATH					0"
		"		TASK_WAIT_FOR_MOVEMENT			0"
		"		TASK_PLAY_SEQUENCE				ACTIVITY:ACT_SQUID_INSPECT_FLOOR"
		"		TASK_SQUID_EAT					50"
		"		TASK_GET_PATH_TO_LASTPOSITION	0"
		"		TASK_WALK_PATH					0"
		"		TASK_WAIT_FOR_MOVEMENT			0"
		"		TASK_CLEAR_LASTPOSITION			0"
		"	"
		"	Interrupts"
		"		COND_LIGHT_DAMAGE"
		"		COND_HEAVY_DAMAGE"
		"		COND_NEW_ENEMY"
	)
	
AI_END_CUSTOM_NPC()
