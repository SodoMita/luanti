#pragma once

namespace video
{

//! Enumeration for all vertex attributes there are.
enum E_VERTEX_ATTRIBUTES
{
	EVA_POSITION = 0,
	EVA_NORMAL,
	EVA_COLOR,
	EVA_AUX,
	EVA_TCOORD0,
	EVA_TCOORD1,
	EVA_TANGENT,
	EVA_BINORMAL,
	EVA_WEIGHTS,
	EVA_JOINT_IDS,
	EVA_INSTANCE_ROW0,
	EVA_INSTANCE_ROW1,
	EVA_INSTANCE_ROW2,
	EVA_INSTANCE_ROW3,
	EVA_COUNT
};

//! Array holding the built in vertex attribute names
const char *const sBuiltInVertexAttributeNames[] = {
		"inVertexPosition",
		"inVertexNormal",
		"inVertexColor_raw", // (BGRA <-> RGBA swapped)
		"inVertexAux",
		"inTexCoord0",
		"inTexCoord1",
		"inVertexTangent",
		"inVertexBinormal",
		"inVertexWeights",
		"inVertexJointIDs",
		"inInstanceRow0",
		"inInstanceRow1",
		"inInstanceRow2",
		"inInstanceRow3",
		0,
	};

} // end namespace video
