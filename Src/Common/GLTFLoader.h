#pragma once
#include <string>
#include <vector>
#include <memory>
#include "Math.h"

#define __STDC_LIB_EXT1__
#include "../Ext/tiny_gltf.h"

using namespace std;
using namespace DirectX;

// Loaded data of a single primitive.
struct GLTFPrimitiveData {
    vector<XMFLOAT3> vertices;
    vector<uint16_t> indices;
};

class GLTFLoader {
public:
    GLTFLoader(string& filename);

    static GLTFPrimitiveData Load(string &filename);

    void LoadModel();

    // Loads a single primitive from the specified node.
    GLTFPrimitiveData LoadPrimitive(int nodeIdx, int primitiveIdx) const;

    unsigned int getPrimitiveCount(int nodeIdx = 0) const;

private:
    string mFilename;

    tinygltf::Model mModel;

    void LoadPrimitivePositions(
        tinygltf::Primitive &primitive,
        GLTFPrimitiveData &primitiveData
    ) const;
};