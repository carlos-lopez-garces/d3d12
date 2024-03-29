#include "SSAOMap.h"

SSAOMap::SSAOMap(
    ID3D12Device *device,
    ID3D12GraphicsCommandList *cmdList,
    UINT width,
    UINT height
) : md3dDevice(device) {
    OnResize(width, height);
}

ID3D12Resource *SSAOMap::GetNormalMap() {
    return mNormalMap.Get();
}

CD3DX12_CPU_DESCRIPTOR_HANDLE SSAOMap::GetNormalMapRtv() const {
    return mhNormalMapCpuRtv;
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

        mScissor = { 0, 0, (int) mRenderTargetWidth / 2, (int) mRenderTargetHeight / 2 };

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

    // Ambient maps.
    mAmbientMap0 = nullptr;
    mAmbientMap1 = nullptr;

    // Since SSAO map is low frequency, half the resolution suffices.
    textureDesc.Width = mRenderTargetWidth / 2;
    textureDesc.Height = mRenderTargetHeight / 2;
    textureDesc.Format = AmbientMapFormat;

    float ambientClearColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    CD3DX12_CLEAR_VALUE ambientClearValue = CD3DX12_CLEAR_VALUE(AmbientMapFormat, ambientClearColor);
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &ambientClearValue,
        IID_PPV_ARGS(&mAmbientMap0))
    );
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &ambientClearValue,
        IID_PPV_ARGS(&mAmbientMap1))
    );
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
    mhAmbientMap0CpuSrv = hCpuSrv.Offset(1, cbvSrvUavDescriptorSize);
    mhAmbientMap1CpuSrv = hCpuSrv.Offset(1, cbvSrvUavDescriptorSize);

    mhNormalMapGpuSrv = hGpuSrv;
    mhDepthMapGpuSrv = hGpuSrv.Offset(1, cbvSrvUavDescriptorSize);
    mhAmbientMap0GpuSrv = hGpuSrv.Offset(1, cbvSrvUavDescriptorSize);
    mhAmbientMap1GpuSrv = hGpuSrv.Offset(1, cbvSrvUavDescriptorSize);

    mhNormalMapCpuRtv = hCpuRtv;
    mhAmbientMap0CpuRtv = hCpuRtv.Offset(1, rtvDescriptorSize);
    mhAmbientMap1CpuRtv = hCpuRtv.Offset(1, rtvDescriptorSize);

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

    // Ambient map SRVs.
    srvDesc.Format = AmbientMapFormat;
    md3dDevice->CreateShaderResourceView(mAmbientMap0.Get(), &srvDesc, mhAmbientMap0CpuSrv);
    md3dDevice->CreateShaderResourceView(mAmbientMap1.Get(), &srvDesc, mhAmbientMap1CpuSrv);

    // RTVs.

    // Normal map RTV.
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = NormalMapFormat;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    md3dDevice->CreateRenderTargetView(mNormalMap.Get(), &rtvDesc, mhNormalMapCpuRtv);

    // Ambient map RTVs.
    rtvDesc.Format = AmbientMapFormat;
    md3dDevice->CreateRenderTargetView(mAmbientMap0.Get(), &rtvDesc, mhAmbientMap0CpuRtv);
    md3dDevice->CreateRenderTargetView(mAmbientMap1.Get(), &rtvDesc, mhAmbientMap1CpuRtv);
}

void SSAOMap::Compute(
    ID3D12RootSignature *rootSignature,
    ID3D12GraphicsCommandList5 *cmdList,
    FrameResource *frameResource,
    int blurCount
) {
    cmdList->SetGraphicsRootSignature(rootSignature);

    cmdList->RSSetViewports(1, &mViewport);
    cmdList->RSSetScissorRects(1, &mScissor);

    CD3DX12_RESOURCE_BARRIER ambientMapRTBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mAmbientMap0.Get(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );
    cmdList->ResourceBarrier(1, &ambientMapRTBarrier);

    float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
    cmdList->ClearRenderTargetView(mhAmbientMap0CpuRtv, clearValue, 0, nullptr);

    // Ambient map 0 is the render target of the SSAO map construction pass.
    cmdList->OMSetRenderTargets(1, &mhAmbientMap0CpuRtv, true, nullptr);

    // Bind constant buffer.
    auto ssaoCBAddress = frameResource->SSAOCB->Resource()->GetGPUVirtualAddress();
    // Register b0 in SSAO shader: cbSSAO.
    cmdList->SetGraphicsRootConstantBufferView(0, ssaoCBAddress);
    // Register b1 in SSAO shader: gHorizontalBlur.
    //cmdList->SetGraphicsRoot32BitConstant(1, 0, 0);
    // Register t0 in SSAO shader: gNormalMap.
    cmdList->SetGraphicsRootDescriptorTable(2, mhNormalMapGpuSrv);

    cmdList->SetPipelineState(mSSAOPso);

    // SSAO shader doesn't use a vertex buffer.
    cmdList->IASetVertexBuffers(0, 0, nullptr);
    cmdList->IASetIndexBuffer(nullptr);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // 1 instance, 6 vertices: the 2 triangles of a fullscreen quad.
    cmdList->DrawInstanced(6, 1, 0, 0);

    CD3DX12_RESOURCE_BARRIER ambientMapReadBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mAmbientMap0.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
    cmdList->ResourceBarrier(1, &ambientMapReadBarrier);    
}

void SSAOMap::SetPSOs(ID3D12PipelineState *ssaoPso) {
    mSSAOPso = ssaoPso;
}