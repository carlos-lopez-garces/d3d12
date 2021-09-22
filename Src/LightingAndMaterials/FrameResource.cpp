#include "FrameResource.h"

FrameResource::FrameResource(
  ID3D12Device* device,
  UINT passCount,
  UINT objectCount,
  UINT materialCount
) {
  ThrowIfFailed(device->CreateCommandAllocator(
    D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(CmdListAlloc.GetAddressOf())
  ));

  // Create upload buffers for constant buffers.
  // 3rd argument true indicates constant buffer.
  PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
  MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
  ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
}