#pragma once

#include "../Common/d3dUtil.h"
#include "../Common/Math.h"
#include "../Common/UploadBuffer.h"

// This is data that is associated with an individual object.
struct ObjectConstants {
  DirectX::XMFLOAT4X4 World = Math::Identity4x4();
};

// This is data that applies to all draw calls and that doesn't depend on
// the object being drawn. For example, the view and projection matrices
// depend on the camera and not on any of the objects in the scene.
struct PassConstants {
  DirectX::XMFLOAT4X4 View = Math::Identity4x4();
  DirectX::XMFLOAT4X4 InvView = Math::Identity4x4();
  DirectX::XMFLOAT4X4 Proj = Math::Identity4x4();
  DirectX::XMFLOAT4X4 InvProj = Math::Identity4x4();
  DirectX::XMFLOAT4X4 ViewProj = Math::Identity4x4();
  DirectX::XMFLOAT4X4 InvViewProj = Math::Identity4x4();
  DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
  // The previous member is 3 floats. This next one aligns the subsequent
  // member on an even boundary (?).
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
  DirectX::XMFLOAT4 Color;
};

// A frame resource is a collection of resources that will be used in the creation
// of a given frame. A frame resouce allows a frame to be processed independently of
// other frames that are in the GPU's queue at a given moment. It will be  reused
// for future frames once the frame is finalized.
struct FrameResource {
  FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount);
  ~FrameResource() {};

  // A command allocator per frame. If a single shared one were used, the
  // application would need to block until the GPU were done with the current
  // commands before resetting it, regardless of frame.
  // By having one per frame, the application can reset and allocate commands
  // from them for frames that aren't currently in the GPU's queue.
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

  // Data that doesn't depend on any individual object in the scene.
  std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
  // Data that is specific to a given object, like its world matrix.
  std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;

  // For synchronizing the application and the GPU with respect to the 
  // corresponding frame. (The fence is the number that the GPU increments
  // as a result of reaching and executing the corresponding command.)
  UINT64 Fence = 0;
};