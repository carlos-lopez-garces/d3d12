#include "../Common/d3dApp.h"
#include "../Common/Math.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"
//#include "../Common/Camera.h"
//#include "FrameResource.h"
#include "ShadowMap.h"

using namespace DirectX;
using namespace Microsoft::WRL;

struct RenderItem {

};

class ShadowMappingApp : public D3DApp
{
public:
  ShadowMappingApp(HINSTANCE hInstance);
  ShadowMappingApp(const ShadowMappingApp& rhs) = delete;
  ShadowMappingApp& operator=(const ShadowMappingApp& rhs) = delete;
  ~ShadowMappingApp();

  virtual bool Initialize() override;

private:
  void LoadTextures();
  void BuildRootSignature();
  void BuildDescriptorHeaps();
  virtual void CreateRtvAndDsvDescriptorHeaps() override;
  void BuildShadersAndInputLayout();
  void BuildShapeGeometry();
  void BuildSkullGeometry();
  void BuildPSOs();
  void BuildFrameResources();
  void BuildMaterials();
  void BuildRenderItems();

  virtual void Draw(const GameTimer& gt) override;
  void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
  void DrawSceneToShadowMap();

  virtual void Update(const GameTimer& gt) override;
  void UpdateObjectCBs(const GameTimer& gt);
  void UpdateMaterialBuffer(const GameTimer& gt);
  void UpdateShadowTransform(const GameTimer& gt);
  void UpdateMainPassCB(const GameTimer& gt);
  void UpdateShadowPassCB(const GameTimer& gt);
  void AnimateMaterials(const GameTimer& gt);

  std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

  virtual void OnResize() override;
  virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
  virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
  virtual void OnMouseMove(WPARAM btnState, int x, int y) override;
  void OnKeyboardInput(const GameTimer& gt);

private:
  // Contains every vertex of the scene.
  DirectX::BoundingSphere mSceneBounds;

  // TODO: implement Camera.
  // Camera mCamera;

  std::unique_ptr<ShadowMap> mShadowMap;

  std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
};

ShadowMappingApp::ShadowMappingApp(HINSTANCE hInstance) : D3DApp(hInstance) {
  // World space origin.
  mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
  // Should contain every vertex of the scene. If the scene changes and becomes
  // larger, we need to recompute the bounding sphere.
  mSceneBounds.Radius = sqrtf(10.0f * 10.0f + 15.0f * 15.0f);
}

ShadowMappingApp::~ShadowMappingApp() {
  if (md3dDevice != nullptr) {
    FlushCommandQueue();
  }
}

bool ShadowMappingApp::Initialize() {
  if (!D3DApp::Initialize()) {
    return false;
  }

  ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

  // TODO.
  // mCamera.SetPosition(0.0f, 2.0f, -15.0f);

  // Fixed resolution? Yes, because what the light source sees is independent of
  // what the camera sees, the size of the window, and the size of the viewport.
  mShadowMap = std::make_unique<ShadowMap>(md3dDevice.Get(), 2048, 2048);

  LoadTextures();
}

void ShadowMappingApp::LoadTextures() {
  // TODO: don't need most of these maps.
  std::vector<std::string> texNames =
  {
    "bricksDiffuseMap",
    "bricksNormalMap",
    "tileDiffuseMap",
    "tileNormalMap",
    "defaultDiffuseMap",
    "defaultNormalMap",
    "skyCubeMap"
  };
  std::vector<std::wstring> texFilenames =
  {
      L"Assets/bricks2.dds",
      L"Assets/bricks2_nmap.dds",
      L"Assets/tile.dds",
      L"Assets/tile_nmap.dds",
      L"Assets/white1x1.dds",
      L"Assets/default_nmap.dds",
      L"Assets/desertcube1024.dds"
  };

  for (int i = 0; i < (int)texNames.size(); ++i) {
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

  // THIS WAS MY ATTEMPT.
  // ================================================================
  //D3D12_RESOURCE_DESC textureDesc;
  //textureDesc.Alignment = 0;
  //textureDesc.DepthOrArraySize = 0;
  //textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  //textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
  //textureDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
  //// Actual image dimensions of bricks.dds, stone.dds, and tile.dds.
  //textureDesc.Width = 512;
  //textureDesc.Height = 512;
  //textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  //textureDesc.MipLevels = 3;
  //textureDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
  //textureDesc.SampleDesc.Quality = m4xMsaaState ? m4xMsaaQuality - 1 : 1;

  //D3D12_CLEAR_VALUE clearValue;
  //clearValue.Format = DXGI_FORMAT_R32G32B32A32_UINT;
  //clearValue.Color[0] = 1.0f;
  //clearValue.Color[1] = 1.0f;
  //clearValue.Color[2] = 1.0f;
  //clearValue.Color[2] = 0.0f;

  //ThrowIfFailed(md3dDevice->CreateCommittedResource(
  //  &CD3DX12_HEAP_PROPERTIES(D3D12_DEFAULT),
  //  D3D12_HEAP_FLAG_NONE,
  //  &textureDesc,
  //  D3D12_RESOURCE_STATE_GENERIC_READ,
  //  &clearValue,
  //  IID_PPV_ARGS(&mTextureResource.Get())
  //));

  //CreateDDSTextureFromFile12(
  //  md3dDevice.Get(),
  //  mCommandList.Get(),
  //  L"/Assets/bricks.dds",
  //  &mTextureResource,

  //);
  // ================================================================
}