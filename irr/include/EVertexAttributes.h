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
	//! Instance transformation matrix (4 vec4 attributes), used for instanced rendering
	EVA_INSTANCE_MATRIX, // occupies EVA_INSTANCE_MATRIX .. EVA_INSTANCE_MATRIX+3
	EVA_COUNT = EVA_INSTANCE_MATRIX + 4
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
		"inInstanceMatrix0",
		"inInstanceMatrix1",
		"inInstanceMatrix2",
		"inInstanceMatrix3",
		0,
	};

} // end namespace video
