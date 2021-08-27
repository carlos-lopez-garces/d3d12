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

// const variables have internal linkage by default; change it to external.
// Application source code that includes this header will set its value.
extern const int gNumFrameResources;

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

  static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
    const std::wstring &filename,
    const D3D_SHADER_MACRO *defines,
    const std::string &entrypoint,
    const std::string &target
  );
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

// A Material's description is passed to shaders in constant buffers.
struct Material {
  std::string Name;
  
  // Materials are passed to shaders in constant buffers; this index is the location
  // of this material in the materials constant buffer.
  int MatCBIndex = -1;

  // The diffuse albedo specifies the fraction of each of the light's color 
  // components that gets reflected; the rest is absorbed.
  DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };

  // Specular color. The Schlick approximation of the Fresnel equations interpolates
  // linearly between this color and white.
  DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };

  // The microfacet distribution function of the material has an exponent m:
  // rho(thetaH) = cos(thetaH)^m, where thetaH is the angle between the surface's
  // macro normal and the microfacet's normal. The larger m is, the larger rho(thetaH)
  // will be for smaller angles thetaH, creating a bias of microfacet normals towards
  // the macro normal, and narrowing the specular lobe. The parameter m effectively
  // controls the spread of the specular lobe.
  //
  // This roughness member is used to compute the parameter m.
  // [0,1], where 0 is perfectly smooth and 1 is the roughest possible.
  float Roughness = 0.25f;

  // This Material will be part of a FrameResource in a constant buffer. If the application
  // modifies it, it has to be updated on the constant buffer of all the frame resources.
  int NumFramesDirty = gNumFrameResources;
};

// Subset of Material to be passed to shaders in constant buffers.
// Will be packed by HLSL as 2 full 4D vectors.
struct MaterialConstants {
  DirectX::XMFLOAT4 DiffuseAlbedo = {1.0f, 1.0f, 1.0f, 1.0f};
  DirectX::XMFLOAT3 FresnelR0 = {0.01f, 0.01f, 0.01f};
  float Roughness = 0.25f;
};


#define MaxLights 16
struct Light {
  // Will be packed by HLSL as 3 full 4D vectors.

  // 1st packed 4D vector.
  // TODO: what's the range of each component?
  DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
  // Parameter of the linear falloff function; a distance from the light source.
  float FalloffStart = 1.0f;

  // 2nd packed 4D vector.
  // For directional lights and spotlights.
  DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };
  // Parameter of the linear falloff function; a distance from the light source.
  float FalloffEnd = 10.0f;

  // 3rd packed 4D vector.
  // For point and spotlights.
  DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
  // Exponent of the angular decay function of a spotlight's intensity.
  float SpotPower = 64.0f;
};