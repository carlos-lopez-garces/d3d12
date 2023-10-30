#pragma once
#include <string>
#include <vector>
#include <memory>
#include "Math.h"

using namespace std;
using namespace DirectX;

namespace tinygltf {
    class Model;
};

struct GLTFData {
    vector<XMFLOAT3> vertices;
    vector<uint16_t> indices;
};

class GLTFLoader {
public:
    static GLTFData Load(string &filename);

    void LoadModel(string &filename);

private:
    unique_ptr<tinygltf::Model> m_model;
};