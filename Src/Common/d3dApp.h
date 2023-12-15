#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "d3dUtil.h"
#include "GameTimer.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

class D3DApp {
public:
  static D3DApp* GetApp();

  HINSTANCE AppInst() const;
  HWND MainWnd() const;
  float AspectRatio() const;

  // 4X MSAA (multisampling antialiasing) upscales the resolution of the front and 
  // back buffers by 4, so that each screen pixel have 4 subpixels. Each screen
  // pixel is sampled only once, at the center, where the 4 subpixels meet. The
  // subpixels that pass the depth test in the current draw call get this sampled 
  // color; the ones that don't, retain their current color. The color of the screen
  // pixel will then be the blend of its 4 subpixels.
  bool Get4xMsaaState() const;
  void Set4xMsaaState(bool value);

  virtual bool Initialize();
  // Render loop (or message loop, see 
  // docs.microsoft.com/en-us/windows/win32/learnwin32/window-messages).
  int Run();
  // Window procedure (through DispatchMessage()->MainWndProc()).
  // See docs.microsoft.com/en-us/windows/win32/learnwin32/writing-the-window-procedure.
  virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:
  // The m prefix stands for "member".

  static D3DApp* mApp;
  // Application instance handle.
  HINSTANCE mhAppInst = nullptr;
  // Window handle.
  HWND mhMainWnd = nullptr;
  std::wstring mMainWndCaption = L"CDX";

  // States.
  bool mAppPaused = false;
  bool mMinimized = false;
  bool mMaximized = false;
  bool mResizing = false;
  bool mFullscreenState = false;
  bool m4xMsaaState = false;
  UINT m4xMsaaQuality = 0;

  GameTimer mTimer;

  // Factory used to create IDXGI objects.
  Microsoft::WRL::ComPtr<IDXGIFactory4> mdxgiFactory;
  Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain;
  Microsoft::WRL::ComPtr<ID3D12Device> md3dDevice;
  Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
  // The application maintains the target integer that signals that the GPU has "crossed" the fence when
  // processing the command queue.
  UINT64 mCurrentFence = 0;
  // The applications puts commands in the queue and the GPU processes them eventually.
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;
  // 1 front and 1 back buffers are usually enough.
  static const int SwapChainBufferCount = 2;
  int mCurrentBackBuffer = 0;
  Microsoft::WRL::ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
  Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer;
  // 8 bits per color component, [0,255] is representable.
  DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  // 24 bits for depth and 8 bits for stencil. Normalized [0,1].
  DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
  // Allocator for render target views (descriptors).
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
  // Allocator for depth/stencil views (descriptors);
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;
  UINT mRtvDescriptorSize = 0;
  UINT mDsvDescriptorSize = 0;
  // Descriptor size for constant buffer views, shader resource views, and unordered access views.
  UINT mCbvSrvUavDescriptorSize = 0;

  D3D12_VIEWPORT mScreenViewport;
  D3D12_RECT mScissorRect;
  int mClientWidth = 800;
  int mClientHeight = 600;

  // Choose hardware accelleration with the GPU, as opposed to a software rendering pipeline.
  D3D_DRIVER_TYPE md3dDriverType = D3D_DRIVER_TYPE_HARDWARE;

  // HINSTANCE is a handle to an instance and represents the executable.
  D3DApp(HINSTANCE hInstance);

  virtual ~D3DApp();

  // Delete the copy constructor and the assignment operator; prevent implicit definition by the compiler.
  D3DApp(const D3DApp &rhs) = delete;
  D3DApp& operator=(const D3DApp& rhs) = delete;

  bool InitMainWindow();
  bool InitDirect3D();

  // Create render target view (descriptor) and depth/stencil view (descriptor).
  // Each get their own heap, because the type of the descriptor allocator depends on the type of descriptor.
  virtual void CreateRtvAndDsvDescriptorHeaps();

  // The swap chain maintains at least 2 buffers: a front and a back buffer. The front buffer is the last
  // frame that was rendered fully and that is currently showing on the screen; the back buffer is where the
  // latest frame is being rendered. Swapping them so that the back buffer become the front buffer is called
  // "presenting".
  void CreateSwapChain();

  ID3D12Resource* CurrentBackBuffer() const;
  D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
  D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

  // Create command queue, command allocator, and command list.
  void CreateCommandObjects();

  // Have the application wait until the GPU has processed all the commands in the queue.
  void FlushCommandQueue();

  // Window/viewport resizing involves recreating the swap chain buffers and their 
  // descriptors (actually, OnResize is the function that creates these descriptors for
  // the first time too).
  virtual void OnResize();
  // Update the scene.
  virtual void Update(const GameTimer& gt) = 0;
  virtual void Draw(const GameTimer& gt) = 0;

  void CalculateFrameStats();
  void LogAdapters();
  void LogAdapterOutputs();
  void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);

  virtual void OnMouseDown(WPARAM btnState, int x, int y) { }
  virtual void OnMouseUp(WPARAM btnState, int x, int y) { }
  virtual void OnMouseMove(WPARAM btnState, int x, int y) { }
};