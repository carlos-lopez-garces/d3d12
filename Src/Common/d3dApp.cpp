#include <WindowsX.h>

#include "d3dApp.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

D3DApp::D3DApp(HINSTANCE hInstance) : mhAppInst(hInstance) {
  // A single mApp (a member of D3DApp) exists across instances of the D3DApp 
  // class because it has static storage duration.
  assert(mApp == nullptr);

  mApp = this;
}

D3DApp::~D3DApp() {
  if (md3dDevice != nullptr) {
    FlushCommandQueue();
  }
}

// TODO: why initialize it here?
D3DApp* D3DApp::mApp = nullptr;
D3DApp* D3DApp::GetApp() {
  return mApp;
}

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

  ComPtr<ID3D12DeviceRemovedExtendedDataSettings> pDredSettings;
  ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)));

  // Turn on auto-breadcrumbs and page fault reporting.
  pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
  pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);

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
    md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence))
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
  // TODO: implement.
  // LogAdapters();
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
    IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())
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
    HANDLE eventHandle = CreateEventExW(nullptr, nullptr, false, EVENT_ALL_ACCESS);
    
    // The ID3D12 API uses the synchapi.h API to listen to and wait for GPU events.
    // In this case, the GPU event is the execution of the fence update.
    ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));
    WaitForSingleObject(eventHandle, INFINITE);
    CloseHandle(eventHandle);
  }
}

void D3DApp::OnResize() {
  assert(md3dDevice);
  assert(mSwapChain);
  assert(mDirectCmdListAlloc);

  // Since resizing will change/recreate resources and these resources may be referenced by
  // commands currently in the queue, we have to let the GPU process all of them 
  // before proceeding.
  FlushCommandQueue();

  ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

  // TODO: what does Reset do exactly?
  for (int i = 0; i < SwapChainBufferCount; ++i) {
    mSwapChainBuffer[i].Reset();
  }

  mDepthStencilBuffer.Reset();

  ThrowIfFailed(mSwapChain->ResizeBuffers(
    SwapChainBufferCount,
    mClientWidth,
    mClientHeight,
    mBackBufferFormat,
    // This flag is what allows the buffers to be resized. See CreateSwapChain.
    DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
  ));

  mCurrentBackBuffer = 0;

  // A descriptor heap contains 1 or more handles stored contiguously.
  // GetCPUDescriptorHandleForHeapStart returns a handle to the first one.
  CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
  for (UINT i = 0; i < SwapChainBufferCount; i++) {
    // Create views (descriptors) for the swap chain buffers.
    ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
    md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
    rtvHeapHandle.Offset(1, mRtvDescriptorSize);
  }

  D3D12_RESOURCE_DESC depthStencilDesc;
  // The term "dimension" doesn't refer to size, but to the dimensionality of
  // the texture: 1D, 2D, 3D (and buffer).
  // TODO: what's the difference between a 1D texture and a buffer?
  depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  depthStencilDesc.Alignment = 0;
  depthStencilDesc.Width = mClientWidth;
  depthStencilDesc.Height = mClientHeight;
  // "Depth size" is for 3D textures. "Aray size" is for 1D and 2D textures.
  depthStencilDesc.DepthOrArraySize = 1;
  depthStencilDesc.MipLevels = 1;
  // Depth/stencil buffers typically have the DXGI_FORMAT_D24_UNORM_S8_UINT format,
  // but here we use TYPELESS because this resource is not only written to, but is
  // also read by some of the shaders: resources that are input to shaders need views
  // with format DXGI_FORMAT_R24_UNORM_X8_TYPELESS; the format we use here,
  // DXGI_FORMAT_R24G8_TYPELESS is compatible with both those formats.
  //
  // SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS.
  // DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT.
  depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
  depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
  depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
  depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_CLEAR_VALUE optClear;
  optClear.Format = mDepthStencilFormat;
  // The farthest normalized z distance. The first object drawn behind a given pixel
  // will be closer than this initial value.
  optClear.DepthStencil.Depth = 1.0f;
  optClear.DepthStencil.Stencil = 0;

  auto heapType = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
  // Create the depth/stencil buffer.
  ThrowIfFailed(md3dDevice->CreateCommittedResource(
    &heapType,
    D3D12_HEAP_FLAG_NONE,
    &depthStencilDesc,
    D3D12_RESOURCE_STATE_COMMON,
    &optClear,
    IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())
  ));

  // Now create the view (descriptor) for that depth/stencil buffer.
  D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
  dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
  // TODO: what happens if the view's dimension doesn't match the resource's dimension?
  dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  dsvDesc.Format = mDepthStencilFormat;
  dsvDesc.Texture2D.MipSlice = 0;
  md3dDevice->CreateDepthStencilView(
    mDepthStencilBuffer.Get(),
    &dsvDesc,
    // This returns a handle to the first descriptor in the depth/stencil descriptor heap.
    DepthStencilView()
  );

  // Transition barriers indicate that a set of resources will transition between 
  // different usages: in this case, from whatever D3D12_RESOURCE_STATE_COMMON is
  // to being used as a depth/stencil buffer.
  auto transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
    mDepthStencilBuffer.Get(),
    D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATE_DEPTH_WRITE
  );

  mCommandList->ResourceBarrier(
    1,
    &transitionBarrier
  );

  ThrowIfFailed(mCommandList->Close());
  ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
  // Put the resizing command in the queue.
  // TODO: what command? I only see the resource barrier for the depth/stencil buffer.
  mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

  FlushCommandQueue();

  // Update viewport accordingly. The viewport is mapped to the entire back buffer / 
  // window / screen.
  mScreenViewport.TopLeftX = 0;
  mScreenViewport.TopLeftY = 0;
  mScreenViewport.Width = static_cast<float>(mClientWidth);
  mScreenViewport.Height = static_cast<float>(mClientHeight);
  mScreenViewport.MinDepth = 0.0f;
  mScreenViewport.MaxDepth = 1.0f;

  // Update scissor rectangle accordingly, mapped to the entire back buffer. The
  // scissor rectangle is used to cull the pixels that area outside of it.
  mScissorRect = { 0, 0, mClientWidth, mClientHeight };
}

int D3DApp::Run() {
  MSG msg = { 0 };

  mTimer.Reset();

  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
      // There are window messages.
      TranslateMessage(&msg);
      // Calls the window procedure; in our case, MainWndProc()->MsgProc()
      // (The window procedure determines much of the behavior of the window,
      // see docs.microsoft.com/en-us/windows/win32/learnwin32/writing-the-window-procedure).
      DispatchMessage(&msg);
    } else {
      mTimer.Tick();

      // The application pauses when the window becomes inactive or minimized or
      // when it's being resized.
      if (!mAppPaused) {
        CalculateFrameStats();

        // ImGui frame.
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::ShowDemoWindow();

        // Update the scene.
        Update(mTimer);
        Draw(mTimer);
      } else {
        Sleep(100);
      }
    }
  }

  return (int)msg.wParam;
}

float D3DApp::AspectRatio() const {
  return static_cast<float>(mClientWidth) / mClientHeight;
}

HINSTANCE D3DApp::AppInst() const {
  return mhAppInst;
}

HWND D3DApp::MainWnd() const {
  return mhMainWnd;
}

bool D3DApp::Get4xMsaaState() const {
  return m4xMsaaState;
}

void D3DApp::Set4xMsaaState(bool value) {
  if (m4xMsaaState != value) {
    m4xMsaaState = value;
    // The MSAA configuration is part of the description of the swap chain
    // buffers, so it needs to be recreated.
    CreateSwapChain();
    // Since the swap chain is new, the views/descriptors of the swap chain
    // buffers need to be recreated.
    OnResize();
  }
}

bool D3DApp::Initialize() {
  if (!InitMainWindow()) {
    return false;
  }

  if (!InitDirect3D()) {
    return false;
  }

  OnResize();

  return true;
}

LRESULT D3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg) {
  case WM_ACTIVATE:
    if (LOWORD(wParam) == WA_INACTIVE) {
      mAppPaused = true;
      mTimer.Stop();
    }
    else {
      mAppPaused = false;
      mTimer.Start();
    }
    return 0;

  case WM_SIZE:
    mClientWidth = LOWORD(lParam);
    mClientHeight = HIWORD(lParam);
    if (md3dDevice) {
      if (wParam == SIZE_MINIMIZED) {
        mAppPaused = true;
        mMinimized = true;
        mMaximized = false;
      }
      else if (wParam == SIZE_MAXIMIZED) {
        mAppPaused = false;
        mMinimized = false;
        mMaximized = true;
        OnResize();
      }
      else if (wParam == SIZE_RESTORED) {

        if (mMinimized) {
          mAppPaused = false;
          mMinimized = false;
          OnResize();
        }

        else if (mMaximized) {
          mAppPaused = false;
          mMaximized = false;
          OnResize();
        }
        else if (mResizing) {}
        else {
          OnResize();
        }
      }
    }
    return 0;

  case WM_ENTERSIZEMOVE:
    mAppPaused = true;
    mResizing = true;
    mTimer.Stop();
    return 0;

  case WM_EXITSIZEMOVE:
    mAppPaused = false;
    mResizing = false;
    mTimer.Start();
    OnResize();
    return 0;

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;

  case WM_MENUCHAR:
    return MAKELRESULT(0, MNC_CLOSE);

  case WM_GETMINMAXINFO:
    ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
    ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
    return 0;

  case WM_LBUTTONDOWN:
  case WM_MBUTTONDOWN:
  case WM_RBUTTONDOWN:
    OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    return 0;
  case WM_LBUTTONUP:
  case WM_MBUTTONUP:
  case WM_RBUTTONUP:
    OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    return 0;
  case WM_MOUSEMOVE:
    OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    return 0;
  case WM_KEYUP:
    if (wParam == VK_ESCAPE) {
      PostQuitMessage(0);
    }
    else if ((int)wParam == VK_F2)
      Set4xMsaaState(!m4xMsaaState);

    return 0;
  }

  return DefWindowProc(hwnd, msg, wParam, lParam);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// The window procedure that determines the behavior of the window. Invoked by
// DispatchMessage in the render/message loop.
// See docs.microsoft.com/en-us/windows/win32/learnwin32/writing-the-window-procedure.
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
    return true;
  }
  return D3DApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

bool D3DApp::InitMainWindow() {
  WNDCLASS wc;
  // Redraw the entire window if a movement or size adjustment changes the width
  // (CS_HREDRAW) or height (CS_VREDRAW) of the client area.
  wc.style = CS_HREDRAW | CS_VREDRAW;
  // Determines the behavior of the window. Invoked by DispatchMessage in the 
  // render/message loop.
  wc.lpfnWndProc = MainWndProc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = mhAppInst;
  wc.hIcon = LoadIcon(0, IDI_APPLICATION);
  wc.hCursor = LoadCursor(0, IDC_ARROW);
  // A null "paint brush" for the background makes sense, because it's our 
  // application that'll draw in the client area.
  wc.hbrBackground = (HBRUSH) GetStockObject(NULL_BRUSH);
  wc.lpszMenuName = 0;
  wc.lpszClassName = L"MainWnd";

  if (!RegisterClass(&wc)) {
    MessageBox(0, L"RegisterClass failed.", 0, 0);
    return false;
  }

  RECT R = { 0, 0, mClientWidth, mClientHeight };
  AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
  int width = R.right - R.left;
  int height = R.bottom - R.top;

  mhMainWnd = CreateWindow(
    L"MainWnd", mMainWndCaption.c_str(),
    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
    width, height, 0, 0, mhAppInst, 0
  );
  if (!mhMainWnd) {
    MessageBox(0, L"CreateWindow failed.", 0, 0);
    return false;
  }

  ShowWindow(mhMainWnd, SW_SHOW);
  UpdateWindow(mhMainWnd);

  return true;
}

void D3DApp::CalculateFrameStats() {
  static int frameCnt = 0;
  static float timeElapsed = 0.0f;

  frameCnt++;

  // Keep counting frames until 1 second has elapsed; that is, don't
  // enter this block unless 1 second has elapsed since the last time.
  if ((mTimer.TotalTime() - timeElapsed) >= 0.0f) {
    // Technically framCnt/1sec.
    float fps = (float)frameCnt;
    // Milliseconds per frame.
    float mspf = 1000.0f / fps;

    wstring fpsStr = to_wstring(fps);
    wstring mspfStr = to_wstring(mspf);

    wstring windowText = mMainWndCaption + L"    fps: " + fpsStr + L"   mspf: " + mspfStr;
    windowText = mMainWndCaption;
    SetWindowText(mhMainWnd, windowText.c_str());

    frameCnt = 0;
    // 1 second elapsed?
    timeElapsed += 1.0f;
  }
}