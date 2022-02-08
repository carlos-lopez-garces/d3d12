#pragma once

#include "../Common/d3dUtil.h"

class ShadowMap {
private:
  ID3D12Device* md3dDevice = nullptr;
  UINT mWidth = 0;
  UINT mHeight = 0;
  D3D12_VIEWPORT mViewport;
  D3D12_RECT mScissorRect;
  DXGI_FORMAT mFormat = DXGI_FORMAT_R24G8_TYPELESS;
  CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv;
  CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv;
  CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuDsv;
  Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMap = nullptr;

public:
  ShadowMap(ID3D12Device* device, UINT width, UINT height);
  ShadowMap(const ShadowMap&) = delete;
  ShadowMap& operator=(const ShadowMap&) = delete;

  UINT Width() const;
  UINT Height() const;

  ID3D12Resource* Resource();

  // Shader resource view.
  CD3DX12_GPU_DESCRIPTOR_HANDLE Srv() const;

  // Depth/stencil view.
  CD3DX12_CPU_DESCRIPTOR_HANDLE Dsv() const;

  D3D12_VIEWPORT Viewport() const;
  D3D12_RECT ScissorRect() const;

  void BuildDescriptors(
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDsv
  );

  void OnResize(UINT newWidth, UINT newHeight);

private:
  void BuildDescriptors();

  void BuildResource();
};