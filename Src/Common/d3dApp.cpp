#include <WindowsX.h>

#include "d3dApp.h"

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

bool D3DApp::InitDirect3D() {
#if defined(DEBUG) || defined(_DEBUG)
  {
    ComPtr<ID3D12Debug> debugController;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
    debugController->EnableDebugLayer();
  }
#endif

  // Used to create other DXGI objects and query device characteristics.
  ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory)));

  HRESULT hardwareResult = D3D12CreateDevice(
    // Choose the default display adapter (the GPU, as opposed to a software emulator).
    nullptr,
    // Fail if the entire Direct3D 11 capability set isn't supported.
    D3D_FEATURE_LEVEL_11_0,
    // The IID_PPV_ARGS macro expands into a REFIID and the input pointer.
    IID_PPV_ARGS(&md3dDevice)
  );
  // FAILED is defined in winerror.h. Negative numbers indicate failure.
  if (FAILED(hardwareResult)) {
    // Try with a software display adapter (graphics emulator). WARP is the Windows Advanced
    // Rasterization Platform.
    ComPtr<IDXGIAdapter> pWarpAdapter;
    ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));
    ThrowIfFailed(D3D12CreateDevice(
      pWarpAdapter.Get(),
      D3D_FEATURE_LEVEL_11_0,
      IID_PPV_ARGS(&md3dDevice)
    ));
  }

  ThrowIfFailed(
    md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&md3dDevice))
  );

  // Descriptor sizes vary across GPUs; that's why they need to be queried; they can't be constant 
  // definitions in headers. Descriptor sizes are used to allocate descriptors (or their handles?).
  mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
  mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // Choose 4X MSAA (multisampling antialiasing) because it's guaranteed to be supported at the
  // D3D_FEATURE_LEVEL_11_0 feature level for all types of render target formats.
  //
  // The MSAA quality level is determined by the type of texture and the desired sample count per pixel.
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
  msQualityLevels.Format = mBackBufferFormat;
  // Take 4 samples per pixel.
  msQualityLevels.SampleCount = 4;
  msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
  // The CheckFeatureSupport returns the level in this member.
  msQualityLevels.NumQualityLevels = 0;
  ThrowIfFailed(md3dDevice->CheckFeatureSupport(
    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
    &msQualityLevels,
    sizeof(msQualityLevels)
  ));
  m4xMsaaQuality = msQualityLevels.NumQualityLevels;
  assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");

#ifdef _DEBUG
  LogAdapters();
#endif

  CreateCommandObjects();
  CreateSwapChain();
  CreateRtvAndDsvDescriptorHeaps();

  return true;
}