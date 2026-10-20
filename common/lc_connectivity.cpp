#include "lc_global.h"
#include "lc_connectivity.h"

float lcConnectivity::GetDeterminant(const lcMatrix44& m)
{
	return lcDot(lcVector3(m.r[0]), lcCross(lcVector3(m.r[1]), lcVector3(m.r[2])));
}

// Where a male shape can sit along a female one running the same way, as the male's offset from
// the female's position: as deep as possible without passing a closed end of either.
std::optional<std::pair<float, float>> lcConnectivity::GetInsertRange(const lcConnectionCylinder& Male, const lcConnectionCylinder& Female)
{
	const auto [MaleStart, MaleEnd] = Male.GetExtent();
	const auto [FemaleStart, FemaleEnd] = Female.GetExtent();
	float Low = FemaleStart - MaleEnd;
	float High = FemaleEnd - MaleStart;

	if (Female.ClosedStart)
		Low = std::max(Low, FemaleStart - MaleStart);

	if (Female.ClosedEnd)
		High = std::min(High, FemaleEnd - MaleEnd);

	if (Male.ClosedStart)
		High = std::min(High, FemaleStart - MaleStart);

	if (Male.ClosedEnd)
		Low = std::max(Low, FemaleEnd - MaleEnd);

	if (Low > High + 0.01f)
		return std::nullopt;

	// The offsets where the shorter shape is completely inside the longer one.
	const float FullLow = std::min(FemaleStart - MaleStart, FemaleEnd - MaleEnd);
	const float FullHigh = std::max(FemaleStart - MaleStart, FemaleEnd - MaleEnd);

	if (FullHigh < Low)
		return std::make_pair(Low, Low);

	if (FullLow > High)
		return std::make_pair(High, High);

	return std::make_pair(std::max(Low, FullLow), std::min(High, FullHigh));
}

// A placement must be a plain rotation and translation: anything else would deform the
// selection while dragging (and send other selected pieces flying).
bool lcConnectivity::IsRigid(const lcMatrix44& m)
{
	for (int Row = 0; Row < 4; Row++)
		for (int Column = 0; Column < 4; Column++)
			if (!std::isfinite(m.r[Row][Column]))
				return false;

	for (int Row = 0; Row < 3; Row++)
		if (fabsf(lcLength(lcVector3(m.r[Row])) - 1.0f) > 0.01f)
			return false;

	return fabsf(GetDeterminant(m) - 1.0f) < 0.01f;
}

bool lcConnectivity::IsRound(const lcConnectionCylinder& Cylinder)
{
	return std::all_of(Cylinder.Sections.begin(), Cylinder.Sections.end(), [](const lcConnectionSection& Section) { return Section.Shape != QLatin1String("A") && Section.Shape != QLatin1String("S"); });
}

bool lcConnectivity::AreCompatible(const lcConnectionCylinder& a, const lcConnectionCylinder& b)
{
	if (a.Gender == b.Gender || a.Group.compare(b.Group, Qt::CaseInsensitive) != 0)
		return false;

	const lcConnectionCylinder& Male = a.Gender == lcConnectionGender::Male ? a : b;
	const lcConnectionCylinder& Female = a.Gender == lcConnectionGender::Male ? b : a;

	// A round male shape also fits a square female one (most anti studs are stored as squares).
	for (const lcConnectionSection& MaleSection : Male.Sections)
		for (const lcConnectionSection& FemaleSection : Female.Sections)
			if (fabsf(MaleSection.Radius - FemaleSection.Radius) < 0.01f && (MaleSection.Shape == FemaleSection.Shape || (MaleSection.Shape == QLatin1String("R") && FemaleSection.Shape == QLatin1String("S"))))
				return true;

	return false;
}

// Whether two compatible shapes at their frames (LeoCAD coordinates, shapes along +Z) are on the
// same axis and seated as deep as the shapes allow.
bool lcConnectivity::IsSeated(const lcConnectionCylinder& Cylinder, const lcMatrix44& Frame, const lcConnectionCylinder& Other, const lcMatrix44& OtherFrame)
{
	const lcVector3 ShapeAxis(0.0f, 0.0f, 1.0f);
	const lcVector3 Axis = lcNormalize(lcMul30(ShapeAxis, Frame));

	if (lcDot(lcNormalize(lcMul30(ShapeAxis, OtherFrame)), Axis) < 0.99f)
		return false;

	const lcVector3 Offset = Frame.GetTranslation() - OtherFrame.GetTranslation();
	const float Along = lcDot(Offset, Axis);

	if (lcLength(Offset - Axis * Along) > 0.5f)
		return false;

	const bool IsMale = Cylinder.Gender == lcConnectionGender::Male;
	const std::optional<std::pair<float, float>> InsertRange = IsMale ? GetInsertRange(Cylinder, Other) : GetInsertRange(Other, Cylinder);
	const float MaleOffset = IsMale ? Along : -Along;

	return InsertRange && MaleOffset > InsertRange->first - 0.5f && MaleOffset < InsertRange->second + 0.5f;
}

void lcConnectivity::AddToBoundingBox(const std::vector<lcConnectionCylinder>& Cylinders, const lcMatrix44& Transform, lcBoundingBox& Box)
{
	for (const lcConnectionCylinder& Cylinder : Cylinders)
	{
		const lcMatrix44 Frame = lcMul(lcMatrix44LDrawToLeoCAD(Cylinder.Transform), Transform);
		const lcVector3 Axis = lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), Frame));
		const lcVector3 Radius(Cylinder.GetRadius(), Cylinder.GetRadius(), Cylinder.GetRadius());
		const auto [Start, End] = Cylinder.GetExtent();

		for (float Along : { Start, End })
		{
			const lcVector3 Point = Frame.GetTranslation() + Axis * Along;

			Box.Min = lcMin(Box.Min, Point - Radius);
			Box.Max = lcMax(Box.Max, Point + Radius);
		}
	}
}

bool lcConnectivity::AreConnected(const std::vector<lcConnectionCylinder>& Cylinders, const lcMatrix44& Transform, const std::vector<lcConnectionCylinder>& OtherCylinders, const lcMatrix44& OtherTransform)
{
	for (const lcConnectionCylinder& Cylinder : Cylinders)
	{
		const lcMatrix44 Frame = lcMul(lcMatrix44LDrawToLeoCAD(Cylinder.Transform), Transform);

		for (const lcConnectionCylinder& OtherCylinder : OtherCylinders)
			if (AreCompatible(Cylinder, OtherCylinder) && IsSeated(Cylinder, Frame, OtherCylinder, lcMul(lcMatrix44LDrawToLeoCAD(OtherCylinder.Transform), OtherTransform)))
				return true;
	}

	return false;
}

bool lcConnectivity::AreConnected(const std::vector<std::pair<const std::vector<lcConnectionCylinder>*, lcMatrix44>>& Shapes, const std::vector<std::pair<const std::vector<lcConnectionCylinder>*, lcMatrix44>>& OtherShapes)
{
	for (const auto& [Cylinders, Transform] : Shapes)
		for (const auto& [OtherCylinders, OtherTransform] : OtherShapes)
			if (AreConnected(*Cylinders, Transform, *OtherCylinders, OtherTransform))
				return true;

	return false;
}

std::optional<lcMatrix44> lcConnectivity::FindPlacement(const std::vector<lcConnectionCylinder>& MovingCylinders, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const std::vector<lcConnectionCylinder>& TargetCylinders, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection)
{
	if (MovingCylinders.empty() || TargetCylinders.empty())
		return std::nullopt;

	// Snap info is in LDraw coordinates, pieces are in LeoCAD coordinates. In a
	// converted snap frame the shape runs along local +Z instead of LDraw's -Y.
	const lcVector3 ShapeAxis(0.0f, 0.0f, 1.0f);

	// Connection candidates: the target's points near the cursor that open towards the camera
	// (a stud points out of its part, a hole opens against the way its shape runs), paired with
	// the moving piece's matching points near where it was grabbed.
	struct lcTargetPoint
	{
		const lcConnectionCylinder* Cylinder;
		lcMatrix44 Frame;
		float Distance;
	};

	std::vector<lcTargetPoint> Targets;
	float NearestDistance = FLT_MAX;

	for (const lcConnectionCylinder& Cylinder : TargetCylinders)
	{
		const lcMatrix44 Frame = lcMul(lcMatrix44LDrawToLeoCAD(Cylinder.Transform), TargetTransform);
		const lcVector3 Axis = lcNormalize(lcMul30(ShapeAxis, Frame));
		const lcVector3 Outward = Cylinder.Gender == lcConnectionGender::Male ? Axis : -Axis;

		if (lcDot(Outward, ViewDirection) > -0.05f)
			continue;

		const float Distance = lcLength(Frame.GetTranslation() - HitPoint);
		Targets.push_back({ &Cylinder, Frame, Distance });
		NearestDistance = std::min(NearestDistance, Distance);
	}

	// Rotations are compared to the piece's orientation when the drag started (ReferenceTransform),
	// so they can't drift while dragging.
	const lcMatrix33 ReferenceRotation(ReferenceTransform);
	std::optional<lcMatrix44> Best;

	// Small round shapes (bars, and the pins under 1x plates and bricks going into hollow studs)
	// are only used when nothing bigger fits: their snap info is stretched along tall bricks,
	// which would sink the brick over the stud.
	for (int Pass = 0; Pass < 2 && !Best; Pass++)
	{
		float BestScore = FLT_MAX;
		float BestTrace = -FLT_MAX;

		for (const lcTargetPoint& Target : Targets)
		{
			if (Target.Distance > NearestDistance + 8.0f)
				continue;

			float MaxRadius = 0.0f;

			for (const lcConnectionSection& Section : Target.Cylinder->Sections)
				MaxRadius = std::max(MaxRadius, Section.Radius);

			if ((MaxRadius < 5.0f) != (Pass == 1))
				continue;

			for (const lcConnectionCylinder& Moving : MovingCylinders)
			{
				if (!AreCompatible(Moving, *Target.Cylinder))
					continue;

				const lcMatrix44 MovingFrame = lcMatrix44LDrawToLeoCAD(Moving.Transform);
				const lcMatrix44 MovingInverse = lcMatrix44AffineInverse(MovingFrame);
				const float Score = Target.Distance + lcLength(MovingFrame.GetTranslation() - GrabPoint);

				if (Score > BestScore + 0.5f)
					continue;

				// Quarter turns around the connection axis. A round shape in a round hole may also
				// keep any angle, but only if the piece already lines up with the connection axis.
				std::vector<float> Angles = { 0.0f, LC_PI / 2.0f, LC_PI, LC_PI * 1.5f };

				if (IsRound(Moving) && IsRound(*Target.Cylinder))
				{
					const lcMatrix33 Turn = lcMul(lcMul(lcMatrix33(MovingFrame), ReferenceRotation), lcMatrix33AffineInverse(lcMatrix33(Target.Frame)));

					if (lcDot(Turn.r[2], lcVector3(0.0f, 0.0f, 1.0f)) > 0.999f)
						Angles.push_back(atan2f(Turn.r[0][1], Turn.r[0][0]));
				}

				for (float Angle : Angles)
				{
					const lcMatrix44 Placement = lcMul(lcMul(MovingInverse, lcMatrix44RotationZ(Angle)), Target.Frame);

					if (!IsRigid(Placement))
						continue;

					const lcMatrix33 Rotation(Placement);
					float Trace = 0.0f;

					for (int Row = 0; Row < 3; Row++)
						Trace += lcDot(Rotation.r[Row], ReferenceRotation.r[Row]);

					// Clearly better pairs win; between equally good ones, the closest rotation.
					if (Score < BestScore - 0.5f || Trace > BestTrace)
					{
						BestTrace = Score < BestScore - 0.5f ? Trace : std::max(BestTrace, Trace);
						BestScore = std::min(BestScore, Score);
						Best = Placement;
					}
				}
			}
		}
	}

	return Best;
}
