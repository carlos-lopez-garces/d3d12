#include "../Common/d3dApp.h"
#include "../Common/Math.h"
#include "../Common/UploadBuffer.h"
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
  FrameResource* mCurrFrameResource = nullptr;
  std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;
  XMFLOAT4X4 mView = Math::Identity4x4();
  XMFLOAT4X4 mProj = Math::Identity4x4();
  XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
  // Data that applies to all draw calls and that doesn't depend on the object
  // being drawn.
  PassConstants mMainPassCB;

private:
  void UpdateObjectCBs(const GameTimer &gt);
  // Updates the pass constants (e.g. view and projection matrices) and sets
  // them in the current frame resource.
  void UpdateMainPassCB(const GameTimer &gt);
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