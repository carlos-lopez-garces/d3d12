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