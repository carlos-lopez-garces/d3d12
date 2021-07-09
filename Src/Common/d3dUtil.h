#pragma once

#include <windows.h>
#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <string>
#include <memory>
#include <algorithm>
#include <vector>
#include <array>
#include <unordered_map>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <cassert>
#include "d3dx12.h"

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    std::wstring wfn = AnsiToWString(__FILE__);                       \
    if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); } \
}
#endif

inline std::wstring AnsiToWString(const std::string& str)
{
  WCHAR buffer[512];
  MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
  return std::wstring(buffer);
}

class DxException
{
public:
  DxException() = default;
  DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

  std::wstring ToString()const;

  HRESULT ErrorCode = S_OK;
  std::wstring FunctionName;
  std::wstring Filename;
  int LineNumber = -1;
};

class d3dUtil {
public:

  // Creates a buffer resource in the default heap, by first copying the data to an upload buffer.
  // The function doesn't manage the upload buffer; instead, the caller does. That's because the
  // function doesn't wait for the GPU to be done using it, so it must survive the call.
  static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
    ID3D12Device *device,
    ID3D12GraphicsCommandList *cmdList,
    const void *initData,
    UINT64 byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource> &uploadBuffer
  );

  // Constant buffers must be multiples of 256 bytes.
  static UINT CalcConstantBufferByteSize(UINT byteSize) {
    return (byteSize + 255) & ~255;
  }
};

// A component mesh of a MeshGeometry. The vertices and indices of a SubmeshGeometry
// are contained in the same buffers as other components of a MeshGeometry. The
// vertices and indices of a given SubmeshGeometry are stored contiguously in the buffers.
struct SubmeshGeometry {
  UINT IndexCount = 0;
  UINT StartIndexLocation = 0;
  INT BaseVertexLocation = 0;
  DirectX::BoundingBox Bounds;
};

// Groups a vertex and index buffer together. May be made of component SubmeshGeometry's.
struct MeshGeometry {
  std::string Name;

  // The stride or pitch is the byte size of an element in the buffer.
  UINT VertexByteStride = 0;
  // Total size.
  UINT VertexBufferByteSize = 0;

  DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;
  // Total size.
  UINT IndexBufferByteSize = 0;

  // This is where the vertex and index buffers are kept on the CPU side.
  Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
  Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

  Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
  Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

  // The vertex and index buffer resources will go in default buffers, so we
  // need upload buffers to copy their data into them (resources in the default
  // heap can only be accessed by the CPU via intermediate upload buffers).
  Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
  Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

  // Component SubmeshGeometry's. The vertices and indices of component 
  // SubmeshGeometry's coexist in the same vertex and index buffers.
  std::unordered_map<std::string, SubmeshGeometry> DrawArgs;

  // Vertex buffer resource descriptor.
  D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const {
    D3D12_VERTEX_BUFFER_VIEW vbv;
    vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
    vbv.StrideInBytes = VertexByteStride;
    vbv.SizeInBytes = VertexBufferByteSize;
    return vbv;
  }

  // Index buffer resource descriptor.
  D3D12_INDEX_BUFFER_VIEW IndexBufferView() const {
    D3D12_INDEX_BUFFER_VIEW ibv;
    ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
    ibv.Format = IndexFormat;
    ibv.SizeInBytes = IndexBufferByteSize;
    return ibv;
  }

  void DisposeUploaders() {
    VertexBufferUploader = nullptr;
    IndexBufferUploader = nullptr;
  }
};