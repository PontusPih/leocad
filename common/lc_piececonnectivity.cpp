#include "lc_global.h"
#include "lc_piececonnectivity.h"
#include "lc_shadowlibrary.h"
#include "lc_piecedata.h"

std::optional<lcMatrix44> lcPieceConnectivity::FindPlacement(const lcPieceData* MovingPieceData, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const lcPieceData* TargetPieceData, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection, const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Others)
{
	const std::optional<lcSnapMatch> Match = FindMatch(MovingPieceData, ReferenceTransform, GrabPoint, TargetPieceData, TargetTransform, HitPoint, ViewDirection, Others);

	if (!Match)
		return std::nullopt;

	return Match->Transform;
}

std::optional<lcSnapMatch> lcPieceConnectivity::FindMatch(const lcPieceData* MovingPieceData, const lcMatrix44& ReferenceTransform, const lcVector3& GrabPoint, const lcPieceData* TargetPieceData, const lcMatrix44& TargetTransform, const lcVector3& HitPoint, const lcVector3& ViewDirection, const std::vector<std::pair<const lcPieceData*, lcMatrix44>>& Others)
{
	if (!MovingPieceData || !TargetPieceData || MovingPieceData->IsModel() || TargetPieceData->IsModel())
		return std::nullopt;

	const std::vector<lcConnectionCylinder>& MovingCylinders = lcShadowLibrary::GetCylinders(MovingPieceData->mFileName);
	const lcBoundingBox& MovingBox = MovingPieceData->GetBoundingBox();
	const float MovingSize = lcLength(MovingBox.Max - MovingBox.Min);
	std::vector<lcSnapNeighbor> Neighbors;

	// Only pieces the moving piece could reach from the cursor.
	for (const auto& [PieceData, Transform] : Others)
	{
		if (!PieceData || PieceData->IsModel())
			continue;

		const lcBoundingBox& Box = PieceData->GetBoundingBox();

		if (lcLength(lcMul31((Box.Min + Box.Max) / 2.0f, Transform) - HitPoint) < MovingSize + lcLength(Box.Max - Box.Min) / 2.0f)
		{
			const std::vector<lcConnectionCylinder>& Cylinders = lcShadowLibrary::GetCylinders(PieceData->mFileName);
			Neighbors.push_back({ &Cylinders, lcConnectivity::GetBody(Box, Cylinders), Transform });
		}
	}

	return lcConnectivity::FindMatch(MovingCylinders, ReferenceTransform, GrabPoint, lcShadowLibrary::GetCylinders(TargetPieceData->mFileName), TargetTransform, HitPoint, ViewDirection, lcConnectivity::GetBody(MovingBox, MovingCylinders).Box, Neighbors);
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
