#pragma once

#include <QtTest/QtTest>

class lcTests : public QObject
{
	Q_OBJECT

private slots:
	void PropertyKeysAffectEquality();
	void LastKeyFrameTracksTimeEdits();
	void HistoryStoresOnlyChangedObjects();
	void HistoryCancelsPendingEdit();
	void HistoryReplaysAndMergesEdits();
	void HistoryDropsRevertedMerge();
	void ShadowParsesLines();
	void ShadowParsesIncludesAndClears();
	void ShadowReadsSubFileReferences();
	void ShadowScalesAndMirrorsSubFileShapes();
	void ConnectivityFitsShapesAlongTheirAxis();
	void ConnectivityConnectsSeatedShapes();
	void SnapPutsBrickOnEveryStud();
	void SnapPrefersAntiStudsOverPins();
	void SnapFindsAllConnections();
	void SnapPutsBarsIntoHoles();
	void SnapPutsClipOntoBarFromTheSide();
	void SnapTurnsAroundConnection();
	void SnapTurnsAroundConnectionAtPointer();
	void SnapBodyBoxLeavesOutStuds();
	void SnapBoxesOverlap();
	void SnapAvoidsRunningIntoOtherPieces();
	void SnapFitsInsideHollowPieces();
	void SnapLinesUpRoundConnections();
	void SnapTurnsRoundConnectionsIntoLine();
	void SnapLinesUpWithNeighbors();
};
