#pragma once

#include <QtTest/QtTest>

class lcTests : public QObject
{
	Q_OBJECT

private slots:
	void PropertyKeysAffectEquality();
	void HistoryStoresOnlyChangedObjects();
	void HistoryCancelsPendingEdit();
	void HistoryReplaysAndMergesEdits();
	void HistoryDropsRevertedMerge();
	void SnapParsesLines();
	void SnapScalesAndMirrorsSubFileShapes();
	void SnapFitsShapesAlongTheirAxis();
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
};
