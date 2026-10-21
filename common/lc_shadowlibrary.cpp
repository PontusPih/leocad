#include "lc_global.h"
#include "lc_shadowlibrary.h"
#include "lc_shadowparser.h"
#include "lc_library.h"
#include "lc_application.h"
#include "lc_file.h"
#include "lc_piecedata.h"

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

std::optional<lcMatrix44> lcShadowLibrary::FindPlacement(const lcPieceData* MovingPieceData, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const lcPieceData* TargetPieceData, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection)
{
	if (!MovingPieceData || !TargetPieceData || MovingPieceData->IsModel() || TargetPieceData->IsModel())
		return std::nullopt;

	return lcSnapShapes::FindPlacement(GetCylinders(MovingPieceData->mFileName), ReferenceTransform, GrabPoint, GetCylinders(TargetPieceData->mFileName), TargetTransform, HitPoint, ViewDirection);
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
