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
    vector<XMFLOAT3> positions;
    vector<uint16_t> indices;
    vector<XMFLOAT3> normals;
    vector<XMFLOAT2> uvs;
    int texture;
    int material;
};

struct GLTFTextureData {
    string uri;
};

struct GLTFMaterialData {
    int baseColorMap;
    int normalMap;
};

class GLTFLoader {
public:
    GLTFLoader(string& filename);

    static GLTFPrimitiveData Load(string &filename);

    void LoadModel();

    unsigned int getPrimitiveCount(int nodeIdx = 0) const;

    // Loads a single primitive from the specified node.
    GLTFPrimitiveData LoadPrimitive(int nodeIdx, int primitiveIdx) const;

    // Loads all textures.
    vector<GLTFTextureData> LoadTextures();

    // Loads all materials.
    vector<GLTFMaterialData> LoadMaterials(
        const std::vector<GLTFTextureData> &textures
    );

private:
    string mFilename;
    string mAssetsDirectory;

    tinygltf::Model mModel;

    void LoadPrimitiveIndices(
        tinygltf::Primitive &primitive,
        GLTFPrimitiveData &primitiveData
    ) const;

    void LoadPrimitivePositions(
        tinygltf::Primitive &primitive,
        GLTFPrimitiveData &primitiveData
    ) const;

    void LoadPrimitiveNormals(
        tinygltf::Primitive &primitive,
        GLTFPrimitiveData &primitiveData
    ) const;

    void LoadPrimitiveUVs(
        tinygltf::Primitive &primitive,
        GLTFPrimitiveData &primitiveData
    ) const;

    void LoadPrimitiveTexture(
        tinygltf::Primitive &primitive,
        GLTFPrimitiveData &primitiveData
    ) const;

    void LoadPrimitiveMaterial(
      tinygltf::Primitive& primitive,
      GLTFPrimitiveData& primitiveData
    ) const;
};