#include "../Common/d3dApp.h"
#include "../Common/Math.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

// Work on 3 frames at a time. Maintain frame resources for 3.
const int gNumFrameResources = 3;

struct RenderItem {
  RenderItem() = default;

  XMFLOAT4X4 World = Math::Identity4x4();

  int NumFramesDirty = gNumFrameResources;

  UINT ObjCBIndex = -1;

  MeshGeometry* Geo = nullptr;

  D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

  // Since we keep vertices of different objects in the same vertex buffer,
  // we need to mark the start of this object's vertices and indices.
  UINT IndexCount = 0;
  UINT StartIndexLocation = 0;
  int BaseVertexLocation = 0;
};

class FrameResourcesApp : public D3DApp {
private:
  // This app maintains 3 frame resources.
  std::vector<std::unique_ptr<FrameResource>> mFrameResources;
  FrameResource* mCurrFrameResource = nullptr;
  int mCurrFrameResourceIndex = 0;
  std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;
  // TODO: ?
  std::vector<RenderItem *> mOpaqueRenderItems;
  XMFLOAT4X4 mView = Math::Identity4x4();
  XMFLOAT4X4 mProj = Math::Identity4x4();
  // Camera position in cartesion coordinates.
  XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
  // Camera position in spherical coordinates.
  float mTheta = 1.5f * XM_PI;
  float mPhi = 0.2f * XM_PI;
  float mRadius = 15.0f;
  // Data that applies to all draw calls and that doesn't depend on the object
  // being drawn.
  PassConstants mMainPassCB;
  // There are 2 root signature parameters (of type table), 1 for the constant buffer
  // of objects (i.e. render items, i.e. drawn instances) and 1 for the constant buffer
  // of the render pass. The object root signature parameter changes on every DrawIndexedInstanced() 
  // called made by DrawRenderItems() (which draws all objects, one after the other);
  // the render pass signature parameter changes on every Draw() call. Every Draw()
  // call makes one DrawRenderItems() call. The object root signature parameter changes
  // on every instance draw because the world matrix is different across instances. The
  // pass root signature parameter only changes once per frame because the view and projection
  // matrices (and other data) doesn't depend on individual instances.
  ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
  // Constant buffer view heap.
  //
  // There are 6 constant buffer resources, 2 per frame resource, 1 for objects and 
  // one for the render pass. Each buffer resource is referenced by multiple constant
  // buffer views, one per render item. Each drawn instance has a render item; each render
  // item has a constant buffer view in each of the frame resources. All of these views
  // come from this heap.
  ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;
  // 1 vertex and 1 index buffer for a number of different geometries.
  std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
  std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
  std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
  // Location of the constant buffer view of the render pass in the constant buffer
  // view heap. There are actually 3 views for the render pass, 1 per frame resource.
  // The first one of them is at this offset; the rest follow the first one.
  UINT mPassCbvOffset = 0;
  // 2 PSOs, 1 for drawing opaque objects, and 1 for drawing wireframes. They differ only
  // in RasterizerState.FillMode: D3D11_FILL_SOLID and D3D12_FILL_MODE_WIREFRAME.
  std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
  bool mIsWireframe = false;

public:
  FrameResourcesApp(HINSTANCE hInstance) : D3DApp(hInstance) {}
  ~FrameResourcesApp() {
    if (md3dDevice != nullptr) {
      FlushCommandQueue();
    }
  }
  bool Initialize();

private:
  // Called on every Update() (which is where the application transitions to
  // the next frame resource). Since the data in the next frame resource is 2 frames
  // behind the current one, it needs to get up to date.
  void UpdateObjectCBs(const GameTimer &gt);
  // Updates the pass constants (e.g. view and projection matrices) and sets
  // them in the current frame resource. See UpdateObjectCBs() too.
  void UpdateMainPassCB(const GameTimer &gt);
  void BuildRootSignature();
  void BuildShapeGeometry();
  void BuildShadersAndInputLayout();
  void BuildFrameResources();
  void BuildRenderItems();
  void DrawRenderItems(ID3D12GraphicsCommandList *cmdList, const std::vector<RenderItem *> &renderItems);
  void BuildDescriptorHeaps();
  void BuildConstantBufferViews();
  void BuildPSOs();
  virtual void Draw(const GameTimer &gt) override;
  // Moves the application on to the next frame resource.
  virtual void Update(const GameTimer &gt) override;
  void OnKeyboardInput(const GameTimer &gt);
  void UpdateCamera(const GameTimer &gt);
  virtual void OnResize() override;
};

void FrameResourcesApp::UpdateObjectCBs(const GameTimer& gt) {
  auto currObjectCB = mCurrFrameResource->ObjectCB.get();

  for (auto& e : mAllRenderItems) {
    if (e->NumFramesDirty > 0) {
      // The object whose render item this is has changed. Update its
      // associated data.
      XMMATRIX world = XMLoadFloat4x4(&e->World);
      ObjectConstants objConstants;
      // TODO: why the transpose?
      XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
      currObjectCB->CopyData(e->ObjCBIndex, objConstants);
      e->NumFramesDirty--;
    }
  }
}

void FrameResourcesApp::UpdateMainPassCB(const GameTimer& gt) {
  XMMATRIX view = XMLoadFloat4x4(&mView);
  XMMATRIX proj = XMLoadFloat4x4(&mProj);

  XMMATRIX viewProj = XMMatrixMultiply(view, proj);
  // TODO: does XMMatrixInverse return the determinant of the given matrix in the
  // first pointer input? Or does it receive it? It looks like XMMatrixDeterminant
  // is the one that computes it, not XMMatrixInverse. Either way, the first argument
  // is NOT optional.
  XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
  XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
  XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

  XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
  XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
  XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
  XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
  XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
  XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
  mMainPassCB.EyePosW = mEyePos;
  mMainPassCB.RenderTargetSize = XMFLOAT2((float) mClientWidth, (float) mClientHeight);
  mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
  mMainPassCB.NearZ = 1.0f;
  mMainPassCB.FarZ = 1.0f;
  mMainPassCB.TotalTime = gt.TotalTime();
  mMainPassCB.DeltaTime = gt.DeltaTime();

  auto currPassCB = mCurrFrameResource->PassCB.get();
  currPassCB->CopyData(0, mMainPassCB);
}

void FrameResourcesApp::BuildRootSignature() {
  // 2 constant buffer descriptor tables, one for the object constant buffer
  // and one for the pass constant buffer.

  CD3DX12_DESCRIPTOR_RANGE cbvTable0;
  // 1 descriptor, use shader register 0 (b0).
  cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

  CD3DX12_DESCRIPTOR_RANGE cbvTable1;
  // 1 descriptor, use shader register 1 (b1).
  cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

  CD3DX12_ROOT_PARAMETER slotRootParameter[2];
  slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
  slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

  CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
    2, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
  );

  ComPtr<ID3DBlob> serializedRootSig = nullptr;
  ComPtr<ID3DBlob> errorBlob = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(
    &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()
  );
  if (errorBlob != nullptr) {
    ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
  }
  ThrowIfFailed(hr);

  ThrowIfFailed(md3dDevice->CreateRootSignature(
    0,
    serializedRootSig->GetBufferPointer(),
    serializedRootSig->GetBufferSize(),
    IID_PPV_ARGS(mRootSignature.GetAddressOf())
  ));
}

void FrameResourcesApp::BuildShapeGeometry() {
  GeometryGenerator geoGen;
  GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
  GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
  GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
  GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

  // Vertex buffer offsets of each object.
  UINT boxVertexOffset = 0;
  UINT gridVertexOffset = (UINT)box.Vertices.size();
  UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
  UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

  // Index buffer offsets of each object.
  UINT boxIndexOffset = 0;
  UINT gridIndexOffset = (UINT)box.Indices32.size();
  UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
  UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

  // Location of box in vertex and index buffers.
  SubmeshGeometry boxSubmesh;
  boxSubmesh.IndexCount = (UINT)box.Indices32.size();
  boxSubmesh.StartIndexLocation = boxIndexOffset;
  boxSubmesh.BaseVertexLocation = boxVertexOffset;

  // Location of grid in vertex and index buffers.
  SubmeshGeometry gridSubmesh;
  gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
  gridSubmesh.StartIndexLocation = gridIndexOffset;
  gridSubmesh.BaseVertexLocation = gridVertexOffset;

  // Location of sphere in vertex and index buffers.
  SubmeshGeometry sphereSubmesh;
  sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
  sphereSubmesh.StartIndexLocation = sphereIndexOffset;
  sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

  // Location of cylinder in vertex and index buffers.
  SubmeshGeometry cylinderSubmesh;
  cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
  cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
  cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

  // Pack the vertices of all the geometries in a single vertex buffer.
  auto totalVertexCount = box.Vertices.size() + grid.Vertices.size() + sphere.Vertices.size() + cylinder.Vertices.size();
  std::vector<Vertex> vertices(totalVertexCount);

  UINT k = 0;
  for (size_t i = 0; i < box.Vertices.size(); ++i, ++k) {
    vertices[k].Pos = box.Vertices[i].Position;
    vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkGreen);
  }

  for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k) {
    vertices[k].Pos = grid.Vertices[i].Position;
    vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
  }

  for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k) {
    vertices[k].Pos = sphere.Vertices[i].Position;
    vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
  }

  for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k) {
    vertices[k].Pos = cylinder.Vertices[i].Position;
    vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
  }

  std::vector<std::uint16_t> indices;
  indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
  indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
  indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
  indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

  // Vertex and index buffers' total byte size.
  const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
  const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

  auto geo = std::make_unique<MeshGeometry>();
  geo->Name = "shapeGeo";

  // Create CPU-side buffers.
  ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
  CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
  ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
  CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

  // Create GPU-side buffers.
  geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
    md3dDevice.Get(), mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader
  );
  geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
    md3dDevice.Get(), mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader
  );

  geo->VertexByteStride = sizeof(Vertex);
  geo->VertexBufferByteSize = vbByteSize;
  geo->IndexFormat = DXGI_FORMAT_R16_UINT;
  geo->IndexBufferByteSize = ibByteSize;

  geo->DrawArgs["box"] = boxSubmesh;
  geo->DrawArgs["grid"] = gridSubmesh;
  geo->DrawArgs["sphere"] = sphereSubmesh;
  geo->DrawArgs["cylinder"] = cylinderSubmesh;

  mGeometries[geo->Name] = std::move(geo);
}

void FrameResourcesApp::BuildShadersAndInputLayout() {
  mShaders["standardVS"] = d3dUtil::CompileShader(L"Src/FrameResources/FrameResources.hlsl", nullptr, "VS", "vs_5_1");
  mShaders["opaquePS"] = d3dUtil::CompileShader(L"Src/FrameResources/FrameResources.hlsl", nullptr, "PS", "ps_5_1");

  // The Vertex struct is defined in FrameResource.h.
  mInputLayout = {
    // 0 is the offset into the 
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };
}

void FrameResourcesApp::BuildFrameResources() {
  for (int i = 0; i < gNumFrameResources; ++i) {
    mFrameResources.push_back(
      // 1 render pass, many render objects.
      std::make_unique<FrameResource>(md3dDevice.Get(), 1, (UINT) mAllRenderItems.size())
    );
  }
}

void FrameResourcesApp::BuildRenderItems() {
  // We'll make a draw call for each render item. Each render item corresponds to an
  // instance. Note that the mesh data of instances of the same shape is shared; what
  // varies is the world matrix.

  auto boxRenderItem = std::make_unique<RenderItem>();
  XMStoreFloat4x4(
    &boxRenderItem->World, 
    XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f)
  );
  boxRenderItem->ObjCBIndex = 0;
  boxRenderItem->Geo = mGeometries["shapeGeo"].get();
  boxRenderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  boxRenderItem->IndexCount = boxRenderItem->Geo->DrawArgs["box"].IndexCount;
  boxRenderItem->StartIndexLocation = boxRenderItem->Geo->DrawArgs["box"].StartIndexLocation;
  boxRenderItem->BaseVertexLocation = boxRenderItem->Geo->DrawArgs["box"].BaseVertexLocation;
  mAllRenderItems.push_back(std::move(boxRenderItem));

  auto gridRenderItem = std::make_unique<RenderItem>();
  gridRenderItem->World = Math::Identity4x4();
  gridRenderItem->ObjCBIndex = 1;
  gridRenderItem->Geo = mGeometries["shapeGeo"].get();
  gridRenderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  gridRenderItem->IndexCount = gridRenderItem->Geo->DrawArgs["grid"].IndexCount;
  gridRenderItem->StartIndexLocation = gridRenderItem->Geo->DrawArgs["grid"].StartIndexLocation;
  gridRenderItem->BaseVertexLocation = gridRenderItem->Geo->DrawArgs["grid"].BaseVertexLocation;
  mAllRenderItems.push_back(std::move(gridRenderItem));

  // 0 and 1 are boxRenderItem and gridRenderItem above.
  UINT objCBIndex = 2;
  for (int i = 0; i < 5; ++i) {
    auto leftCylRenderItem = std::make_unique<RenderItem>();
    auto rightCylRenderItem = std::make_unique<RenderItem>();
    auto leftSphereRenderItem = std::make_unique<RenderItem>();
    auto rightSphereRenderItem = std::make_unique<RenderItem>();

    XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
    XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);
    XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
    XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

    XMStoreFloat4x4(&leftCylRenderItem->World, leftCylWorld);
    leftCylRenderItem->ObjCBIndex = objCBIndex++;
    leftCylRenderItem->Geo = mGeometries["shapeGeo"].get();
    leftCylRenderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    leftCylRenderItem->IndexCount = leftCylRenderItem->Geo->DrawArgs["cylinder"].IndexCount;
    leftCylRenderItem->StartIndexLocation = leftCylRenderItem->Geo->DrawArgs["cylinder"].StartIndexLocation;
    leftCylRenderItem->BaseVertexLocation = leftCylRenderItem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

    XMStoreFloat4x4(&rightCylRenderItem->World, rightCylWorld);
    rightCylRenderItem->ObjCBIndex = objCBIndex++;
    rightCylRenderItem->Geo = mGeometries["shapeGeo"].get();
    rightCylRenderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    rightCylRenderItem->IndexCount = rightCylRenderItem->Geo->DrawArgs["cylinder"].IndexCount;
    rightCylRenderItem->StartIndexLocation = rightCylRenderItem->Geo->DrawArgs["cylinder"].StartIndexLocation;
    rightCylRenderItem->BaseVertexLocation = rightCylRenderItem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

    XMStoreFloat4x4(&leftSphereRenderItem->World, leftSphereWorld);
    leftSphereRenderItem->ObjCBIndex = objCBIndex++;
    leftSphereRenderItem->Geo = mGeometries["shapeGeo"].get();
    leftSphereRenderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    leftSphereRenderItem->IndexCount = leftSphereRenderItem->Geo->DrawArgs["sphere"].IndexCount;
    leftSphereRenderItem->StartIndexLocation = leftSphereRenderItem->Geo->DrawArgs["sphere"].StartIndexLocation;
    leftSphereRenderItem->BaseVertexLocation = leftSphereRenderItem->Geo->DrawArgs["sphere"].BaseVertexLocation;

    XMStoreFloat4x4(&rightSphereRenderItem->World, rightSphereWorld);
    rightSphereRenderItem->ObjCBIndex = objCBIndex++;
    rightSphereRenderItem->Geo = mGeometries["shapeGeo"].get();
    rightSphereRenderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    rightSphereRenderItem->IndexCount = rightSphereRenderItem->Geo->DrawArgs["sphere"].IndexCount;
    rightSphereRenderItem->StartIndexLocation = rightSphereRenderItem->Geo->DrawArgs["sphere"].StartIndexLocation;
    rightSphereRenderItem->BaseVertexLocation = rightSphereRenderItem->Geo->DrawArgs["sphere"].BaseVertexLocation;

    mAllRenderItems.push_back(std::move(leftCylRenderItem));
    mAllRenderItems.push_back(std::move(rightCylRenderItem));
    mAllRenderItems.push_back(std::move(leftSphereRenderItem));
    mAllRenderItems.push_back(std::move(rightSphereRenderItem));
  }

  for (auto& renderItem : mAllRenderItems) {
    mOpaqueRenderItems.push_back(renderItem.get());
  }
}

// The application uses 1 command list per frame resource. The input cmdList
// is allocated from the corresponding frame resource's command list allocator.
//
// Member variable mCurrFrameResourceIndex tells which of the 3 frame resources
// this is.
void FrameResourcesApp::DrawRenderItems(
  ID3D12GraphicsCommandList* cmdList,
  const std::vector<RenderItem*>& renderItems
) {
  UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
  auto objectCB = mCurrFrameResource->ObjectCB->Resource();

  for (size_t i = 0; i < renderItems.size(); ++i) {
    auto renderItem = renderItems[i];
    cmdList->IASetVertexBuffers(0, 1, &renderItem->Geo->VertexBufferView());
    cmdList->IASetIndexBuffer(&renderItem->Geo->IndexBufferView());
    cmdList->IASetPrimitiveTopology(renderItem->PrimitiveType);

    // The index into the constant buffer view heap where this render item's descriptor
    // is.
    UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRenderItems.size() + renderItem->ObjCBIndex;
    // CD3DX12_GPU_DESCRIPTOR_HANDLE is a struct. 
    auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    // Sets the internal offset of CD3DX12_GPU_DESCRIPTOR_HANDLE to the specified location,
    // where this render item's descriptor is.
    cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);

    // 1st root parameter is this render item's constant buffer descriptor.
    cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);
    
    // This instance's world matrix is in the constant buffer that was just set in the root
    // signature.
    cmdList->DrawIndexedInstanced(
      renderItem->IndexCount,
      // Draw 1 instance of this geometry (vertices + indices).
      1,
      renderItem->StartIndexLocation,
      renderItem->BaseVertexLocation,
      0
    );
  }
}

void FrameResourcesApp::BuildDescriptorHeaps() {
  // One render item per drawn instance.
  UINT objCount = (UINT)mOpaqueRenderItems.size();

  // One descriptor per render item per frame resource.
  // An additional per frame resource for the render pass.
  UINT numDescriptors = (objCount + 1) * gNumFrameResources;

  // Record the location of the first render pass constant buffer view.
  // The other render pass views follow the first one.
  mPassCbvOffset = objCount * gNumFrameResources;

  D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
  cbvHeapDesc.NumDescriptors = numDescriptors;
  cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  // Makes the descriptors accesible to shaders.
  cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  cbvHeapDesc.NodeMask = 0;
  ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
    &cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)
  ));
}

void FrameResourcesApp::BuildConstantBufferViews() {
  UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
  UINT objCount = (UINT)mOpaqueRenderItems.size();

  // There are 6 constant buffers, 2 per frame resource, 1 for object and 1 for the render pass.
  
  // Each constant buffer for objects (3 in total) stores data for all of the render items
  // (i.e. drawn instances). Constant buffer views for each render item are created for each
  // constant buffer.
  for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex) {
    auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();

    // Create a view for each render item for this constant buffer.
    for (UINT i = 0; i < objCount; ++i) {
      D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();
      cbAddress += i * objCBByteSize;

      int heapIndex = frameIndex * objCount + i;
      auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
      handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

      D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
      cbvDesc.BufferLocation = cbAddress;
      cbvDesc.SizeInBytes = objCBByteSize;

      md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
    }
  }

  UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

  // The last 3 views (1 per frame resource) are for the render pass.
  for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex) {
    auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();
    int heapIndex = mPassCbvOffset + frameIndex;
    auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
    handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = passCBByteSize;
    md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
  }
}

void FrameResourcesApp::BuildPSOs() {
  // 2 PSOs, 1 for drawing opaque objects, and 1 for drawing wireframes.
  // They differ only in RasterizerState.FillMode: D3D11_FILL_SOLID and D3D12_FILL_MODE_WIREFRAME.

  D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePSODesc;
  ZeroMemory(&opaquePSODesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
  opaquePSODesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
  opaquePSODesc.pRootSignature = mRootSignature.Get();
  opaquePSODesc.VS = {
    reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
    mShaders["standardVS"]->GetBufferSize()
  };
  opaquePSODesc.PS = {
    reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
    mShaders["opaquePS"]->GetBufferSize()
  };
  opaquePSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  opaquePSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  opaquePSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  opaquePSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  opaquePSODesc.SampleMask = UINT_MAX;
  opaquePSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  opaquePSODesc.NumRenderTargets = 1;
  opaquePSODesc.RTVFormats[0] = mBackBufferFormat;
  opaquePSODesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
  opaquePSODesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
  opaquePSODesc.DSVFormat = mDepthStencilFormat;
  ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePSODesc, IID_PPV_ARGS(&mPSOs["opaque"])));

  D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePSODesc = opaquePSODesc;
  opaqueWireframePSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
  ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePSODesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));
}

// Draws all the objects.
void FrameResourcesApp::Draw(const GameTimer& gt) {
  // Using a command list that isn't currently in the GPU's command queue allows the
  // application to get ahead and build/update the resources for the next frame without
  // waiting for the GPU to execute other command lists.
  auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
  ThrowIfFailed(cmdListAlloc->Reset());

  if (mIsWireframe) {
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
  } else {
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
  }

  mCommandList->RSSetViewports(1, &mScreenViewport);
  mCommandList->RSSetScissorRects(1, &mScissorRect);

  // Don't we need the swap chain to have more than 2 buffers if there are 3 frame
  // resources? No. The application may prepare 2 frame resources and then block while
  // the GPU is still using the frame resources of the frame currently being drawn, but
  // these are frame resources, not render targets. When the application blocks, 3 frame
  // resources will be present in the GPU command queue; when the GPU gets to the next
  // frame resource, it will also swap render targets; and when it gets to the last frame
  // resource, it will swap render targets again. See how the number of frame resources
  // doesn't have any influence on the swap chain?
  mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
    CurrentBackBuffer(),
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET
  ));

  mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
  mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

  // Output merger stage.
  mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

  ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
  mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

  mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

  // The pass root signature parameter changes once per frame (and not once per drawn
  // instance) because this data is independent of individual object instances.
  int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
  auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
  passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
  mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

  // Use the current frame resource's command list. The application guarantees that
  // this command list is not currently in the command queue. This allows the application
  // to get ahead and build/update the resources for the next frame(s).
  DrawRenderItems(mCommandList.Get(), mOpaqueRenderItems);

  // Prepare to present the drawn back buffer to the screen.
  mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
    CurrentBackBuffer(),
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_PRESENT
  ));

  ThrowIfFailed(mCommandList->Close());
  ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
  mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

  // Present the drawn back buffer to the screen.
  ThrowIfFailed(mSwapChain->Present(0, 0));
  mCurrentBackBuffer = (mCurrentBackBuffer + 1) % SwapChainBufferCount;

  // Add the fence to the command queue. The application doesn't block here; it
  // blocks in Update(), where the application transitions to the next frame resource.
  mCurrFrameResource->Fence = ++mCurrentFence;
  mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

// Moves the application on to the next frame resource.
void FrameResourcesApp::Update(const GameTimer& gt) {
  OnKeyboardInput(gt);
  UpdateCamera(gt);
  
  mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
  mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

  if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence) {
    // The frame resource we moved on to is still in the GPU command queue (it
    // follows that the other 2 frame resources are also in the queue, but behind it).
    // Block until the frame is completed.
    HANDLE eventHandle = CreateEventExW(nullptr, nullptr, false, EVENT_ALL_ACCESS);
    ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
    WaitForSingleObject(eventHandle, INFINITE);
    CloseHandle(eventHandle);
  }

  UpdateObjectCBs(gt);
  UpdateMainPassCB(gt);
}

void FrameResourcesApp::OnKeyboardInput(const GameTimer& gt)
{
  if (GetAsyncKeyState('1') & 0x8000) {
    mIsWireframe = true;
  } else {
    mIsWireframe = false;
  }
}

void FrameResourcesApp::UpdateCamera(const GameTimer& gt) {
  mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
  mEyePos.y = mRadius * cosf(mPhi);
  mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
  XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
  // Always look at the origin of world space.
  XMVECTOR target = XMVectorZero();
  XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
  XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
  XMStoreFloat4x4(&mView, view);
}

bool FrameResourcesApp::Initialize() {
  if (!D3DApp::Initialize()) {
    return false;
  }

  ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

  BuildRootSignature();
  BuildShadersAndInputLayout();
  BuildShapeGeometry();
  BuildRenderItems();
  BuildFrameResources();
  BuildDescriptorHeaps();
  BuildConstantBufferViews();
  BuildPSOs();

  ThrowIfFailed(mCommandList->Close());
  ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
  mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

  // Block.
  FlushCommandQueue();

  return true;
}

void FrameResourcesApp::OnResize() {
  D3DApp::OnResize();
  XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * Math::Pi, AspectRatio(), 1.0f, 1000.0f);
  XMStoreFloat4x4(&mProj, P);
}

int WINAPI WinMain(
  HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd
) {
#if defined(DEBUG) | defined(_DEBUG)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  try {
    FrameResourcesApp theApp(hInstance);
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