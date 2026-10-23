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

// Distance from Point to the axis of a shape at Frame (LeoCAD coordinates, shape along +Z).
float lcConnectivity::GetDistanceToShape(const lcConnectionCylinder& Cylinder, const lcMatrix44& Frame, const lcVector3& Point)
{
	const auto [Start, End] = Cylinder.GetExtent();
	const lcVector3 Position = Frame.GetTranslation();
	const lcVector3 Axis = lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), Frame));
	const float Along = lcClamp(lcDot(Point - Position, Axis), Start, End);

	return lcLength(Position + Axis * Along - Point);
}

// The middle of where two shapes on the same axis overlap, both at their frames (LeoCAD
// coordinates, shapes along +Z).
lcVector3 lcConnectivity::GetOverlapCenter(const lcConnectionCylinder& Cylinder, const lcMatrix44& Frame, const lcConnectionCylinder& Other, const lcMatrix44& OtherFrame)
{
	const lcVector3 Position = Frame.GetTranslation();
	const lcVector3 Axis = lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), Frame));
	const auto [Start, End] = Cylinder.GetExtent();
	const auto [OtherStart, OtherEnd] = Other.GetExtent();
	const float OtherAlong = lcDot(OtherFrame.GetTranslation() - Position, Axis);
	const float Low = std::max(Start, OtherAlong + OtherStart);
	const float High = std::min(End, OtherAlong + OtherEnd);

	return Position + Axis * ((Low + High) / 2.0f);
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

std::optional<lcSnapMatch> lcConnectivity::FindMatch(const std::vector<lcConnectionCylinder>& MovingCylinders, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const std::vector<lcConnectionCylinder>& TargetCylinders, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection, const lcBoundingBox& MovingBody, const std::vector<std::pair<lcSnapBody, lcMatrix44>>& Obstacles)
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
	std::optional<lcSnapMatch> Best;
	bool BestInterferes = false;

	// Small round shapes (bars, and the pins under 1x plates and bricks, which fit hollow studs)
	// are only used when nothing bigger fits: a brick held over a stud is meant to go on with an
	// anti stud.
	for (int Pass = 0; Pass < 2 && (!Best || BestInterferes); Pass++)
	{
		std::optional<lcSnapMatch> PassBest;
		bool PassBestInterferes = false;
		float BestScore = FLT_MAX;
		float BestTrace = -FLT_MAX;

		for (const lcTargetPoint& Target : Targets)
		{
			if (Target.Distance > NearestDistance + 8.0f)
				continue;

			const float MaxRadius = Target.Cylinder->GetRadius();

			if ((MaxRadius < 5.0f) != (Pass == 1))
				continue;

			const lcVector3 TargetAxis = lcNormalize(lcMul30(ShapeAxis, Target.Frame));

			for (const lcConnectionCylinder& Moving : MovingCylinders)
			{
				if (!AreCompatible(Moving, *Target.Cylinder))
					continue;

				const bool MovingIsMale = Moving.Gender == lcConnectionGender::Male;
				const std::optional<std::pair<float, float>> InsertRange = MovingIsMale ? GetInsertRange(Moving, *Target.Cylinder) : GetInsertRange(*Target.Cylinder, Moving);

				if (!InsertRange)
					continue;

				// How far the moving piece may be shifted along the axis from the aligned positions.
				const float ShiftLow = MovingIsMale ? InsertRange->first : -InsertRange->second;
				const float ShiftHigh = MovingIsMale ? InsertRange->second : -InsertRange->first;
				const bool Slide = Moving.Slide || Target.Cylinder->Slide;

				const lcMatrix44 MovingFrame = lcMatrix44LDrawToLeoCAD(Moving.Transform);
				const lcMatrix44 MovingInverse = lcMatrix44AffineInverse(MovingFrame);
				const float Score = Target.Distance + GetDistanceToShape(Moving, MovingFrame, GrabPoint);

				if (Score > BestScore + 0.5f)
					continue;

				// Quarter turns around the connection axis. A round shape in a round hole may also
				// keep any angle, but only if the piece already lines up with the connection axis.
				std::vector<float> Angles = { 0.0f, LC_PI / 2.0f, LC_PI, LC_PI * 1.5f };
				const bool Round = IsRound(Moving) && IsRound(*Target.Cylinder);

				if (Round)
				{
					const lcMatrix33 Turn = lcMul(lcMul(lcMatrix33(MovingFrame), ReferenceRotation), lcMatrix33AffineInverse(lcMatrix33(Target.Frame)));

					if (lcDot(Turn.r[2], lcVector3(0.0f, 0.0f, 1.0f)) > 0.999f)
						Angles.push_back(atan2f(Turn.r[0][1], Turn.r[0][0]));

					// And the angles that turn another of the moving piece's points onto one of the
					// target's, so the piece settles where its other points line up too.
					const lcMatrix44 TargetInverse = lcMatrix44AffineInverse(Target.Frame);

					for (const lcConnectionCylinder& OtherMoving : MovingCylinders)
					{
						const lcVector3 From = lcMul31(lcMatrix44LDrawToLeoCAD(OtherMoving.Transform).GetTranslation(), MovingInverse);
						const float FromRadius = lcLength(lcVector3(From[0], From[1], 0.0f));

						if (&OtherMoving == &Moving || FromRadius < 1.0f)
							continue;

						for (const lcConnectionCylinder& OtherTarget : TargetCylinders)
						{
							if (&OtherTarget == Target.Cylinder || !AreCompatible(OtherMoving, OtherTarget))
								continue;

							const lcVector3 To = lcMul31(lcMul(lcMatrix44LDrawToLeoCAD(OtherTarget.Transform), TargetTransform).GetTranslation(), TargetInverse);

							if (fabsf(lcLength(lcVector3(To[0], To[1], 0.0f)) - FromRadius) < 0.5f)
								Angles.push_back(atan2f(To[1], To[0]) - atan2f(From[1], From[0]));
						}
					}
				}

				struct lcAngleCandidate
				{
					lcMatrix44 Placement;
					bool Interferes;
					size_t Connections;
				};

				std::vector<lcAngleCandidate> Candidates;

				for (float Angle : Angles)
				{
					lcMatrix44 Placement = lcMul(lcMul(MovingInverse, lcMatrix44RotationZ(Angle)), Target.Frame);

					if (!IsRigid(Placement))
						continue;

					// Along the axis, follow the cursor where the shapes can slide; otherwise take the
					// end of the allowed range nearer to it.
					const float CursorShift = lcDot(HitPoint - lcMul31(GrabPoint, Placement), TargetAxis);
					float Shift;

					if (Slide)
						Shift = lcClamp(CursorShift, ShiftLow, ShiftHigh);
					else
						Shift = fabsf(CursorShift - ShiftLow) <= fabsf(CursorShift - ShiftHigh) ? ShiftLow : ShiftHigh;

					Placement.SetTranslation(Placement.GetTranslation() + TargetAxis * Shift);

					// A little overlap is allowed, as pieces that touch share their faces.
					const bool Interferes = std::any_of(Obstacles.begin(), Obstacles.end(), [&MovingBody, &Placement](const std::pair<lcSnapBody, lcMatrix44>& Obstacle)
					{
						return DoesBodyBlock(MovingBody, Placement, Obstacle.first, Obstacle.second, 0.5f);
					});
					const size_t Connections = Round ? FindConnections(MovingCylinders, Placement, { { &TargetCylinders, TargetTransform } }).size() : 0;

					Candidates.push_back({ Placement, Interferes, Connections });
				}

				// A round connection leaves the angle free: use the angles where the most of the
				// moving piece's points connect, preferring those that run into nothing.
				if (Round && !Candidates.empty())
				{
					const auto Better = [](const lcAngleCandidate& a, const lcAngleCandidate& b)
					{
						return a.Interferes != b.Interferes ? !a.Interferes : a.Connections > b.Connections;
					};
					const lcAngleCandidate Preferred = *std::min_element(Candidates.begin(), Candidates.end(), Better);

					Candidates.erase(std::remove_if(Candidates.begin(), Candidates.end(), [&Preferred, &Better](const lcAngleCandidate& Candidate) { return Better(Preferred, Candidate); }), Candidates.end());
				}

				for (const lcAngleCandidate& Candidate : Candidates)
				{
					const lcMatrix44& Placement = Candidate.Placement;

					// Running into another piece counts for more than any distance, so it only wins
					// when every placement does.
					const float PlacementScore = Candidate.Interferes ? Score + 1000.0f : Score;

					if (PlacementScore > BestScore + 0.5f)
						continue;

					const lcMatrix33 Rotation(Placement);
					float Trace = 0.0f;

					for (int Row = 0; Row < 3; Row++)
						Trace += lcDot(Rotation.r[Row], ReferenceRotation.r[Row]);

					// Clearly better pairs win; between equally good ones, the closest rotation.
					if (PlacementScore < BestScore - 0.5f || Trace > BestTrace)
					{
						BestTrace = PlacementScore < BestScore - 0.5f ? Trace : std::max(BestTrace, Trace);
						BestScore = std::min(BestScore, PlacementScore);
						PassBest = lcSnapMatch{ Placement, GetOverlapCenter(Moving, lcMul(MovingFrame, Placement), *Target.Cylinder, Target.Frame), TargetAxis, MaxRadius, nullptr, TargetTransform, {} };
						PassBestInterferes = Candidate.Interferes;
					}
				}
			}
		}

		// A placement with small shapes that runs into nothing beats one with bigger shapes that does.
		if (PassBest && (!Best || (BestInterferes && !PassBestInterferes)))
		{
			Best = PassBest;
			BestInterferes = PassBestInterferes;
		}
	}

	if (Best)
		Best->Connections = FindConnections(MovingCylinders, Best->Transform, { { &TargetCylinders, TargetTransform } });

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
		std::optional<lcVector3> Center;

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
				const std::optional<std::pair<float, float>> InsertRange = IsMale ? GetInsertRange(Cylinder, OtherCylinder) : GetInsertRange(OtherCylinder, Cylinder);
				const float MaleOffset = IsMale ? Along : -Along;

				if (InsertRange && MaleOffset > InsertRange->first - 0.5f && MaleOffset < InsertRange->second + 0.5f)
				{
					Center = GetOverlapCenter(Cylinder, Frame, OtherCylinder, OtherFrame);
					break;
				}
			}

			if (Center)
				break;
		}

		if (Center)
			Connections.push_back({ *Center, Axis, Cylinder.GetRadius() });
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

lcSnapBody lcConnectivity::GetBody(const lcBoundingBox& BoundingBox, const std::vector<lcConnectionCylinder>& Cylinders)
{
	lcSnapBody Body = { BoundingBox, {} };

	// Studs stick out of the body and go into other pieces; cut a box face back to the stud bases
	// when stud size shapes end on it. Smaller ones, like the pins under 1x plates, are inside.
	for (const lcConnectionCylinder& Cylinder : Cylinders)
	{
		if (Cylinder.Gender != lcConnectionGender::Male || Cylinder.GetRadius() < 5.0f)
			continue;

		const lcMatrix44 Frame = lcMatrix44LDrawToLeoCAD(Cylinder.Transform);
		const lcVector3 Axis = lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), Frame));
		const auto [Start, End] = Cylinder.GetExtent();
		const lcVector3 Base = Frame.GetTranslation() + Axis * Start;
		const lcVector3 Tip = Frame.GetTranslation() + Axis * End;

		for (int Coordinate = 0; Coordinate < 3; Coordinate++)
		{
			if (Axis[Coordinate] > 0.99f && fabsf(Tip[Coordinate] - BoundingBox.Max[Coordinate]) < 0.5f)
				Body.Box.Max[Coordinate] = std::max(std::min(Body.Box.Max[Coordinate], Base[Coordinate]), Body.Box.Min[Coordinate]);
			else if (Axis[Coordinate] < -0.99f && fabsf(Tip[Coordinate] - BoundingBox.Min[Coordinate]) < 0.5f)
				Body.Box.Min[Coordinate] = std::min(std::max(Body.Box.Min[Coordinate], Base[Coordinate]), Body.Box.Max[Coordinate]);
		}
	}

	// Above a stud inside the box, one stud wide, up to the box's face.
	for (const lcConnectionCylinder& Cylinder : Cylinders)
	{
		if (Cylinder.Gender != lcConnectionGender::Male || Cylinder.GetRadius() < 5.0f)
			continue;

		const lcMatrix44 Frame = lcMatrix44LDrawToLeoCAD(Cylinder.Transform);
		const lcVector3 Axis = lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), Frame));
		const lcVector3 Base = Frame.GetTranslation() + Axis * Cylinder.GetExtent().first;

		for (int Coordinate = 0; Coordinate < 3; Coordinate++)
		{
			if (fabsf(Axis[Coordinate]) < 0.99f)
				continue;

			lcBoundingBox Column = { Base - lcVector3(10.0f, 10.0f, 10.0f), Base + lcVector3(10.0f, 10.0f, 10.0f) };

			if (Axis[Coordinate] > 0.0f)
			{
				Column.Min[Coordinate] = Base[Coordinate];
				Column.Max[Coordinate] = Body.Box.Max[Coordinate];
			}
			else
			{
				Column.Min[Coordinate] = Body.Box.Min[Coordinate];
				Column.Max[Coordinate] = Base[Coordinate];
			}

			if (Column.Max[Coordinate] - Column.Min[Coordinate] > 0.5f)
				Body.Free.push_back(Column);
		}
	}

	return Body;
}

// Separating axis test of two boxes, each shrunk by Tolerance on every side.
bool lcConnectivity::DoBoxesOverlap(const lcBoundingBox& a, const lcMatrix44& TransformA, const lcBoundingBox& b, const lcMatrix44& TransformB, float Tolerance)
{
	const lcVector3 Shrink(Tolerance, Tolerance, Tolerance);
	const lcVector3 HalfA = (a.Max - a.Min) / 2.0f - Shrink;
	const lcVector3 HalfB = (b.Max - b.Min) / 2.0f - Shrink;

	for (int Axis = 0; Axis < 3; Axis++)
		if (HalfA[Axis] <= 0.0f || HalfB[Axis] <= 0.0f)
			return false;

	const lcVector3 Offset = lcMul31((b.Min + b.Max) / 2.0f, TransformB) - lcMul31((a.Min + a.Max) / 2.0f, TransformA);
	lcVector3 AxesA[3], AxesB[3];

	for (int Axis = 0; Axis < 3; Axis++)
	{
		AxesA[Axis] = lcNormalize(lcVector3(TransformA.r[Axis]));
		AxesB[Axis] = lcNormalize(lcVector3(TransformB.r[Axis]));
	}

	std::vector<lcVector3> Separators(AxesA, AxesA + 3);
	Separators.insert(Separators.end(), AxesB, AxesB + 3);

	for (const lcVector3& AxisA : AxesA)
	{
		for (const lcVector3& AxisB : AxesB)
		{
			const lcVector3 Cross = lcCross(AxisA, AxisB);

			if (lcLength(Cross) > 0.0001f)
				Separators.push_back(lcNormalize(Cross));
		}
	}

	for (const lcVector3& Separator : Separators)
	{
		float RadiusA = 0.0f;
		float RadiusB = 0.0f;

		for (int Axis = 0; Axis < 3; Axis++)
		{
			RadiusA += HalfA[Axis] * fabsf(lcDot(AxesA[Axis], Separator));
			RadiusB += HalfB[Axis] * fabsf(lcDot(AxesB[Axis], Separator));
		}

		if (fabsf(lcDot(Offset, Separator)) >= RadiusA + RadiusB)
			return false;
	}

	return true;
}

bool lcConnectivity::DoesBodyBlock(const lcBoundingBox& Box, const lcMatrix44& BoxTransform, const lcSnapBody& Body, const lcMatrix44& BodyTransform, float Tolerance)
{
	if (!DoBoxesOverlap(Box, BoxTransform, Body.Box, BodyTransform, Tolerance))
		return false;

	if (Body.Free.empty())
		return true;

	// The free space can only be checked exactly when the box lines up with the body's axes.
	const lcMatrix44 ToBody = lcMul(BoxTransform, lcMatrix44AffineInverse(BodyTransform));

	for (int Row = 0; Row < 3; Row++)
	{
		const lcVector3 Direction(ToBody.r[Row]);

		if (std::max({ fabsf(Direction[0]), fabsf(Direction[1]), fabsf(Direction[2]) }) < 0.999f)
			return true;
	}

	// Where the box is inside the body, in the body's coordinates.
	lcVector3 Corners[8];
	lcGetBoxCorners(Box, Corners);
	lcBoundingBox Inside = { Body.Box.Min + lcVector3(Tolerance, Tolerance, Tolerance), Body.Box.Max - lcVector3(Tolerance, Tolerance, Tolerance) };
	lcVector3 Min(FLT_MAX, FLT_MAX, FLT_MAX), Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (const lcVector3& Corner : Corners)
	{
		const lcVector3 Point = lcMul31(Corner, ToBody);
		Min = lcMin(Min, Point);
		Max = lcMax(Max, Point);
	}

	for (int Coordinate = 0; Coordinate < 3; Coordinate++)
	{
		Inside.Min[Coordinate] = std::max(Inside.Min[Coordinate], Min[Coordinate] + Tolerance);
		Inside.Max[Coordinate] = std::min(Inside.Max[Coordinate], Max[Coordinate] - Tolerance);

		if (Inside.Min[Coordinate] >= Inside.Max[Coordinate])
			return false;
	}

	// Split that region at the free boxes' faces; every cell must be inside one of them.
	std::vector<float> Splits[3];

	for (int Coordinate = 0; Coordinate < 3; Coordinate++)
	{
		Splits[Coordinate] = { Inside.Min[Coordinate], Inside.Max[Coordinate] };

		for (const lcBoundingBox& Free : Body.Free)
			for (float Face : { Free.Min[Coordinate], Free.Max[Coordinate] })
				if (Face > Inside.Min[Coordinate] && Face < Inside.Max[Coordinate])
					Splits[Coordinate].push_back(Face);

		std::sort(Splits[Coordinate].begin(), Splits[Coordinate].end());
	}

	for (size_t x = 0; x + 1 < Splits[0].size(); x++)
	{
		for (size_t y = 0; y + 1 < Splits[1].size(); y++)
		{
			for (size_t z = 0; z + 1 < Splits[2].size(); z++)
			{
				const lcVector3 Center((Splits[0][x] + Splits[0][x + 1]) / 2.0f, (Splits[1][y] + Splits[1][y + 1]) / 2.0f, (Splits[2][z] + Splits[2][z + 1]) / 2.0f);
				const bool InFree = std::any_of(Body.Free.begin(), Body.Free.end(), [&Center](const lcBoundingBox& Free)
				{
					return Center[0] >= Free.Min[0] && Center[0] <= Free.Max[0] && Center[1] >= Free.Min[1] && Center[1] <= Free.Max[1] && Center[2] >= Free.Min[2] && Center[2] <= Free.Max[2];
				});

				if (!InFree)
					return true;
			}
		}
	}

	return false;
}
