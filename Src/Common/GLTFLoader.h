#pragma once
#include <string>
#include <vector>
#include "Math.h"

using namespace std;
using namespace DirectX;

struct GLTFData {
    vector<XMFLOAT3> vertices;
    vector<uint16_t> indices;
};

class GLTFLoader {
public:
    static GLTFData Load(string &filename);
};