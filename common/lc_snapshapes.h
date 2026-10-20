#pragma once

#include "lc_math.h"

enum class lcSnapGender
{
	Male,
	Female
};

struct lcSnapSection
{
	QString Shape; // R (round), A (axle), S (square), _L or L_ (radius extension)
	float Radius;
	float Length;
};

struct lcSnapCylinder
{
	lcMatrix44 Transform; // In LDraw part coordinates, the shape runs along its -Y axis.
	lcSnapGender Gender;
	std::vector<lcSnapSection> Sections;
	QString Id;
	QString Group;
	bool Slide;
	bool Center;
	bool InheritMirrored; // mirror=cor (the default): flip a radius axis when used mirrored; mirror=none: drop it.
	std::string Source;

	lcVector3 GetPosition() const
	{
		return Transform.GetTranslation();
	}

	lcVector3 GetAxis() const
	{
		return lcNormalize(lcMul30(lcVector3(0.0f, -1.0f, 0.0f), Transform));
	}
};

// A connection in use between two pieces, in LeoCAD world coordinates.
struct lcSnapConnection
{
	lcVector3 Position;
	lcVector3 Axis; // The way the connected shapes run.
};

// How connection shapes fit together, on lists of shapes and transforms, wherever the shapes come from.
class lcSnapShapes
{
public:
	static bool IsRound(const lcSnapCylinder& Cylinder);
	static bool AreCompatible(const lcSnapCylinder& a, const lcSnapCylinder& b);

	// Snaps the moving shapes' point closest to GrabPoint (in the moving piece's LeoCAD coordinates)
	// onto the target's point closest to HitPoint that faces the viewer, in LeoCAD world coordinates.
	// ReferenceTransform is the moving piece's transform when the drag started.
	static std::optional<lcMatrix44> FindPlacement(const std::vector<lcSnapCylinder>& MovingCylinders, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const std::vector<lcSnapCylinder>& TargetCylinders, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection);
	// Connections by which shapes at Transform are attached to any of Others.
	static std::vector<lcSnapConnection> FindConnections(const std::vector<lcSnapCylinder>& Cylinders, const lcMatrix44& Transform, const std::vector<std::pair<const std::vector<lcSnapCylinder>*, lcMatrix44>>& Others);
	// A quarter turn (Direction 1 or -1) around a connection that attaches shapes at Transform to
	// Others: the one nearest PointerRay (start and end of the line under the pointer), or without it
	// the one closest to Center (in the shapes' coordinates). Returns the pivot and the rotation, or
	// nothing if they aren't attached.
	static std::optional<std::pair<lcVector3, lcMatrix33>> GetConnectionTurn(const std::vector<lcSnapCylinder>& Cylinders, const lcMatrix44& Transform, const lcVector3& Center, const std::vector<std::pair<const std::vector<lcSnapCylinder>*, lcMatrix44>>& Others, int Direction, const std::optional<std::array<lcVector3, 2>>& PointerRay);

	static bool IsRigid(const lcMatrix44& m);
	static float GetDeterminant(const lcMatrix44& m);
private:
};
