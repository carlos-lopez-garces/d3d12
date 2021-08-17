#pragma once

#include "../Common/d3dUtil.h"
#include "../Common/Math.h"
#include "../Common/UploadBuffer.h"

// Per-object / per-draw-call constants.
struct ObjectConstants {
  DirectX::XMFLOAT4X4 World = Math::Identity4x4();
};

// Per-pass / per-frame constants that apply to all objects / draw calls.
struct PassConstants {
  DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
  DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
  DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
  DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
  DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
  DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
  DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
  float cbPerObjectPad1 = 0.0f;
  DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
  DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
  float NearZ = 0.0f;
  float FarZ = 0.0f;
  float TotalTime = 0.0f;
  float DeltaTime = 0.0f;
};

struct Vertex {
  DirectX::XMFLOAT3 Pos;

  // Per-vertex normals are crucial for evaluating lighting models.
  DirectX::XMFLOAT3 Normal;
};

// The frame resource of an application is very specific to its needs.
struct FrameResource {
  FrameResource(
    ID3D12Device *device,
    UINT passCount,
    UINT objectCount,
    UINT materialCount,
    UINT waveVertCount
  );
  ~FrameResource();

  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

  // Constant buffer resources.
  std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
  std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;
  std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;

  UINT Fence = 0;
};