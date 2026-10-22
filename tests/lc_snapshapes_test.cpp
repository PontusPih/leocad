#include "lc_global.h"
#include "lc_tests.h"
#include "lc_shadowparser.h"

// Made-up parts, described with SNAP_CYL lines like those of the LDCad shadow library, in LDraw
// coordinates: studs point up along -Y.
static std::vector<lcSnapCylinder> MakePart(std::initializer_list<const char*> Lines)
{
	std::vector<lcSnapCylinder> Cylinders;

	for (const char* Line : Lines)
	{
		const std::vector<lcSnapCylinder> Parsed = lcShadowParser::ParseCylinder(QLatin1String(Line), "test");
		Cylinders.insert(Cylinders.end(), Parsed.begin(), Parsed.end());
	}

	return Cylinders;
}

static std::vector<lcSnapCylinder> MakePlate(int Width, int Length)
{
	const std::string Grid = QString("C %1 C %2 20 20").arg(Length).arg(Width).toStdString();
	const std::string Studs = "0 !LDCAD SNAP_CYL [gender=M] [caps=one] [secs=R 6 4] [grid=" + Grid + "]";
	const std::string AntiStuds = "0 !LDCAD SNAP_CYL [gender=F] [caps=one] [secs=S 6 4] [pos=0 8 0] [grid=" + Grid + "]";

	return MakePart({ Studs.c_str(), AntiStuds.c_str() });
}

static std::vector<lcSnapCylinder> MakeBrick1x2()
{
	return MakePart({
		"0 !LDCAD SNAP_CYL [gender=M] [caps=one] [secs=R 6 4] [grid=C 2 1 20 0]",
		"0 !LDCAD SNAP_CYL [gender=F] [caps=one] [secs=S 6 20] [pos=0 24 0] [grid=C 2 1 20 0]",
		"0 !LDCAD SNAP_CYL [gender=M] [caps=one] [secs=R 4 20] [pos=0 4 0] [ori=-1 0 0 0 -1 0 0 0 1]",
	});
}

static std::vector<lcSnapCylinder> MakeBar3L()
{
	return MakePart({ "0 !LDCAD SNAP_CYL [gender=M] [caps=none] [secs=R 4 60] [center=true] [slide=true] [pos=0 30 0]" });
}

static const lcSnapCylinder* FindShape(const std::vector<lcSnapCylinder>& Cylinders, lcSnapGender Gender, float Radius)
{
	for (const lcSnapCylinder& Cylinder : Cylinders)
		if (Cylinder.Gender == Gender && Cylinder.GetRadius() == Radius)
			return &Cylinder;

	return nullptr;
}

static lcVector3 GetShapeAxis(const lcMatrix44& Frame)
{
	return lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), Frame));
}

void lcTests::SnapParsesLines()
{
	const std::pair<const char*, size_t> Lines[] =
	{
		{ "0 !LDCAD SNAP_CYL [gender=F] [caps=one] [secs=S 6 20] [pos=0 24 0] [grid=C 2 1 20 0]", 2 },
		{ "0 !LDCAD SNAP_CYL [gender=M] [secs=R 6 4] [pos=0 x 0]", 0 },
		{ "0 !LDCAD SNAP_CYL [gender=M] [secs=R 6] [pos=0 0 0]", 0 },
		{ "0 !LDCAD SNAP_CYL [gender=M] [secs=R 6 4] [ori=1 0 0]", 0 },
		{ "0 !LDCAD SNAP_CYL [gender=M] [secs=R 6 4] [grid=C 2 1 20]", 0 },
		{ "0 !LDCAD SNAP_CYL [gender=M] [secs=R 6 4] [grid=2 99999 20 20]", 0 },
		{ "0 !LDCAD SNAP_CYL [gender=M] [secs=R 6 4] [pos=0 inf 0]", 0 },
	};

	for (const auto& [Line, Expected] : Lines)
		QVERIFY2(lcShadowParser::ParseCylinder(QLatin1String(Line), "test").size() == Expected, Line);

	const std::vector<lcSnapCylinder> Bar = MakeBar3L();

	QCOMPARE(Bar.size(), size_t(1));
	QVERIFY(Bar[0].Center && Bar[0].Slide && !Bar[0].ClosedStart && !Bar[0].ClosedEnd);
	QCOMPARE(Bar[0].GetExtent(), std::make_pair(-30.0f, 30.0f));

	// caps=one closes the base of a male shape and the far end of a female one.
	const std::vector<lcSnapCylinder> Plate = MakePlate(1, 1);
	const lcSnapCylinder* Stud = FindShape(Plate, lcSnapGender::Male, 6.0f);
	const lcSnapCylinder* AntiStud = FindShape(Plate, lcSnapGender::Female, 6.0f);

	QVERIFY(Stud && AntiStud);
	QVERIFY(Stud->ClosedStart && !Stud->ClosedEnd);
	QVERIFY(!AntiStud->ClosedStart && AntiStud->ClosedEnd);
	QVERIFY(lcSnapShapes::AreCompatible(*Stud, *AntiStud));
}

void lcTests::SnapScalesAndMirrorsSubFileShapes()
{
	// A shape used from a sub-file stretched 5 times along Y keeps the stretch only with scale=YOnly.
	const lcMatrix44 Stretched = lcShadowParser::LDrawMatrix(std::array<float, 9>{ 1, 0, 0, 0, 5, 0, 0, 0, 1 }.data(), lcVector3(0.0f, 0.0f, 0.0f));
	lcSnapCylinder Pin = MakePart({ "0 !LDCAD SNAP_CYL [gender=M] [caps=one] [secs=R 4 4] [scale=YOnly]" }).front();
	lcSnapCylinder Stud = MakePart({ "0 !LDCAD SNAP_CYL [gender=M] [caps=one] [secs=R 6 4]" }).front();

	Pin.Transform = lcMul(Pin.Transform, Stretched);
	Stud.Transform = lcMul(Stud.Transform, Stretched);

	QVERIFY(lcShadowParser::NormalizeFrame(Pin));
	QCOMPARE(Pin.GetLength(), 20.0f);
	QVERIFY(lcSnapShapes::IsRigid(Pin.Transform));
	QVERIFY(!lcShadowParser::NormalizeFrame(Stud));

	// A mirrored shape is corrected by flipping a radius axis (mirror=cor), or dropped (mirror=none).
	lcSnapCylinder Mirrored = MakePart({ "0 !LDCAD SNAP_CYL [gender=M] [caps=one] [secs=R 6 4]" }).front();
	lcSnapCylinder NotMirrored = MakePart({ "0 !LDCAD SNAP_CYL [gender=M] [caps=one] [secs=R 6 4] [mirror=none]" }).front();
	const lcMatrix44 Mirror = lcShadowParser::LDrawMatrix(std::array<float, 9>{ -1, 0, 0, 0, 1, 0, 0, 0, 1 }.data(), lcVector3(10.0f, 0.0f, 0.0f));

	Mirrored.Transform = lcMul(Mirrored.Transform, Mirror);
	NotMirrored.Transform = lcMul(NotMirrored.Transform, Mirror);

	QVERIFY(lcShadowParser::NormalizeFrame(Mirrored));
	QVERIFY(lcSnapShapes::IsRigid(Mirrored.Transform));
	QCOMPARE(Mirrored.GetPosition(), lcVector3(10.0f, 0.0f, 0.0f));
	QVERIFY(!lcShadowParser::NormalizeFrame(NotMirrored));
}

void lcTests::SnapFitsShapesAlongTheirAxis()
{
	const std::vector<lcSnapCylinder> Plate = MakePlate(1, 1);
	const lcSnapCylinder* Stud = FindShape(Plate, lcSnapGender::Male, 6.0f);
	const lcSnapCylinder* AntiStud = FindShape(Plate, lcSnapGender::Female, 6.0f);

	// A stud goes all the way into an anti stud and no further.
	QCOMPARE(lcSnapShapes::GetInsertRange(*Stud, *AntiStud), std::make_optional(std::make_pair(0.0f, 0.0f)));

	// A 3L bar through a hole open at both ends keeps the 16 long hole covered.
	const lcSnapCylinder Bar = MakeBar3L().front();
	const lcSnapCylinder OpenHole = MakePart({ "0 !LDCAD SNAP_CYL [gender=F] [caps=none] [secs=R 4 16] [center=true]" }).front();

	QCOMPARE(lcSnapShapes::GetInsertRange(Bar, OpenHole), std::make_optional(std::make_pair(-22.0f, 22.0f)));

	// Into a 10 deep tube closed at the far end, the bar goes to the end.
	const lcSnapCylinder Tube = MakePart({ "0 !LDCAD SNAP_CYL [gender=F] [caps=one] [secs=R 4 10] [slide=true]" }).front();

	QCOMPARE(lcSnapShapes::GetInsertRange(Bar, Tube), std::make_optional(std::make_pair(-20.0f, -20.0f)));
}

void lcTests::SnapPutsBrickOnEveryStud()
{
	// Dragged over every stud of a 6x8 plate, held above its left end with the cursor a little off
	// the stud, a 1x2 brick's left anti stud lands on the stud under the cursor.
	const std::vector<lcSnapCylinder> Plate = MakePlate(6, 8);
	const std::vector<lcSnapCylinder> Brick = MakeBrick1x2();
	const lcVector3 ViewDirection = lcNormalize(lcVector3(0.3f, 0.5f, -0.8f));
	const lcVector3 GrabPoint = lcVector3LDrawToLeoCAD(lcVector3(-10.0f, 0.0f, 0.0f));
	const lcVector3 LeftAntiStud = lcVector3LDrawToLeoCAD(lcVector3(-10.0f, 24.0f, 0.0f));
	const lcMatrix44 BrickStart = lcMatrix44Translation(lcVector3(0.0f, 0.0f, 100.0f));
	int Studs = 0;

	for (const lcSnapCylinder& Cylinder : Plate)
	{
		if (Cylinder.Gender != lcSnapGender::Male)
			continue;

		const lcVector3 Stud = lcMatrix44LDrawToLeoCAD(Cylinder.Transform).GetTranslation();
		const std::optional<lcSnapMatch> Match = lcSnapShapes::FindMatch(Brick, BrickStart, GrabPoint, Plate, lcMatrix44Identity(), Stud + lcVector3(4.0f, -3.0f, 0.0f), ViewDirection);

		QVERIFY2(Match, qPrintable(QString("stud at %1 %2").arg(Stud[0]).arg(Stud[1])));
		QVERIFY2(lcLength(lcMul31(LeftAntiStud, Match->Transform) - Stud) < 0.01f, qPrintable(QString("stud at %1 %2").arg(Stud[0]).arg(Stud[1])));
		Studs++;
	}

	QCOMPARE(Studs, 48);
}

void lcTests::SnapPrefersAntiStudsOverPins()
{
	// Held at the top centre, where its centre pin is closer, an upright 1x2 brick still goes onto a
	// side stud with an anti stud rather than with its pin into the hollow stud, at a quarter turn.
	const std::vector<lcSnapCylinder> SideStud = MakePart({
		"0 !LDCAD SNAP_CYL [gender=M] [caps=one] [secs=R 6 4] [pos=20 12 0] [ori=0 -1 0 1 0 0 0 0 1]",
		"0 !LDCAD SNAP_CYL [gender=F] [caps=one] [secs=R 4 4] [pos=24 12 0] [ori=0 1 0 -1 0 0 0 0 1]",
	});
	const std::vector<lcSnapCylinder> Brick = MakeBrick1x2();
	const lcMatrix44 StudFrame = lcMatrix44LDrawToLeoCAD(SideStud.front().Transform);
	const lcVector3 Stud = StudFrame.GetTranslation();
	const std::optional<lcSnapMatch> Match = lcSnapShapes::FindMatch(Brick, lcMatrix44Identity(), lcVector3(0.0f, 0.0f, 0.0f), SideStud, lcMatrix44Identity(), Stud, -GetShapeAxis(StudFrame));

	QVERIFY(Match);
	QCOMPARE(Match->Radius, 6.0f);

	for (int Row = 0; Row < 3; Row++)
		for (int Column = 0; Column < 3; Column++)
			QVERIFY(fabsf(Match->Transform.r[Row][Column] - roundf(Match->Transform.r[Row][Column])) < 0.001f);

	bool OnAntiStud = false;

	for (const lcSnapCylinder& Cylinder : Brick)
		if (Cylinder.Gender == lcSnapGender::Female && lcLength(lcMul31(lcMatrix44LDrawToLeoCAD(Cylinder.Transform).GetTranslation(), Match->Transform) - Stud) < 0.01f)
			OnAntiStud = true;

	QVERIFY(OnAntiStud);
}

void lcTests::SnapFindsAllConnections()
{
	// Snapped onto the middle of a plate, a 1x2 brick connects by both its anti studs.
	const std::vector<lcSnapCylinder> Plate = MakePlate(6, 8);
	const std::vector<lcSnapCylinder> Brick = MakeBrick1x2();
	const std::optional<lcSnapMatch> Match = lcSnapShapes::FindMatch(Brick, lcMatrix44Translation(lcVector3(0.0f, 0.0f, 100.0f)), lcVector3LDrawToLeoCAD(lcVector3(-10.0f, 0.0f, 0.0f)), Plate, lcMatrix44Identity(), lcVector3(-10.0f, 10.0f, 0.0f), lcNormalize(lcVector3(0.3f, 0.5f, -0.8f)));

	QVERIFY(Match);

	std::vector<lcVector3> Places;

	for (const lcSnapConnection& Connection : Match->Connections)
		if (std::none_of(Places.begin(), Places.end(), [&Connection](const lcVector3& Place) { return lcLength(Place - Connection.Position) < 0.5f; }))
			Places.push_back(Connection.Position);

	QCOMPARE(Places.size(), size_t(2));
}

void lcTests::SnapPutsBarsIntoHoles()
{
	// Held at different points along it, with the cursor at the hole, a 3L bar fills an open clip
	// and slides to put the held point at the cursor; into a tube it goes to the closed end. Either
	// way the connection is in the middle of the hole, which the bar covers.
	const std::vector<lcSnapCylinder> Bar = MakeBar3L();
	const std::vector<lcSnapCylinder> Holes[] =
	{
		MakePart({ "0 !LDCAD SNAP_CYL [gender=F] [caps=none] [secs=R 4 16] [center=true] [pos=0 2 -20] [ori=0 1 0 -1 0 0 0 0 1]" }),
		MakePart({ "0 !LDCAD SNAP_CYL [gender=F] [caps=one] [secs=R 4 10] [slide=true] [pos=0 30 0]" }),
	};

	for (const std::vector<lcSnapCylinder>& Hole : Holes)
	{
		const lcMatrix44 HoleFrame = lcMatrix44LDrawToLeoCAD(Hole.front().Transform);
		const lcVector3 Axis = GetShapeAxis(HoleFrame);
		const lcVector3 HitPoint = HoleFrame.GetTranslation();
		const auto [HoleStart, HoleEnd] = Hole.front().GetExtent();
		const lcVector3 HoleMiddle = HitPoint + Axis * ((HoleStart + HoleEnd) / 2.0f);
		const bool Closed = Hole.front().ClosedEnd;

		for (float Held : { 10.0f, 30.0f, 50.0f })
		{
			const lcVector3 GrabPoint = lcVector3LDrawToLeoCAD(lcVector3(0.0f, Held, 0.0f));
			const std::optional<lcSnapMatch> Match = lcSnapShapes::FindMatch(Bar, lcMatrix44Identity(), GrabPoint, Hole, lcMatrix44Identity(), HitPoint, Axis);
			const QByteArray Case = QString("%1 hole, held at %2").arg(Closed ? "closed" : "open").arg(Held).toLatin1();

			QVERIFY2(Match, Case);

			const lcMatrix44 BarFrame = lcMul(lcMatrix44LDrawToLeoCAD(Bar.front().Transform), Match->Transform);
			const lcVector3 Offset = BarFrame.GetTranslation() - HitPoint;
			const float Along = lcDot(Offset, Axis);

			QVERIFY2(fabsf(lcDot(GetShapeAxis(BarFrame), Axis)) > 0.999f, Case);
			QVERIFY2(lcLength(Offset - Axis * Along) < 0.01f, Case);

			if (Closed)
				QVERIFY2(fabsf(Along - (HoleEnd - 30.0f)) < 0.01f, Case);
			else
				QVERIFY2(fabsf(lcDot(lcMul31(GrabPoint, Match->Transform) - HitPoint, Axis)) < 0.01f, Case);

			QVERIFY2(lcLength(Match->MovingPosition - HoleMiddle) < 0.01f, Case);
			QVERIFY2(!Match->Connections.empty() && lcLength(Match->Connections.front().Position - HoleMiddle) < 0.01f, Case);
		}
	}
}

void lcTests::SnapPutsClipOntoBarFromTheSide()
{
	// A plate with a clip, dragged onto a bar seen from the side, goes around the bar where the
	// cursor is.
	const std::vector<lcSnapCylinder> Bar = MakeBar3L();
	const std::vector<lcSnapCylinder> ClipPlate = MakePart({ "0 !LDCAD SNAP_CYL [gender=F] [caps=none] [secs=R 4 16] [center=true] [pos=0 2 -20] [ori=0 1 0 -1 0 0 0 0 1]" });
	const lcMatrix44 BarFrame = lcMatrix44LDrawToLeoCAD(Bar.front().Transform);
	const lcVector3 Axis = GetShapeAxis(BarFrame);
	const lcVector3 ViewDirection = lcNormalize(lcCross(Axis, lcVector3(1.0f, 0.0f, 0.0f)) + lcCross(Axis, lcVector3(0.0f, 1.0f, 0.0f)));
	const lcVector3 GrabPoint = lcVector3LDrawToLeoCAD(lcVector3(0.0f, 2.0f, -20.0f));

	for (float Along : { -15.0f, 0.0f, 15.0f })
	{
		const lcVector3 HitPoint = BarFrame.GetTranslation() + Axis * Along;
		const std::optional<lcSnapMatch> Match = lcSnapShapes::FindMatch(ClipPlate, lcMatrix44Translation(lcVector3(0.0f, 0.0f, 100.0f)), GrabPoint, Bar, lcMatrix44Identity(), HitPoint, ViewDirection);

		QVERIFY(Match);
		QVERIFY(lcLength(lcMul31(GrabPoint, Match->Transform) - HitPoint) < 0.01f);
	}
}

void lcTests::SnapTurnsAroundConnection()
{
	// Turning a 1x4 plate sitting on a 6x8 plate keeps it attached by as many studs as before, and
	// four turns bring it back.
	const std::vector<lcSnapCylinder> Base = MakePlate(6, 8);
	const std::vector<lcSnapCylinder> Plate = MakePlate(1, 4);
	const std::vector<std::pair<const std::vector<lcSnapCylinder>*, lcMatrix44>> Others = { { &Base, lcMatrix44Identity() } };
	const lcMatrix44 Start = lcMatrix44Translation(lcVector3(0.0f, 10.0f, 8.0f));
	const size_t StartConnections = lcSnapShapes::FindConnections(Plate, Start, Others).size();
	lcMatrix44 Current = Start;

	QCOMPARE(StartConnections, size_t(4));

	for (int Turn = 0; Turn < 4; Turn++)
	{
		const std::optional<std::pair<lcVector3, lcMatrix33>> Pivot = lcSnapShapes::GetConnectionTurn(Plate, Current, lcVector3(0.0f, 0.0f, 0.0f), Others, 1, std::nullopt);

		QVERIFY(Pivot);

		const auto& [PivotPosition, Rotation] = *Pivot;
		Current = lcMatrix44(lcMul(lcMatrix33(Current), Rotation), lcMul(Current.GetTranslation() - PivotPosition, Rotation) + PivotPosition);

		QVERIFY(lcSnapShapes::FindConnections(Plate, Current, Others).size() > 0);
	}

	for (int Row = 0; Row < 4; Row++)
		QVERIFY(lcLength(lcVector3(Current.r[Row]) - lcVector3(Start.r[Row])) < 0.001f);
}

void lcTests::SnapTurnsAroundConnectionAtPointer()
{
	// With the pointer over one of the studs a 1x6 plate sits on, the turn pivots there.
	const std::vector<lcSnapCylinder> Base = MakePlate(6, 8);
	const std::vector<lcSnapCylinder> Plate = MakePlate(1, 6);
	const std::vector<std::pair<const std::vector<lcSnapCylinder>*, lcMatrix44>> Others = { { &Base, lcMatrix44Identity() } };
	const lcMatrix44 Start = lcMatrix44Translation(lcVector3(0.0f, 10.0f, 8.0f));

	for (int Stud = 0; Stud < 6; Stud++)
	{
		const lcVector3 Pointer((Stud - 2.5f) * 20.0f, 10.0f, 8.0f);
		const std::array<lcVector3, 2> PointerRay = {{ Pointer + lcVector3(0.0f, 0.0f, 1000.0f), Pointer - lcVector3(0.0f, 0.0f, 1000.0f) }};
		const std::optional<std::pair<lcVector3, lcMatrix33>> Turn = lcSnapShapes::GetConnectionTurn(Plate, Start, lcVector3(0.0f, 0.0f, 0.0f), Others, 1, PointerRay);

		QVERIFY(Turn);
		QVERIFY(lcLength(lcVector3(Turn->first[0], Turn->first[1], 0.0f) - lcVector3(Pointer[0], Pointer[1], 0.0f)) < 0.01f);
	}
}
