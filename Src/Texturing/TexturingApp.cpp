#include "../Common/d3dApp.h"
#include "../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include <DirectXMath.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

const int gNumFrameResources = 3;

struct RenderItem {
  XMFLOAT4X4 World = Math::Identity4x4();

  int NumFramesDirty = gNumFrameResources;

  // Index into the object constant buffer where this render item's data is passed to the pipeline.
  UINT ObjCBIndex = -1;

  // A pointer because multople render items may use the same material.
  Material* Mat = nullptr;
  
  MeshGeometry* Geo = nullptr;

  // The D3D12 version of TRIANGLELIST doesn't seem to exist.
  D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

  UINT IndexCount = 0;
  UINT StartIndexLocation = 0;
  int BaseVertexLocation = 0;
};

class TexturingApp : public D3DApp {
private:
  std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
  std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
  std::vector<std::unique_ptr<FrameResource>> mFrameResources;
  FrameResource *mCurrFrameResource = nullptr;
  int mCurrFrameResourceIndex = 0;
  PassConstants mMainPassCB;

  // All render items.
  std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;
  // This app only needs 1 render layer because all the render items can be drawn with the same
  // PSO.
  std::vector<RenderItem*> mOpaqueRenderItems;

  // Eye.
  XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
  XMFLOAT4X4 mView = Math::Identity4x4();
  XMFLOAT4X4 mProj = Math::Identity4x4();
  // Polar angle.
  float mTheta = 1.5 * XM_PI;
  // Colatitude.
  float mPhi = 0.2f * XM_PI;
  float mRadius = 15.0f;

  // Pipeline configuration.
  ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
  std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
  ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;
  std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

  POINT mLastMousePos;

public:
  TexturingApp(HINSTANCE hInstance) : D3DApp(hInstance) {}
  ~TexturingApp() { 
    if (md3dDevice != nullptr) {
      FlushCommandQueue();
    }
  }
  virtual bool Initialize() override;

private:
  void BuildMaterials();
  void UpdateMaterialCBs(const GameTimer &gt);
  void DrawRenderItems(ID3D12GraphicsCommandList *cmdList, const std::vector<RenderItem *> &ritems);
  void BuildRenderItems();
  void UpdateObjectCBs(const GameTimer &gt);
  void BuildFrameResources();
  void UpdateCamera(const GameTimer &gt);
  void BuildRootSignature();
  void BuildShadersAndInputLayout();
  void BuildPSOs();
  virtual void OnResize() override;
  void Draw(const GameTimer& gt);
  void UpdateMainPassCB(const GameTimer &gt);
  void BuildShapeGeometry();
  virtual void Update(const GameTimer& gt) override;
  virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
  virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
  virtual void OnMouseMove(WPARAM btnState, int x, int y) override;
};

void TexturingApp::BuildMaterials() {
  auto brick = std::make_unique<Material>();
  brick->Name = "brick";
  brick->MatCBIndex = 0;
  brick->DiffuseAlbedo = XMFLOAT4(Colors::ForestGreen);
  brick->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
  // Relatively smooth.
  brick->Roughness = 0.1f;

  auto stone = std::make_unique<Material>();
  stone->Name = "stone";
  stone->MatCBIndex = 1;
  stone->DiffuseAlbedo = XMFLOAT4(Colors::LightSteelBlue);
  stone->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
  // Rougher than brick.
  stone->Roughness = 0.3f;

  auto tile = std::make_unique<Material>();
  tile->Name = "tile";
  tile->MatCBIndex = 2;
  tile->DiffuseAlbedo = XMFLOAT4(Colors::LightGray);
  tile->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
  // Rougher than brick, but smoother than stone.
  tile->Roughness = 0.2f;

  mMaterials["brick"] = std::move(brick);
  mMaterials["stone"] = std::move(stone);
  mMaterials["tile"] = std::move(tile);
}

void TexturingApp::UpdateMaterialCBs(const GameTimer& gt) {
  auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
  for (auto& e : mMaterials) {
    Material* mat = e.second.get();
    if (mat->NumFramesDirty > 0) {
      // The application changed the material. Update the copies stored in constant
      // buffers of each of the frame resources.
      MaterialConstants matConstants;
      matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
      matConstants.FresnelR0 = mat->FresnelR0;
      matConstants.Roughness = mat->Roughness;

      currMaterialCB->CopyData(mat->MatCBIndex, matConstants);
      mat->NumFramesDirty--;
    }
  }
}

void TexturingApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems) {
  UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
  UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

  auto objectCB = mCurrFrameResource->ObjectCB->Resource();
  auto matCB = mCurrFrameResource->MaterialCB->Resource();

  for (size_t i = 0; i < ritems.size(); ++i) {
    auto ri = ritems[i];
    auto vbv = ri->Geo->VertexBufferView();
    cmdList->IASetVertexBuffers(0, 1, &vbv);
    auto ibv = ri->Geo->IndexBufferView();
    cmdList->IASetIndexBuffer(&ibv);
    cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

    D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
    // The constant buffer index where the material goes is not stored directly in the
    // RenderItem because it may be shared among many.
    D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;
  
    cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
    cmdList->SetGraphicsRootConstantBufferView(1, matCBAddress);
    cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
  }
}

void TexturingApp::BuildRenderItems()
{
  auto boxRitem = std::make_unique<RenderItem>();
  XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
  boxRitem->ObjCBIndex = 0;
  boxRitem->Mat = mMaterials["stone"].get();
  boxRitem->Geo = mGeometries["shapeGeo"].get();
  boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
  boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
  boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
  mOpaqueRenderItems.push_back(boxRitem.get());
  mAllRenderItems.push_back(std::move(boxRitem));

  auto gridRitem = std::make_unique<RenderItem>();
  gridRitem->World = Math::Identity4x4();
  gridRitem->ObjCBIndex = 1;
  gridRitem->Mat = mMaterials["tile"].get();
  gridRitem->Geo = mGeometries["shapeGeo"].get();
  gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
  gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
  gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
  mOpaqueRenderItems.push_back(gridRitem.get());
  mAllRenderItems.push_back(std::move(gridRitem));

  UINT objCBIndex = 2;
  for (int i = 0; i < 5; ++i) {
    auto leftCylRitem = std::make_unique<RenderItem>();
    auto rightCylRitem = std::make_unique<RenderItem>();
    auto leftSphereRitem = std::make_unique<RenderItem>();
    auto rightSphereRitem = std::make_unique<RenderItem>();

    XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
    XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

    XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
    XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

    XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
    leftCylRitem->ObjCBIndex = objCBIndex++;
    leftCylRitem->Mat = mMaterials["brick"].get();
    leftCylRitem->Geo = mGeometries["shapeGeo"].get();
    leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
    leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
    leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
    mOpaqueRenderItems.push_back(leftCylRitem.get());
    mAllRenderItems.push_back(std::move(leftCylRitem));

    XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
    rightCylRitem->ObjCBIndex = objCBIndex++;
    rightCylRitem->Mat = mMaterials["brick"].get();
    rightCylRitem->Geo = mGeometries["shapeGeo"].get();
    rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
    rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
    rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
    mOpaqueRenderItems.push_back(rightCylRitem.get());
    mAllRenderItems.push_back(std::move(rightCylRitem));

    XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
    leftSphereRitem->ObjCBIndex = objCBIndex++;
    leftSphereRitem->Mat = mMaterials["stone"].get();
    leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
    leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
    leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
    mOpaqueRenderItems.push_back(leftSphereRitem.get());
    mAllRenderItems.push_back(std::move(leftSphereRitem));

    XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
    rightSphereRitem->ObjCBIndex = objCBIndex++;
    rightSphereRitem->Mat = mMaterials["stone"].get();
    rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
    rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
    rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
    mOpaqueRenderItems.push_back(rightSphereRitem.get());
    mAllRenderItems.push_back(std::move(rightSphereRitem));
  }
}

void TexturingApp::UpdateObjectCBs(const GameTimer& gt) {
  auto currObjectCB = mCurrFrameResource->ObjectCB.get();
  // Update in the constant buffers the world matrices of objects that changed.
  for (auto& renderItem : mAllRenderItems) {
    if (renderItem->NumFramesDirty > 0) {
      XMMATRIX world = XMLoadFloat4x4(&renderItem->World);
      ObjectConstants objConstants;
      XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
      // Copy to upload buffer for eventual transfer to the constant buffer.
      currObjectCB->CopyData(renderItem->ObjCBIndex, objConstants);
      renderItem->NumFramesDirty--;
    }
  }
}

void TexturingApp::BuildFrameResources() {
  for (int i = 0; i < gNumFrameResources; ++i) {
    mFrameResources.push_back(std::make_unique<FrameResource>(
      md3dDevice.Get(),
      // 1 pass.
      1,
      (UINT) mAllRenderItems.size(),
      (UINT) mMaterials.size()
    ));
  }
}

void TexturingApp::UpdateCamera(const GameTimer& gt) {
  // Rho is spherical radius.
  // Polar radius r = rho * sin(phi)
  // x = r * cos(theta)
  // y = r * sin(theta)
  mEyePos.x = (mRadius * sinf(mPhi)) * cosf(mTheta);
  mEyePos.y = (mRadius * sinf(mPhi)) * sinf(mTheta);
  mEyePos.z = mRadius * cosf(mPhi);

  // Orthonormal basis of view space.
  XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
  // Look at the world space origin.
  XMVECTOR target = XMVectorZero();
  XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

  // View matrix.
  XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
  XMStoreFloat4x4(&mView, view);
}

// The root signature is an array of root parameters that describe the resources that the
// application will bind to the pipeline and that shaders will access on the next and 
// subsequent draw calls.
void TexturingApp::BuildRootSignature() {
  CD3DX12_ROOT_PARAMETER slotRootParameter[3];
  // Argument is shader register number.
  slotRootParameter[0].InitAsConstantBufferView(0);
  slotRootParameter[1].InitAsConstantBufferView(1);
  // For pass constant buffer; see Draw().
  slotRootParameter[2].InitAsConstantBufferView(2);

  CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
    // 3 root parameters.
    3,
    slotRootParameter,
    0,
    nullptr,
    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
  );

  ComPtr<ID3DBlob> serializedRootSig = nullptr;
  ComPtr<ID3DBlob> errorBlob = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(
    &rootSigDesc,
    D3D_ROOT_SIGNATURE_VERSION_1,
    serializedRootSig.GetAddressOf(),
    errorBlob.GetAddressOf()
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

void TexturingApp::BuildShadersAndInputLayout() {
  mShaders["standardVS"] = d3dUtil::CompileShader(L"Src/Texturing/Texturing.hlsl", nullptr, "VS", "vs_5_1");
  mShaders["opaquePS"] = d3dUtil::CompileShader(L"Src/Texturing/Texturing.hlsl", nullptr, "PS", "ps_5_1");

  mInputLayout = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
  };
}

void TexturingApp::BuildPSOs() {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
  ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
  opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT) mInputLayout.size() };
  opaquePsoDesc.pRootSignature = mRootSignature.Get();
  opaquePsoDesc.VS = {
    reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
    mShaders["standardVS"]->GetBufferSize()
  };
  opaquePsoDesc.PS = {
    reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
    mShaders["opaquePS"]->GetBufferSize()
  };
  opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  opaquePsoDesc.SampleMask = UINT_MAX;
  opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  opaquePsoDesc.NumRenderTargets = 1;
  opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
  opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
  opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
  opaquePsoDesc.DSVFormat = mDepthStencilFormat;

  ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
    &opaquePsoDesc, IID_PPV_ARGS(&mOpaquePSO)
  ));
}

void TexturingApp::OnResize() {
  D3DApp::OnResize();
  // LH stands for left-handed.
  XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * Math::Pi, AspectRatio(), 1.0f, 1000.0f);
  XMStoreFloat4x4(&mProj, P);
}

void TexturingApp::Draw(const GameTimer &gt) {
  // The resources of the current frame resource, including the command list, are no longer
  // in the command queue and can be reclaimed and reused by the app.
  auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
  ThrowIfFailed(cmdListAlloc->Reset());
  ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mOpaquePSO.Get()));

  mCommandList->RSSetViewports(1, &mScreenViewport);
  mCommandList->RSSetScissorRects(1, &mScissorRect);

  auto renderTargetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
    CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
  );
  mCommandList->ResourceBarrier(
    1,
    &renderTargetBarrier
  );

  mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
  mCommandList->ClearDepthStencilView(
    DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr
  );
  auto cbbv = CurrentBackBufferView();
  auto dsv = DepthStencilView();
  mCommandList->OMSetRenderTargets(1, &cbbv, true, &dsv);
  
  mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

  auto passCB = mCurrFrameResource->PassCB->Resource();
  // 2 is the root parameter index of the pass constant buffer.
  mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

  // Use the current frame resource's command list for drawing objects.
  DrawRenderItems(mCommandList.Get(), mOpaqueRenderItems);

  // Drawing commands with the current back buffer as render target have been queued already.
  // Insert command to present it to the screen.
  auto presentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
    CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT
  );
  mCommandList->ResourceBarrier(
    1,
    &presentBarrier
  );

  ThrowIfFailed(mCommandList->Close());

  ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
  mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

  ThrowIfFailed(mSwapChain->Present(0, 0));

  mCurrentBackBuffer = (mCurrentBackBuffer + 1) % SwapChainBufferCount;
  mCurrFrameResource->Fence = ++mCurrentFence;
  mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TexturingApp::UpdateMainPassCB(const GameTimer& gt) {
  XMMATRIX view = XMLoadFloat4x4(&mView);
  XMMATRIX proj = XMLoadFloat4x4(&mProj);
  XMMATRIX viewProj = XMMatrixMultiply(view, proj);
  auto viewDeterminant = XMMatrixDeterminant(view);
  XMMATRIX invView = XMMatrixInverse(&viewDeterminant, view);
  auto projDeterminant = XMMatrixDeterminant(proj);
  XMMATRIX invProj = XMMatrixInverse(&projDeterminant, proj);
  auto viewProjDeterminant = XMMatrixDeterminant(viewProj);
  XMMATRIX invViewProj = XMMatrixInverse(&viewProjDeterminant, viewProj);

  XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
  XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
  XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
  XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
  XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
  XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
  mMainPassCB.EyePosW = mEyePos;
  mMainPassCB.RenderTargetSize = XMFLOAT2((float) mClientWidth, (float) mClientHeight);
  mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.f/mClientWidth, 1.f/mClientHeight);
  mMainPassCB.NearZ = 1.0f;
  mMainPassCB.FarZ = 1000.0f;
  mMainPassCB.TotalTime = gt.TotalTime();
  mMainPassCB.DeltaTime = gt.DeltaTime();

  // Lighting.
  mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
  // 3 directional lights.
  mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
  mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
  mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
  mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
  mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
  mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

  // Copy updated main pass data to upload buffer for eventual transfer to contant buffer.
  auto currPassCB = mCurrFrameResource->PassCB.get();
  currPassCB->CopyData(0, mMainPassCB);
}

void TexturingApp::BuildShapeGeometry() {
  GeometryGenerator geoGen;
  GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
  GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
  GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
  GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

  UINT boxVertexOffset = 0;
  UINT gridVertexOffset = (UINT) box.Vertices.size();
  UINT sphereVertexOffset = gridVertexOffset + (UINT) grid.Vertices.size();
  UINT cylinderVertexOffset = sphereVertexOffset + (UINT) sphere.Vertices.size();

  UINT boxIndexOffset = 0;
  UINT gridIndexOffset = (UINT) box.Indices32.size();
  UINT sphereIndexOffset = gridIndexOffset + (UINT) grid.Indices32.size();
  UINT cylinderIndexOffset = sphereIndexOffset + (UINT) sphere.Indices32.size();

  SubmeshGeometry boxSubmesh;
  boxSubmesh.IndexCount = (UINT) box.Indices32.size();
  boxSubmesh.StartIndexLocation = boxIndexOffset;
  boxSubmesh.BaseVertexLocation = boxVertexOffset;

  SubmeshGeometry gridSubmesh;
  gridSubmesh.IndexCount = (UINT) grid.Indices32.size();
  gridSubmesh.StartIndexLocation = gridIndexOffset;
  gridSubmesh.BaseVertexLocation = gridVertexOffset;

  SubmeshGeometry sphereSubmesh;
  sphereSubmesh.IndexCount = (UINT) sphere.Indices32.size();
  sphereSubmesh.StartIndexLocation = sphereIndexOffset;
  sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

  SubmeshGeometry cylinderSubmesh;
  cylinderSubmesh.IndexCount = (UINT) cylinder.Indices32.size();
  cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
  cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

  auto totalVertexCount =
    box.Vertices.size() +
    grid.Vertices.size() +
    sphere.Vertices.size() +
    cylinder.Vertices.size();

  std::vector<Vertex> vertices(totalVertexCount);

  UINT k = 0;
  for (size_t i = 0; i < box.Vertices.size(); ++i, ++k) {
    vertices[k].Pos = box.Vertices[i].Position;
    vertices[k].Normal = box.Vertices[i].Normal;
  }

  for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k) {
    vertices[k].Pos = grid.Vertices[i].Position;
    vertices[k].Normal = grid.Vertices[i].Normal;
  }

  for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k) {
    vertices[k].Pos = sphere.Vertices[i].Position;
    vertices[k].Normal = sphere.Vertices[i].Normal;
  }

  for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k) {
    vertices[k].Pos = cylinder.Vertices[i].Position;
    vertices[k].Normal = cylinder.Vertices[i].Normal;
  }

  std::vector<std::uint16_t> indices;
  indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
  indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
  indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
  indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

  const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
  const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

  auto geo = std::make_unique<MeshGeometry>();
  geo->Name = "shapeGeo";

  ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
  CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

  ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
  CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

  geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
    md3dDevice.Get(),
    mCommandList.Get(),
    vertices.data(),
    vbByteSize,
    geo->VertexBufferUploader
  );

  geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
    md3dDevice.Get(),
    mCommandList.Get(),
    indices.data(),
    ibByteSize,
    geo->IndexBufferUploader
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

bool TexturingApp::Initialize() {
  if (!D3DApp::Initialize()) {
    return false;
  }

  ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

  // Query the driver for the increment size of a descriptor of the CbvSrvUav heap.
  mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  BuildRootSignature();
  BuildShadersAndInputLayout();
  BuildShapeGeometry();
  BuildMaterials();
  // Render items associate geometry instances and materials.
  BuildRenderItems();
  BuildFrameResources();
  BuildPSOs();

  ThrowIfFailed(mCommandList->Close());
  ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
  mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
  // Blocks.
  FlushCommandQueue();

  return true;
}

void TexturingApp::Update(const GameTimer &gt) {
  UpdateCamera(gt);

  // Move to the next frame's resources.
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
  UpdateMaterialCBs(gt);
  UpdateMainPassCB(gt);
}

void TexturingApp::OnMouseDown(WPARAM btnState, int x, int y) {
  mLastMousePos.x = x;
  mLastMousePos.y = y;
  SetCapture(mhMainWnd);
}

void TexturingApp::OnMouseUp(WPARAM btnState, int x, int y) {
  ReleaseCapture();
}

void TexturingApp::OnMouseMove(WPARAM btnState, int x, int y) {
  if ((btnState & MK_LBUTTON) != 0) {
    // Rotate the camera.

    // 1 pixel change in the x direction corresponds to 1/4 of a degree.
    float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
    // 1 pixel change in the y direction corresponds to 1/4 of a degree.
    float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

    mTheta += dx;
    mPhi += dy;

    // Maximum colatitude angle is PI.
    mPhi = Math::Clamp(mPhi, 0.1f, Math::Pi - 0.1f);
  } else if ((btnState & MK_RBUTTON) != 0) {
    // Pan the camera.

    float dx = 0.5f * static_cast<float>(x - mLastMousePos.x);
    float dy = 0.5f * static_cast<float>(y - mLastMousePos.y);

    mRadius += dx - dy;
    mRadius = Math::Clamp(mRadius, 5.0f, 150.0f);
  }

  mLastMousePos.x = x;
  mLastMousePos.y = y;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd) {
  try {
    TexturingApp app(hInstance);

    if (!app.Initialize()) {
      return 0;
    }

    return app.Run();
  } catch (DxException& e) {
    MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
    return 0;
  }
}