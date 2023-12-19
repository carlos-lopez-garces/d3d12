#include "SSAOMap.h"

SSAOMap::SSAOMap(
    ID3D12Device *device,
    ID3D12GraphicsCommandList *cmdList,
    UINT width,
    UINT height
) : md3dDevice(device) {

}

void SSAOMap::OnResize(UINT width, UINT height) {
    if (width != mRenderTargetWidth || height != mRenderTargetHeight) {
        mRenderTargetWidth = width;
        mRenderTargetHeight = height;

        mViewport.Width = mRenderTargetWidth / 2;
        mViewport.Height = mRenderTargetHeight / 2;
        mViewport.TopLeftX = 0.0f;
        mViewport.TopLeftY = 0.0f;
        mViewport.MinDepth = 0.0f;
        mViewport.MaxDepth = 1.0f;

        mScissor = { 0, 0, (int)mRenderTargetWidth / 2, (int)mRenderTargetHeight / 2 };

        // Rebuild render target.
        BuildResources();
    }
}

void SSAOMap::BuildResources() {
    mNormalMap = nullptr;

    D3D12_RESOURCE_DESC textureDesc;
    ZeroMemory(&textureDesc, sizeof(D3D12_RESOURCE_DESC));
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    // asawicki.info/news_1726_secrets_of_direct3d_12_resource_alignment.
    textureDesc.Alignment = 0;
    textureDesc.Width = mRenderTargetWidth;
    textureDesc.Height = mRenderTargetHeight;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    float normalClearColor[] = {0.0f, 0.0f, 1.0f, 0.0f};
    CD3DX12_CLEAR_VALUE normalClearValue(DXGI_FORMAT_R16G16B16A16_FLOAT, normalClearColor);
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &normalClearValue,
        IID_PPV_ARGS(&mNormalMap)
    ));
}