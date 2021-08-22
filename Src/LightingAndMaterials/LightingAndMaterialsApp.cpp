#include "../Common/d3dApp.h"
#include "FrameResource.h"
#include < DirectXMath.h >

using namespace DirectX;

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

class LightingAndMaterialsApp : D3DApp {
private:
  std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
  std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
  std::vector<std::unique_ptr<FrameResource>> mFrameResources;
  FrameResource *mCurrFrameResource = nullptr;
  int mCurrFrameResourceIndex = 0;

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
  float mTheta = 1.5 * XM_PI;
  float mPhi = XM_PIDIV2 - 0.1f;
  float mRadius = 50.0f;

public:

private:
  void BuildMaterials();
  void UpdateMaterialCBs(const GameTimer &gt);
  void DrawRenderItems(ID3D12GraphicsCommandList *cmdList, const std::vector<RenderItem *> &ritems);
  void BuildRenderItems();
  void UpdateObjectCBs(const GameTimer &gt);
  void BuildFrameResources();
  void UpdateCamera(const GameTimer &gt);
};

void LightingAndMaterialsApp::BuildMaterials() {
  auto grass = std::make_unique<Material>();
  grass->Name = "grass";
  grass->MatCBIndex = 0;
  grass->DiffuseAlbedo = XMFLOAT4(0.2f, 0.6f, 0.2f, 1.0f);
  grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
  // Relatively smooth.
  grass->Roughness = 0.125f;

  auto water = std::make_unique<Material>();
  water->Name = "water";
  water->MatCBIndex = 1;
  water->DiffuseAlbedo = XMFLOAT4(0.0f, 0.2f, 0.6f, 1.0f);
  water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
  // Completely smooth.
  water->Roughness = 0.0f;

  mMaterials["grass"] = std::move(grass);
  mMaterials["water"] = std::move(water);
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
  auto wavesRitem = std::make_unique<RenderItem>();
  wavesRitem->World = Math::Identity4x4();
  wavesRitem->ObjCBIndex = 0;
  wavesRitem->Mat = mMaterials["water"].get();
  wavesRitem->Geo = mGeometries["waterGeo"].get();
  wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
  wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
  wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

  mWavesRenderItem = wavesRitem.get();

  mRenderItemLayer[(int)RenderLayer::Opaque].push_back(wavesRitem.get());

  auto gridRitem = std::make_unique<RenderItem>();
  gridRitem->World = Math::Identity4x4();
  gridRitem->ObjCBIndex = 1;
  gridRitem->Mat = mMaterials["grass"].get();
  gridRitem->Geo = mGeometries["landGeo"].get();
  gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
  gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
  gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

  mRenderItemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());

  mAllRenderItems.push_back(std::move(wavesRitem));
  mAllRenderItems.push_back(std::move(gridRitem));
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
      (UINT) mMaterials.size(),
      mWaves->VertexCount()
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