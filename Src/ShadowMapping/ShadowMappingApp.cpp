#include "../Common/d3dApp.h"
#include "../Common/Math.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"
//#include "../Common/Camera.h"
#include "FrameResource.h"
#include "ShadowMap.h"

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

  std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;

  std::vector<std::unique_ptr<RenderItem>> mAllRitems;

  // One layer per PSO.
  std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

  std::vector<std::unique_ptr<FrameResource>> mFrameResources;

  std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
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
  BuildRootSignature();
  BuildDescriptorHeaps();
  BuildShadersAndInputLayout();
  BuildShapeGeometry();
  BuildSkullGeometry();
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

void ShadowMappingApp::BuildRootSignature() {
  // TextureCube gCubeMap : register(t0).
  CD3DX12_DESCRIPTOR_RANGE texTable0;
  // 2 descriptors in range, base shader register 0, register space 0.
  texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

  // Texture2D gTextureMaps[10] : register(t2).
  CD3DX12_DESCRIPTOR_RANGE texTable1;
  // 10 descriptors in range, base shader register 2, register space 0.
  texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 2, 0);

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
  // TODO: why do we need 14?
  srvHeapDesc.NumDescriptors = 14;
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

  // SRV for the sky cubemap.
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
  srvDesc.TextureCube.MostDetailedMip = 0;
  srvDesc.TextureCube.MipLevels = skyCubeMap->GetDesc().MipLevels;
  srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
  srvDesc.Format = skyCubeMap->GetDesc().Format;
  md3dDevice->CreateShaderResourceView(skyCubeMap.Get(), &srvDesc, hDescriptor);

  // Save heap indices of SRVs.
  mSkyTexHeapIndex = (UINT)tex2DList.size();
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

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Src/ShadowMapping/Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Src/ShadowMapping/Default.hlsl", nullptr, "PS", "ps_5_1");

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

void ShadowMappingApp::BuildSkullGeometry() {
  std::ifstream fin("Assets/skull.txt");

  if (!fin) {
      MessageBox(0, L"Assets/skull.txt not found.", 0, 0);
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
  geo->Name = "skullGeo";

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

  geo->DrawArgs["skull"] = submesh;

  mGeometries[geo->Name] = std::move(geo);
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

  auto skullMat = std::make_unique<Material>();
  skullMat->Name = "skullMat";
  skullMat->MatCBIndex = 3;
  skullMat->DiffuseSrvHeapIndex = 4;
  skullMat->NormalSrvHeapIndex = 5;
  skullMat->DiffuseAlbedo = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
  skullMat->FresnelR0 = XMFLOAT3(0.6f, 0.6f, 0.6f);
  skullMat->Roughness = 0.2f;

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
  mMaterials["skullMat"] = std::move(skullMat);
  mMaterials["sky"] = std::move(sky);
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

	mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));

  auto skullRitem = std::make_unique<RenderItem>();
  XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(0.4f, 0.4f, 0.4f)*XMMatrixTranslation(0.0f, 1.0f, 0.0f));
  skullRitem->TexTransform = Math::Identity4x4();
  skullRitem->ObjCBIndex = 3;
  skullRitem->Mat = mMaterials["skullMat"].get();
  skullRitem->Geo = mGeometries["skullGeo"].get();
  skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
  skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
  skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;

  mRitemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());
  mAllRitems.push_back(std::move(skullRitem));

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

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i*5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i*5.0f);

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

		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}
}

void ShadowMappingApp::BuildFrameResources() {
  for(int i = 0; i < gNumFrameResources; ++i) {
    mFrameResources.push_back(std::make_unique<FrameResource>(
      md3dDevice.Get(), 2, (UINT)mAllRitems.size(), (UINT)mMaterials.size())
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