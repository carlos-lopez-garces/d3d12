#pragma once

#include "d3dUtil.h"

// Upload buffers in upload heaps can be updated from the CPU side.
template <typename T> class UploadBuffer {
private:
  Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
  // Constant buffers receive special treatment: they are required to be
  // of a size multiple of 256 bytes.
  bool mIsConstantBuffer = false;
  UINT mElementByteSize = 0;
  BYTE* mMappedData = nullptr;

public:
  UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer)
    : mIsConstantBuffer(isConstantBuffer) {
    
    mElementByteSize = sizeof(T);

    if (isConstantBuffer) {
      mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));
    }

    auto uploadHeapType = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadBufferDescriptor = CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount);

    ThrowIfFailed(device->CreateCommittedResource(
      &uploadHeapType,
      D3D12_HEAP_FLAG_NONE,
      &uploadBufferDescriptor,
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&mUploadBuffer)
    ));

    ThrowIfFailed(mUploadBuffer->Map(
      0, nullptr, reinterpret_cast<void**>(&mMappedData))
    );
  }

  ID3D12Resource* Resource()const
  {
    return mUploadBuffer.Get();
  }

  void CopyData(int elementIndex, const T& data)
  {
    memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
  }
};