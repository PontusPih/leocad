#include "lc_global.h"
#include "lc_piececonnectivity.h"
#include "lc_shadowlibrary.h"
#include "lc_piecedata.h"

std::optional<lcMatrix44> lcPieceConnectivity::FindPlacement(const lcPieceData* MovingPieceData, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const lcPieceData* TargetPieceData, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection)
{
	if (!MovingPieceData || !TargetPieceData || MovingPieceData->IsModel() || TargetPieceData->IsModel())
		return std::nullopt;

	return lcConnectivity::FindPlacement(lcShadowLibrary::GetCylinders(MovingPieceData->mFileName), ReferenceTransform, GrabPoint, lcShadowLibrary::GetCylinders(TargetPieceData->mFileName), TargetTransform, HitPoint, ViewDirection);
}

std::vector<std::pair<const std::vector<lcConnectionCylinder>*, lcMatrix44>> lcPieceConnectivity::GetPieceShapes(const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Pieces)
{
	std::vector<std::pair<const std::vector<lcConnectionCylinder>*, lcMatrix44>> Shapes;

	for (const auto& [PieceData, Transform] : Pieces)
		if (PieceData && !PieceData->IsModel())
			Shapes.emplace_back(&lcShadowLibrary::GetCylinders(PieceData->mFileName), Transform);

	return Shapes;
}

std::vector<lcSnapConnection> lcPieceConnectivity::FindConnections(const lcPieceData* PieceData, const lcMatrix44& Transform, const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Others)
{
	if (!PieceData || PieceData->IsModel())
		return {};

	return lcConnectivity::FindConnections(lcShadowLibrary::GetCylinders(PieceData->mFileName), Transform, GetPieceShapes(Others));
}

std::optional<std::pair<lcVector3, lcMatrix33>> lcPieceConnectivity::GetConnectionTurn(const lcPieceData* PieceData, const lcMatrix44& Transform, const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Others, int Direction, const std::optional<std::array<lcVector3, 2>>& PointerRay)
{
	if (!PieceData || PieceData->IsModel())
		return std::nullopt;

	const lcBoundingBox& BoundingBox = PieceData->GetBoundingBox();

	return lcConnectivity::GetConnectionTurn(lcShadowLibrary::GetCylinders(PieceData->mFileName), Transform, (BoundingBox.Min + BoundingBox.Max) / 2.0f, GetPieceShapes(Others), Direction, PointerRay);
}
