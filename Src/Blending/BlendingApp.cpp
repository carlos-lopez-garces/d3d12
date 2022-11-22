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

    virtual bool Initialize() override;

private:
    // Descriptor size for constant buffer views and shader resource views.
    UINT mCbvSrvDescriptorSize = 0;

    std::unique_ptr<Waves> mWaves;

    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;

    void LoadTextures();
};

BlendingApp::BlendingApp(HINSTANCE hInstance) : D3DApp(hInstance) {}

BlendingApp::~BlendingApp() {
    if (md3dDevice != nullptr) {
        FlushCommandQueue();
    }
}

bool BlendingApp::Initialize() {
    if (~D3DApp::Initialize()) {
        return false;
    }

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

    // TODO: load and build.
    LoadTextures();

    // The first command list has been built. Close it before putting it in the command
    // queue for GPU-side execution. 
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList *cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // FlushCommandQueue has the effect of pausing CPU-side execution until the GPU has
    // executed all of the commands in the queue. 
    FlushCommandQueue();

    return true;
}

void BlendingApp::LoadTextures() {
    std::vector<std::string> texNames = {
        "grassTex",
        "waterTex",
        "fenceTex"
    };

    std::vector<std::wstring> texFilenames = {
        L"Assets/grass.dds",
        L"Assets/water1.dds",
        L"Assets/fence.dds",
    };

    for (int i = 0; i < (int) texNames.size(); ++i) {
        auto textureMap = std::make_unique<Texture>();
        textureMap->Name = texNames[i];
        textureMap->Filename = texFilenames[i];
        ThrowIfFailed(CreateDDSTextureFromFile12(
            md3dDevice.Get(),
            mCommandList.Get(),
            textureMap->Filename.c_str(),
            textureMap->Resource,
            textureMap->UploadHeap
        ));
        mTextures[textureMap->Name] = std::move(textureMap);
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
