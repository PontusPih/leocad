#pragma once

#include "lc_math.h"

enum class lcConnectionGender
{
	Male,
	Female
};

struct lcConnectionSection
{
	QString Shape; // R (round), A (axle), S (square), _L or L_ (radius extension)
	float Radius;
	float Length;
};

// Which scaling of a sub-file a shape survives; it is dropped from scaled sub-files otherwise.
enum class lcConnectionScale
{
	None,
	Length,
	Radius,
	LengthOrRadius
};

struct lcConnectionCylinder
{
	lcMatrix44 Transform; // In LDraw part coordinates, the shape runs along its -Y axis.
	lcConnectionGender Gender;
	std::vector<lcConnectionSection> Sections;
	QString Id;
	QString Group;
	bool Slide;
	bool Center;
	bool ClosedStart; // caps: A, the end at pos (or the near end of a centered shape).
	bool ClosedEnd; // caps: B, the far end along the shape.
	lcConnectionScale Scale;
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

	float GetLength() const
	{
		float Length = 0.0f;

		for (const lcConnectionSection& Section : Sections)
			Length += Section.Length;

		return Length;
	}

	float GetRadius() const
	{
		float Radius = 0.0f;

		for (const lcConnectionSection& Section : Sections)
			Radius = std::max(Radius, Section.Radius);

		return Radius;
	}

	// Start and end along the shape's axis, from its position.
	std::pair<float, float> GetExtent() const
	{
		const float Length = GetLength();
		return Center ? std::make_pair(-Length / 2.0f, Length / 2.0f) : std::make_pair(0.0f, Length);
	}
};

// A connection in use between two pieces, in LeoCAD world coordinates.
struct lcSnapConnection
{
	lcVector3 Position; // The middle of where the shapes overlap.
	lcVector3 Axis; // The way the connected shapes run.
	float Radius;
};

// A placement found for a moving piece and the connection it was built on, in LeoCAD world coordinates.
struct lcSnapMatch
{
	lcMatrix44 Transform; // Of the moving piece.
	lcVector3 MovingPosition; // The middle of where the shapes overlap, at Transform.
	lcVector3 Axis; // The way the connected shapes run.
	float Radius; // Of the target's shape.
	const lcPieceData* TargetPieceData; // Set by lcShadowLibrary.
	lcMatrix44 TargetTransform;
	std::vector<lcSnapConnection> Connections; // All the placement makes with the target, this one included.
};

// How connection shapes fit together, on lists of shapes and transforms, wherever the shapes come from.
class lcConnectivity
{
public:
	static bool IsRound(const lcConnectionCylinder& Cylinder);
	static bool AreCompatible(const lcConnectionCylinder& a, const lcConnectionCylinder& b);
	// Whether any of the shapes at Transform is engaged with any of the other shapes at OtherTransform.
	static bool AreConnected(const std::vector<lcConnectionCylinder>& Cylinders, const lcMatrix44& Transform, const std::vector<lcConnectionCylinder>& OtherCylinders, const lcMatrix44& OtherTransform);
	// The same for sets of shapes, each at its own transform, such as the parts of two pieces.
	static bool AreConnected(const std::vector<std::pair<const std::vector<lcConnectionCylinder>*, lcMatrix44>>& Shapes, const std::vector<std::pair<const std::vector<lcConnectionCylinder>*, lcMatrix44>>& OtherShapes);
	// Grows Box to hold the shapes at Transform: shapes that are connected have overlapping boxes.
	static void AddToBoundingBox(const std::vector<lcConnectionCylinder>& Cylinders, const lcMatrix44& Transform, lcBoundingBox& Box);

	// Where a male shape can sit along a female one running the same way, as ranges of the male's
	// offset from the female's position; none if it can't.
	static std::vector<std::pair<float, float>> GetInsertRanges(const lcConnectionCylinder& Male, const lcConnectionCylinder& Female);

	// Snaps the moving shapes' point closest to GrabPoint (in the moving piece's LeoCAD coordinates)
	// onto the target's point closest to HitPoint that faces the viewer, in LeoCAD world coordinates.
	// ReferenceTransform is the moving piece's transform when the drag started.
	static std::optional<lcSnapMatch> FindMatch(const std::vector<lcConnectionCylinder>& MovingCylinders, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const std::vector<lcConnectionCylinder>& TargetCylinders, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection);
	// Connections by which shapes at Transform are attached to any of Others.
	static std::vector<lcSnapConnection> FindConnections(const std::vector<lcConnectionCylinder>& Cylinders, const lcMatrix44& Transform, const std::vector<std::pair<const std::vector<lcConnectionCylinder>*, lcMatrix44>>& Others);
	// A quarter turn (Direction 1 or -1) around a connection that attaches shapes at Transform to
	// Others: the one nearest PointerRay (start and end of the line under the pointer), or without it
	// the one closest to Center (in the shapes' coordinates). Returns the pivot and the rotation, or
	// nothing if they aren't attached.
	static std::optional<std::pair<lcVector3, lcMatrix33>> GetConnectionTurn(const std::vector<lcConnectionCylinder>& Cylinders, const lcMatrix44& Transform, const lcVector3& Center, const std::vector<std::pair<const std::vector<lcConnectionCylinder>*, lcMatrix44>>& Others, int Direction, const std::optional<std::array<lcVector3, 2>>& PointerRay);

	static lcVector3 GetOverlapCenter(const lcConnectionCylinder& Cylinder, const lcMatrix44& Frame, const lcConnectionCylinder& Other, const lcMatrix44& OtherFrame);
	static bool IsRigid(const lcMatrix44& m);
	static float GetDeterminant(const lcMatrix44& m);

private:
	static bool AreSectionsCompatible(const lcConnectionSection& Male, const lcConnectionSection& Female);
	static bool FitsAt(const lcConnectionCylinder& Male, const lcConnectionCylinder& Female, float Offset);
	static bool IsSeated(const lcConnectionCylinder& Cylinder, const lcMatrix44& Frame, const lcConnectionCylinder& Other, const lcMatrix44& OtherFrame);
	static float GetDistanceToShape(const lcConnectionCylinder& Cylinder, const lcMatrix44& Frame, const lcVector3& Point);
};
