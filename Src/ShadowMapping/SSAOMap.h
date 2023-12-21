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
    Microsoft::WRL::ComPtr<ID3D12Resource> mAmbientMap0;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mhNormalMapCpuSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mhNormalMapGpuSrv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mhNormalMapCpuRtv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mhDepthMapCpuSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mhDepthMapGpuSrv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap0CpuRtv;
    ID3D12PipelineState *mSSAOPso;

public:
    static const DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static const DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;

    SSAOMap(
        ID3D12Device *device,
        ID3D12GraphicsCommandList *cmdList,
        UINT width,
        UINT height
    );

    ID3D12Resource *GetNormalMap();

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetNormalMapRtv() const;

    void OnResize(UINT width, UINT height);

    void BuildResources();
    
    void BuildDescriptors(
        ID3D12Resource *depthStencilBuffer,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
        UINT cbvSrvUavDescriptorSize,
        UINT rtvDescriptorSize
    );

    void RebuildDescriptors(ID3D12Resource *depthStencilBuffer);

    void Compute(
        ID3D12RootSignature *rootSignature,
        ID3D12GraphicsCommandList5 *cmdList,
        FrameResource *frameResource,
        int blurCount
    );

    void SetPSOs(ID3D12PipelineState *ssaoPso);
};