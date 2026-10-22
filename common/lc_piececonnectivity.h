#pragma once

#include "lc_connectivity.h"

// The lcConnectivity functions for pieces, with their parts' shapes from the shadow library;
// submodels have no shapes.
class lcPieceConnectivity
{
public:
	// Others are the pieces the moving piece should not run into.
	static std::optional<lcSnapMatch> FindMatch(const lcPieceData* MovingPieceData, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const lcPieceData* TargetPieceData, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection, const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Others);
	// The placement of FindMatch() alone.
	static std::optional<lcMatrix44> FindPlacement(const lcPieceData* MovingPieceData, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const lcPieceData* TargetPieceData, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection, const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Others);
	static std::vector<lcSnapConnection> FindConnections(const lcPieceData* PieceData, const lcMatrix44& Transform, const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Others);
	// Turns around the piece's bounding box centre when there is no pointer.
	static std::optional<std::pair<lcVector3, lcMatrix33>> GetConnectionTurn(const lcPieceData* PieceData, const lcMatrix44& Transform, const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Others, int Direction, const std::optional<std::array<lcVector3, 2>>& PointerRay);

private:
	// The shapes of each piece that has any, for the lcConnectivity functions.
	static std::vector<std::pair<const std::vector<lcConnectionCylinder>*, lcMatrix44>> GetPieceShapes(const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Pieces);
};
