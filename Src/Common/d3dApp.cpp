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

void D3DApp::CreateCommandObjects() {
  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  // A direct command list is a list of commands that the GPU can execute directly.
  //
  // Other command list types are bundle, compute, and copy. A bundle is a group of
  // commands recorded together; this avoids the overhead of recording (executing?)
  // them directly one by one.
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  ThrowIfFailed(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

  ThrowIfFailed(md3dDevice->CreateCommandAllocator(
    D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&mDirectCmdListAlloc.GetAddressOf())
  ));

  ThrowIfFailed(md3dDevice->CreateCommandList(
    0,
    D3D12_COMMAND_LIST_TYPE_DIRECT,
    mDirectCmdListAlloc.Get(),
    // No pipeline state object. A pipeline state object is required for actually
    // drawing. Looks like we are not going to draw yet.
    nullptr,
    IID_PPV_ARGS(mCommandList.GetAddressOf())
  ));

  // Because we'll reset it shortly and Reset expects it to be closed.
  mCommandList->Close();
}

void D3DApp::CreateSwapChain() {
  // In case we want to recreate the swap chain (e.g. for changing settings at runtime).
  mSwapChain.Reset();

  DXGI_SWAP_CHAIN_DESC sd;

  // WE ARE DESCRIBING THE BACK BUFFER HERE NEXT:

  // The BufferDesc member is of type DXGI_MODE_DESC, "a display mode".
  sd.BufferDesc.Width = mClientWidth;
  sd.BufferDesc.Height = mClientHeight;
  // In hertz (frequency, times per second).
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.BufferDesc.Format = mBackBufferFormat;
  // The order in which the image is drawn: progressive (scanline to scanline),
  // upper field first (what's a field?), lower field first.
  sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
  sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

  // The SampleDesc member is of type DXGI_SAMPLE_DESC; multisampling parameters
  // (basically just sample count and quality).
  sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
  // TODO: Why do we set the quality ourselves? Isn't it determined by the device
  // based on sample count and texture (render target) type?
  sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;

  // The BufferUsage member is of type DXGI_USAGE and indicates what we are going
  // to use the back buffer for: as a render target. Other usage types for a buffer
  // include SHADER_INPUT (if the texture is to be used as input for a shader).
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

  // 2.
  sd.BufferCount = SwapChainBufferCount;
  
  sd.OutputWindow = mhMainWnd;
  // Windowed vs full screen.
  sd.Windowed = true;
  
  // The SwapEffect member is of type DXGI_SWAP_EFFECT and instructs what is to be
  // done with the back buffer after presenting it. Only DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
  // and DXGI_SWAP_EFFECT_FLIP_DISCARD are supported by Direct3D 12.
  //
  // In the "flip model" of back buffer presentation, the (desktop) window manager has 
  // direct access to the back buffer; in the bitblt model, a copy has to be made for the
  // window manager to access the contents of the back buffer.
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

  // Allow for sd.BufferDesc (the "display mode") to change when calling ResizeTarget.
  // sd.BufferDesc contains the width and height of the back buffer. This flag allows those
  // dimensions to change (when going from windowed to full screen modes and viceversa, and
  // also when resizing the window, presumably).
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

  ThrowIfFailed(mdxgiFactory->CreateSwapChain(
    mCommandQueue.Get(),
    &sd,
    mSwapChain.GetAddressOf()
  ));
}

void D3DApp::CreateRtvAndDsvDescriptorHeaps() {
  // The heap for render target descriptors (which include the 2 buffers of the swap chain).
  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
  rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  rtvHeapDesc.NodeMask = 0;
  ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
    &rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())
  ));

  // The heap for the depth/stencil buffer descriptor.
  D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
  dsvHeapDesc.NumDescriptors = 1;
  dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  dsvHeapDesc.NodeMask = 0;
  ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
    &dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())
  ));
}

ID3D12Resource* D3DApp::CurrentBackBuffer() const {
  return mSwapChainBuffer[mCurrentBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::CurrentBackBufferView() const {
  return CD3DX12_CPU_DESCRIPTOR_HANDLE(
    // Address of 1st element + current back buffer index * size of descriptor.
    mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
    mCurrentBackBuffer,
    mRtvDescriptorSize
  );
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::DepthStencilView() const {
  return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void D3DApp::FlushCommandQueue() {
  mCurrentFence++;

  // Signal updates the fence to the specified value. Since this is also
  // a command that's added to the queue, the fence update won't occur
  // until the GPU processes the prior commands.
  ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));

  if (mFence->GetCompletedValue() < mCurrentFence) {
    // The GPU hasn't updated the fence: it still has at least one command
    // to execute before getting to the fence.
    //
    // CreateEventEx, and the function that waits for the event to be signaled,
    // WaitForSingleObject, are part of the synchapi.h API.
    HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
    
    // The ID3D12 API uses the synchapi.h API to listen to and wait for GPU events.
    // In this case, the GPU event is the execution of the fence update.
    ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));
    WaitForSingleObject(eventHandle, INFINITE);
    CloseHandle(eventHandle);
  }
}
