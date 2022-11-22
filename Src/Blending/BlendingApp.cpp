#include "../Common/d3dApp.h"
#include "../Common/Math.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"
#include "../Common/Camera.h"
#include "Waves.h"

using namespace DirectX;
using namespace Microsoft::WRL;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

class BlendingApp : public D3DApp {
public:
    BlendingApp(HINSTANCE hInstance);
    BlendingApp(const BlendingApp &rhs) = delete;
    BlendingApp &operator=(const BlendingApp &rhs) = delete;
    ~BlendingApp();
};

BlendingApp::BlendingApp(HINSTANCE hInstance) : D3DApp(hInstance) {}

BlendingApp::~BlendingApp() {
    if (md3dDevice != nullptr) {
        FlushCommandQueue();
    }
}

int WINAPI WinMain(
  HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd
) {
#if defined(DEBUG) | defined(_DEBUG)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  try {
    BlendingApp app(hInstance);
    if(!app.Initialize()) {
      return 0;
    }

    return app.Run();
  }
  catch(DxException& e) {
    MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
    return 0;
  }
}
