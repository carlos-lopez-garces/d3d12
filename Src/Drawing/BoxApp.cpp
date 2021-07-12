#include "../Common/d3dApp.h"
#include "../Common/Math.h"
#include "../Common/UploadBuffer.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

// Vertex format.
struct Vertex {
  // 0-byte offset. 3D floating-point vector.
  XMFLOAT3 Pos;
  // 12-byte offset. 4D floating-point vector.
  XMFLOAT4 Color;
};

// Data to be accessed by the vertex shader from a constant buffer resource.
struct ObjectConstants {
  // Composite world, view, and projection matrix.
  XMFLOAT4X4 WorldViewProj = Math::Identity4x4();
};

// TODO: Rename to DrawApp.
class BoxApp : public D3DApp {
private:
  std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
  // Vertex and index buffers and views.
  std::unique_ptr<MeshGeometry> mBoxGeo = nullptr;
  // Additional data for shaders (vertex transformation matrix).
  std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

private:
  void BuildShadersAndInputLayout();
  void BuildBoxGeometry();
  void BuildDescriptorHeaps();
  void BuildConstantBuffers();
};

void BoxApp::BuildShadersAndInputLayout() {
  mInputLayout = {
    // struct Vertex.Pos.
    // DXGI_FORMAT_R32G32B32_FLOAT is the data type of a 3D vector.
    // The 5th entry is the offset into the Vertex struct where the Pos member is.
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    // struct Vertex.Color.
    // DXGI_FORMAT_R32G32B32A32_FLOAT is the data type of a 4D vector.
    // The 5th entry is the offset into the Vertex struct where the Color member is.
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
  };
}

void BoxApp::BuildBoxGeometry() {
  std::array<Vertex, 8> vertices = {
    // Is this a cast?
    Vertex({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Tomato) }),
    Vertex({ XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT4(Colors::Bisque) }),
    Vertex({ XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT4(Colors::CornflowerBlue) }),
    Vertex({ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Honeydew) }),
    Vertex({ XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT4(Colors::MediumOrchid) }),
    Vertex({ XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT4(Colors::Gainsboro) }),
    Vertex({ XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT4(Colors::PowderBlue) }),
    Vertex({ XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT4(Colors::Teal) })
  };

  std::array<std::uint16_t, 36> indices = {
    // Triangles of front face.
    0, 1, 2,
    0, 2, 3,

    // Triangles of back face.
    4, 6, 5,
    4, 7, 6,

    // Triangles of left face.
    4, 5, 1,
    4, 1, 0,

    // Triangles of right face.
    3, 2, 6,
    3, 6, 7,

    // Triangles of top face.
    1, 5, 6,
    1, 6, 2,

    // Triangles of bottom face.
    4, 0, 3,
    4, 3, 7
  };

  const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
  const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);
  
  mBoxGeo = std::make_unique<MeshGeometry>();
  mBoxGeo->Name = "boxGeo";

  // Note that vertices and indices are copied twice: the first copy is into persistent
  // object memory (so that we don't lose it; with CopyMemory); the second copy is the
  // upload to the default buffers (with d3dUtil::CreateDefaultBuffer).

  ThrowIfFailed(D3DCreateBlob(vbByteSize, &mBoxGeo->VertexBufferCPU));
  CopyMemory(mBoxGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

  ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeo->IndexBufferCPU));
  CopyMemory(mBoxGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

  mBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
    md3dDevice.Get(),
    mCommandList.Get(),
    vertices.data(),
    vbByteSize,
    mBoxGeo->VertexBufferUploader
  );

  mBoxGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
    md3dDevice.Get(),
    mCommandList.Get(),
    indices.data(),
    ibByteSize,
    mBoxGeo->IndexBufferUploader
  );

  mBoxGeo->VertexByteStride = sizeof(Vertex);
  mBoxGeo->VertexBufferByteSize = vbByteSize;
  mBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
  mBoxGeo->IndexBufferByteSize = ibByteSize;

  SubmeshGeometry submesh;
  submesh.IndexCount = (UINT)indices.size();
  submesh.StartIndexLocation = 0;
  submesh.BaseVertexLocation = 0;

  mBoxGeo->DrawArgs["box"] = submesh;
}

void BoxApp::BuildDescriptorHeaps() {
  // Constant buffer descriptors / views.
  D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
  cbvHeapDesc.NumDescriptors = 1;
  // Can store constant buffer views (CBV), shader resource views (SRV), and 
  // unordered access views (UAV).
  cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  // Makes the descriptors accesible to shaders.
  cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  cbvHeapDesc.NodeMask = 0;
  ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
    &cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)
  ));
}

void BoxApp::BuildConstantBuffers() {
  mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);
  UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
  D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
  int boxCBufIndex = 0;
  cbAddress += boxCBufIndex * objCBByteSize;
  
  // Resource descriptor / view.
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
  cbvDesc.BufferLocation = cbAddress;
  cbvDesc.SizeInBytes = objCBByteSize;
  
  md3dDevice->CreateConstantBufferView(
    &cbvDesc,
    mCbvHeap->GetCPUDescriptorHandleForHeapStart()
  );
}

int WINAPI WinMain(
  HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd
) {
  return 0;
}