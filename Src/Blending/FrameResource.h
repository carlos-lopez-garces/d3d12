#pragma once

#include "../Common/d3dUtil.h"
#include "../Common/UploadBuffer.h"

struct Vertex {
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;
};

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 World = Math::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform = Math::Identity4x4();
};

struct PassConstants
{
    DirectX::XMFLOAT4X4 View = Math::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = Math::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = Math::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = Math::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = Math::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = Math::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float cbPerObjectPad1 = 0.0f;
    DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;
    DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };
	DirectX::XMFLOAT4 FogColor = { 0.7f, 0.7f, 0.7f, 1.0f };
	float gFogStart = 5.0f;
	float gFogRange = 150.0f;
	DirectX::XMFLOAT2 cbPerObjectPad2;
    Light Lights[MaxLights];
};

struct FrameResource {
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount, UINT waveVertCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;

    std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;

    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;

    std::unique_ptr<UploadBuffer<Vertex>> WavesVB = nullptr;

    UINT64 Fence = 0;
};