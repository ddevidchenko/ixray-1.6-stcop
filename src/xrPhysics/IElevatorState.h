#pragma once

enum Estate
{
	clbNone			=0	,				
	clbNearUp			,			
	clbNearDown			,		
	clbClimbingUp		,		
	clbClimbingDown		,	
	clbDepart			,
	clbNoLadder			,
	clbNoState			
};
class IPhysicsShellHolder;
class IClimableObject;
class IElevatorState
{
public:
	virtual Estate	State		()							= 0;
	virtual	void	NetRelcase	( IPhysicsShellHolder* O )	= 0;
	// NPC path traversal supplies its own steering and exit points.
	virtual bool BeginDirectedClimb(IClimableObject& Ladder, bool Up) = 0;
	virtual void EndDirectedClimb() = 0;
	virtual bool DirectedClimbActive() const = 0;
protected:
	virtual	~IElevatorState() {}
};