#pragma once

#include "lc_snapshapes.h"

// The connection shapes of library parts, read from the LDCad shadow library and inherited from
// the parts' sub-files.
class lcShadowLibrary
{
public:
	// An empty path, or one without a shadow library, turns snapping off.
	static bool SetPath(const QString& Path);
	static bool IsLoaded();

	// PartName is a library file name such as "3001.dat" or "s/3001s01.dat".
	static const std::vector<lcSnapCylinder>& GetCylinders(const std::string& PartName);

	// The lcSnapShapes functions for pieces; submodels have no shapes.
	static std::optional<lcMatrix44> FindPlacement(const lcPieceData* MovingPieceData, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const lcPieceData* TargetPieceData, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection);

private:
	static std::string NormalizeName(const std::string& Name);
	static const std::vector<QString>& GetShadowLines(const std::string& Name);
	static std::vector<lcSnapCylinder> GetOwnCylinders(const std::string& Name);

	static std::map<std::string, QString> mShadowFiles;
	static std::map<std::string, std::vector<QString>> mShadowLines;
	static std::map<std::string, std::vector<lcSnapCylinder>> mCylinders;
	static QRecursiveMutex mMutex;
};
