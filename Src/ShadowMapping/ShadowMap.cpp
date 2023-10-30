#include "ShadowMap.h"

ShadowMap::ShadowMap(ID3D12Device* device, UINT width, UINT height) {
  md3dDevice = device;
  mWidth = width;
  mHeight = height;
  // Top left X, Y; width and height; min and max depth.
  mViewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
  mScissorRect = { 0, 0, (int)width, (int)height };

  BuildResource();
}

UINT ShadowMap::Width() const {
  return mWidth;
}

UINT ShadowMap::Height() const {
  return mHeight;
}

ID3D12Resource* ShadowMap::Resource() {
  return mShadowMap.Get();
}

// Shader resource view.
CD3DX12_GPU_DESCRIPTOR_HANDLE ShadowMap::Srv() const {
  return mhGpuSrv;
}

// Depth/stencil view.
CD3DX12_CPU_DESCRIPTOR_HANDLE ShadowMap::Dsv() const {
  return mhCpuDsv;
}

D3D12_VIEWPORT ShadowMap::Viewport() const {
  return mViewport;
}

D3D12_RECT ShadowMap::ScissorRect() const {
  return mScissorRect;
}

void ShadowMap::BuildDescriptors(
  // CPU and GPU handles to location in SRV heap where the shadow map 
  // descriptor should be created. SRV heap is owned by the caller.
  CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
  CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
  // Handle to location in DSV heap where the depth-stencil buffer
  // descriptor should be created. DSV heap is owned by the caller.
  CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDsv
) {
  // Create SRV and DSV descriptors at locations supplied by the caller.
  // The caller owns the SRV and DSV heaps.

  mhCpuSrv = hCpuSrv;
  mhGpuSrv = hGpuSrv;
  mhCpuDsv = hCpuDsv;
  BuildDescriptors();
}

void ShadowMap::BuildDescriptors() {
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MostDetailedMip = 0;
  srvDesc.Texture2D.MipLevels = 1;
  srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
  srvDesc.Texture2D.PlaneSlice = 0;
  md3dDevice->CreateShaderResourceView(mShadowMap.Get(), &srvDesc, mhCpuSrv);

  D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
  dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
  dsvDesc.Texture2D.MipSlice = 0;
  md3dDevice->CreateDepthStencilView(mShadowMap.Get(), &dsvDesc, mhCpuDsv);
}

void ShadowMap::BuildResource() {
  D3D12_RESOURCE_DESC resourceDesc;
  ZeroMemory(&resourceDesc, sizeof(D3D12_RESOURCE_DESC));
  resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resourceDesc.Alignment = 0;
  resourceDesc.Width = mWidth;
  resourceDesc.Height = mHeight;
  // I thought this would be mWidth * mHeight, because I thought this would be total number of pixels.
  resourceDesc.DepthOrArraySize = 1;
  resourceDesc.MipLevels = 1;
  resourceDesc.Format = mFormat;
  // Multisampling configuration differs from the main render target's because we don't actually display the
  // shadow map; we use it to render them to the main render target, where multisampling does have a 
  // configuration that optimizes quality.
  resourceDesc.SampleDesc.Count = 1;
  resourceDesc.SampleDesc.Quality = 0;
  resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  // I thought D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET was needed because we are rendering the shadow map
  // to this texture.
  resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_CLEAR_VALUE clearValue;
  clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  clearValue.DepthStencil.Depth = 1.0;
  clearValue.DepthStencil.Stencil = 0;

  CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

  ThrowIfFailed(md3dDevice->CreateCommittedResource(
    // The hardware creates a depth buffer out of this texture. No need for CPU access.
    &heapProperties,
    D3D12_HEAP_FLAG_NONE,
    &resourceDesc,
    // Initial resource state.
    D3D12_RESOURCE_STATE_GENERIC_READ,
    &clearValue,
    IID_PPV_ARGS(&mShadowMap)
  ));
}

void ShadowMap::OnResize(UINT newWidth, UINT newHeight) {
  if((mWidth != newWidth) || (mHeight != newHeight)) {
    mWidth = newWidth;
    mHeight = newHeight;
    BuildResource();
    BuildDescriptors();
  }
}