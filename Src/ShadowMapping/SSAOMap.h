#pragma once

#include "../Common/d3dUtil.h"
#include "FrameResource.h"

class SSAOMap {
private:
    ID3D12Device *md3dDevice;
    UINT mRenderTargetWidth;
    UINT mRenderTargetHeight;
    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissor;
    Microsoft::WRL::ComPtr<ID3D12Resource> mNormalMap;

public:
    SSAOMap(
        ID3D12Device *device,
        ID3D12GraphicsCommandList *cmdList,
        UINT width,
        UINT height
    );

    void OnResize(UINT width, UINT height);

    void BuildResources();
};