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
  Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
  // Pipeline state object. Binds most objects to the pipeline: root
  // signature, shader byte code, rasterizer state, input layout, etc.
  Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO = nullptr;
  Microsoft::WRL::ComPtr<ID3DBlob> mvsByteCode = nullptr;
  Microsoft::WRL::ComPtr<ID3DBlob> mpsByteCode = nullptr;

private:
  void BuildShadersAndInputLayout();
  void BuildBoxGeometry();
  void BuildDescriptorHeaps();
  void BuildConstantBuffers();
  // The root signature describes the resources that will be bound to the
  // pipeline and that shaders will access.
  void BuildRootSignature();
  void BuildPSO();
};

void BoxApp::BuildShadersAndInputLayout() {
  // VS and PS are the entrypoints of the vertex and pixel shaders in the same file.
  // vs_5_0 and ps_5_0 are shader profiles of shader model 5.
  mvsByteCode = d3dUtil::CompileShader(L"Drawing.hlsl", nullptr, "VS", "vs_5_0");
  mpsByteCode = d3dUtil::CompileShader(L"Drawing.hlsl", nullptr, "PS", "ps_5_0");

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

void BoxApp::BuildRootSignature() {
  CD3DX12_ROOT_PARAMETER slotRootParameter[1];

  CD3DX12_DESCRIPTOR_RANGE cbvTable;
  // Descriptor table with 1 descriptor. 0 is "baseShaderRegister".
  // Bound to register b0 (b is for constant buffers).
  cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
  // This parameter will be a descriptor table, but it could be a root constant
  // or a single descriptor.
  slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

  // The root signature is a list of root parameters, slotRootParameter. We only
  // defined one.
  // Without the flag, the input assembler stage is omitted.
  CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
  
  Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
  Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;

  HRESULT hr = D3D12SerializeRootSignature(
    &rootSigDesc,
    D3D_ROOT_SIGNATURE_VERSION_1,
    serializedRootSig.GetAddressOf(),
    errorBlob.GetAddressOf()
  );
  if (errorBlob != nullptr)
  {
    ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
  }
  ThrowIfFailed(hr);

  ThrowIfFailed(md3dDevice->CreateRootSignature(
    0,
    serializedRootSig->GetBufferPointer(),
    serializedRootSig->GetBufferSize(),
    IID_PPV_ARGS(&mRootSignature))
  );
}

void BoxApp::BuildPSO()
{
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
  ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
  psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
  psoDesc.pRootSignature = mRootSignature.Get();
  psoDesc.VS =
  {
    reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
    mvsByteCode->GetBufferSize()
  };
  psoDesc.PS =
  {
    reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
    mpsByteCode->GetBufferSize()
  };
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = mBackBufferFormat;
  psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
  psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
  psoDesc.DSVFormat = mDepthStencilFormat;
  ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}

int WINAPI WinMain(
  HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd
) {
  return 0;
}