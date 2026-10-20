#include "lc_global.h"
#include "lc_connectivity.h"

float lcConnectivity::GetDeterminant(const lcMatrix44& m)
{
	return lcDot(lcVector3(m.r[0]), lcCross(lcVector3(m.r[1]), lcVector3(m.r[2])));
}

// Whether a male shape fits into a female one running the same way, with its position at Offset
// from the female's: every part of the male inside the female is no wider than the female there,
// and at least one matching pair of sections grips, the shorter one completely inside the longer.
// Radius extensions (_L, L_) may squeeze into narrower sections.
bool lcConnectivity::FitsAt(const lcConnectionCylinder& Male, const lcConnectionCylinder& Female, float Offset)
{
	const auto IsExtension = [](const lcConnectionSection& Section)
	{
		return Section.Shape == QLatin1String("_L") || Section.Shape == QLatin1String("L_");
	};

	bool Grips = false;
	float MaleStart = Male.GetExtent().first + Offset;

	for (const lcConnectionSection& MaleSection : Male.Sections)
	{
		const float MaleEnd = MaleStart + MaleSection.Length;
		float FemaleStart = Female.GetExtent().first;

		for (const lcConnectionSection& FemaleSection : Female.Sections)
		{
			const float FemaleEnd = FemaleStart + FemaleSection.Length;
			const float Overlap = std::min(MaleEnd, FemaleEnd) - std::max(MaleStart, FemaleStart);

			if (Overlap > 0.01f)
			{
				if (MaleSection.Radius > FemaleSection.Radius + 0.01f && !IsExtension(MaleSection) && !IsExtension(FemaleSection))
					return false;

				const bool MaleInside = MaleStart > FemaleStart - 0.01f && MaleEnd < FemaleEnd + 0.01f;
				const bool FemaleInside = FemaleStart > MaleStart - 0.01f && FemaleEnd < MaleEnd + 0.01f;

				if (AreSectionsCompatible(MaleSection, FemaleSection) && (MaleInside || FemaleInside))
					Grips = true;
			}

			FemaleStart = FemaleEnd;
		}

		MaleStart = MaleEnd;
	}

	return Grips;
}

// Where a male shape can sit along a female one running the same way, as ranges of the male's
// offset from the female's position: where it fits without passing a closed end of either.
std::vector<std::pair<float, float>> lcConnectivity::GetInsertRanges(const lcConnectionCylinder& Male, const lcConnectionCylinder& Female)
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
		return {};

	// Whether the male fits only changes where a section boundary of one passes one of the other,
	// so testing those offsets and the ones halfway between them finds every range.
	std::vector<float> MaleBoundaries = { MaleStart };
	std::vector<float> FemaleBoundaries = { FemaleStart };

	for (const lcConnectionSection& Section : Male.Sections)
		MaleBoundaries.push_back(MaleBoundaries.back() + Section.Length);

	for (const lcConnectionSection& Section : Female.Sections)
		FemaleBoundaries.push_back(FemaleBoundaries.back() + Section.Length);

	std::vector<float> Offsets = { Low, High };

	for (float FemaleBoundary : FemaleBoundaries)
		for (float MaleBoundary : MaleBoundaries)
			Offsets.push_back(FemaleBoundary - MaleBoundary);

	Offsets.erase(std::remove_if(Offsets.begin(), Offsets.end(), [Low, High](float Offset) { return Offset < Low - 0.01f || Offset > High + 0.01f; }), Offsets.end());
	std::sort(Offsets.begin(), Offsets.end());
	Offsets.erase(std::unique(Offsets.begin(), Offsets.end(), [](float a, float b) { return b - a < 0.01f; }), Offsets.end());

	for (size_t Index = Offsets.size() - 1; Index > 0; Index--)
		Offsets.insert(Offsets.begin() + Index, (Offsets[Index - 1] + Offsets[Index]) / 2.0f);

	std::vector<std::pair<float, float>> Ranges;
	bool Fitted = false;

	for (float Offset : Offsets)
	{
		const bool Fits = FitsAt(Male, Female, Offset);

		if (Fits && Fitted)
			Ranges.back().second = Offset;
		else if (Fits)
			Ranges.emplace_back(Offset, Offset);

		Fitted = Fits;
	}

	return Ranges;
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

// Distance from Point to the axis of a shape at Frame (LeoCAD coordinates, shape along +Z).
float lcConnectivity::GetDistanceToShape(const lcConnectionCylinder& Cylinder, const lcMatrix44& Frame, const lcVector3& Point)
{
	const auto [Start, End] = Cylinder.GetExtent();
	const lcVector3 Position = Frame.GetTranslation();
	const lcVector3 Axis = lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), Frame));
	const float Along = lcClamp(lcDot(Point - Position, Axis), Start, End);

	return lcLength(Position + Axis * Along - Point);
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

	for (const lcConnectionSection& MaleSection : Male.Sections)
		for (const lcConnectionSection& FemaleSection : Female.Sections)
			if (AreSectionsCompatible(MaleSection, FemaleSection))
				return true;

	return false;
}

// A round male section also fits a square female one (most anti studs are stored as squares).
bool lcConnectivity::AreSectionsCompatible(const lcConnectionSection& Male, const lcConnectionSection& Female)
{
	return fabsf(Male.Radius - Female.Radius) < 0.01f && (Male.Shape == Female.Shape || (Male.Shape == QLatin1String("R") && Female.Shape == QLatin1String("S")));
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
	const std::vector<std::pair<float, float>> InsertRanges = IsMale ? GetInsertRanges(Cylinder, Other) : GetInsertRanges(Other, Cylinder);
	const float MaleOffset = IsMale ? Along : -Along;

	return std::any_of(InsertRanges.begin(), InsertRanges.end(), [MaleOffset](const std::pair<float, float>& Range) { return MaleOffset > Range.first - 0.5f && MaleOffset < Range.second + 0.5f; });
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
	// (a stud points out of its part, a hole opens against the way its shape runs; a bar or a
	// hole open at both ends can be reached from any side), paired with the moving piece's
	// matching points near where it was grabbed.
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
		const bool OpenBothEnds = !Cylinder.ClosedStart && !Cylinder.ClosedEnd;

		if (!OpenBothEnds && lcDot(Outward, ViewDirection) > -0.05f)
			continue;

		const float Distance = GetDistanceToShape(Cylinder, Frame, HitPoint);
		Targets.push_back({ &Cylinder, Frame, Distance });
		NearestDistance = std::min(NearestDistance, Distance);
	}

	// Rotations are compared to the piece's orientation when the drag started (ReferenceTransform),
	// so they can't drift while dragging.
	const lcMatrix33 ReferenceRotation(ReferenceTransform);
	std::optional<lcMatrix44> Best;

	// Small round shapes (bars, and the pins under 1x plates and bricks, which fit hollow studs)
	// are only used when nothing bigger fits: a brick held over a stud is meant to go on with an
	// anti stud.
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

			const lcVector3 TargetAxis = lcNormalize(lcMul30(ShapeAxis, Target.Frame));

			for (const lcConnectionCylinder& Moving : MovingCylinders)
			{
				if (!AreCompatible(Moving, *Target.Cylinder))
					continue;

				const bool MovingIsMale = Moving.Gender == lcConnectionGender::Male;
				const std::vector<std::pair<float, float>> InsertRanges = MovingIsMale ? GetInsertRanges(Moving, *Target.Cylinder) : GetInsertRanges(*Target.Cylinder, Moving);

				if (InsertRanges.empty())
					continue;

				// How far the moving piece may be shifted along the axis from the aligned positions.
				std::vector<std::pair<float, float>> ShiftRanges;

				for (const std::pair<float, float>& Range : InsertRanges)
					ShiftRanges.emplace_back(MovingIsMale ? Range.first : -Range.second, MovingIsMale ? Range.second : -Range.first);

				const bool Slide = Moving.Slide || Target.Cylinder->Slide;

				const lcMatrix44 MovingFrame = lcMatrix44LDrawToLeoCAD(Moving.Transform);
				const lcMatrix44 MovingInverse = lcMatrix44AffineInverse(MovingFrame);
				const float Score = Target.Distance + GetDistanceToShape(Moving, MovingFrame, GrabPoint);

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
					lcMatrix44 Placement = lcMul(lcMul(MovingInverse, lcMatrix44RotationZ(Angle)), Target.Frame);

					if (!IsRigid(Placement))
						continue;

					// Along the axis, in the allowed range nearest the cursor, follow the cursor where the
					// shapes can slide; otherwise take the end of the range nearer to it.
					const float CursorShift = lcDot(HitPoint - lcMul31(GrabPoint, Placement), TargetAxis);
					const std::pair<float, float>& ShiftRange = *std::min_element(ShiftRanges.begin(), ShiftRanges.end(), [CursorShift](const std::pair<float, float>& a, const std::pair<float, float>& b)
					{
						return fabsf(CursorShift - lcClamp(CursorShift, a.first, a.second)) < fabsf(CursorShift - lcClamp(CursorShift, b.first, b.second));
					});
					const float ShiftLow = ShiftRange.first;
					const float ShiftHigh = ShiftRange.second;
					float Shift;

					if (Slide)
						Shift = lcClamp(CursorShift, ShiftLow, ShiftHigh);
					else
						Shift = fabsf(CursorShift - ShiftLow) <= fabsf(CursorShift - ShiftHigh) ? ShiftLow : ShiftHigh;

					Placement.SetTranslation(Placement.GetTranslation() + TargetAxis * Shift);

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

std::vector<lcSnapConnection> lcConnectivity::FindConnections(const std::vector<lcConnectionCylinder>& Cylinders, const lcMatrix44& Transform, const std::vector<std::pair<const std::vector<lcConnectionCylinder>*, lcMatrix44>>& Others)
{
	std::vector<lcSnapConnection> Connections;
	const lcVector3 ShapeAxis(0.0f, 0.0f, 1.0f);

	for (const lcConnectionCylinder& Cylinder : Cylinders)
	{
		const lcMatrix44 Frame = lcMul(lcMatrix44LDrawToLeoCAD(Cylinder.Transform), Transform);
		const lcVector3 Axis = lcNormalize(lcMul30(ShapeAxis, Frame));
		bool Connected = false;

		for (const auto& [OtherCylinders, OtherTransform] : Others)
		{
			for (const lcConnectionCylinder& OtherCylinder : *OtherCylinders)
			{
				if (!AreCompatible(Cylinder, OtherCylinder))
					continue;

				const lcMatrix44 OtherFrame = lcMul(lcMatrix44LDrawToLeoCAD(OtherCylinder.Transform), OtherTransform);

				if (lcDot(lcNormalize(lcMul30(ShapeAxis, OtherFrame)), Axis) < 0.99f)
					continue;

				// On the same axis, and seated as deep as the shapes allow.
				const lcVector3 Offset = Frame.GetTranslation() - OtherFrame.GetTranslation();
				const float Along = lcDot(Offset, Axis);

				if (lcLength(Offset - Axis * Along) > 0.5f)
					continue;

				const bool IsMale = Cylinder.Gender == lcConnectionGender::Male;
				const std::vector<std::pair<float, float>> InsertRanges = IsMale ? GetInsertRanges(Cylinder, OtherCylinder) : GetInsertRanges(OtherCylinder, Cylinder);
				const float MaleOffset = IsMale ? Along : -Along;

				if (std::any_of(InsertRanges.begin(), InsertRanges.end(), [MaleOffset](const std::pair<float, float>& Range) { return MaleOffset > Range.first - 0.5f && MaleOffset < Range.second + 0.5f; }))
				{
					Connected = true;
					break;
				}
			}

			if (Connected)
				break;
		}

		if (Connected)
			Connections.push_back({ Frame.GetTranslation(), Axis });
	}

	return Connections;
}

std::optional<std::pair<lcVector3, lcMatrix33>> lcConnectivity::GetConnectionTurn(const std::vector<lcConnectionCylinder>& Cylinders, const lcMatrix44& Transform, const lcVector3& Center, const std::vector<std::pair<const std::vector<lcConnectionCylinder>*, lcMatrix44>>& Others, int Direction, const std::optional<std::array<lcVector3, 2>>& PointerRay)
{
	const std::vector<lcSnapConnection> Connections = FindConnections(Cylinders, Transform, Others);

	if (Connections.empty())
		return std::nullopt;

	// Turn around the connection the pointer is at; otherwise the one closest to the piece's
	// centre, so it turns in place.
	const lcVector3 PieceCenter = lcMul31(Center, Transform);
	const auto Distance = [&PointerRay, &PieceCenter](const lcSnapConnection& Connection)
	{
		if (PointerRay)
			return lcRayPointDistance(Connection.Position, (*PointerRay)[0], (*PointerRay)[1]);

		return lcLength(Connection.Position - PieceCenter);
	};

	const lcSnapConnection* Pivot = &Connections.front();

	for (const lcSnapConnection& Connection : Connections)
		if (Distance(Connection) < Distance(*Pivot) - 0.01f)
			Pivot = &Connection;

	return std::make_pair(Pivot->Position, lcMatrix33FromAxisAngle(Pivot->Axis, Direction * LC_PI / 2.0f));
}
