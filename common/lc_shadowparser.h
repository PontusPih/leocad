#pragma once

#include "lc_snapshapes.h"

// Connection shapes ("snap info") in the LDCad shadow library format:
// https://www.melkert.net/LDCad/tech/meta
// Only SNAP_CYL, SNAP_INCL and SNAP_CLEAR are supported (studs, anti studs,
// holes, axles and bars); clips, fingers and generic shapes are ignored.

class lcShadowParser
{
public:
	// The shapes of a SNAP_CYL line, one per grid position; none if the line is malformed.
	static std::vector<lcSnapCylinder> ParseCylinder(const QString& Line, const std::string& Source);

	// For reading shapes from sub-files and SNAP_INCL lines.
	static lcMatrix44 LDrawMatrix(const float* Ori, const lcVector3& Position);
	static std::map<QString, QString> ParseParams(const QString& Line);
	static QString GetParam(const std::map<QString, QString>& Params, const char* Key);
	static std::optional<lcMatrix44> ParseTransform(const std::map<QString, QString>& Params);
	static std::optional<std::vector<lcVector3>> ParseGridOffsets(const std::map<QString, QString>& Params);
	static lcMatrix44 GetOffsetTransform(const lcMatrix44& Transform, const lcVector3& Offset);
	static bool NormalizeFrame(lcSnapCylinder& Cylinder);

private:
	static QStringList SplitTokens(const QString& Text);
	static bool IsParamTrue(const std::map<QString, QString>& Params, const char* Key);
	static std::optional<std::vector<float>> ParseNumbers(const QString& Text, qsizetype Count);
};
