#include "StdAfx.h"
#include "ClimableObject.h"
#include "ai_space.h"
#include "Multiplayer/ArenaLadderNavigation.h"
#include "Multiplayer/ArenaLadderGeometry.h"
#include "../xrPhysics/IPHStaticGeomShell.h"
#include "xrServer_Objects_ALife.h"
#include "../xrPhysics/PHCharacter.h"
#include "../xrPhysics/MathUtils.h"
#include "../xrPhysics/ExtendedGeom.h"
#include "../xrEngine/GameMtlLib.h"

#ifdef DEBUG_DRAW
#	include "debug_renderer.h"
#	include "Level.h"
#	include "PHDebug.h"
#endif

static const float down_leader_extension_tolerance=0.2f;
static const float up_leader_extension_tolerance=0.0f;

namespace
{
using Multiplayer::ArenaLadders::Point;

Point LadderPoint(const Fvector& Vector)
{
	return {Vector.x, Vector.y, Vector.z};
}

void AssignLadderPoint(Fvector& Vector, Point Value)
{
	Vector.set(Value.X, Value.Y, Value.Z);
}
}

CClimableObject::CClimableObject		():  m_pStaticShell ( nullptr ), m_radius(0.f), m_material(u16(-1))
{
	
}

CClimableObject::~CClimableObject	()
{

}
void CClimableObject::	Load				( const char* section)
{
	inherited::Load(section);
}
bool CClimableObject::	net_Spawn			( CSE_Abstract* DC)
{
	CSE_Abstract				*e = (CSE_Abstract*)(DC);
	CSE_ALifeObjectClimable	*CLB=smart_cast<CSE_ALifeObjectClimable*>(e);
	const auto* Box = CLB && !CLB->shapes.empty() && CLB->shapes[0].type == CShapeData::cfBox
		? &CLB->shapes[0].data.box : nullptr;
	using namespace Multiplayer::ArenaLadders;
	const auto Half = Box ? HalfSizes{Box->_11, Box->_22, Box->_33} : HalfSizes{};
	if (!ValidHalfSizes(Half))
	{
		// The failed-spawn path calls net_Destroy, which requires base spawn initialization.
		inherited::net_Spawn(DC);
		Msg("! Invalid climbable spawn geometry");
		return false;
	}
	m_material = GMLib.GetMaterialIdx(CLB->material.c_str());
	m_radius = std::max({Half[0], Half[1], Half[2]});
	const bool ret = inherited::net_Spawn(DC);
	const auto& Frame = XFORM();
	const auto Shape = BuildGeometry(LadderPoint(Frame.c),
		{LadderPoint(Frame.i), LadderPoint(Frame.j), LadderPoint(Frame.k)}, Half);
	if (!Shape)
	{
		Msg("! Invalid climbable spawn transform");
		return false;
	}

	SpatialComponent->type &= ~ESPATIAL_TYPE::VISIBLEFORAI;
	SpatialComponent->type |= ESPATIAL_TYPE::LADDER;

	AssignLadderPoint(CObject::Position(), Shape->Center);
	AssignLadderPoint(m_axis, Shape->Axis);
	AssignLadderPoint(m_side, Shape->Side);
	AssignLadderPoint(m_norm, Shape->Normal);
	m_box.m_halfsize.set(Shape->Half[0], Shape->Half[1], Shape->Half[2]);
	m_box.xform_set(Fidentity);
	m_pStaticShell = P_BuildLeaderGeomShell(this, ObjectContactCallback, m_box);

//	shedule_unregister();
	processing_deactivate();
	if (ret)
	{
		if (auto* Ladders = ai().ArenaLadderLinks())
		{
			Ladders->Register(*this);
		}
	}
	return ret;
}
void CClimableObject::	net_Destroy			()
{
	if (auto* Ladders = ai().ArenaLadderLinks())
	{
		Ladders->Unregister(*this);
	}
	inherited::net_Destroy();
	DestroyStaticGeomShell( m_pStaticShell );

	//m_pStaticShell->Deactivate();
	//xr_delete(m_pStaticShell);
}
void CClimableObject::	shedule_Update		( u32 dt)							// Called by shedule
{
	inherited::shedule_Update(dt);
}
void CClimableObject::	UpdateCL			( )								// Called each frame, so no need for d
{
	inherited::UpdateCL();
}

void	CClimableObject::Center				(Fvector &C) const
{
		C.set(XFORM().c);
}
float	CClimableObject::Radius				() const
{
		return							m_radius;
}

float		CClimableObject::	DDLowerP		(CPHCharacter	*actor,Fvector	&out_dir)const
{
	VERIFY(actor);
	Fvector pos;
	LowerPoint(out_dir);
	actor->GetFootCenter(pos);
	out_dir.sub(pos);
	return to_mag_and_dir(out_dir);
}
float		CClimableObject::	DDUpperP		(CPHCharacter	*actor,Fvector	&out_dir)const
{
	VERIFY(actor);
	Fvector pos;
	UpperPoint(out_dir);
	actor->GetFootCenter(pos);
	out_dir.sub(pos);
	return to_mag_and_dir(out_dir);
}


void		CClimableObject::	DefineClimbState	(CPHCharacter	*actor)const
{


}
float		CClimableObject::DDAxis(Fvector& dir)const
{
	dir.set(m_axis);
	return to_mag_and_dir(dir);
}

float	CClimableObject::DDSide(Fvector& dir)const
{
	dir.set(m_side);
	return to_mag_and_dir(dir);
}
float	CClimableObject::DDNorm(Fvector &dir)const
{
	dir.set(m_norm);
	return to_mag_and_dir(dir);
}
float		CClimableObject::	DDToAxis			(CPHCharacter	*actor,Fvector &out_dir)const
{
	VERIFY(actor);
	DToAxis(actor,out_dir);
	return to_mag_and_dir(out_dir);
}

void	CClimableObject::	POnAxis	(CPHCharacter	*actor,Fvector	&P)const
{
	VERIFY(actor);
	actor->GetFootCenter(P);
	prg_pos_on_axis(Position(),m_axis,P);
}
void		CClimableObject::	LowerPoint			(Fvector &P)const
{
	AssignLadderPoint(P, Multiplayer::ArenaLadders::ClimbEndpoint(LadderPoint(XFORM().c), LadderPoint(m_axis), LadderPoint(m_norm), false));
}

void		CClimableObject::	UpperPoint			(Fvector &P)const
{
	AssignLadderPoint(P, Multiplayer::ArenaLadders::ClimbEndpoint(LadderPoint(XFORM().c), LadderPoint(m_axis), LadderPoint(m_norm), true));
}

void		CClimableObject::DToAxis(CPHCharacter *actor,Fvector &dir)const
{
	VERIFY(actor);
	POnAxis(actor,dir);
	Fvector pos;actor->GetFootCenter(pos);
	dir.sub(pos);
}
void CClimableObject::DSideToAxis		(CPHCharacter	*actor,Fvector	&dir)const
{
	VERIFY(actor);
	DToAxis(actor,dir);
	Fvector side;side.set(m_side);
	to_mag_and_dir(side);
	side.mul(side.dotproduct(dir));
	dir.set(side);
}

float CClimableObject::DDSideToAxis(CPHCharacter *actor,Fvector &dir)const
{
	VERIFY(actor);
	DToAxis(actor,dir);
	Fvector side;side.set(m_side);to_mag_and_dir(side);
	float dot=side.dotproduct(dir);
	if(dot>0.f)
	{
		dir.set(side);
		return dot;
	}
	else
	{
		dir.set(side);
		dir.invert();
		return -dot;
	}
	
}

void CClimableObject::DToPlain(CPHCharacter *actor,Fvector &dist)const
{
 VERIFY(actor);
 DToAxis(actor,dist);
 Fvector norm;norm.set(m_norm);
 to_mag_and_dir(norm);
 float dot=norm.dotproduct(dist);
 norm.mul(dot);
 dist.set(norm);
}

float CClimableObject::DDToPlain(CPHCharacter *actor,Fvector &dir)const
{
	VERIFY(actor);
	DToPlain(actor,dir);
	return to_mag_and_dir(dir);
}

bool CClimableObject::InTouch(CPHCharacter *actor)const
{
	VERIFY(actor);
	Fvector dir;
	const float normal_tolerance=0.05f;
	float foot_radius=actor->FootRadius();
	return (DDToPlain(actor,dir)<foot_radius+m_norm.magnitude()+normal_tolerance&&
			DDSideToAxis(actor,dir)<m_side.magnitude())&&InRange(actor);
	
}

float CClimableObject::AxDistToUpperP(CPHCharacter *actor)const
{
	VERIFY(actor);
	Fvector v1,v2;
	actor->GetFootCenter(v1);
	UpperPoint(v2);
	v2.sub(v1);
	v1.set(m_axis);to_mag_and_dir(v1);
	return v1.dotproduct(v2);
}

float CClimableObject::AxDistToLowerP(CPHCharacter *actor)const
{
	VERIFY(actor);
	Fvector v1,v2;
	actor->GetFootCenter(v1);
	LowerPoint(v2);
	v2.sub(v1);
	v1.set(m_axis);to_mag_and_dir(v1);
	return -v1.dotproduct(v2);
}
bool CClimableObject::InRange(CPHCharacter *actor)const
{
	VERIFY(actor);
	return AxDistToLowerP(actor)>-down_leader_extension_tolerance && AxDistToUpperP(actor)+actor->FootRadius()>-up_leader_extension_tolerance;
}


bool CClimableObject::BeforeLadder(CPHCharacter *actor,float tolerance/*=0.f*/)const
{
	VERIFY(actor);
	Fvector d;
	DToAxis(actor,d);
	Fvector n;n.set(Norm());
	float width=to_mag_and_dir(n);
	return d.dotproduct(n)<-(width+actor->FootRadius()/2.f+tolerance);
}

bool CClimableObject::UsedAI_Locations()
{
	return false;
}


void CClimableObject::ObjectContactCallback(bool&	do_colide,bool bo1,dContact& c,SGameMtl * /*material_1*/,SGameMtl * /*material_2*/)
{
	dxGeomUserData* usr_data_1= PHRetrieveGeomUserData(c.geom.g1);
	dxGeomUserData* usr_data_2=PHRetrieveGeomUserData(c.geom.g2);
	dxGeomUserData* usr_data_ch=nullptr;
	dxGeomUserData* usr_data_lad=nullptr;
	CClimableObject* this_object=nullptr;
	CPHCharacter* ch=nullptr;
	float norm_sign=0.f;
	if(bo1) {
			usr_data_ch=usr_data_2;
			usr_data_lad=usr_data_1;
			norm_sign=-1.f;
		}
	else {
			norm_sign=1.f;
			usr_data_ch=usr_data_1;
			usr_data_lad=usr_data_2;
	}

	if(usr_data_ch&&usr_data_ch->ph_object&&usr_data_ch->ph_object->CastType()==CPHObject::tpCharacter)
		ch=static_cast<CPHCharacter*>(usr_data_ch->ph_object);
	else
	{
		do_colide=false;
		return;
	}
	VERIFY(ch);
	VERIFY(usr_data_lad);
	this_object=static_cast<CClimableObject*>(usr_data_lad->ph_ref_object);
	VERIFY(this_object);
	if(!this_object->BeforeLadder(ch,-0.1f)) do_colide=false;
	
}
#ifdef DEBUG_DRAW
extern	Flags32	dbg_net_Draw_Flags;
void CClimableObject ::OnRender()
{
	if (!dbg_net_Draw_Flags.test(dbg_draw_climbable)
#ifdef DEBUG
		&&!ph_dbg_draw_mask.test(phDbgLadder)
#endif
		) return;

	Fmatrix form;m_box.xform_get(form);
	//form.mulA(XFORM());
	Level().debug_renderer().draw_obb(XFORM(), m_box.m_halfsize, color_xrgb(0, 0, 255));
	Fvector p1,p2,d;
	d.set(m_axis);
	p1.add(XFORM().c,d);
	p2.sub(XFORM().c,d);
	Level().debug_renderer().draw_line(Fidentity, p1, p2, color_xrgb(255, 0, 0));

	d.set(m_side);
	p1.add(XFORM().c,d);
	p2.sub(XFORM().c,d);
	Level().debug_renderer().draw_line(Fidentity, p1, p2, color_xrgb(255, 0, 0));

	d.set(m_norm);
	d.mul(10.f);
	p1.add(XFORM().c,d);
	p2.set(XFORM().c);
	Level().debug_renderer().draw_line(Fidentity, p1, p2, color_xrgb(0, 255, 0));
}
#endif