#include "../Common/d3dApp.h"
#include "FrameResource.h"
#include < DirectXMath.h >

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

// This app only needs 1 render layer because all the render items can be drawn with the same
// PSO.
enum class RenderLayer : int {
  Opaque = 0,
  Count
};

class LightingAndMaterialsApp : public D3DApp {
private:
  std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
  std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
  std::vector<std::unique_ptr<FrameResource>> mFrameResources;
  FrameResource *mCurrFrameResource = nullptr;
  int mCurrFrameResourceIndex = 0;
  PassConstants mMainPassCB;

  // All render items.
  std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;
  // Render items organized by layer, because they might need different PSOs
  // (although this app only needs one).
  std::vector<RenderItem*> mRenderItemLayer[(int) RenderLayer::Count];
  RenderItem* mWavesRenderItem = nullptr;
  std::unique_ptr<Waves> mWaves;

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
  std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
  std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

public:
  LightingAndMaterialsApp(HINSTANCE hInstance) : D3DApp(hInstance) {}
  ~LightingAndMaterialsApp() { 
    if (md3dDevice != nullptr) {
      FlushCommandQueue();
    }
  }

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
};

void LightingAndMaterialsApp::BuildMaterials() {
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

void LightingAndMaterialsApp::UpdateMaterialCBs(const GameTimer& gt) {
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

void LightingAndMaterialsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems) {
  UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
  UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

  auto objectCB = mCurrFrameResource->ObjectCB->Resource();
  auto matCB = mCurrFrameResource->MaterialCB->Resource();

  for (size_t i = 0; i < ritems.size(); ++i) {
    auto ri = ritems[i];
    cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
    cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
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

void LightingAndMaterialsApp::BuildRenderItems()
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
  mRenderItemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
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
  mRenderItemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
  mAllRenderItems.push_back(std::move(gridRitem));

  UINT objCBIndex = 3;
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
    mRenderItemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
    mAllRenderItems.push_back(std::move(leftCylRitem));

    XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
    rightCylRitem->ObjCBIndex = objCBIndex++;
    rightCylRitem->Mat = mMaterials["brick"].get();
    rightCylRitem->Geo = mGeometries["shapeGeo"].get();
    rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
    rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
    rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
    mRenderItemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
    mAllRenderItems.push_back(std::move(rightCylRitem));

    XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
    leftSphereRitem->ObjCBIndex = objCBIndex++;
    leftSphereRitem->Mat = mMaterials["stone"].get();
    leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
    leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
    leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
    mRenderItemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());
    mAllRenderItems.push_back(std::move(leftSphereRitem));

    XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
    rightSphereRitem->ObjCBIndex = objCBIndex++;
    rightSphereRitem->Mat = mMaterials["stone"].get();
    rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
    rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
    rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
    mRenderItemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());
    mAllRenderItems.push_back(std::move(rightSphereRitem));
  }
}

void LightingAndMaterialsApp::UpdateObjectCBs(const GameTimer& gt) {
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

void LightingAndMaterialsApp::BuildFrameResources() {
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

void LightingAndMaterialsApp::UpdateCamera(const GameTimer& gt) {
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
void LightingAndMaterialsApp::BuildRootSignature() {
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

void LightingAndMaterialsApp::BuildShadersAndInputLayout() {
  mShaders["standardVS"] = d3dUtil::CompileShader(L"Src/LightingAndMaterials/LightingAndMaterials.hlsl", nullptr, "VS", "vs_5_0");
  mShaders["opaquePS"] = d3dUtil::CompileShader(L"Src/LightingAndMaterials/LightingAndMaterials.hlsl", nullptr, "PS", "ps_5_0");

  mInputLayout = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
  };
}

void LightingAndMaterialsApp::BuildPSOs() {
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
  opaquePsoDesc.SampleDesc.Count = m4xMsaaQuality ? 4 : 1;
  opaquePsoDesc.SampleDesc.Quality = m4xMsaaQuality ? (m4xMsaaQuality - 1) : 0;
  opaquePsoDesc.DSVFormat = mDepthStencilFormat;

  ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
    &opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])
  ));
}

void LightingAndMaterialsApp::OnResize() {
  D3DApp::OnResize();
  // LH stands for left-handed.
  XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * Math::Pi, AspectRatio(), 1.0f, 1000.0f);
  XMStoreFloat4x4(&mProj, P);
}

void LightingAndMaterialsApp::Draw(const GameTimer &gt) {
  // The resources of the current frame resource, including the command list, are no longer
  // in the command queue and can be reclaimed and reused by the app.
  auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
  ThrowIfFailed(cmdListAlloc->Reset());
  ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

  mCommandList->RSSetViewports(1, &mScreenViewport);
  mCommandList->RSSetScissorRects(1, &mScissorRect);

  mCommandList->ResourceBarrier(
    1,
    &CD3DX12_RESOURCE_BARRIER::Transition(
      CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
    )
  );

  mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
  mCommandList->ClearDepthStencilView(
    DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr
  );
  mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());
  
  mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

  auto passCB = mCurrFrameResource->PassCB->Resource();
  // 2 is the root parameter index of the pass constant buffer.
  mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

  // Use the current frame resource's command list for drawing objects.
  DrawRenderItems(mCommandList.Get(), mRenderItemLayer[(int) RenderLayer::Opaque]);

  // Drawing commands with the current back buffer as render target have been queued already.
  // Insert command to present it to the screen.
  mCommandList->ResourceBarrier(
    1,
    &CD3DX12_RESOURCE_BARRIER::Transition(
      CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT
    )
  );

  ThrowIfFailed(mCommandList->Close());

  ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
  mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

  ThrowIfFailed(mSwapChain->Present(0, 0));
  mCurrentBackBuffer = (mCurrentBackBuffer + 1) % SwapChainBufferCount;
  mCurrFrameResource->Fence = ++mCurrentFence;
  mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void LightingAndMaterialsApp::UpdateMainPassCB(const GameTimer& gt) {
  XMMATRIX view = XMLoadFloat4x4(&mView);
  XMMATRIX proj = XMLoadFloat4x4(&mProj);
  XMMATRIX viewProj = XMMatrixMultiply(view, proj);
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