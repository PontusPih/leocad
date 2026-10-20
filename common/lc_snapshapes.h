#pragma once

#include "lc_math.h"

enum class lcSnapGender
{
	Male,
	Female
};

struct lcSnapSection
{
	QString Shape; // R (round), A (axle), S (square), _L or L_ (radius extension)
	float Radius;
	float Length;
};

struct lcSnapCylinder
{
	lcMatrix44 Transform; // In LDraw part coordinates, the shape runs along its -Y axis.
	lcSnapGender Gender;
	std::vector<lcSnapSection> Sections;
	QString Id;
	QString Group;
	bool Slide;
	bool Center;
	bool InheritMirrored; // mirror=cor (the default): flip a radius axis when used mirrored; mirror=none: drop it.
	std::string Source;

	lcVector3 GetPosition() const
	{
		return Transform.GetTranslation();
	}

	lcVector3 GetAxis() const
	{
		return lcNormalize(lcMul30(lcVector3(0.0f, -1.0f, 0.0f), Transform));
	}
};

// How connection shapes fit together, on lists of shapes and transforms, wherever the shapes come from.
class lcSnapShapes
{
public:


	static float GetDeterminant(const lcMatrix44& m);
private:
};
