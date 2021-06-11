#include "../Common/d3dApp.h"
#include <DirectXColors.h>

#include namespace DirectX;

class InitDirect3DApp : public D3DApp {
public:
  InitDirect3DApp(HINSTANCE hInstance);
  ~InitDirect3DApp();

  virtual bool Initialize() override;

private:
  virtual void OnResize() override;
  // Update the scene.
  virtual void Update(const GameTimer& gt) override;
  virtual void Draw(const GameTimer& gt) override;
};

int WINAPI WinMain(
  HINSTANCE hInstance,
  HINSTANCE prevInstance,
  PSTR cmdLine,
  int showCmd) {

#if defined(DEBUG) | defined(_DEBUG)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  try {
    InitDirect3DApp app(hInstance);
    if (!app.Initialize()) {
      return 0;
    }
    // TODO: implement D3DApp::Run.
    return app.Run();
  }
  catch (DxException& e) {
    // TODO: implement DxException.
    // TODO: implement MessageBox.
    MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
    return 0;
  }
}

InitDirect3DApp::InitDirect3DApp(HINSTANCE hInstance)
  : D3DApp(hInstance) {
}

InitDirect3DApp::~InitDirect3DApp() {
}

bool InitDirect3DApp::Initialize() {
  if (!D3DApp::Initialize()) {
    return false;
  }
  return true;
}

void InitDirect3DApp::OnResize() {
  D3DApp::OnResize();
}

void InitDirect3DApp::Update(const GameTimer& gt) {
}

void InitDirect3DApp::Draw(const GameTimer& gt) {
  // Record a series of commands in the command list and then put this
  // list on the queue.

  ThrowIfFailed(mDirectCmdListAlloc->Reset());

  ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

  mCommandList->ResourceBarrier(
    1,
    &CD3DX12_RESOURCE_BARRIER::Transition(
      CurrentBackBuffer(),
      // Before.
      // TODO: this transition suggests that the (current) back buffer has been
      // presented to the screen and it's changing its usage to render target
      // (a back buffer, that is). Wouldn't whichever the current back buffer is
      // be always in the render target state?
      D3D12_RESOURCE_STATE_PRESENT,
      // After.
      D3D12_RESOURCE_STATE_RENDER_TARGET
    )
  );

  // The command list was reset, so these need to be reset too.
  mCommandList->RSSetViewports(1, &mScreenViewport);
  mCommandList->RSSetScissorRects(1, &mScissorRect);

  mCommandList->ClearRenderTargetView(
    CurrentBackBufferView(),
    Colors::LightSteelBlue,
    0,
    nullptr
  );

  mCommandList->ClearDepthStencilView(
    DepthStencilView(),
    D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
    // Clear depth to 1.0s.
    1.0f,
    // Clear stencil to 0.0s.
    0,
    0,
    nullptr
  );

  // OM stands for output merger (a pipeline stage).
  // Bind the render target and the depth/stencil buffer to the output merger.
  mCommandList->OMSetRenderTargets(
    // The 2nd argument is the start address of an array of descriptor; that's
    // why we pass the count.
    1,
    &CurrentBackBufferView().
    true,
    &DepthStencilView()
  );

  mCommandList->ResourceBarrier(
    1,
    &CD3DX12_RESOURCE_BARRIER::Transition(
      CurrentBackBuffer(),
      // Before.
      D3D12_RESOURCE_STATE_RENDER_TARGET,
      // After.
      D3D12_RESOURCE_STATE_PRESENT
    )
  );

  // We are done recording commands in the list. It needs to be closed before
  // putting it on the queue.
  ThrowIfFailed(mCommandList->Close());

  // Put command list on the queue. (Remember that it won't execute synchronously,
  // but until the GPU gets to it.)
  ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
  mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

  // Swap the buffers.
  ThrowIfFailed(mSwapChain->Present(0, 0));
  mCurrentBackBuffer = (mCurrentBackBuffer + 1) % SwapChainBufferCount;

  // Wait until all the commands that are currently in the queue are executed,
  // including the ones we just inserted.
  FlushCommandQueue();
}