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
class DrawingApp : public D3DApp {
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
  XMFLOAT4X4 mWorld = Math::Identity4x4();
  XMFLOAT4X4 mView = Math::Identity4x4();
  XMFLOAT4X4 mProj = Math::Identity4x4();
  // In this app, the camera can only rotate around a fixed point and 
  // zoom in and out of it. That point is the origin of world space.
  // The position and orientation of the camera is thus described more
  // conveniently using spherical coordinates, where the radius r is the
  // distance from the fixed point to the camera's position; this radius
  // will be shortened or enlarged when zooming in and out; and phi is
  // angle between the Y axis and the line of vision.
  // 3PI/2.
  //
  // The following are the initial spherical coordinates of the camera.
  float mTheta = 1.5f * XM_PI;
  // PI/4.
  float mPhi = XM_PIDIV4;
  float mRadius = 5.0f;

public:
  DrawingApp(HINSTANCE hInstance) : D3DApp(hInstance) {};
  ~DrawingApp() {};
  virtual bool Initialize() override;

private:
  void BuildShadersAndInputLayout();
  void BuildBoxGeometry();
  void BuildDescriptorHeaps();
  void BuildConstantBuffers();
  // The root signature describes the resources that will be bound to the
  // pipeline and that shaders will access.
  void BuildRootSignature();
  void BuildPSO();

  virtual void Draw(const GameTimer &gt) override;
  // Updates the world view projection caused by changes in the camera's 
  // position and orientation, or in the size of the window (which may change
  // the aspect ratio and thus the projection matrix).
  virtual void Update(const GameTimer &gt) override;
  // Updates the projection matrix as a result of changes to the aspect ratio
  // of the window.
  virtual void OnResize() override;
};

void DrawingApp::BuildShadersAndInputLayout() {
  // VS and PS are the entrypoints of the vertex and pixel shaders in the same file.
  // vs_5_0 and ps_5_0 are shader profiles of shader model 5.
  mvsByteCode = d3dUtil::CompileShader(L"Src/Drawing/Drawing.hlsl", nullptr, "VS", "vs_5_0");
  mpsByteCode = d3dUtil::CompileShader(L"Src/Drawing/Drawing.hlsl", nullptr, "PS", "ps_5_0");

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

void DrawingApp::BuildBoxGeometry() {
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

void DrawingApp::BuildDescriptorHeaps() {
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

void DrawingApp::BuildConstantBuffers() {
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

void DrawingApp::BuildRootSignature() {
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

void DrawingApp::BuildPSO()
{
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
  ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
  psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
  psoDesc.pRootSignature = mRootSignature.Get();
  psoDesc.VS = {
    reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
    mvsByteCode->GetBufferSize()
  };
  psoDesc.PS = {
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

void DrawingApp::Draw(const GameTimer& gt) {
  // Need to make sure that all command lists allocated from here and
  // in the command queue have been executed before resetting the allocator.
  ThrowIfFailed(mDirectCmdListAlloc->Reset());
  ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

  mCommandList->RSSetViewports(1, &mScreenViewport);
  mCommandList->RSSetScissorRects(1, &mScissorRect);

  mCommandList->ResourceBarrier(1, 
    &CD3DX12_RESOURCE_BARRIER::Transition(
      CurrentBackBuffer(),
      D3D12_RESOURCE_STATE_PRESENT,
      D3D12_RESOURCE_STATE_RENDER_TARGET
    )
  );

  // The depth and stencil tests occure in the output merger stage. Blending
  // is also implemented in that stage: blend the output color of the pixel shader
  // for the corresponding fragment with the color currently held by that pixel
  // in the back buffer.
  mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
  mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
  mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

  ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get()};
  mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

  // The root signature describes the inputs and outputs of the shaders.
  mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

  mCommandList->IASetVertexBuffers(0, 1, &mBoxGeo->VertexBufferView());
  mCommandList->IASetIndexBuffer(&mBoxGeo->IndexBufferView());
  mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

  // The actual draw command.
  mCommandList->DrawIndexedInstanced(mBoxGeo->DrawArgs["box"].IndexCount, 1, 0, 0, 0);

  // State transition to present the back buffer to the screen.
  mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
    CurrentBackBuffer(),
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_PRESENT
  ));

  // Now's the GPU's turn.
  ThrowIfFailed(mCommandList->Close());
  ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
  mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

  // Present to the screen the back buffer.
  ThrowIfFailed(mSwapChain->Present(0, 0));
  mCurrentBackBuffer = (mCurrentBackBuffer + 1) % SwapChainBufferCount;

  // Block until the GPU is done processing the command queue.
  FlushCommandQueue();
}

bool DrawingApp::Initialize() {
  if (!D3DApp::Initialize()) {
    return false;
  }

  ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

  BuildDescriptorHeaps();
  BuildConstantBuffers();
  BuildRootSignature();
  BuildShadersAndInputLayout();
  BuildBoxGeometry();
  BuildPSO();

  ThrowIfFailed(mCommandList->Close());
  ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
  mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

  // Block until the GPU processes all of the commands.
  FlushCommandQueue();

  return true;
}

void DrawingApp::Update(const GameTimer& gt) {
  // Convert the world space spherical coordinates of the camera to cartesian.
  float x = mRadius * sinf(mPhi) * cosf(mTheta);
  float y = mRadius * cosf(mPhi);
  float z = mRadius * sinf(mPhi) * sinf(mTheta);

  // Position, target, and a up vector are enough to establish an orthonormal
  // basis for view space, out of which a view transformation matrix can be
  // obtained.
  XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
  // The camera's target is always the origin of world space.
  XMVECTOR target = XMVectorZero();
  XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

  // View matrix.
  XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
  XMStoreFloat4x4(&mView, view);

  XMMATRIX world = XMLoadFloat4x4(&mWorld);
  XMMATRIX proj = XMLoadFloat4x4(&mProj);
  // The * operator has left-to-right associativity in C++, so this composite
  // transformation is PVW. (I'm used to evaluating matrix multiplications from
  // right to left; that's why PVW is world * view * proj.)
  XMMATRIX worldViewProj = world * view * proj;

  ObjectConstants objConstants;
  XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
  mObjectCB->CopyData(0, objConstants);
}

void DrawingApp::OnResize() {
  D3DApp::OnResize();

  XMMATRIX P = XMMatrixPerspectiveFovLH(
    // Vertical FOV angle.
    0.25f * Math::Pi,
    // Obtains the aspect ratio from the window's current width and height.
    AspectRatio(),
    // Near plane.
    1.0f,
    // Far plane.
    1000.0f
  );

  XMStoreFloat4x4(&mProj, P);
}

int WINAPI WinMain(
  HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd
) {
  try {
    DrawingApp theApp(hInstance);
    if (!theApp.Initialize()) {
      return 0;
    }

    return theApp.Run();
  }
  catch (DxException& e) {
    MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
    return 0;
  }
}