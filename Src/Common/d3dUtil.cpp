#include "d3dUtil.h"
#include <comdef.h>

using Microsoft::WRL::ComPtr;

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
  ErrorCode(hr),
  FunctionName(functionName),
  Filename(filename),
  LineNumber(lineNumber) {
}

std::wstring DxException::ToString() const {
  _com_error err(ErrorCode);
  std::wstring msg = err.ErrorMessage();
  return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}

Microsoft::WRL::ComPtr<ID3D12Resource> d3dUtil::CreateDefaultBuffer(
  ID3D12Device* device,
  ID3D12GraphicsCommandList* cmdList,
  const void* initData,
  UINT64 byteSize,
  Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer
) {
  ComPtr<ID3D12Resource> defaultBuffer;

  ThrowIfFailed(device->CreateCommittedResource(
    // Static geometry usually goes in the default heap (the default heap can only
    // be accessed by the GPU; if the CPU needs to change the geometry, via e.g.
    // animation, then the buffer needs to be allocated somewhere else).
    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
    D3D12_HEAP_FLAG_NONE,
    // The buffer resource descriptor.
    &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
    D3D12_RESOURCE_STATE_COMMON,
    nullptr,
    // The buffer resource.
    IID_PPV_ARGS(defaultBuffer.GetAddressOf())
  ));

  ThrowIfFailed(device->CreateCommittedResource(
    // Upload heap.
    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
    D3D12_HEAP_FLAG_NONE,
    // The upload buffer resource descriptor.
    &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(uploadBuffer.GetAddressOf())
  ));

  D3D12_SUBRESOURCE_DATA subResourceData = {};
  subResourceData.pData = initData;
  subResourceData.RowPitch = byteSize;
  subResourceData.SlicePitch = subResourceData.RowPitch;

  cmdList->ResourceBarrier(
    1,
    &CD3DX12_RESOURCE_BARRIER::Transition(
      defaultBuffer.Get(),
      D3D12_RESOURCE_STATE_COMMON,
      D3D12_RESOURCE_STATE_COPY_DEST
    )
  );

  // From d3dx12.h. Copies data to a destination resource via an intermediate buffer.
  // In this case the input initData is put in the the upload buffer and then the GPU
  // copies it to the default buffer (only the GPU has access to the default heap).
  UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);

  cmdList->ResourceBarrier(1,
    &CD3DX12_RESOURCE_BARRIER::Transition(
      defaultBuffer.Get(),
      D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_GENERIC_READ
    )
  );

  return defaultBuffer;
}

Microsoft::WRL::ComPtr<ID3DBlob> d3dUtil::CompileShader(
  const std::wstring& filename,
  const D3D_SHADER_MACRO* defines,
  const std::string& entrypoint,
  const std::string& target
) {
  UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
  compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

  HRESULT hr = S_OK;

  ComPtr<ID3DBlob> byteCode = nullptr;
  ComPtr<ID3DBlob> errors;
  hr = D3DCompileFromFile(
    filename.c_str(),
    defines,
    D3D_COMPILE_STANDARD_FILE_INCLUDE,
    entrypoint.c_str(),
    target.c_str(),
    compileFlags,
    0,
    &byteCode,
  &errors);

  if (errors != nullptr) {
    OutputDebugStringA((char*)errors->GetBufferPointer());
  }

  ThrowIfFailed(hr);

  return byteCode;
}