#include "SSAOMap.h"

SSAOMap::SSAOMap(
    ID3D12Device *device,
    ID3D12GraphicsCommandList *cmdList,
    UINT width,
    UINT height
) : md3dDevice(device) {

}

ID3D12Resource *SSAOMap::GetNormalMap() {
    return mNormalMap.Get();
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
    textureDesc.Format = NormalMapFormat;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    float normalClearColor[] = {0.0f, 0.0f, 1.0f, 0.0f};
    CD3DX12_CLEAR_VALUE normalClearValue(NormalMapFormat, normalClearColor);
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &normalClearValue,
        IID_PPV_ARGS(&mNormalMap)
    ));
}

void SSAOMap::BuildDescriptors(
    ID3D12Resource *depthStencilBuffer,
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
    UINT cbvSrvUavDescriptorSize,
    UINT rtvDescriptorSize
) {
    mhNormalMapCpuSrv = hCpuSrv;
    mhDepthMapCpuSrv = hCpuSrv.Offset(1, cbvSrvUavDescriptorSize);

    mhNormalMapGpuSrv = hGpuSrv;
    mhDepthMapGpuSrv = hGpuSrv.Offset(1, cbvSrvUavDescriptorSize);

    mhNormalMapCpuRtv = hCpuRtv;

    RebuildDescriptors(depthStencilBuffer);
}

void SSAOMap::RebuildDescriptors(ID3D12Resource *depthStencilBuffer) {
    // SRVs.

    // Normal map SRV.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = NormalMapFormat;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    md3dDevice->CreateShaderResourceView(mNormalMap.Get(), &srvDesc, mhNormalMapCpuSrv);

    // Depth map SRV.
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    md3dDevice->CreateShaderResourceView(depthStencilBuffer, &srvDesc, mhDepthMapCpuSrv);

    // RTVs.

    // Normal map RTV.
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = NormalMapFormat;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    md3dDevice->CreateRenderTargetView(mNormalMap.Get(), &rtvDesc, mhNormalMapCpuRtv);
}