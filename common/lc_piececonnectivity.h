#pragma once

#include "lc_connectivity.h"

// The lcConnectivity functions for pieces, with their parts' shapes from the shadow library;
// submodels have no shapes.
class lcPieceConnectivity
{
public:
	static std::optional<lcMatrix44> FindPlacement(const lcPieceData* MovingPieceData, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const lcPieceData* TargetPieceData, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection);
};
