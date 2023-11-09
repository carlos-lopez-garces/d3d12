#include "../Common/d3dApp.h"
#include "../Common/Math.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"
#include "../Common/Camera.h"
#include "../Common/GLTFLoader.h"
#include "FrameResource.h"
#include "ShadowMap.h"
#include <iostream>

using namespace DirectX;
using namespace Microsoft::WRL;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

struct RenderItem {
  RenderItem() = default;
  RenderItem(const RenderItem &) = delete;

  XMFLOAT4X4 World = Math::Identity4x4();
	XMFLOAT4X4 TexTransform = Math::Identity4x4();

  int NumFramesDirty = gNumFrameResources;

	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

  D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

  UINT IndexCount = 0;
  UINT StartIndexLocation = 0;
  int BaseVertexLocation = 0;
};

enum class RenderLayer : int {
	Opaque = 0,
  Debug,
	Sky,
	Count
};

class ShadowMappingApp : public D3DApp {
public:
  ShadowMappingApp(HINSTANCE hInstance);
  ShadowMappingApp(const ShadowMappingApp& rhs) = delete;
  ShadowMappingApp& operator=(const ShadowMappingApp& rhs) = delete;
  ~ShadowMappingApp();

  virtual bool Initialize() override;

private:
  void LoadModelFromGLTF();
  void LoadTextures();
  void LoadTexturesFromGLTF();
  void BuildRootSignature();
  void BuildDescriptorHeaps();
  virtual void CreateRtvAndDsvDescriptorHeaps() override;
  void BuildShadersAndInputLayout();
  void BuildShapeGeometry();
  void BuildMainModelGeometry();
  void BuildGeometryFromGLTF();
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
  std::unique_ptr<GLTFLoader> mGLTFLoader;

  // Contains every vertex of the scene.
  DirectX::BoundingSphere mSceneBounds;

  Camera mCamera;

  std::unique_ptr<ShadowMap> mShadowMap;

  std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
  std::vector<std::unique_ptr<Texture>> mUnnamedTextures;

  ComPtr<ID3D12RootSignature> mRootSignature;

  // SRV heap.
  ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap;
  // CBV heap.
  ComPtr<ID3D12DescriptorHeap> mCbvDescriptorHeap;
  // Shader resource.
  // StructuredBuffer<MaterialData> gMaterialData : register(t0, space1).
  ComPtr<ID3D12Resource> mSrvResource;

  // Index locations of SRVs in mSrvDescriptorHeap.
  UINT mSkyTexHeapIndex = 0;
  UINT mShadowMapHeapIndex = 0;
  UINT mNullCubeSrvIndex = 0;
  UINT mNullTexSrvIndex = 0;

  // TODO: what's this SRV for?
  CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

  std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

  std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

  std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
  std::vector<std::unique_ptr<MeshGeometry>> mUnnamedGeometries;

  std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
  std::vector<std::unique_ptr<Material>> mUnnamedMaterials;

  std::vector<std::unique_ptr<RenderItem>> mAllRitems;

  // One layer per PSO.
  std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

  std::vector<std::unique_ptr<FrameResource>> mFrameResources;
  FrameResource *mCurrFrameResource = nullptr;
  int mCurrFrameResourceIndex = 0;

  std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

  float mLightRotationAngle = 0.0f;
  XMFLOAT3 mBaseLightDirections[3] = {
    XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
    XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
    XMFLOAT3(0.0f, -0.707f, -0.707f)
  };
  XMFLOAT3 mRotatedLightDirections[3];

  // Light's view frustum.
  float mLightNearZ = 0.0f;
  float mLightFarZ = 0.0f;
  XMFLOAT3 mLightPosW;
  XMFLOAT4X4 mLightView = Math::Identity4x4();
  XMFLOAT4X4 mLightProj = Math::Identity4x4();
  XMFLOAT4X4 mShadowTransform = Math::Identity4x4();

  PassConstants mMainPassCB;
  PassConstants mShadowPassCB;

  POINT mLastMousePos;
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
  mCamera.SetPosition(0.0f, 2.0f, -15.0f);

  // Fixed resolution? Yes, because what the light source sees is independent of
  // what the camera sees, the size of the window, and the size of the viewport.
  mShadowMap = std::make_unique<ShadowMap>(md3dDevice.Get(), 2048, 2048);

  LoadModelFromGLTF();

  LoadTextures();
  LoadTexturesFromGLTF();
  BuildRootSignature();
  BuildDescriptorHeaps();
  BuildShadersAndInputLayout();
  BuildShapeGeometry();
  BuildMainModelGeometry();
  BuildGeometryFromGLTF();
  BuildMaterials();
  BuildRenderItems();
  BuildFrameResources();
  BuildPSOs();

  ThrowIfFailed(mCommandList->Close());
  ID3D12CommandList *cmdsLists[] = {
    mCommandList.Get()
  };
  mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
  FlushCommandQueue();

  return true;
}

void ShadowMappingApp::LoadModelFromGLTF() {
  mGLTFLoader = std::make_unique<GLTFLoader>(string("C:/Users/carlo/Code/src/github.com/carlos-lopez-garces/d3d12/Assets/Sponza/Sponza.gltf"));
  mGLTFLoader->LoadModel();
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
      L"Assets/sponza_floor_a.dds",
      L"Assets/sponza_floor_a_normal.dds",
      L"Assets/white1x1.dds",
      L"Assets/default_nmap.dds",
      L"Assets/cosmic_sky.dds"
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

void ShadowMappingApp::LoadTexturesFromGLTF() {
  vector<GLTFTextureData> gltfTextures = mGLTFLoader->LoadTextures();
  unsigned int textureCount = gltfTextures.size();
  mUnnamedTextures.resize(textureCount);

  for (int i = 0; i < textureCount; ++i) {
    GLTFTextureData &gltfTexture = gltfTextures[i];

    auto texture = make_unique<Texture>();
    texture->Filename = AnsiToWString(gltfTexture.uri);

    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
      md3dDevice.Get(),
      mCommandList.Get(),
      texture->Filename.c_str(),
      texture->Resource,
      texture->UploadHeap
    ));

    mUnnamedTextures[i] = std::move(texture);
  }
}

void ShadowMappingApp::BuildRootSignature() {
  // TextureCube gCubeMap : register(t0).
  CD3DX12_DESCRIPTOR_RANGE texTable0;
  // 2 descriptors in range, base shader register 0, register space 0.
  texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

  // Texture2D gTextureMaps[100] : register(t2).
  // Sponza glTF file comes with about 70 textures, that's why 100.
  CD3DX12_DESCRIPTOR_RANGE texTable1;
  // 100 descriptors in range, base shader register 2, register space 0.
  texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 80, 2, 0);

#define NUM_ROOT_PARAMETERS 5
  CD3DX12_ROOT_PARAMETER rootParameters[NUM_ROOT_PARAMETERS];
  // CBVs in shader registers 0 and 1 in register space 0.
  // cbuffer cbPerObject : register(b0).
  rootParameters[0].InitAsConstantBufferView(0);
  // cbuffer cbPass : register(b1).
  rootParameters[1].InitAsConstantBufferView(1);
  // StructuredBuffer<MaterialData> gMaterialData : register(t0, space1).
  rootParameters[2].InitAsShaderResourceView(0, 1);
  rootParameters[3].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
  rootParameters[4].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

  auto staticSamplers = GetStaticSamplers();

  CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(
    NUM_ROOT_PARAMETERS,
    rootParameters,
    (UINT)staticSamplers.size(),
    staticSamplers.data(),
    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
  );

  ComPtr<ID3DBlob> serializedRootSignature = nullptr;
  ComPtr<ID3DBlob> errorBlob = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(
    &rootSignatureDesc,
    D3D_ROOT_SIGNATURE_VERSION_1,
    serializedRootSignature.GetAddressOf(),
    errorBlob.GetAddressOf()
  );
  if (errorBlob != nullptr) {
    ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
  }
  ThrowIfFailed(hr);

  ThrowIfFailed(md3dDevice->CreateRootSignature(
    0,
    serializedRootSignature->GetBufferPointer(),
    serializedRootSignature->GetBufferSize(),
    IID_PPV_ARGS(mRootSignature.GetAddressOf())
  ));

  // THIS WAS MY ATTEMPT.
  // ================================================================
  // CBV, SRV, and UAV descriptor heaps.
  //D3D12_DESCRIPTOR_HEAP_DESC cbvSrvUavHeapDesc;
  //cbvSrvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  //cbvSrvUavHeapDesc.NumDescriptors = 4;
  //cbvSrvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  //cbvSrvUavHeapDesc.NodeMask = 0;
  //
  //ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
  //  &cbvSrvUavHeapDesc,
  //  IID_PPV_ARGS(&mCbvSrvUavHeap)
  //));
  //
  //#define NUM_ROOT_PARAMETERS 6
  //
  //// D3A12_ROOT_PARAMETER rootParameters[NUM_ROOT_PARAMETERS];
  //CD3DX12_ROOT_PARAMETER rootParameters[NUM_ROOT_PARAMETERS];
  //
  //// TextureCube gCubeMap : register(t0).
  //// rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  //// rootParameters[0].Descriptor.ShaderRegister = 0;
  //// rootParameters[0].Descriptor.RegisterSpace = 0;
  //// rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  //rootParameters[0].InitAsShaderResourceView(0);
  //
  //// Texture2D gShadowMap : register(t1).
  //// rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  //// rootParameters[1].Descriptor.ShaderRegister = 1;
  //// rootParameters[1].Descriptor.RegisterSpace = 0;
  //// rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  //rootParameters[1].InitAsShaderResourceView(1);
  //
  //// Texture2D gTextureMaps[10] : register(t2).
  //// rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  //// rootParameters[2].Descriptor.ShaderRegister = 2;
  //// rootParameters[2].Descriptor.RegisterSpace = 0;
  //// rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  //rootParameters[2].InitAsShaderResourceView(2);
  //
  //// StructuredBuffer<MaterialData> gMaterialData : register(t0, space1).
  //// rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  //// rootParameters[3].Descriptor.ShaderRegister = 0;
  //// rootParameters[3].Descriptor.RegisterSpace = 1;
  //// rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  //// Register 0 again, but in register space 1 now.
  //rootParameters[3].InitAsShaderResourceView(0, 1);
  //
  //// cbuffer cbPerObject : register(b0).
  //// rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  //// rootParameters[4].Descriptor.ShaderRegister = 0;
  //// rootParameters[4].Descriptor.RegisterSpace = 0;
  //// rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  //rootParameters[4].InitAsConstantBufferView(0);
  //
  //// cbuffer cbPass : register(b1).
  //// rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  //// rootParameters[5].Descriptor.ShaderRegister = 1;
  //// rootParameters[5].Descriptor.RegisterSpace = 0;
  //// rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  //rootParameters[5].InitAsConstantBufferView(1);
  //
  //D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc;
  //rootSignatureDesc.NumParameters = NUM_ROOT_PARAMETERS;
  //rootSignatureDesc.pParameters = rootParameters;
  //rootSignatureDesc.NumStaticSamplers = 7;
  //rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  //
  //ID3DBlob *serializedRootSignature;
  //ThrowIfFailed(D3DCreateBlob(
  //  sizeof(rootSignatureDesc),
  //  &serializedRootSignature
  //));
  //
  //ThrowIfFailed(D3D12SerializeRootSignature(
  //  &rootSignatureDesc,
  //  D3D_ROOT_SIGNATURE_VERSION_1,
  //  &serializedRootSignature,
  //  nullptr
  //));
  //
  //ThrowIfFailed(md3dDevice->CreateRootSignature(
  //  // Only 1 GPU.
  //  0,
  //  serializedRootSignature->GetBufferPointer(),
  //  serializedRootSignature->GetBufferSize(),
  //  IID_PPV_ARGS(&mRootSignature)
  //));
  // ================================================================
}

void ShadowMappingApp::BuildDescriptorHeaps() {
  D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
  // TODO: why do we need 14? 6 textures, each with their normal map, and the sky.
  srvHeapDesc.NumDescriptors = 14 + mUnnamedTextures.size();
  srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

  // Fill out the SRV heap.
  CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
    mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart()
  );

  std::vector<ComPtr<ID3D12Resource>> tex2DList = {
    mTextures["bricksDiffuseMap"]->Resource,
    mTextures["bricksNormalMap"]->Resource,
    mTextures["tileDiffuseMap"]->Resource,
    mTextures["tileNormalMap"]->Resource,
    mTextures["defaultDiffuseMap"]->Resource,
    mTextures["defaultNormalMap"]->Resource
  };
  auto skyCubeMap = mTextures["skyCubeMap"]->Resource;

  // Create SRVs for textures in SRV heap.
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MostDetailedMip = 0;
  srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
  for (UINT i = 0; i < (UINT)tex2DList.size(); ++i) {
    // hDescriptor is pointing at the start of the block in the SRV heap where
    // we are going to create this SRV.
    srvDesc.Format = tex2DList[i]->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = tex2DList[i]->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(tex2DList[i].Get(), &srvDesc, hDescriptor);
    // Advance hDescriptor to point to the block where the next SRV will be created.
    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
  }

  // SRV descriptors for textures loaded from glTF.
  for (auto &tex : mUnnamedTextures) {
    // hDescriptor is pointing at the start of the block in the SRV heap where
    // we are going to create this SRV.
    srvDesc.Format = tex->Resource->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = tex->Resource->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(tex->Resource.Get(), &srvDesc, hDescriptor);
    // Advance hDescriptor to point to the block where the next SRV will be created.
    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
  }

  // SRV for the sky cubemap.
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
  srvDesc.TextureCube.MostDetailedMip = 0;
  srvDesc.TextureCube.MipLevels = skyCubeMap->GetDesc().MipLevels;
  srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
  srvDesc.Format = skyCubeMap->GetDesc().Format;
  md3dDevice->CreateShaderResourceView(skyCubeMap.Get(), &srvDesc, hDescriptor);

  // Save heap indices of SRVs.
  mSkyTexHeapIndex = (UINT)tex2DList.size() + mUnnamedTextures.size();
  mShadowMapHeapIndex = mSkyTexHeapIndex + 1;
  mNullCubeSrvIndex = mShadowMapHeapIndex + 1;
  mNullTexSrvIndex = mNullCubeSrvIndex + 1;

  auto srvCpuStart = mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  auto srvGpuStart = mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
  auto dsvCpuStart = mDsvHeap->GetCPUDescriptorHandleForHeapStart();

  // TODO: what's this SRV for?
  auto nullSrv = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, mNullCubeSrvIndex, mCbvSrvUavDescriptorSize);
  mNullSrv = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, mNullCubeSrvIndex, mCbvSrvUavDescriptorSize);
  md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
  nullSrv.Offset(1, mCbvSrvUavDescriptorSize);
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srvDesc.Texture2D.MostDetailedMip = 0;
  srvDesc.Texture2D.MipLevels = 1;
  srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
  md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

  mShadowMap->BuildDescriptors(
    CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, mShadowMapHeapIndex, mCbvSrvUavDescriptorSize),
    CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, mShadowMapHeapIndex, mCbvSrvUavDescriptorSize),
    CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvCpuStart, 1, mDsvDescriptorSize)
  );

  // THIS WAS MY ATTEMPT.
  // ================================================================

  // // SRV heap.
  // D3D12_DESCRIPTOR_HEAP_DESC srvDescriptorHeapDesc;
  // srvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  // srvDescriptorHeapDesc.NumDescriptors = 1;
  // srvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  // srvDescriptorHeapDesc.NodeMask = 0;
  // ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
  //   &srvDescriptorHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)
  // ));

  // // Shader resource.
  // // StructuredBuffer<MaterialData> gMaterialData : register(t0, space1).
  // CD3DX12_RESOURCE_DESC srvResource;
  // srvResource.Alignment = 0;
  // srvResource.DepthOrArraySize = 1;
  // srvResource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  // srvResource.Flags = D3D12_RESOURCE_FLAG_NONE;
  // srvResource.Format = DXGI_FORMAT_UNKNOWN;
  // // TODO: more ...
  // CD3DX12_HEAP_PROPERTIES srvHeapProperties;
  // srvHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
  // ThrowIfFailed(md3dDevice->CreateCommittedResource(
  //   &srvHeapProperties,
  //   D3D12_HEAP_FLAG_NONE,
  //   &srvResource,
  //   D3D12_RESOURCE_STATE_GENERIC_READ,
  //   nullptr,
  //   IID_PPV_ARGS(mSrvResource.GetAddressOf())
  // ));

  // // SRV.
  // D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
  // srvDesc.Format = DXGI_FORMAT_UNKNOWN;
  // srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  // srvDesc.Shader4ComponentMapping = D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0;
  // // TODO.
  // srvDesc.Buffer;
  // md3dDevice->CreateShaderResourceView(
  //   mSrvResource.Get(),
  //   &srvDesc,
  //   mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart()
  // );

  // // CBV heap.
  // D3D12_DESCRIPTOR_HEAP_DESC cbvDescriptorHeapDesc;
  // cbvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  // cbvDescriptorHeapDesc.NumDescriptors = 2;
  // cbvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  // cbvDescriptorHeapDesc.NodeMask = 0;
  // ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
  //   &cbvDescriptorHeapDesc, IID_PPV_ARGS(mCbvDescriptorHeap.GetAddressOf())
  // ));

  // ================================================================
}

void ShadowMappingApp::BuildShadersAndInputLayout() {
  const D3D_SHADER_MACRO alphaTestDefines[] = {
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Src/ShadowMapping/ShadowMapping.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Src/ShadowMapping/ShadowMapping.hlsl", nullptr, "PS", "ps_5_1");

  mShaders["shadowVS"] = d3dUtil::CompileShader(L"Src/ShadowMapping/Shadows.hlsl", nullptr, "VS", "vs_5_1");
  mShaders["shadowOpaquePS"] = d3dUtil::CompileShader(L"Src/ShadowMapping/Shadows.hlsl", nullptr, "PS", "ps_5_1");
  mShaders["shadowAlphaTestedPS"] = d3dUtil::CompileShader(L"Src/ShadowMapping/Shadows.hlsl", alphaTestDefines, "PS", "ps_5_1");

  mShaders["debugVS"] = d3dUtil::CompileShader(L"Src/ShadowMapping/ShadowDebug.hlsl", nullptr, "VS", "vs_5_1");
  mShaders["debugPS"] = d3dUtil::CompileShader(L"Src/ShadowMapping/ShadowDebug.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["skyVS"] = d3dUtil::CompileShader(L"Src/ShadowMapping/Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"Src/ShadowMapping/Sky.hlsl", nullptr, "PS", "ps_5_1");

  mInputLayout = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };
}

void ShadowMappingApp::BuildShapeGeometry() {
  GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
  GeometryGenerator::MeshData quad = geoGen.CreateQuad(0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
  UINT quadVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
  UINT quadIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

  SubmeshGeometry quadSubmesh;
  quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
  quadSubmesh.StartIndexLocation = quadIndexOffset;
  quadSubmesh.BaseVertexLocation = quadVertexOffset;

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() + 
    quad.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for(size_t i = 0; i < box.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
		vertices[k].TangentU = box.Vertices[i].TangentU;
	}

	for(size_t i = 0; i < grid.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
		vertices[k].TangentU = grid.Vertices[i].TangentU;
	}

	for(size_t i = 0; i < sphere.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;
	}

	for(size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
	}

  for(int i = 0; i < quad.Vertices.size(); ++i, ++k) {
    vertices[k].Pos = quad.Vertices[i].Position;
    vertices[k].Normal = quad.Vertices[i].Normal;
    vertices[k].TexC = quad.Vertices[i].TexC;
    vertices[k].TangentU = quad.Vertices[i].TangentU;
  }

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
  indices.insert(indices.end(), std::begin(quad.GetIndices16()), std::end(quad.GetIndices16()));

  const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
  const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
    md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader
  );

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
    md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader
  );

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
  geo->DrawArgs["quad"] = quadSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void ShadowMappingApp::BuildMainModelGeometry() {
  std::ifstream fin("Assets/car.txt");

  if (!fin) {
      MessageBox(0, L"Assets/car.txt not found.", 0, 0);
      return;
  }

  UINT vcount = 0;
  UINT tcount = 0;
  std::string ignore;

  fin >> ignore >> vcount;
  fin >> ignore >> tcount;
  fin >> ignore >> ignore >> ignore >> ignore;

  XMFLOAT3 vMinf3(+Math::Infinity, +Math::Infinity, +Math::Infinity);
  XMFLOAT3 vMaxf3(-Math::Infinity, -Math::Infinity, -Math::Infinity);

  XMVECTOR vMin = XMLoadFloat3(&vMinf3);
  XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

  std::vector<Vertex> vertices(vcount);
  for (UINT i = 0; i < vcount; ++i) {
    fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
    fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;

    vertices[i].TexC = { 0.0f, 0.0f };

    XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

    XMVECTOR N = XMLoadFloat3(&vertices[i].Normal);

    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if(fabsf(XMVectorGetX(XMVector3Dot(N, up))) < 1.0f - 0.001f) {
        XMVECTOR T = XMVector3Normalize(XMVector3Cross(up, N));
        XMStoreFloat3(&vertices[i].TangentU, T);
    } else {
        up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        XMVECTOR T = XMVector3Normalize(XMVector3Cross(N, up));
        XMStoreFloat3(&vertices[i].TangentU, T);
    }
    
    vMin = XMVectorMin(vMin, P);
    vMax = XMVectorMax(vMax, P);
  }

  BoundingBox bounds;
  XMStoreFloat3(&bounds.Center, 0.5f*(vMin + vMax));
  XMStoreFloat3(&bounds.Extents, 0.5f*(vMax - vMin));

  fin >> ignore;
  fin >> ignore;
  fin >> ignore;

  std::vector<std::int32_t> indices(3 * tcount);
  for (UINT i = 0; i < tcount; ++i) {
      fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
  }

  fin.close();

  const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

  const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

  auto geo = std::make_unique<MeshGeometry>();
  geo->Name = "mainModelGeo";

  ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
  CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

  ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
  CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

  geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
    md3dDevice.Get(), mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader
  );

  geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
    md3dDevice.Get(), mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader
  );

  geo->VertexByteStride = sizeof(Vertex);
  geo->VertexBufferByteSize = vbByteSize;
  geo->IndexFormat = DXGI_FORMAT_R32_UINT;
  geo->IndexBufferByteSize = ibByteSize;

  SubmeshGeometry submesh;
  submesh.IndexCount = (UINT)indices.size();
  submesh.StartIndexLocation = 0;
  submesh.BaseVertexLocation = 0;
  submesh.Bounds = bounds;

  geo->DrawArgs["mainModel"] = submesh;

  mGeometries[geo->Name] = std::move(geo);
}

void ShadowMappingApp::BuildGeometryFromGLTF() {
    unsigned int primCount = mGLTFLoader->getPrimitiveCount();
    mUnnamedGeometries.resize(primCount);

    for (int primIdx = 0; primIdx < primCount; ++primIdx) {
        GLTFPrimitiveData loadedData = mGLTFLoader->LoadPrimitive(0, primIdx);

        std::vector<std::uint16_t> &indices = loadedData.indices;
        std::vector<Vertex> vertices(loadedData.positions.size());

        float scale = 0.08;
        for (int i = 0; i < loadedData.positions.size(); ++i) {
            // TODO: overloaded operator XMFLOAT3 * float not recognized.
            vertices[i].Pos.x = loadedData.positions[i].x * scale;
            vertices[i].Pos.y = loadedData.positions[i].y * scale;
            vertices[i].Pos.z = loadedData.positions[i].z * scale;

            vertices[i].Normal.x = loadedData.normals[i].x;
            vertices[i].Normal.y = loadedData.normals[i].y;
            vertices[i].Normal.z = loadedData.normals[i].z;

            vertices[i].TexC.x = loadedData.uvs[i].x;
            vertices[i].TexC.y = loadedData.uvs[i].y;
        }

        const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

        const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

        auto geo = std::make_unique<MeshGeometry>();
        geo->Name = std::to_string(primIdx);

        ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
        CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

        ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
        CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

        geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
            md3dDevice.Get(),
            mCommandList.Get(),
            vertices.data(),
            vbByteSize,
            geo->VertexBufferUploader
        );

        geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
            md3dDevice.Get(),
            mCommandList.Get(),
            indices.data(),
            ibByteSize,
            geo->IndexBufferUploader
        );

        geo->VertexByteStride = sizeof(Vertex);
        geo->VertexBufferByteSize = vbByteSize;
        geo->IndexFormat = DXGI_FORMAT_R16_UINT;
        geo->IndexBufferByteSize = ibByteSize;

        SubmeshGeometry submesh;
        submesh.IndexCount = (UINT)indices.size();
        submesh.StartIndexLocation = 0;
        submesh.BaseVertexLocation = 0;
        submesh.TextureIndex = loadedData.texture;

        geo->DrawArgs["mainModel"] = submesh;

        mUnnamedGeometries[primIdx] = std::move(geo);
    }
}

void ShadowMappingApp::BuildMaterials() {
  auto bricks = std::make_unique<Material>();
  bricks->Name = "bricks";
  bricks->MatCBIndex = 0;
  bricks->DiffuseSrvHeapIndex = 0;
  bricks->NormalSrvHeapIndex = 1;
  bricks->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
  bricks->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
  bricks->Roughness = 0.3f;

  auto tile = std::make_unique<Material>();
  tile->Name = "tile";
  tile->MatCBIndex = 1;
  tile->DiffuseSrvHeapIndex = 2;
  tile->NormalSrvHeapIndex = 3;
  tile->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
  tile->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
  tile->Roughness = 0.1f;

  auto mirror = std::make_unique<Material>();
  mirror->Name = "mirror";
  mirror->MatCBIndex = 2;
  mirror->DiffuseSrvHeapIndex = 4;
  mirror->NormalSrvHeapIndex = 5;
  mirror->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
  mirror->FresnelR0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
  mirror->Roughness = 0.1f;

  auto mainModelMat = std::make_unique<Material>();
  mainModelMat->Name = "mainModelMat";
  mainModelMat->MatCBIndex = 3;
  mainModelMat->DiffuseSrvHeapIndex = 4;
  mainModelMat->NormalSrvHeapIndex = 5;
  mainModelMat->DiffuseAlbedo = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
  mainModelMat->FresnelR0 = XMFLOAT3(0.6f, 0.6f, 0.6f);
  mainModelMat->Roughness = 0.2f;

  auto sky = std::make_unique<Material>();
  sky->Name = "sky";
  sky->MatCBIndex = 4;
  sky->DiffuseSrvHeapIndex = 6;
  sky->NormalSrvHeapIndex = 7;
  sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
  sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
  sky->Roughness = 1.0f;

  mMaterials["bricks"] = std::move(bricks);
  mMaterials["tile"] = std::move(tile);
  mMaterials["mirror"] = std::move(mirror);
  mMaterials["mainModelMat"] = std::move(mainModelMat);
  mMaterials["sky"] = std::move(sky);

  int cbIndex = 5;
  int srvHeapIndex = 8;
  // TODO.
  // int normalSrvHeapIndex = 9;
  mUnnamedMaterials.resize(mUnnamedTextures.size());
  for (int i = 0; i < mUnnamedTextures.size(); ++i) {
    auto material = std::make_unique<Material>();
    // TODO.
    material->Name = "unnamed";
    material->MatCBIndex = cbIndex++;
    material->DiffuseSrvHeapIndex = srvHeapIndex++;
    // TODO.
    // material->NormalSrvHeapIndex = normalSrvHeapIndex++;
    material->DiffuseAlbedo = XMFLOAT4(Colors::White);
    material->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    // Rougher than brick.
    material->Roughness = 0.3f;

    mUnnamedMaterials[i] = std::move(material);
  }
}

void ShadowMappingApp::BuildRenderItems() {
  auto skyRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skyRitem->World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
	skyRitem->TexTransform = Math::Identity4x4();
	skyRitem->ObjCBIndex = 0;
	skyRitem->Mat = mMaterials["sky"].get();
	skyRitem->Geo = mGeometries["shapeGeo"].get();
	skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Sky].push_back(skyRitem.get());
	mAllRitems.push_back(std::move(skyRitem));
    
  auto quadRitem = std::make_unique<RenderItem>();
  quadRitem->World = Math::Identity4x4();
  quadRitem->TexTransform = Math::Identity4x4();
  quadRitem->ObjCBIndex = 1;
  quadRitem->Mat = mMaterials["bricks"].get();
  quadRitem->Geo = mGeometries["shapeGeo"].get();
  quadRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  quadRitem->IndexCount = quadRitem->Geo->DrawArgs["quad"].IndexCount;
  quadRitem->StartIndexLocation = quadRitem->Geo->DrawArgs["quad"].StartIndexLocation;
  quadRitem->BaseVertexLocation = quadRitem->Geo->DrawArgs["quad"].BaseVertexLocation;

  mRitemLayer[(int)RenderLayer::Debug].push_back(quadRitem.get());
  mAllRitems.push_back(std::move(quadRitem));
    
	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 1.0f, 2.0f)*XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 0.5f, 1.0f));
	boxRitem->ObjCBIndex = 2;
	boxRitem->Mat = mMaterials["bricks"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	// mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));

  auto mainModelRitem = std::make_unique<RenderItem>();
  XMStoreFloat4x4(&mainModelRitem->World, XMMatrixScaling(0.4f, 0.4f, 0.4f)*XMMatrixTranslation(0.0f, 1.0f, 0.0f));
  mainModelRitem->TexTransform = Math::Identity4x4();
  mainModelRitem->ObjCBIndex = 3;
  mainModelRitem->Mat = mMaterials["mainModelMat"].get();
  mainModelRitem->Geo = mGeometries["mainModelGeo"].get();
  mainModelRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  mainModelRitem->IndexCount = mainModelRitem->Geo->DrawArgs["mainModel"].IndexCount;
  mainModelRitem->StartIndexLocation = mainModelRitem->Geo->DrawArgs["mainModel"].StartIndexLocation;
  mainModelRitem->BaseVertexLocation = mainModelRitem->Geo->DrawArgs["mainModel"].BaseVertexLocation;

  mRitemLayer[(int)RenderLayer::Opaque].push_back(mainModelRitem.get());
  mAllRitems.push_back(std::move(mainModelRitem));

  auto gridRitem = std::make_unique<RenderItem>();
  gridRitem->World = Math::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 4;
	gridRitem->Mat = mMaterials["tile"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
  gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
  gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	XMMATRIX brickTexTransform = XMMatrixScaling(1.5f, 2.0f, 1.0f);
	UINT objCBIndex = 5;
	for(int i = 0; i < 5; ++i) {
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i*5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i*5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-3.0f, 2.5f, -10.0f + i*5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+3.0f, 2.5f, -10.0f + i*5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["bricks"].get();
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["bricks"].get();
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->TexTransform = Math::Identity4x4();
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["mirror"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->TexTransform = Math::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["mirror"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		// mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
		// mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}

  for (int i = 0; i < mUnnamedGeometries.size(); ++i) {
    auto unnamedGeomRenderItem = std::make_unique<RenderItem>();
    unnamedGeomRenderItem->World = Math::Identity4x4();
    unnamedGeomRenderItem->TexTransform = Math::Identity4x4();
    // TODO.
    unnamedGeomRenderItem->ObjCBIndex = objCBIndex++;
    // unnamedGeomRenderItem->ObjCBIndex = 3;
    unnamedGeomRenderItem->Geo = mUnnamedGeometries[i].get();
    unnamedGeomRenderItem->Mat = mUnnamedMaterials[unnamedGeomRenderItem->Geo->DrawArgs["mainModel"].TextureIndex].get();
    unnamedGeomRenderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    unnamedGeomRenderItem->IndexCount = unnamedGeomRenderItem->Geo->DrawArgs["mainModel"].IndexCount;
    unnamedGeomRenderItem->StartIndexLocation = unnamedGeomRenderItem->Geo->DrawArgs["mainModel"].StartIndexLocation;
    unnamedGeomRenderItem->BaseVertexLocation = unnamedGeomRenderItem->Geo->DrawArgs["mainModel"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(unnamedGeomRenderItem.get());
    mAllRitems.push_back(std::move(unnamedGeomRenderItem));
  }
}

void ShadowMappingApp::BuildFrameResources() {
  for(int i = 0; i < gNumFrameResources; ++i) {
    mFrameResources.push_back(std::make_unique<FrameResource>(
      md3dDevice.Get(), 2, (UINT)mAllRitems.size(), (UINT)mMaterials.size() + mUnnamedMaterials.size())
    );
  }
}

void ShadowMappingApp::BuildPSOs() {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
  ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
  opaquePsoDesc.InputLayout = {};
  opaquePsoDesc.InputLayout.pInputElementDescs = mInputLayout.data();
  opaquePsoDesc.InputLayout.NumElements = mInputLayout.size();
  opaquePsoDesc.pRootSignature = mRootSignature.Get();
  opaquePsoDesc.VS = {};
  opaquePsoDesc.VS.pShaderBytecode = reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer());
  opaquePsoDesc.VS.BytecodeLength = mShaders["standardVS"]->GetBufferSize();
  opaquePsoDesc.PS = {};
  opaquePsoDesc.PS.pShaderBytecode = reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer());
  opaquePsoDesc.PS.BytecodeLength = mShaders["opaquePS"]->GetBufferSize();
  opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  opaquePsoDesc.SampleMask = UINT_MAX;
  opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  opaquePsoDesc.NumRenderTargets = 1;
  opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
  opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
  opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
  opaquePsoDesc.DSVFormat = mDepthStencilFormat;
  ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
    &opaquePsoDesc,
    IID_PPV_ARGS(&mPSOs["opaque"])
  ));

  D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = opaquePsoDesc;
  smapPsoDesc.RasterizerState.DepthBias = 100000;
  smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
  smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
  smapPsoDesc.pRootSignature = mRootSignature.Get();
  smapPsoDesc.VS = {
    reinterpret_cast<BYTE*>(mShaders["shadowVS"]->GetBufferPointer()),
    mShaders["shadowVS"]->GetBufferSize()
  };
  smapPsoDesc.PS = {
    reinterpret_cast<BYTE*>(mShaders["shadowOpaquePS"]->GetBufferPointer()),
    mShaders["shadowOpaquePS"]->GetBufferSize()
  };
  smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
  smapPsoDesc.NumRenderTargets = 0;
  ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
    &smapPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"]))
  );

  D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = opaquePsoDesc;
  debugPsoDesc.pRootSignature = mRootSignature.Get();
  debugPsoDesc.VS = {
      reinterpret_cast<BYTE*>(mShaders["debugVS"]->GetBufferPointer()),
      mShaders["debugVS"]->GetBufferSize()
  };
  debugPsoDesc.PS = {
      reinterpret_cast<BYTE*>(mShaders["debugPS"]->GetBufferPointer()),
      mShaders["debugPS"]->GetBufferSize()
  };
  ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
    &debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"]))
  );

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = opaquePsoDesc;
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.pRootSignature = mRootSignature.Get();
	skyPsoDesc.VS = {
		reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
		mShaders["skyVS"]->GetBufferSize()
	};
	skyPsoDesc.PS = {
		reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
		mShaders["skyPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
    &skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"]))
  );
}

void ShadowMappingApp::CreateRtvAndDsvDescriptorHeaps()
{
  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
  rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  rtvHeapDesc.NodeMask = 0;
  ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
    &rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf()))
  );

  D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
  dsvHeapDesc.NumDescriptors = 2;
  dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  dsvHeapDesc.NodeMask = 0;
  ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
    &dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf()))
  );
}

void ShadowMappingApp::Draw(const GameTimer &gt) {
  auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
  ThrowIfFailed(cmdListAlloc->Reset());
  
  ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

  ID3D12DescriptorHeap *descriptorHeaps[] = {
    mSrvDescriptorHeap.Get()
  };
  mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

  mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

  auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
  mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());
  mCommandList->SetGraphicsRootDescriptorTable(3, mNullSrv);
  mCommandList->SetGraphicsRootDescriptorTable(4, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

  DrawSceneToShadowMap();

  mCommandList->RSSetViewports(1, &mScreenViewport);
  mCommandList->RSSetScissorRects(1, &mScissorRect);

  CD3DX12_RESOURCE_BARRIER backBufferRTBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
    CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
  );
  mCommandList->ResourceBarrier(1, &backBufferRTBarrier);
  mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
  mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
  D3D12_CPU_DESCRIPTOR_HANDLE backBufferViewHandle = CurrentBackBufferView();
  D3D12_CPU_DESCRIPTOR_HANDLE depthStencilViewHandle = DepthStencilView();
  mCommandList->OMSetRenderTargets(1, &backBufferViewHandle, true, &depthStencilViewHandle);

  auto passCB = mCurrFrameResource->PassCB->Resource();
  mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

  CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
  skyTexDescriptor.Offset(mSkyTexHeapIndex, mCbvSrvUavDescriptorSize);
  mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);

  mCommandList->SetPipelineState(mPSOs["opaque"].Get());
  DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

  // mCommandList->SetPipelineState(mPSOs["debug"].Get());
  // DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Debug]);

	mCommandList->SetPipelineState(mPSOs["sky"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);

  CD3DX12_RESOURCE_BARRIER backBufferPresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
    CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT
  );
  mCommandList->ResourceBarrier(1, &backBufferPresentBarrier);

  ThrowIfFailed(mCommandList->Close());

  ID3D12CommandList* cmdsLists[] = {
    mCommandList.Get()
  };
  mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

  try {
    ThrowIfFailed(mSwapChain->Present(0, 0));
  } catch (DxException &e) {
    std::cout << md3dDevice->GetDeviceRemovedReason() << std::endl;
    exit(1);
  }
	mCurrentBackBuffer = (mCurrentBackBuffer + 1) % SwapChainBufferCount;

  mCurrFrameResource->Fence = ++mCurrentFence;

  mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ShadowMappingApp::DrawRenderItems(
  ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems
) {
  UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

  auto objectCB = mCurrFrameResource->ObjectCB->Resource();

  for (size_t i = 0; i < ritems.size(); ++i) {
    auto ri = ritems[i];
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = ri->Geo->VertexBufferView();
    D3D12_INDEX_BUFFER_VIEW indexBufferView = ri->Geo->IndexBufferView();
    cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
    cmdList->IASetIndexBuffer(&indexBufferView);
    cmdList->IASetPrimitiveTopology(ri->PrimitiveType);
    D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
    cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
    cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
  }
}

void ShadowMappingApp::DrawSceneToShadowMap() {
  D3D12_VIEWPORT shadowMapViewport = mShadowMap->Viewport();
  mCommandList->RSSetViewports(1, &shadowMapViewport);
  D3D12_RECT shadowMapScissorRect = mShadowMap->ScissorRect();
  mCommandList->RSSetScissorRects(1, &shadowMapScissorRect);

  CD3DX12_RESOURCE_BARRIER shadowMapDepthWriteBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
    mShadowMap->Resource(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE
  );
  mCommandList->ResourceBarrier(1, &shadowMapDepthWriteBarrier);

  mCommandList->ClearDepthStencilView(
    mShadowMap->Dsv(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr
  );

  CD3DX12_CPU_DESCRIPTOR_HANDLE shadowMapDepthStencilView = mShadowMap->Dsv();
  mCommandList->OMSetRenderTargets(0, nullptr, false, &shadowMapDepthStencilView);

  UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
  auto passCB = mCurrFrameResource->PassCB->Resource();
  D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1*passCBByteSize;
  mCommandList->SetGraphicsRootConstantBufferView(1, passCBAddress);

  mCommandList->SetPipelineState(mPSOs["shadow_opaque"].Get());

  DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

  CD3DX12_RESOURCE_BARRIER shadowMapReadBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
    mShadowMap->Resource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ
  );
  mCommandList->ResourceBarrier(1, &shadowMapReadBarrier);
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> ShadowMappingApp::GetStaticSamplers() {
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP
  );

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP
  );

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, 
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, 
		D3D12_TEXTURE_ADDRESS_MODE_WRAP
  );

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP
  );

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, 
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, 
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, 
		0.0f,
		8
  );

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 
		0.0f,
		8
  );

  const CD3DX12_STATIC_SAMPLER_DESC shadow(
    6,
    D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
    D3D12_TEXTURE_ADDRESS_MODE_BORDER,
    D3D12_TEXTURE_ADDRESS_MODE_BORDER,
    D3D12_TEXTURE_ADDRESS_MODE_BORDER, 
    0.0f,
    16,
    D3D12_COMPARISON_FUNC_LESS_EQUAL,
    D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK
  );

	return { 
		pointWrap,
    pointClamp,
		linearWrap,
    linearClamp, 
		anisotropicWrap,
    anisotropicClamp,
    shadow 
  };
}

void ShadowMappingApp::Update(const GameTimer &gt) {
  OnKeyboardInput(gt);

  mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
  mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

  if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence) {
    HANDLE eventHandle = CreateEventExW(nullptr, nullptr, false, EVENT_ALL_ACCESS);
    ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
    WaitForSingleObject(eventHandle, INFINITE);
    CloseHandle(eventHandle);
  }

  mLightRotationAngle += 0.1f * gt.DeltaTime();
  XMMATRIX R = XMMatrixRotationY(mLightRotationAngle);
  for(int i = 0; i < 3; ++i) {
    XMVECTOR lightDir = DirectX::XMLoadFloat3(&mBaseLightDirections[i]);
    lightDir = DirectX::XMVector3TransformNormal(lightDir, R);
    DirectX::XMStoreFloat3(&mRotatedLightDirections[i], lightDir);
  }

  AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
  UpdateShadowTransform(gt);
	UpdateMainPassCB(gt);
  UpdateShadowPassCB(gt);
}

void ShadowMappingApp::UpdateObjectCBs(const GameTimer &gt) {
  auto currObjectCB = mCurrFrameResource->ObjectCB.get();
  for (auto &ritem : mAllRitems) {
    if (ritem->NumFramesDirty > 0) {
      XMMATRIX world = DirectX::XMLoadFloat4x4(&ritem->World);
      XMMATRIX texTransform = DirectX::XMLoadFloat4x4(&ritem->TexTransform);
      ObjectConstants objConstants;
      DirectX::XMStoreFloat4x4(&objConstants.World, DirectX::XMMatrixTranspose(world));
      DirectX::XMStoreFloat4x4(&objConstants.TexTransform, DirectX::XMMatrixTranspose(texTransform));
      objConstants.MaterialIndex = ritem->Mat->MatCBIndex;
      currObjectCB->CopyData(ritem->ObjCBIndex, objConstants);
      ritem->NumFramesDirty--;
    }
  }
}

void ShadowMappingApp::UpdateMaterialBuffer(const GameTimer &gt) {
  auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
  for (auto &entry : mMaterials) {
    Material *mat = entry.second.get();
    if (mat->NumFramesDirty > 0) {
      XMMATRIX matTransform = DirectX::XMLoadFloat4x4(&mat->MatTransform);
      MaterialData matData;
      matData.DiffuseAlbedo = mat->DiffuseAlbedo;
      matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			DirectX::XMStoreFloat4x4(&matData.MatTransform, DirectX::XMMatrixTranspose(matTransform));
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
			matData.NormalMapIndex = mat->NormalSrvHeapIndex;
			currMaterialBuffer->CopyData(mat->MatCBIndex, matData);
			mat->NumFramesDirty--;
    }
  }

  for (auto &mat : mUnnamedMaterials) {
    if (mat->NumFramesDirty > 0) {
      XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

      // The application changed the material. Update the copies stored in constant
      // buffers of each of the frame resources.
      MaterialData matConstants;
      matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
      matConstants.FresnelR0 = mat->FresnelR0;
      matConstants.Roughness = mat->Roughness;
      XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));
      // TODO.
      matConstants.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
      currMaterialBuffer->CopyData(mat->MatCBIndex, matConstants);
      mat->NumFramesDirty--;
    }
  }
}

void ShadowMappingApp::UpdateShadowTransform(const GameTimer &gt) {
  // The main light's direction vector lies in the lower hemisphere of the scene's bounding sphere.
  XMVECTOR lightDir = DirectX::XMLoadFloat3(&mRotatedLightDirections[0]);
  // Translates the light back along its direction vector, positioning it in the upper hemisphere.
  XMVECTOR lightPos = -2.0 * mSceneBounds.Radius * lightDir;
  XMVECTOR targetPos = DirectX::XMLoadFloat3(&mSceneBounds.Center);
  XMVECTOR lightUp = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
  XMMATRIX lightView = DirectX::XMMatrixLookAtLH(lightPos, targetPos, lightUp);
  // Light's world space position.
  DirectX::XMStoreFloat3(&mLightPosW, lightPos);

  // Transform bounding sphere to light space.
  XMFLOAT3 sphereCenterLS;
  DirectX::XMStoreFloat3(&sphereCenterLS, DirectX::XMVector3TransformCoord(targetPos, lightView));

  // Orthographic frustum. Left, bottom, near, right, top, far.
  float l = sphereCenterLS.x - mSceneBounds.Radius;
  float b = sphereCenterLS.y - mSceneBounds.Radius;
  float n = sphereCenterLS.z - mSceneBounds.Radius;
  float r = sphereCenterLS.x + mSceneBounds.Radius;
  float t = sphereCenterLS.y + mSceneBounds.Radius;
  float f = sphereCenterLS.z + mSceneBounds.Radius;

  mLightNearZ = n;
  mLightFarZ = f;
  // Orthographic projection matrix.
  XMMATRIX lightProj = DirectX::XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

  // Transform NDC space [-1,+1]^2 to texture space [0,1]^2.
  XMMATRIX T(
    0.5f, 0.0f, 0.0f, 0.0f,
    0.0f, -0.5f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.5f, 0.5f, 0.0f, 1.0f
  );

  XMMATRIX S = lightView * lightProj * T;
  DirectX::XMStoreFloat4x4(&mLightView, lightView);
  DirectX::XMStoreFloat4x4(&mLightProj, lightProj);
  DirectX::XMStoreFloat4x4(&mShadowTransform, S);
}

void ShadowMappingApp::UpdateMainPassCB(const GameTimer &gt) {
  XMMATRIX view = mCamera.GetView();
  XMMATRIX proj = mCamera.GetProj();
  XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);
  XMVECTOR viewDeterminant = DirectX::XMMatrixDeterminant(view);
  XMMATRIX invView = DirectX::XMMatrixInverse(&viewDeterminant, view);
  XMVECTOR projDeterminant = DirectX::XMMatrixDeterminant(proj);
  XMMATRIX invProj = DirectX::XMMatrixInverse(&projDeterminant, proj);
  XMVECTOR viewProjDeterminant = DirectX::XMMatrixDeterminant(viewProj);
  XMMATRIX invViewProj = DirectX::XMMatrixInverse(&viewProjDeterminant, viewProj);
  XMMATRIX shadowTransform = DirectX::XMLoadFloat4x4(&mShadowTransform);

  XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
  XMStoreFloat4x4(&mMainPassCB.ShadowTransform, XMMatrixTranspose(shadowTransform));

  mMainPassCB.EyePosW = mCamera.GetPosition3f();
  mMainPassCB.RenderTargetSize = XMFLOAT2((float) mClientWidth, (float) mClientHeight);
  mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
  mMainPassCB.NearZ = 1.0f;
  mMainPassCB.FarZ = 1000.0f;
  mMainPassCB.TotalTime = gt.TotalTime();
  mMainPassCB.DeltaTime = gt.DeltaTime();
  mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
  mMainPassCB.Lights[0].Direction = mRotatedLightDirections[0];
  mMainPassCB.Lights[0].Strength = { 0.9f, 0.8f, 0.7f };
	mMainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
	mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

  auto currPassCB = mCurrFrameResource->PassCB.get();
  currPassCB->CopyData(0, mMainPassCB);
}

void ShadowMappingApp::UpdateShadowPassCB(const GameTimer &gt) {
  XMMATRIX view = DirectX::XMLoadFloat4x4(&mLightView);
  XMMATRIX proj = DirectX::XMLoadFloat4x4(&mLightProj);
  XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);
  XMVECTOR viewDeterminant = DirectX::XMMatrixDeterminant(view);
  XMMATRIX invView = DirectX::XMMatrixInverse(&viewDeterminant, view);
  XMVECTOR projDeterminant = DirectX::XMMatrixDeterminant(proj);
  XMMATRIX invProj = DirectX::XMMatrixInverse(&projDeterminant, proj);
  XMVECTOR viewProjDeterminant = DirectX::XMMatrixDeterminant(viewProj);
  XMMATRIX invViewProj = DirectX::XMMatrixInverse(&viewProjDeterminant, viewProj);

  UINT w = mShadowMap->Width();
  UINT h = mShadowMap->Height();

  XMStoreFloat4x4(&mShadowPassCB.View, XMMatrixTranspose(view));
  XMStoreFloat4x4(&mShadowPassCB.InvView, XMMatrixTranspose(invView));
  XMStoreFloat4x4(&mShadowPassCB.Proj, XMMatrixTranspose(proj));
  XMStoreFloat4x4(&mShadowPassCB.InvProj, XMMatrixTranspose(invProj));
  XMStoreFloat4x4(&mShadowPassCB.ViewProj, XMMatrixTranspose(viewProj));
  XMStoreFloat4x4(&mShadowPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
  mShadowPassCB.EyePosW = mLightPosW;
  mShadowPassCB.RenderTargetSize = XMFLOAT2((float) w, (float) h);
  mShadowPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / w, 1.0f / h);
  mShadowPassCB.NearZ = mLightNearZ;
  mShadowPassCB.FarZ = mLightFarZ;

  auto currPassCB = mCurrFrameResource->PassCB.get();
  currPassCB->CopyData(1, mShadowPassCB);
}

void ShadowMappingApp::AnimateMaterials(const GameTimer& gt) {}

void ShadowMappingApp::OnResize() {
  D3DApp::OnResize();
  mCamera.SetLens(0.25f * Math::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void ShadowMappingApp::OnMouseDown(WPARAM btnState, int x, int y) {
  mLastMousePos.x = x;
  mLastMousePos.y = y;
  SetCapture(mhMainWnd);
}

void ShadowMappingApp::OnMouseUp(WPARAM btnState, int x, int y) {
  ReleaseCapture();
}

void ShadowMappingApp::OnMouseMove(WPARAM btnState, int x, int y) {
  if((btnState & MK_LBUTTON) != 0) {
    float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
    float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));
    mCamera.Pitch(dy);
    mCamera.RotateY(dx);
  }
  mLastMousePos.x = x;
  mLastMousePos.y = y;
}

void ShadowMappingApp::OnKeyboardInput(const GameTimer &gt) {
	const float dt = gt.DeltaTime();

	if(GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(10.0f*dt);

	if(GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-10.0f*dt);

	if(GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-10.0f*dt);

	if(GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(10.0f*dt);

	mCamera.UpdateViewMatrix();
}

int WINAPI WinMain(
  HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd
) {
#if defined(DEBUG) | defined(_DEBUG)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  try {
    ShadowMappingApp app(hInstance);
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
