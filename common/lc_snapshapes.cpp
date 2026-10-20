#include "lc_global.h"
#include "lc_snapshapes.h"

float lcSnapShapes::GetDeterminant(const lcMatrix44& m)
{
	return lcDot(lcVector3(m.r[0]), lcCross(lcVector3(m.r[1]), lcVector3(m.r[2])));
}
