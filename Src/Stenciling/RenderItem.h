#pragma once

#include "../Common/Math.h"
#include "FrameResource.h"

using namespace DirectX;

struct RenderItem {
    RenderItem(int numFramesDirty) : NumFramesDirty(numFramesDirty) {}

    XMFLOAT4X4 World = Math::Identity4x4();

    XMFLOAT4X4 TexTransform = Math::Identity4x4();

    int NumFramesDirty;

    UINT ObjCBIndex = -1;

    Material *Mat = nullptr;

    MeshGeometry *Geo = nullptr;

    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    UINT IndexCount = 0;

    UINT StartIndexLocation = 0;

    int BaseVertexLocation = 0;    
};