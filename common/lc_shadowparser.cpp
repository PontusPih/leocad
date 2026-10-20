#include "lc_global.h"
#include "lc_shadowparser.h"

lcMatrix44 lcShadowParser::LDrawMatrix(const float* Ori, const lcVector3& Position)
{
	// Ori is in LDraw order (a b c d e f g h i), LeoCAD matrices are transposed.
	return lcMatrix44(lcVector4(Ori[0], Ori[3], Ori[6], 0.0f), lcVector4(Ori[1], Ori[4], Ori[7], 0.0f), lcVector4(Ori[2], Ori[5], Ori[8], 0.0f), lcVector4(Position, 1.0f));
}

// The [key=value] parameters of a snap line, with lower case keys.
std::map<QString, QString> lcShadowParser::ParseParams(const QString& Line)
{
	std::map<QString, QString> Params;
	qsizetype Start = 0;

	while ((Start = Line.indexOf('[', Start)) != -1)
	{
		const qsizetype Equal = Line.indexOf('=', Start);
		const qsizetype End = Line.indexOf(']', Start);

		if (Equal == -1 || End == -1 || Equal > End)
			break;

		Params[Line.mid(Start + 1, Equal - Start - 1).trimmed().toLower()] = Line.mid(Equal + 1, End - Equal - 1).trimmed();
		Start = End + 1;
	}

	return Params;
}

QStringList lcShadowParser::SplitTokens(const QString& Text)
{
	return Text.simplified().split(' ', Qt::SkipEmptyParts);
}

QString lcShadowParser::GetParam(const std::map<QString, QString>& Params, const char* Key)
{
	const std::map<QString, QString>::const_iterator It = Params.find(QLatin1String(Key));
	return It != Params.end() ? It->second : QString();
}

bool lcShadowParser::IsParamTrue(const std::map<QString, QString>& Params, const char* Key)
{
	return GetParam(Params, Key).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

// Count numbers, or nothing if Text holds anything else.
std::optional<std::vector<float>> lcShadowParser::ParseNumbers(const QString& Text, qsizetype Count)
{
	const QStringList Tokens = SplitTokens(Text);

	if (Tokens.size() != Count)
		return std::nullopt;

	std::vector<float> Numbers;

	for (const QString& Token : Tokens)
	{
		bool Ok = false;
		const float Number = Token.toFloat(&Ok);

		if (!Ok || !std::isfinite(Number))
			return std::nullopt;

		Numbers.push_back(Number);
	}

	return Numbers;
}

// Ori and position of a SNAP_CYL or SNAP_INCL line, or nothing if either is malformed.
std::optional<lcMatrix44> lcShadowParser::ParseTransform(const std::map<QString, QString>& Params)
{
	float Ori[9] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };
	lcVector3 Position(0.0f, 0.0f, 0.0f);

	const QString OriText = GetParam(Params, "ori");

	if (!OriText.isEmpty())
	{
		const std::optional<std::vector<float>> Numbers = ParseNumbers(OriText, 9);

		if (!Numbers)
			return std::nullopt;

		std::copy(Numbers->begin(), Numbers->end(), Ori);
	}

	const QString PosText = GetParam(Params, "pos");

	if (!PosText.isEmpty())
	{
		const std::optional<std::vector<float>> Numbers = ParseNumbers(PosText, 3);

		if (!Numbers)
			return std::nullopt;

		Position = lcVector3((*Numbers)[0], (*Numbers)[1], (*Numbers)[2]);
	}

	return LDrawMatrix(Ori, Position);
}

// Offsets (x, z) for a "[C] xcnt [C] zcnt xstep zstep" grid, a single zero offset without a grid,
// or nothing if the grid is malformed.
std::optional<std::vector<lcVector3>> lcShadowParser::ParseGridOffsets(const std::map<QString, QString>& Params)
{
	const QStringList Tokens = SplitTokens(GetParam(Params, "grid"));

	if (Tokens.empty())
		return std::vector<lcVector3>{ lcVector3(0.0f, 0.0f, 0.0f) };

	qsizetype Index = 0;
	int Counts[2] = { 1, 1 };
	bool Centered[2] = { false, false };

	for (int Axis = 0; Axis < 2; Axis++)
	{
		if (Index < Tokens.size() && Tokens[Index].compare(QLatin1String("C"), Qt::CaseInsensitive) == 0)
		{
			Centered[Axis] = true;
			Index++;
		}

		bool Ok = false;
		Counts[Axis] = Index < Tokens.size() ? Tokens[Index++].toInt(&Ok) : 0;

		// A real grid is a part's row of studs; anything larger is a typo.
		if (!Ok || Counts[Axis] < 1 || Counts[Axis] > 1000)
			return std::nullopt;
	}

	const std::optional<std::vector<float>> Steps = ParseNumbers(Tokens.mid(Index).join(' '), 2);

	if (!Steps)
		return std::nullopt;

	std::vector<lcVector3> Offsets;

	for (int x = 0; x < Counts[0]; x++)
	{
		const float OffsetX = (Centered[0] ? x - (Counts[0] - 1) / 2.0f : x) * (*Steps)[0];

		for (int z = 0; z < Counts[1]; z++)
		{
			const float OffsetZ = (Centered[1] ? z - (Counts[1] - 1) / 2.0f : z) * (*Steps)[1];
			Offsets.emplace_back(OffsetX, 0.0f, OffsetZ);
		}
	}

	return Offsets;
}

// Parts are often built from scaled and mirrored sub-files. Snap frames must stay plain
// rotations, so scaling is removed (only the position matters for matching) and a mirrored
// frame is corrected by flipping a radius axis (mirror=cor), or dropped (mirror=none).
// Returns false if the cylinder should be dropped.
bool lcShadowParser::NormalizeFrame(lcSnapCylinder& Cylinder)
{
	for (int Row = 0; Row < 3; Row++)
	{
		const lcVector3 Axis = lcNormalize(lcVector3(Cylinder.Transform.r[Row]));
		Cylinder.Transform.r[Row] = lcVector4(Axis, 0.0f);
	}

	if (lcSnapShapes::GetDeterminant(Cylinder.Transform) > 0.0f)
		return true;

	if (!Cylinder.InheritMirrored)
		return false;

	const lcVector4& RadiusAxis = Cylinder.Transform.r[0];
	Cylinder.Transform.r[0] = lcVector4(-RadiusAxis[0], -RadiusAxis[1], -RadiusAxis[2], RadiusAxis[3]);
	return true;
}

lcMatrix44 lcShadowParser::GetOffsetTransform(const lcMatrix44& Transform, const lcVector3& Offset)
{
	lcMatrix44 Result = Transform;
	Result.SetTranslation(Transform.GetTranslation() + lcMul30(Offset, Transform));
	return Result;
}

// The shapes of a SNAP_CYL line, one per grid position; none if the line is malformed.
std::vector<lcSnapCylinder> lcShadowParser::ParseCylinder(const QString& Line, const std::string& Source)
{
	const std::map<QString, QString> Params = ParseParams(Line);
	lcSnapCylinder Cylinder;

	Cylinder.Gender = GetParam(Params, "gender").startsWith('F', Qt::CaseInsensitive) ? lcSnapGender::Female : lcSnapGender::Male;
	Cylinder.Id = GetParam(Params, "id");
	Cylinder.Group = GetParam(Params, "group");
	Cylinder.Slide = IsParamTrue(Params, "slide");
	Cylinder.Center = IsParamTrue(Params, "center");
	Cylinder.InheritMirrored = GetParam(Params, "mirror").compare(QLatin1String("none"), Qt::CaseInsensitive) != 0;
	Cylinder.Source = Source;

	const QStringList Secs = SplitTokens(GetParam(Params, "secs"));

	if (Secs.empty() || Secs.size() % 3 != 0)
		return {};

	for (qsizetype i = 0; i < Secs.size(); i += 3)
	{
		const std::optional<std::vector<float>> Numbers = ParseNumbers(Secs[i + 1] + ' ' + Secs[i + 2], 2);

		if (!Numbers)
			return {};

		Cylinder.Sections.push_back({ Secs[i].toUpper(), (*Numbers)[0], (*Numbers)[1] });
	}

	const std::optional<lcMatrix44> Transform = ParseTransform(Params);
	const std::optional<std::vector<lcVector3>> Offsets = ParseGridOffsets(Params);

	if (!Transform || !Offsets)
		return {};

	std::vector<lcSnapCylinder> Cylinders;

	for (const lcVector3& Offset : *Offsets)
	{
		Cylinder.Transform = GetOffsetTransform(*Transform, Offset);

		if (NormalizeFrame(Cylinder))
			Cylinders.push_back(Cylinder);
	}

	return Cylinders;
}
