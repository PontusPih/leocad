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
