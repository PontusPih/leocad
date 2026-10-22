#include "lc_global.h"
#include "lc_shadowlibrary.h"
#include "lc_shadowparser.h"
#include "lc_library.h"
#include "lc_application.h"
#include "lc_file.h"
#include "lc_piecedata.h"
#include "piece.h"
#include "lc_model.h"

std::map<std::string, QString> lcShadowLibrary::mShadowFiles;
std::map<std::string, std::vector<QString>> lcShadowLibrary::mShadowLines;
std::map<std::string, std::vector<lcSnapCylinder>> lcShadowLibrary::mCylinders;
QRecursiveMutex lcShadowLibrary::mMutex;

static const std::vector<lcSnapCylinder> gNoCylinders;

bool lcShadowLibrary::SetPath(const QString& Path)
{
	QMutexLocker Lock(&mMutex);

	mShadowFiles.clear();
	mShadowLines.clear();
	mCylinders.clear();

	if (Path.isEmpty())
		return false;

	const QDir Root(Path);

	for (const char* Folder : { "parts", "p" })
	{
		const QDir Dir(Root.filePath(QLatin1String(Folder)));
		QDirIterator It(Dir.absolutePath(), QStringList() << QLatin1String("*.dat"), QDir::Files, QDirIterator::Subdirectories);

		while (It.hasNext())
		{
			const QString FilePath = It.next();
			const std::string Name = NormalizeName(Dir.relativeFilePath(FilePath).toStdString());
			mShadowFiles.emplace(Name, FilePath);
		}
	}

	return !mShadowFiles.empty();
}

bool lcShadowLibrary::IsLoaded()
{
	mMutex.lock();
	const bool Loaded = !mShadowFiles.empty();
	mMutex.unlock();

	return Loaded;
}

std::string lcShadowLibrary::NormalizeName(const std::string& Name)
{
	std::string Result = lcPiecesLibrary::NormalizePieceDataName(Name.c_str());

	if (Result.size() < 4 || Result.compare(Result.size() - 4, 4, ".DAT") != 0)
		Result += ".DAT";

	return Result;
}

const std::vector<QString>& lcShadowLibrary::GetShadowLines(const std::string& Name)
{
	const std::map<std::string, std::vector<QString>>::const_iterator LinesIt = mShadowLines.find(Name);

	if (LinesIt != mShadowLines.end())
		return LinesIt->second;

	std::vector<QString>& Lines = mShadowLines[Name];
	const std::map<std::string, QString>::const_iterator FileIt = mShadowFiles.find(Name);

	if (FileIt == mShadowFiles.end())
		return Lines;

	QFile File(FileIt->second);

	if (!File.open(QIODevice::ReadOnly | QIODevice::Text))
		return Lines;

	while (!File.atEnd())
	{
		const QString Line = QString::fromUtf8(File.readLine()).trimmed();

		if (Line.startsWith(QLatin1String("0 !LDCAD SNAP_")))
			Lines.push_back(Line);
	}

	return Lines;
}

std::vector<lcSnapCylinder> lcShadowLibrary::GetOwnCylinders(const std::string& Name)
{
	std::vector<lcSnapCylinder> Cylinders;

	for (const QString& Line : GetShadowLines(Name))
	{
		if (Line.startsWith(QLatin1String("0 !LDCAD SNAP_CYL")))
		{
			std::vector<lcSnapCylinder> Parsed = lcShadowParser::ParseCylinder(Line, Name);
			Cylinders.insert(Cylinders.end(), Parsed.begin(), Parsed.end());
		}
	}

	return Cylinders;
}

const std::vector<lcSnapCylinder>& lcShadowLibrary::GetCylinders(const std::string& PartName)
{
	QMutexLocker Lock(&mMutex);

	if (mShadowFiles.empty())
		return gNoCylinders;

	const std::string Name = NormalizeName(PartName);
	const auto CylindersIt = mCylinders.find(Name);

	if (CylindersIt != mCylinders.end())
		return CylindersIt->second;

	// Insert first so a file that (indirectly) references itself can't recurse forever.
	std::vector<lcSnapCylinder>& Collected = mCylinders[Name];
	std::vector<lcSnapCylinder> Result;

	// Inherit the snap info of every sub-file, moved to where the sub-file is used.
	std::vector<std::pair<std::string, lcMatrix44>> References;

	const auto ReadReferences = [&References](lcFile& File)
	{
		char Line[1024];

		while (File.ReadLine(Line, sizeof(Line)))
		{
			int LineType;
			float fm[12];
			char FileName[LC_MAXPATH];

			if (sscanf(Line, "%d %*s %f %f %f %f %f %f %f %f %f %f %f %f %1023s", &LineType, &fm[0], &fm[1], &fm[2], &fm[3], &fm[4], &fm[5], &fm[6], &fm[7], &fm[8], &fm[9], &fm[10], &fm[11], FileName) != 14 || LineType != 1)
				continue;

			References.emplace_back(FileName, lcShadowParser::LDrawMatrix(fm + 3, lcVector3(fm[0], fm[1], fm[2])));
		}
	};

	lcPiecesLibrary* Library = lcGetPiecesLibrary();
	lcLibraryPrimitive* Primitive = Library->FindPrimitive(Name.c_str());

	const lcResult<std::unique_ptr<lcFile>> File = Primitive ? Library->OpenPrimitiveFile(Primitive) : Library->OpenPieceDataFile(Name.c_str());

	if (File)
		ReadReferences(*File.value());

	for (const auto& [ReferenceName, ReferenceTransform] : References)
	{
		for (lcSnapCylinder Cylinder : GetCylinders(ReferenceName))
		{
			Cylinder.Transform = lcMul(Cylinder.Transform, ReferenceTransform);

			if (lcShadowParser::NormalizeFrame(Cylinder))
				Result.push_back(std::move(Cylinder));
		}
	}

	// Then the part's own shadow lines, in order, so SNAP_CLEAR only affects what came before it.
	for (const QString& Line : GetShadowLines(Name))
	{
		const std::map<QString, QString> Params = lcShadowParser::ParseParams(Line);

		if (Line.startsWith(QLatin1String("0 !LDCAD SNAP_CLEAR")))
		{
			const QString Id = lcShadowParser::GetParam(Params, "id");

			if (Id.isEmpty())
				Result.clear();
			else
				Result.erase(std::remove_if(Result.begin(), Result.end(), [&Id](const lcSnapCylinder& Cylinder) { return Id.compare(Cylinder.Id, Qt::CaseInsensitive) == 0; }), Result.end());
		}
		else if (Line.startsWith(QLatin1String("0 !LDCAD SNAP_CYL")))
		{
			std::vector<lcSnapCylinder> Parsed = lcShadowParser::ParseCylinder(Line, Name);
			Result.insert(Result.end(), Parsed.begin(), Parsed.end());
		}
		else if (Line.startsWith(QLatin1String("0 !LDCAD SNAP_INCL")))
		{
			const std::optional<lcMatrix44> Transform = lcShadowParser::ParseTransform(Params);
			const std::optional<std::vector<lcVector3>> Offsets = lcShadowParser::ParseGridOffsets(Params);

			if (!Transform || !Offsets)
				continue;

			const std::vector<lcSnapCylinder> Included = GetOwnCylinders(NormalizeName(lcShadowParser::GetParam(Params, "ref").toStdString()));

			for (const lcVector3& Offset : *Offsets)
			{
				const lcMatrix44 OffsetTransform = lcShadowParser::GetOffsetTransform(*Transform, Offset);

				for (lcSnapCylinder Cylinder : Included)
				{
					Cylinder.Transform = lcMul(Cylinder.Transform, OffsetTransform);

					if (lcShadowParser::NormalizeFrame(Cylinder))
						Result.push_back(std::move(Cylinder));
				}
			}
		}
	}

	Collected = std::move(Result);
	return Collected;
}

std::optional<lcMatrix44> lcShadowLibrary::FindPlacement(const lcPieceData* MovingPieceData, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const lcPieceData* TargetPieceData, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection, const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Others)
{
	const std::optional<lcSnapMatch> Match = FindMatch(MovingPieceData, ReferenceTransform, GrabPoint, TargetPieceData, TargetTransform, HitPoint, ViewDirection, Others);

	if (!Match)
		return std::nullopt;

	return Match->Transform;
}

std::optional<lcSnapMatch> lcShadowLibrary::FindMatch(const lcPieceData* MovingPieceData, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const lcPieceData* TargetPieceData, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection, const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Others)
{
	if (!MovingPieceData || !TargetPieceData || MovingPieceData->IsModel() || TargetPieceData->IsModel())
		return std::nullopt;

	const std::vector<lcSnapCylinder>& MovingCylinders = GetCylinders(MovingPieceData->mFileName);
	const lcBoundingBox& MovingBox = MovingPieceData->GetBoundingBox();
	const float MovingSize = lcLength(MovingBox.Max - MovingBox.Min);
	std::vector<std::pair<lcSnapBody, lcMatrix44>> Obstacles;

	// Only pieces the moving piece could reach from the cursor.
	for (const auto& [PieceData, Transform] : Others)
	{
		if (!PieceData || PieceData->IsModel())
			continue;

		const lcBoundingBox& Box = PieceData->GetBoundingBox();

		if (lcLength(lcMul31((Box.Min + Box.Max) / 2.0f, Transform) - HitPoint) < MovingSize + lcLength(Box.Max - Box.Min) / 2.0f)
			Obstacles.emplace_back(lcSnapShapes::GetBody(Box, GetCylinders(PieceData->mFileName)), Transform);
	}

	std::optional<lcSnapMatch> Match = lcSnapShapes::FindMatch(MovingCylinders, ReferenceTransform, GrabPoint, GetCylinders(TargetPieceData->mFileName), TargetTransform, HitPoint, ViewDirection, lcSnapShapes::GetBody(MovingBox, MovingCylinders).Box, Obstacles);

	if (Match)
		Match->TargetPieceData = TargetPieceData;

	return Match;
}

std::vector<std::pair<const std::vector<lcSnapCylinder>*, lcMatrix44>> lcShadowLibrary::GetPieceShapes(const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Pieces)
{
	std::vector<std::pair<const std::vector<lcSnapCylinder>*, lcMatrix44>> Shapes;

	for (const auto& [PieceData, Transform] : Pieces)
		if (PieceData && !PieceData->IsModel())
			Shapes.emplace_back(&GetCylinders(PieceData->mFileName), Transform);

	return Shapes;
}

std::vector<lcSnapConnection> lcShadowLibrary::FindConnections(const lcPieceData* PieceData, const lcMatrix44& Transform, const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Others)
{
	if (!PieceData || PieceData->IsModel())
		return {};

	return lcSnapShapes::FindConnections(GetCylinders(PieceData->mFileName), Transform, GetPieceShapes(Others));
}

std::optional<std::pair<lcVector3, lcMatrix33>> lcShadowLibrary::GetConnectionTurn(const lcPieceData* PieceData, const lcMatrix44& Transform, const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Others, int Direction, const std::optional<std::array<lcVector3, 2>>& PointerRay)
{
	if (!PieceData || PieceData->IsModel())
		return std::nullopt;

	const lcBoundingBox& BoundingBox = PieceData->GetBoundingBox();

	return lcSnapShapes::GetConnectionTurn(GetCylinders(PieceData->mFileName), Transform, (BoundingBox.Min + BoundingBox.Max) / 2.0f, GetPieceShapes(Others), Direction, PointerRay);
}

QString lcShadowLibrary::DumpCylinders(const std::string& PartName)
{
	const auto Number = [](float Value)
	{
		const float Rounded = std::round(Value * 1000.0f) / 1000.0f;
		return QString::number(Rounded == 0.0f ? 0.0f : Rounded, 'f', 3);
	};

	QStringList Lines;

	for (const lcSnapCylinder& Cylinder : GetCylinders(PartName))
	{
		QStringList Sections;

		for (const lcSnapSection& Section : Cylinder.Sections)
			Sections << QString("%1%2/%3").arg(Section.Shape).arg(Section.Radius).arg(Section.Length);

		const lcVector3 Position = Cylinder.GetPosition();
		const lcVector3 Axis = Cylinder.GetAxis();

		Lines << QString("%1 %2 %3 %4 %5 %6 %7 %8").arg(QChar(Cylinder.Gender == lcSnapGender::Male ? 'M' : 'F')).arg(Sections.join(','))
			.arg(Number(Position[0]), Number(Position[1]), Number(Position[2]), Number(Axis[0]), Number(Axis[1]), Number(Axis[2]));
	}

	Lines.sort();
	return Lines.join('\n');
}

// For every pair of pieces that are connected in the model, pretend the first one is dragged
// onto the second with the cursor on a shared connection point: it should land where it is.
QString lcShadowLibrary::SelfTest(const lcModel* Model)
{
	const auto WorldCylinders = [](const lcPiece* Piece)
	{
		std::vector<std::pair<const lcSnapCylinder*, lcMatrix44>> Result;

		if (!Piece->mPieceData->IsModel())
			for (const lcSnapCylinder& Cylinder : GetCylinders(Piece->mPieceData->mFileName))
				Result.emplace_back(&Cylinder, lcMul(lcMatrix44LDrawToLeoCAD(Cylinder.Transform), Piece->mModelWorld));

		return Result;
	};

	QStringList Failures;
	int Tested = 0;

	for (const std::unique_ptr<lcPiece>& Moving : Model->GetPieces())
	{
		const std::vector<std::pair<const lcSnapCylinder*, lcMatrix44>> MovingCylinders = WorldCylinders(Moving.get());

		for (const std::unique_ptr<lcPiece>& Target : Model->GetPieces())
		{
			if (Moving == Target)
				continue;

			const std::vector<std::pair<const lcSnapCylinder*, lcMatrix44>> TargetCylinders = WorldCylinders(Target.get());
			bool Found = false;

			for (const auto& [MovingCylinder, MovingFrame] : MovingCylinders)
			{
				for (const auto& [TargetCylinder, TargetFrame] : TargetCylinders)
				{
					const lcVector3 Axis1 = lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), MovingFrame));
					const lcVector3 Axis2 = lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), TargetFrame));

					if (!lcSnapShapes::AreCompatible(*MovingCylinder, *TargetCylinder) || lcLength(MovingFrame.GetTranslation() - TargetFrame.GetTranslation()) > 0.5f || lcDot(Axis1, Axis2) < 0.99f)
						continue;

					// Look at the target's connection point from the side it opens to.
					const lcVector3 HitPoint = TargetFrame.GetTranslation();
					const lcVector3 GrabPoint = lcMul31(HitPoint, lcMatrix44AffineInverse(Moving->mModelWorld));
					const lcVector3 ViewDirection = TargetCylinder->Gender == lcSnapGender::Male ? -Axis2 : Axis2;
					const std::optional<lcMatrix44> Placement = FindPlacement(Moving->mPieceData.Get(), Moving->mModelWorld, GrabPoint, Target->mPieceData.Get(), Target->mModelWorld, HitPoint, ViewDirection, {});

					Tested++;
					bool Same = Placement.has_value() && lcLength(Placement->GetTranslation() - Moving->mModelWorld.GetTranslation()) < 0.01f;

					for (int Row = 0; Same && Row < 3; Row++)
						Same = lcLength(lcVector3(Placement->r[Row]) - lcVector3(Moving->mModelWorld.r[Row])) < 0.001f;

					if (!Same)
					{
						float RotationError = 0.0f;

						if (Placement)
							for (int Row = 0; Row < 3; Row++)
								RotationError = std::max(RotationError, lcLength(lcVector3(Placement->r[Row]) - lcVector3(Moving->mModelWorld.r[Row])));

						Failures << QString("%1 (line %2) onto %3 (line %4): moved %5, rotation off by %6").arg(Moving->mPieceData->mFileName).arg(Moving->GetFileLine()).arg(Target->mPieceData->mFileName).arg(Target->GetFileLine())
							.arg(Placement ? lcLength(Placement->GetTranslation() - Moving->mModelWorld.GetTranslation()) : -1.0f).arg(RotationError);
					}

					Found = true;
					break;
				}

				if (Found)
					break;
			}
		}
	}

	QString Report = QString("%1 connected pairs tested, %2 failed\n").arg(Tested).arg(Failures.size()) + Failures.join('\n');

	// Drag a 1x2 brick, held above its left end, over every stud of a 6x8 plate with the
	// cursor a little off each stud: its left anti stud should land on the stud under the cursor.
	lcPiecesLibrary* Library = lcGetPiecesLibrary();
	std::vector<lcPieceDataRef> TestParts;
	const auto GetPart = [Library, &TestParts](const char* Name) -> lcPieceData*
	{
		lcPieceDataRef PieceData = Library->AcquirePieceData(Name, nullptr, false, false);

		if (!PieceData || !Library->EnsurePieceDataAssetsReady(PieceData))
			return nullptr;

		TestParts.push_back(PieceData);
		return PieceData.Get();
	};

	lcPieceData* Brick = GetPart("3004.dat");
	lcPieceData* Plate = GetPart("3036.dat");

	if (Brick && Plate)
	{
		const lcVector3 ViewDirection = lcNormalize(lcVector3(0.3f, 0.5f, -0.8f));
		const lcVector3 GrabPoint = lcVector3LDrawToLeoCAD(lcVector3(-10.0f, 0.0f, 0.0f));
		const lcVector3 LeftAntiStud = lcVector3LDrawToLeoCAD(lcVector3(-10.0f, 24.0f, 0.0f));
		const lcMatrix44 BrickStart = lcMatrix44Translation(lcVector3(0.0f, 0.0f, 100.0f));
		int Studs = 0, Wrong = 0;

		for (const lcSnapCylinder& Cylinder : GetCylinders("3036.dat"))
		{
			const lcMatrix44 Frame = lcMatrix44LDrawToLeoCAD(Cylinder.Transform);

			if (Cylinder.Gender != lcSnapGender::Male || lcMul30(lcVector3(0.0f, 0.0f, 1.0f), Frame)[2] < 0.5f)
				continue;

			const lcVector3 Stud = Frame.GetTranslation();
			const lcVector3 HitPoint = Stud + lcVector3(4.0f, -3.0f, 0.0f);
			const std::optional<lcMatrix44> Placement = FindPlacement(Brick, BrickStart, GrabPoint, Plate, lcMatrix44Identity(), HitPoint, ViewDirection, {});

			Studs++;

			if (!Placement || lcLength(lcMul31(LeftAntiStud, *Placement) - Stud) > 0.01f)
			{
				Wrong++;
				Report += QString("\nbrick on stud %1 %2 landed %3").arg(Stud[0]).arg(Stud[1]).arg(Placement ? QString("with its anti stud at %1 %2 %3").arg(lcMul31(LeftAntiStud, *Placement)[0]).arg(lcMul31(LeftAntiStud, *Placement)[1]).arg(lcMul31(LeftAntiStud, *Placement)[2]) : QString("nowhere"));
			}
		}

		Report += QString("\n1x2 brick dragged over %1 studs of a 6x8 plate, %2 landed wrong").arg(Studs).arg(Wrong);

		// Snapped onto the middle of the plate, the brick connects by both its anti studs.
		const std::optional<lcSnapMatch> Match = FindMatch(Brick, BrickStart, GrabPoint, Plate, lcMatrix44Identity(), lcVector3(-10.0f, 10.0f, 8.0f), ViewDirection, {});
		std::vector<lcVector3> Places;

		if (Match)
			for (const lcSnapConnection& Connection : Match->Connections)
				if (std::none_of(Places.begin(), Places.end(), [&Connection](const lcVector3& Place) { return lcLength(Place - Connection.Position) < 0.5f; }))
					Places.push_back(Connection.Position);

		Report += QString("\n1x2 brick snapped onto the middle of a 6x8 plate: connections at %1 places, expected 2").arg(Places.size());
	}

	// Drag an upright 1x2 brick, held at the top centre, onto each side stud of a 2x4x2 brick with
	// studs on sides: it should attach with an anti stud (not its bottom pin into the hollow stud)
	// and end up at a clean quarter turn.
	lcPieceData* SideStuds = GetPart("2434.dat");

	if (Brick && SideStuds)
	{
		int Studs = 0, Wrong = 0;

		for (const lcSnapCylinder& Cylinder : GetCylinders("2434.dat"))
		{
			const lcMatrix44 Frame = lcMatrix44LDrawToLeoCAD(Cylinder.Transform);
			const lcVector3 Axis = lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), Frame));

			if (Cylinder.Gender != lcSnapGender::Male || fabsf(Axis[2]) > 0.5f)
				continue;

			const lcVector3 Stud = Frame.GetTranslation();
			const lcVector3 ViewDirection = lcNormalize(-Axis + lcVector3(0.2f, 0.2f, -0.3f));
			const lcVector3 HitPoint = Stud + lcVector3(0.0f, 0.0f, 3.0f);
			const std::optional<lcMatrix44> Placement = FindPlacement(Brick, lcMatrix44Identity(), lcVector3(0.0f, 0.0f, 0.0f), SideStuds, lcMatrix44Identity(), HitPoint, ViewDirection, {});

			Studs++;
			bool Good = Placement.has_value();

			for (int Row = 0; Good && Row < 3; Row++)
				for (int Column = 0; Good && Column < 3; Column++)
					Good = fabsf(fabsf(Placement->r[Row][Column]) - 0.5f) > 0.499f;

			bool AntiStud = false;

			for (const lcSnapCylinder& Moving : Good ? GetCylinders("3004.dat") : std::vector<lcSnapCylinder>())
			{
				const lcMatrix44 MovingFrame = lcMul(lcMatrix44LDrawToLeoCAD(Moving.Transform), *Placement);

				if (Moving.Gender == lcSnapGender::Female && !Moving.Sections.empty() && Moving.Sections[0].Radius == 6.0f && lcLength(MovingFrame.GetTranslation() - Stud) < 0.01f)
					AntiStud = true;
			}

			if (!Good || !AntiStud)
			{
				const char* Problem = "not on an anti stud";

				if (!Placement)
					Problem = "no placement";
				else if (!Good)
					Problem = "not at a quarter turn";

				Wrong++;
				Report += QString("\nbrick on side stud %1 %2 %3: %4").arg(Stud[0]).arg(Stud[1]).arg(Stud[2]).arg(Problem);
			}
		}

		Report += QString("\n1x2 brick dragged onto %1 side studs, %2 landed wrong").arg(Studs).arg(Wrong);
	}

	// Drag a 3L bar, held at different points along it, into a bar hole with the cursor at the
	// hole: in the open clip of a 1x1 plate it must fill the hole and slide to put the held point
	// at the cursor; in a bar tube it must go to the closed end, however it is held.
	lcPieceData* Bar = GetPart("87994.dat");

	for (const char* HoleName : { "4081b.dat", "11090.dat" })
	{
		lcPieceData* HolePart = GetPart(HoleName);

		if (!Bar || !HolePart)
			continue;

		const lcSnapCylinder* Hole = nullptr;

		for (const lcSnapCylinder& Cylinder : GetCylinders(HoleName))
			if (Cylinder.Gender == lcSnapGender::Female && Cylinder.Sections.size() == 1 && Cylinder.Sections[0].Radius == 4.0f)
				Hole = &Cylinder;

		if (!Hole || GetCylinders("87994.dat").empty())
			continue;

		const lcSnapCylinder& BarShape = GetCylinders("87994.dat").front();
		const lcMatrix44 HoleFrame = lcMatrix44LDrawToLeoCAD(Hole->Transform);
		const lcVector3 Axis = lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), HoleFrame));
		const lcVector3 HitPoint = HoleFrame.GetTranslation();
		const bool Closed = Hole->ClosedEnd;
		int Wrong = 0;

		for (float Held : { 10.0f, 30.0f, 50.0f })
		{
			const lcVector3 GrabPoint = lcVector3LDrawToLeoCAD(lcVector3(0.0f, Held, 0.0f));
			const std::optional<lcMatrix44> Placement = FindPlacement(Bar, lcMatrix44Identity(), GrabPoint, HolePart, lcMatrix44Identity(), HitPoint, Axis, {});

			if (!Placement)
			{
				Wrong++;
				Report += QString("\nbar held at %1 not placed in %2").arg(Held).arg(HoleName);
				continue;
			}

			const lcMatrix44 BarFrame = lcMul(lcMatrix44LDrawToLeoCAD(BarShape.Transform), *Placement);
			const lcVector3 Offset = BarFrame.GetTranslation() - HitPoint;
			const float Along = lcDot(Offset, Axis);
			const float Aside = lcLength(Offset - Axis * Along);
			const float BarAxisDot = fabsf(lcDot(lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), BarFrame)), Axis));
			const auto [HoleStart, HoleEnd] = Hole->GetExtent();
			const float HeldAlong = lcDot(lcMul31(GrabPoint, *Placement) - HitPoint, Axis);
			// The bar's centre: at the closed end minus half the bar, or anywhere that keeps the hole covered.
			const bool Right = Closed ? fabsf(Along - (HoleEnd - 30.0f)) < 0.01f : fabsf(Along - (HoleStart + HoleEnd) / 2.0f) <= 30.0f - (HoleEnd - HoleStart) / 2.0f + 0.01f && fabsf(HeldAlong) < 0.01f;

			if (BarAxisDot < 0.999f || Aside > 0.01f || !Right)
			{
				Wrong++;
				Report += QString("\nbar held at %1 in %2: %3 off the axis, centre %4 along it").arg(Held).arg(HoleName).arg(Aside).arg(Along);
			}

			// The bar covers the whole hole, so its connection is in the middle of the hole.
			const std::optional<lcSnapMatch> Match = FindMatch(Bar, lcMatrix44Identity(), GrabPoint, HolePart, lcMatrix44Identity(), HitPoint, Axis, {});
			const lcVector3 HoleMiddle = HitPoint + Axis * ((HoleStart + HoleEnd) / 2.0f);

			if (!Match || lcLength(Match->MovingPosition - HoleMiddle) > 0.01f || Match->Connections.empty() || lcLength(Match->Connections.front().Position - HoleMiddle) > 0.01f)
			{
				Wrong++;
				Report += QString("\nbar held at %1 in %2: connection not in the middle of the hole").arg(Held).arg(HoleName);
			}
		}

		Report += QString("\n3L bar into %1 (%2), held at 3 points, %3 landed wrong").arg(HoleName).arg(Closed ? "closed" : "open").arg(Wrong);
	}

	// The other way round: drag the 1x1 plate with clip onto the bar, seen from the side, with the
	// cursor at points along the bar. Its clip must go around the bar where the cursor is.
	if (lcPieceData* ClipPlate = GetPart("4081b.dat"); Bar && ClipPlate && !GetCylinders("87994.dat").empty())
	{
		const lcSnapCylinder& BarShape = GetCylinders("87994.dat").front();
		const lcMatrix44 BarFrame = lcMatrix44LDrawToLeoCAD(BarShape.Transform);
		const lcVector3 Axis = lcNormalize(lcMul30(lcVector3(0.0f, 0.0f, 1.0f), BarFrame));
		const lcVector3 ViewDirection = lcNormalize(lcCross(Axis, lcVector3(1.0f, 0.0f, 0.0f)) + lcCross(Axis, lcVector3(0.0f, 1.0f, 0.0f)));
		int Wrong = 0;

		for (float Along : { -15.0f, 0.0f, 15.0f })
		{
			const lcVector3 HitPoint = BarFrame.GetTranslation() + Axis * Along;
			const lcVector3 GrabPoint = lcVector3LDrawToLeoCAD(lcVector3(0.0f, 2.0f, -20.0f));
			const std::optional<lcMatrix44> Placement = FindPlacement(ClipPlate, lcMatrix44Translation(lcVector3(0.0f, 0.0f, 100.0f)), GrabPoint, Bar, lcMatrix44Identity(), HitPoint, ViewDirection, {});
			const lcVector3 Clip = Placement ? lcMul31(GrabPoint, *Placement) : lcVector3(FLT_MAX, FLT_MAX, FLT_MAX);

			if (!Placement || lcLength(Clip - HitPoint) > 0.01f)
			{
				Wrong++;
				Report += QString("\nclip onto the bar at %1: %2").arg(Along).arg(Placement ? QString("clip %1 away from the cursor").arg(lcLength(Clip - HitPoint)) : QString("not placed"));
			}
		}

		Report += QString("\n1x1 plate with clip onto a 3L bar from the side, at 3 points, %1 landed wrong").arg(Wrong);
	}

	// The free move tool turns the whole selection the way the dragged piece turns, using
	// lcPiece::Rotate as lcModel::UpdateFreeMoveTool does. Check with a turn around a horizontal
	// axis (like snapping onto side studs): the dragged piece must end up exactly at the target
	// and a second selected piece must keep its offset and orientation relative to it.
	if (Brick)
	{
		lcPiece Dragged(Brick), Other(Brick);
		const lcMatrix44 DraggedStart = lcMul(lcMatrix44RotationZ(LC_PI / 2.0f), lcMatrix44Translation(lcVector3(10.0f, 20.0f, 30.0f)));
		const lcMatrix44 OtherStart = lcMul(lcMatrix44RotationY(LC_PI / 2.0f), lcMatrix44Translation(lcVector3(-30.0f, 0.0f, 50.0f)));
		const lcMatrix44 Target = lcMul(lcMatrix44RotationX(LC_PI / 2.0f), lcMatrix44Translation(lcVector3(100.0f, -40.0f, 24.0f)));
		const lcMatrix44 OtherInDragged = lcMul(OtherStart, lcMatrix44AffineInverse(DraggedStart));

		Dragged.Initialize(DraggedStart, 1);
		Other.Initialize(OtherStart, 1);

		const lcVector3 Distance = Target.GetTranslation() - Dragged.mModelWorld.GetTranslation();
		const lcMatrix33 RelativeRotation = Dragged.GetRelativeRotation();
		const lcMatrix33 RotationMatrix = lcMul(RelativeRotation, lcMul(lcMatrix33AffineInverse(lcMatrix33(Dragged.mModelWorld)), lcMatrix33(Target)));
		const lcMatrix33 WorldToFocusMatrix = lcMatrix33AffineInverse(RelativeRotation);

		for (lcPiece* Piece : { &Dragged, &Other })
		{
			Piece->MoveSelected(1, false, Distance);
			Piece->UpdatePosition(1);
			Piece->Rotate(1, false, RotationMatrix, Target.GetTranslation(), WorldToFocusMatrix);
			Piece->UpdatePosition(1);
		}

		const auto Difference = [](const lcMatrix44& a, const lcMatrix44& b)
		{
			float Max = 0.0f;

			for (int Row = 0; Row < 4; Row++)
				Max = std::max(Max, lcLength(lcVector3(a.r[Row]) - lcVector3(b.r[Row])));

			return Max;
		};

		const float DraggedError = Difference(Dragged.mModelWorld, Target);
		const float OtherError = Difference(Other.mModelWorld, lcMul(OtherInDragged, Target));

		Report += QString("\nselection turned around a horizontal axis: dragged piece off by %1, other piece off by %2").arg(DraggedError).arg(OtherError);
	}

	// Every pairing of two parts' connection points must give a rigid placement, including the
	// pins under plates, which come from mirrored sub-files.
	{
		const char* Parts[] = { "3710.dat", "73983.dat", "2434.dat", "3004.dat", "3008.dat", "3023.dat", "4162.dat", "30374.dat" };
		int Pairs = 0, NonRigid = 0;

		for (const char* MovingName : Parts)
		{
			for (const char* TargetName : Parts)
			{
				for (const lcSnapCylinder& Moving : GetCylinders(MovingName))
				{
					for (const lcSnapCylinder& TargetCylinder : GetCylinders(TargetName))
					{
						if (!lcSnapShapes::AreCompatible(Moving, TargetCylinder))
							continue;

						const lcMatrix44 Placement = lcMul(lcMatrix44AffineInverse(lcMatrix44LDrawToLeoCAD(Moving.Transform)), lcMatrix44LDrawToLeoCAD(TargetCylinder.Transform));
						Pairs++;

						if (!lcSnapShapes::IsRigid(Placement))
							NonRigid++;
					}
				}
			}
		}

		Report += QString("\n%1 connection pairings between 8 parts, %2 not rigid").arg(Pairs).arg(NonRigid);
	}

	// Drag a selection of two pieces for 100 frames towards a snapped placement the way the free
	// move tool does every frame: the other piece must keep its place relative to the dragged one.
	if (Brick)
	{
		lcPiece Dragged(Brick), Other(Brick);
		Dragged.Initialize(lcMatrix44Translation(lcVector3(0.0f, 0.0f, 50.0f)), 1);
		Other.Initialize(lcMul(lcMatrix44RotationY(LC_PI / 2.0f), lcMatrix44Translation(lcVector3(40.0f, 0.0f, 50.0f))), 1);
		const lcMatrix44 OtherInDragged = lcMul(Other.mModelWorld, lcMatrix44AffineInverse(Dragged.mModelWorld));

		for (int Frame = 0; Frame < 100; Frame++)
		{
			const lcMatrix44 Target = lcMul(lcMatrix44RotationX(Frame % 2 ? LC_PI / 2.0f : 0.0f), lcMatrix44Translation(lcVector3(Frame * 2.0f, 10.0f, 24.0f)));
			const lcVector3 Distance = Target.GetTranslation() - Dragged.mModelWorld.GetTranslation();
			const lcMatrix33 RelativeRotation = Dragged.GetRelativeRotation();
			const lcMatrix33 RotationMatrix = lcMul(RelativeRotation, lcMul(lcMatrix33AffineInverse(lcMatrix33(Dragged.mModelWorld)), lcMatrix33(Target)));

			for (lcPiece* Piece : { &Dragged, &Other })
			{
				Piece->MoveSelected(1, false, Distance);
				Piece->UpdatePosition(1);
				Piece->Rotate(1, false, RotationMatrix, Target.GetTranslation(), lcMatrix33AffineInverse(RelativeRotation));
				Piece->UpdatePosition(1);
			}
		}

		const lcMatrix44 Expected = lcMul(OtherInDragged, Dragged.mModelWorld);
		float Error = 0.0f;

		for (int Row = 0; Row < 4; Row++)
			Error = std::max(Error, lcLength(lcVector3(Other.mModelWorld.r[Row]) - lcVector3(Expected.r[Row])));

		Report += QString("\n100 frames of dragging a selection: other piece drifted by %1").arg(Error);
	}

	// Rotate Around Connection: turning a 1x4 plate and a corner plate sitting on a 6x8 plate must
	// keep them attached by as many studs as before, and four turns must bring them back.
	if (Plate)
	{
		const std::vector<std::pair<const lcPieceData*, lcMatrix44>> Base = { { Plate, lcMatrix44Identity() } };

		for (const char* Name : { "3710.dat", "2420.dat" })
		{
			lcPieceData* PieceData = GetPart(Name);

			if (!PieceData)
				continue;

			// Put it on the plate, its first anti stud on the plate's stud at (10, 10).
			const lcMatrix44 Start = lcMatrix44Translation(lcVector3(10.0f, 10.0f, 8.0f) - lcVector3LDrawToLeoCAD(lcVector3(Name[0] == '3' ? -30.0f : 0.0f, 0.0f, 0.0f)));
			lcMatrix44 Current = Start;
			const size_t StartConnections = FindConnections(PieceData, Start, Base).size();
			bool Attached = StartConnections > 0;

			for (int Turn = 0; Turn < 4; Turn++)
			{
				const std::optional<std::pair<lcVector3, lcMatrix33>> Pivot = GetConnectionTurn(PieceData, Current, Base, 1, std::nullopt);

				if (!Pivot)
				{
					Attached = false;
					break;
				}

				const auto& [PivotPosition, Rotation] = *Pivot;
				lcMatrix44 Next(lcMul(lcMatrix33(Current), Rotation), lcMul(Current.GetTranslation() - PivotPosition, Rotation) + PivotPosition);
				Current = Next;

				if (FindConnections(PieceData, Current, Base).size() != StartConnections)
					Attached = false;
			}

			float Error = 0.0f;

			for (int Row = 0; Row < 4; Row++)
				Error = std::max(Error, lcLength(lcVector3(Current.r[Row]) - lcVector3(Start.r[Row])));

			Report += QString("\n%1 on a 6x8 plate (%2 studs): four turns around a connection %3, back at the start within %4").arg(Name).arg(StartConnections).arg(Attached ? "stayed attached" : "came loose").arg(Error);
		}
	}

	// With the pointer over one of the studs a 1x6 plate sits on, the turn must pivot there.
	if (lcPieceData* LongPlate = GetPart("3666.dat"); Plate && LongPlate)
	{
		const std::vector<std::pair<const lcPieceData*, lcMatrix44>> Base = { { Plate, lcMatrix44Identity() } };
		const lcMatrix44 Start = lcMatrix44Translation(lcVector3(0.0f, 10.0f, 8.0f));
		int Wrong = 0;

		for (int Stud = 0; Stud < 6; Stud++)
		{
			const lcVector3 Pointer((Stud - 2.5f) * 20.0f, 10.0f, 8.0f);
			const std::array<lcVector3, 2> PointerRay = {{ Pointer + lcVector3(0.0f, 0.0f, 1000.0f), Pointer - lcVector3(0.0f, 0.0f, 1000.0f) }};
			const std::optional<std::pair<lcVector3, lcMatrix33>> Turn = GetConnectionTurn(LongPlate, Start, Base, 1, PointerRay);

			if (!Turn || lcLength(lcVector3(Turn->first[0], Turn->first[1], 0.0f) - lcVector3(Pointer[0], Pointer[1], 0.0f)) > 0.01f)
				Wrong++;
		}

		Report += QString("\n3666.dat on a 6x8 plate, pointer over each of its 6 studs: %1 turned around another stud").arg(Wrong);
	}

	// Malformed shadow lines must be skipped, and well-formed ones still read.
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
	int WrongLines = 0;

	for (const auto& [Line, Expected] : Lines)
	{
		const size_t Parsed = lcShadowParser::ParseCylinder(QLatin1String(Line), "test").size();

		if (Parsed != Expected)
		{
			WrongLines++;
			Report += QString("\n%1: %2 shapes, expected %3").arg(Line).arg(Parsed).arg(Expected);
		}
	}

	Report += QString("\n%1 shadow lines read, %2 wrong").arg(std::size(Lines)).arg(WrongLines);

	return Report;
}
