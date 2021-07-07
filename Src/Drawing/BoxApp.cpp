#include "../Common/d3dApp.h"

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

// TODO: Rename to DrawApp.
class BoxApp : public D3DApp {
private:
  std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
  // Vertex and index buffers and views.
  std::unique_ptr<MeshGeometry> mBoxGeo = nullptr;

private:
  void BuildShadersAndInputLayout();
  void BuildBoxGeometry();
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