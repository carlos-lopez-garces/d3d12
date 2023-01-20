#include "../Common/d3dApp.h"
#include "../Common/Math.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"
#include "../Common/Camera.h"
#include "FrameResource.h"
#include "RenderItem.h"

using namespace DirectX;
using namespace Microsoft::WRL;
// COM interfaces are prefixed with a capital I: ID3D12RootSignature, ID3D12DescriptorHeap, etc.
using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

enum class RenderLayer : int {
    Opaque = 0,
    Mirrors,
    Reflected,
	Transparent,
	Shadow,
	Count
};

class StencilingApp : public D3DApp {
public:
    StencilingApp(HINSTANCE hInstance);
    StencilingApp(const StencilingApp &rhs) = delete;
    StencilingApp &operator=(const StencilingApp &rhs) = delete;
    ~StencilingApp();

    virtual bool Initialize() override;

private:
    PassConstants mMainPassCB;

    PassConstants mReflectedPassCB;

    // Descriptor size for constant buffer views and shader resource views.
    // Used for computing offsets to store contiguous data in these descriptor
    // heaps.
    UINT mCbvSrvDescriptorSize = 0;

    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;

    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;

    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    RenderItem *mSkullRenderItem = nullptr;
    RenderItem *mReflectedSkullRenderItem = nullptr;
    RenderItem *mShadowedSkullRenderItem = nullptr;

    std::vector<RenderItem*> mRenderItemLayer[(int)RenderLayer::Count];

    std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;

    int mCurrFrameResourceIndex = 0;

    FrameResource* mCurrFrameResource = nullptr;

    XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };

	XMFLOAT4X4 mView = Math::Identity4x4();

	XMFLOAT4X4 mProj = Math::Identity4x4();

    XMFLOAT3 mSkullTranslation = { 0.0f, 1.0f, -5.0f };

    float mTheta = 1.24f*XM_PI;

    float mPhi = 0.42f*XM_PI;

    float mRadius = 12.0f;

    POINT mLastMousePos;

    void LoadTextures();

    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildGeometry();
    void BuildMainModelGeometry();
    void BuildMaterials();
    void BuildRenderItems();
    void BuildFrameResources();
    void BuildPSOs();

    virtual void Draw(const GameTimer& gt) override;
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

    virtual void Update(const GameTimer& gt) override;
    void UpdateCamera(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateWaves(const GameTimer& gt);
    void AnimateMaterials(const GameTimer& gt);

    void OnKeyboardInput(const GameTimer& gt);
    void OnResize();
    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();
};

StencilingApp::StencilingApp(HINSTANCE hInstance) : D3DApp(hInstance) {}

StencilingApp::~StencilingApp() {
    if (md3dDevice != nullptr) {
        FlushCommandQueue();
    }
}

bool StencilingApp::Initialize() {
    if (!D3DApp::Initialize()) {
        return false;
    }

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildGeometry();
    BuildMainModelGeometry();
    BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

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

void StencilingApp::LoadTextures() {
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

void StencilingApp::BuildRootSignature() {
    // Texture2D gDiffuseMap : register(t0);
    CD3DX12_DESCRIPTOR_RANGE texTable;
    // 1 descriptor in range, base shader register 0, default register space 0.
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    // 4 root parameters.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];
    // 1 descriptor range (the texture texture table), visible only to the pixel shader.
    slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    // cbuffer cbPerObject : register(b0).
    slotRootParameter[1].InitAsConstantBufferView(0);
    // cbuffer cbPass : register(b1).
    slotRootParameter[2].InitAsConstantBufferView(1);
    // cbuffer cbMaterial : register(b2).
    slotRootParameter[3].InitAsConstantBufferView(2);

    auto staticSamplers = GetStaticSamplers();

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(
        4,
        slotRootParameter,
        (UINT) staticSamplers.size(),
        staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    // Serialize root signature.
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

    // Create root signature.
    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSignature->GetBufferPointer(),
        serializedRootSignature->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())
    ));
}

void StencilingApp::BuildDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 3;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    // Input: descriptor of heap to create. Output: the heap.
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)
    ));

    CD3DX12_CPU_DESCRIPTOR_HANDLE heapHandle(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    auto grassTex = mTextures["grassTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto fenceTex = mTextures["fenceTex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = -1;

    srvDesc.Format = grassTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, heapHandle);

    heapHandle.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = waterTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, heapHandle);

    heapHandle.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = fenceTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(fenceTex.Get(), &srvDesc, heapHandle);
}

void StencilingApp::BuildShadersAndInputLayout() {
    const D3D_SHADER_MACRO defines[] = {
        "FOG", "1",
        NULL, NULL
    };

    const D3D_SHADER_MACRO alphaTestDefines[] = {
        "FOG", "1",
        "ALPHA_TEST", "1",
        NULL, NULL
    };

    mShaders["standardVS"] = d3dUtil::CompileShader(L"Src/Blending/Blending.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Src/Blending/Blending.hlsl", defines, "PS", "ps_5_1");
    mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Src/Blending/Blending.hlsl", alphaTestDefines, "PS", "ps_5_1");

    mInputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void StencilingApp::BuildGeometry() {
    GeometryGenerator terrainGeoGenerator;
    GeometryGenerator::MeshData grid = terrainGeoGenerator.CreateGrid(160.0f, 160.0f, 50, 50);
    std::vector<Vertex> terrainVertices(grid.Vertices.size());
    for(size_t i = 0; i < grid.Vertices.size(); ++i) {
        auto& p = grid.Vertices[i].Position;
        terrainVertices[i].Pos = p;
        terrainVertices[i].Pos.y = 0.3f*(p.z*sinf(0.1f*p.x) + p.x*cosf(0.1f*p.z));
         XMFLOAT3 n(
            -0.03f*p.z*cosf(0.1f*p.x) - 0.3f*cosf(0.1f*p.z),
            1.0f,
            -0.3f*sinf(0.1f*p.x) + 0.03f*p.x*sinf(0.1f*p.z)
        );
        XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
        XMStoreFloat3(&n, unitNormal);
        terrainVertices[i].Normal = n;
		terrainVertices[i].TexC = grid.Vertices[i].TexC;
    }
    const UINT terrainVBByteSize = (UINT) terrainVertices.size() * sizeof(Vertex);
    std::vector<std::uint16_t> terrainIndices = grid.GetIndices16();
    const UINT terrainIBByteSize = (UINT) terrainIndices.size() * sizeof(std::uint16_t);
	auto terrainGeometry = std::make_unique<MeshGeometry>();
	terrainGeometry->Name = "landGeo";
	ThrowIfFailed(D3DCreateBlob(terrainVBByteSize, &terrainGeometry->VertexBufferCPU));
	CopyMemory(terrainGeometry->VertexBufferCPU->GetBufferPointer(), terrainVertices.data(), terrainVBByteSize);
	ThrowIfFailed(D3DCreateBlob(terrainIBByteSize, &terrainGeometry->IndexBufferCPU));
	CopyMemory(terrainGeometry->IndexBufferCPU->GetBufferPointer(), terrainIndices.data(), terrainIBByteSize);
	terrainGeometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), terrainVertices.data(), terrainVBByteSize, terrainGeometry->VertexBufferUploader
    );
	terrainGeometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), terrainIndices.data(), terrainIBByteSize, terrainGeometry->IndexBufferUploader
    );
	terrainGeometry->VertexByteStride = sizeof(Vertex);
	terrainGeometry->VertexBufferByteSize = terrainVBByteSize;
	terrainGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	terrainGeometry->IndexBufferByteSize = terrainIBByteSize;
	SubmeshGeometry terrainSubmesh;
	terrainSubmesh.IndexCount = (UINT) terrainIndices.size();
	terrainSubmesh.StartIndexLocation = 0;
	terrainSubmesh.BaseVertexLocation = 0;
	terrainGeometry->DrawArgs["grid"] = terrainSubmesh;
	mGeometries["landGeo"] = std::move(terrainGeometry);

    std::vector<std::uint16_t> waterIndices(3 * mWaves->TriangleCount());
	assert(mWaves->VertexCount() < 0x0000ffff);
    int m = mWaves->RowCount();
    int n = mWaves->ColumnCount();
    int k = 0;
    for(int i = 0; i < m - 1; ++i) {
        for(int j = 0; j < n - 1; ++j) {
            waterIndices[k] = i*n + j;
            waterIndices[k + 1] = i*n + j + 1;
            waterIndices[k + 2] = (i + 1)*n + j;
            waterIndices[k + 3] = (i + 1)*n + j;
            waterIndices[k + 4] = i*n + j + 1;
            waterIndices[k + 5] = (i + 1)*n + j + 1;
            k += 6;
        }
    }
	UINT waterVBByteSize = mWaves->VertexCount()*sizeof(Vertex);
	UINT waterIBByteSize = (UINT) waterIndices.size()*sizeof(std::uint16_t);
	auto waterGeometry = std::make_unique<MeshGeometry>();
	waterGeometry->Name = "waterGeo";
	waterGeometry->VertexBufferCPU = nullptr;
	waterGeometry->VertexBufferGPU = nullptr;
	ThrowIfFailed(D3DCreateBlob(waterIBByteSize, &waterGeometry->IndexBufferCPU));
	CopyMemory(waterGeometry->IndexBufferCPU->GetBufferPointer(), waterIndices.data(), waterIBByteSize);
	waterGeometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), waterIndices.data(), waterIBByteSize, waterGeometry->IndexBufferUploader
    );
	waterGeometry->VertexByteStride = sizeof(Vertex);
	waterGeometry->VertexBufferByteSize = waterVBByteSize;
	waterGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	waterGeometry->IndexBufferByteSize = waterIBByteSize;
	SubmeshGeometry waterSubmesh;
	waterSubmesh.IndexCount = (UINT) waterIndices.size();
	waterSubmesh.StartIndexLocation = 0;
	waterSubmesh.BaseVertexLocation = 0;
	waterGeometry->DrawArgs["grid"] = waterSubmesh;
	mGeometries["waterGeo"] = std::move(waterGeometry);

	GeometryGenerator boxGeoGenerator;
	GeometryGenerator::MeshData box = boxGeoGenerator.CreateBox(8.0f, 8.0f, 8.0f, 3);
	std::vector<Vertex> boxVertices(box.Vertices.size());
	for (size_t i = 0; i < box.Vertices.size(); ++i) {
		auto& p = box.Vertices[i].Position;
		boxVertices[i].Pos = p;
		boxVertices[i].Normal = box.Vertices[i].Normal;
		boxVertices[i].TexC = box.Vertices[i].TexC;
	}
	const UINT boxVBByteSize = (UINT) boxVertices.size() * sizeof(Vertex);
	std::vector<std::uint16_t> boxIndices = box.GetIndices16();
	const UINT boxIBByteSize = (UINT) boxIndices.size() * sizeof(std::uint16_t);
	auto boxGeometry = std::make_unique<MeshGeometry>();
	boxGeometry->Name = "boxGeo";
	ThrowIfFailed(D3DCreateBlob(boxVBByteSize, &boxGeometry->VertexBufferCPU));
	CopyMemory(boxGeometry->VertexBufferCPU->GetBufferPointer(), boxVertices.data(), boxVBByteSize);
	ThrowIfFailed(D3DCreateBlob(boxIBByteSize, &boxGeometry->IndexBufferCPU));
	CopyMemory(boxGeometry->IndexBufferCPU->GetBufferPointer(), boxIndices.data(), boxIBByteSize);
	boxGeometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), boxVertices.data(), boxVBByteSize, boxGeometry->VertexBufferUploader
    );
    boxGeometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), boxIndices.data(), boxIBByteSize, boxGeometry->IndexBufferUploader
    );
	boxGeometry->VertexByteStride = sizeof(Vertex);
	boxGeometry->VertexBufferByteSize = boxVBByteSize;
	boxGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	boxGeometry->IndexBufferByteSize = boxIBByteSize;
	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT) boxIndices.size();
	boxSubmesh.StartIndexLocation = 0;
	boxSubmesh.BaseVertexLocation = 0;
	boxGeometry->DrawArgs["box"] = boxSubmesh;
	mGeometries["boxGeo"] = std::move(boxGeometry);
}

void StencilingApp::BuildMainModelGeometry() {
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

void StencilingApp::BuildPSOs() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT) mInputLayout.size() };
    opaquePsoDesc.pRootSignature = mRootSignature.Get();
    opaquePsoDesc.VS = {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };
    opaquePsoDesc.PS = {
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
        mShaders["opaquePS"]->GetBufferSize()
    };
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
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    // Take the opaque PSO as base for the transparency PSO and then fill out blend state.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;
	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_SUBTRACT;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

    // Alpha tested objects.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
	alphaTestedPsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
		mShaders["alphaTestedPS"]->GetBufferSize()
	};
	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));
}

void StencilingApp::BuildMaterials() {
    auto grassMaterial = std::make_unique<Material>();
    grassMaterial->Name = "grass";
    grassMaterial->MatCBIndex = 0;
    grassMaterial->DiffuseSrvHeapIndex = 0;
    grassMaterial->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    grassMaterial->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    grassMaterial->Roughness = 0.125f;

    auto waterMaterial = std::make_unique<Material>();
	waterMaterial->Name = "water";
	waterMaterial->MatCBIndex = 1;
	waterMaterial->DiffuseSrvHeapIndex = 1;
	waterMaterial->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	waterMaterial->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	waterMaterial->Roughness = 0.0f;

	auto wirefenceMaterial = std::make_unique<Material>();
	wirefenceMaterial->Name = "wirefence";
	wirefenceMaterial->MatCBIndex = 2;
	wirefenceMaterial->DiffuseSrvHeapIndex = 2;
	wirefenceMaterial->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefenceMaterial->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	wirefenceMaterial->Roughness = 0.25f;

    auto mainModelMaterial = std::make_unique<Material>();
    mainModelMaterial->Name = "mainModelMat";
    mainModelMaterial->MatCBIndex = 3;
    mainModelMaterial->DiffuseSrvHeapIndex = -1;
    mainModelMaterial->NormalSrvHeapIndex = 5;
    mainModelMaterial->DiffuseAlbedo = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
    mainModelMaterial->FresnelR0 = XMFLOAT3(0.6f, 0.6f, 0.6f);
    mainModelMaterial->Roughness = 0.2f;

    mMaterials["grass"] = std::move(grassMaterial);
    mMaterials["water"] = std::move(waterMaterial);
    mMaterials["wirefence"] = std::move(wirefenceMaterial);
    mMaterials["mainModelMat"] = std::move(mainModelMaterial);
}

void StencilingApp::BuildRenderItems() {
    auto wavesRenderItem = std::make_unique<RenderItem>(gNumFrameResources);
    XMStoreFloat4x4(&wavesRenderItem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    wavesRenderItem->ObjCBIndex = 0;
    wavesRenderItem->Mat = mMaterials["water"].get();
    wavesRenderItem->Geo = mGeometries["waterGeo"].get();
    wavesRenderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wavesRenderItem->IndexCount = wavesRenderItem->Geo->DrawArgs["grid"].IndexCount;
    wavesRenderItem->StartIndexLocation = wavesRenderItem->Geo->DrawArgs["grid"].StartIndexLocation;
    wavesRenderItem->BaseVertexLocation = wavesRenderItem->Geo->DrawArgs["grid"].BaseVertexLocation;
    mWavesRenderItem = wavesRenderItem.get();
    mRenderItemLayer[(int)RenderLayer::Transparent].push_back(wavesRenderItem.get());

    auto gridRenderItem = std::make_unique<RenderItem>(gNumFrameResources);
    gridRenderItem->World = Math::Identity4x4();
	XMStoreFloat4x4(&gridRenderItem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	gridRenderItem->ObjCBIndex = 1;
	gridRenderItem->Mat = mMaterials["grass"].get();
	gridRenderItem->Geo = mGeometries["landGeo"].get();
	gridRenderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRenderItem->IndexCount = gridRenderItem->Geo->DrawArgs["grid"].IndexCount;
    gridRenderItem->StartIndexLocation = gridRenderItem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRenderItem->BaseVertexLocation = gridRenderItem->Geo->DrawArgs["grid"].BaseVertexLocation;
	// mRenderItemLayer[(int)RenderLayer::Opaque].push_back(gridRenderItem.get());

	auto boxRenderItem = std::make_unique<RenderItem>(gNumFrameResources);
	XMStoreFloat4x4(&boxRenderItem->World, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
	boxRenderItem->ObjCBIndex = 2;
	boxRenderItem->Mat = mMaterials["wirefence"].get();
	boxRenderItem->Geo = mGeometries["boxGeo"].get();
	boxRenderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRenderItem->IndexCount = boxRenderItem->Geo->DrawArgs["box"].IndexCount;
	boxRenderItem->StartIndexLocation = boxRenderItem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRenderItem->BaseVertexLocation = boxRenderItem->Geo->DrawArgs["box"].BaseVertexLocation;
	// mRenderItemLayer[(int)RenderLayer::AlphaTested].push_back(boxRenderItem.get());

    auto mainModelRitem = std::make_unique<RenderItem>(gNumFrameResources);
    XMStoreFloat4x4(&mainModelRitem->World, XMMatrixScaling(0.4f, 0.4f, 0.4f)*XMMatrixTranslation(0.0f, 1.0f, 0.0f));
    mainModelRitem->TexTransform = Math::Identity4x4();
    mainModelRitem->ObjCBIndex = 3;
    mainModelRitem->Mat = mMaterials["mainModelMat"].get();
    mainModelRitem->Geo = mGeometries["mainModelGeo"].get();
    mainModelRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    mainModelRitem->IndexCount = mainModelRitem->Geo->DrawArgs["mainModel"].IndexCount;
    mainModelRitem->StartIndexLocation = mainModelRitem->Geo->DrawArgs["mainModel"].StartIndexLocation;
    mainModelRitem->BaseVertexLocation = mainModelRitem->Geo->DrawArgs["mainModel"].BaseVertexLocation;
    mRenderItemLayer[(int)RenderLayer::Opaque].push_back(mainModelRitem.get());

    mAllRenderItems.push_back(std::move(wavesRenderItem));
    mAllRenderItems.push_back(std::move(gridRenderItem));
	mAllRenderItems.push_back(std::move(boxRenderItem));
    mAllRenderItems.push_back(std::move(mainModelRitem));
}

void StencilingApp::BuildFrameResources() {
    for (int i = 0; i < gNumFrameResources; ++i) {
        mFrameResources.push_back(std::make_unique<FrameResource>(
            md3dDevice.Get(), 1, (UINT) mAllRenderItems.size(), (UINT)mMaterials.size(), mWaves->VertexCount()
        ));
    }
}

void StencilingApp::Update(const GameTimer& gt) {
    OnKeyboardInput(gt);
	UpdateCamera(gt);

    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence) {
        HANDLE eventHandle = CreateEventExW(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    AnimateMaterials(gt);
    UpdateObjectCBs(gt);
    UpdateMaterialCBs(gt);
    UpdateMainPassCB(gt);
    UpdateReflectedPassCB(gt);
}

void StencilingApp::UpdateCamera(const GameTimer& gt) {
	mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
	mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
	mEyePos.y = mRadius*cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void StencilingApp::UpdateObjectCBs(const GameTimer& gt) {
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRenderItems) {
		if (e->NumFramesDirty > 0) {
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);
			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			currObjectCB->CopyData(e->ObjCBIndex, objConstants);
			e->NumFramesDirty--;
		}
	}
}

void StencilingApp::UpdateMaterialCBs(const GameTimer& gt) {
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials) {
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0) {
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);
			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));
			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);
			mat->NumFramesDirty--;
		}
	}
}

void StencilingApp::UpdateMainPassCB(const GameTimer& gt) {
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMVECTOR viewDeterminant = DirectX::XMMatrixDeterminant(view);
	XMMATRIX invView = XMMatrixInverse(&viewDeterminant, view);
    XMVECTOR projDeterminant = DirectX::XMMatrixDeterminant(proj);
	XMMATRIX invProj = XMMatrixInverse(&projDeterminant, proj);
    XMVECTOR viewProjDeterminant = DirectX::XMMatrixDeterminant(viewProj);
	XMMATRIX invViewProj = XMMatrixInverse(&viewProjDeterminant, viewProj);
	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float) mClientWidth, (float) mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.9f, 0.9f, 0.8f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void StencilingApp::UpdateWaves(const GameTimer& gt) {
	static float t_base = 0.0f;
	if ((mTimer.TotalTime() - t_base) >= 0.25f) {
		t_base += 0.25f;
		int i = Math::Rand(4, mWaves->RowCount() - 5);
		int j = Math::Rand(4, mWaves->ColumnCount() - 5);
		float r = Math::RandF(0.2f, 0.5f);
		mWaves->Disturb(i, j, r);
	}

	mWaves->Update(gt.DeltaTime());

	auto currWavesVB = mCurrFrameResource->WavesVB.get();
	for (int i = 0; i < mWaves->VertexCount(); ++i) {
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);
		
		v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
		v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

		currWavesVB->CopyData(i, v);
	}

	mWavesRenderItem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void StencilingApp::AnimateMaterials(const GameTimer& gt) {
	auto waterMat = mMaterials["water"].get();
	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);
	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();
	if(tu >= 1.0f) {
        tu -= 1.0f;
    }
	if(tv >= 1.0f) {
        tv -= 1.0f;
    }
	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;
	waterMat->NumFramesDirty = gNumFrameResources;
}

void StencilingApp::OnKeyboardInput(const GameTimer& gt) {}

void StencilingApp::OnMouseDown(WPARAM btnState, int x, int y) {
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}

void StencilingApp::OnMouseUp(WPARAM btnState, int x, int y) {
    ReleaseCapture();
}

void StencilingApp::OnMouseMove(WPARAM btnState, int x, int y) {
    if ((btnState & MK_LBUTTON) != 0) {
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));
        mTheta += dx;
        mPhi += dy;
        mPhi = Math::Clamp(mPhi, 0.1f, Math::Pi - 0.1f);
    } else if ((btnState & MK_RBUTTON) != 0) {
        float dx = 0.2f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.2f*static_cast<float>(y - mLastMousePos.y);
        mRadius += dx - dy;
        mRadius = Math::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void StencilingApp::OnResize() {
    D3DApp::OnResize();
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*Math::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void StencilingApp::Draw(const GameTimer& gt) {
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    ThrowIfFailed(cmdListAlloc->Reset());

    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
    CD3DX12_RESOURCE_BARRIER backBufferResourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &backBufferResourceBarrier);

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferViewHandle = CurrentBackBufferView();
    D3D12_CPU_DESCRIPTOR_HANDLE depthStencilViewHandle = DepthStencilView();
    mCommandList->OMSetRenderTargets(1, &backBufferViewHandle, true, &depthStencilViewHandle);

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

    // Draw opaque layer. Mirrors are not part of this layer.
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[(int)RenderLayer::Opaque]);

    // Draw mirrors on stencil buffer:
    // a. Disable writes to the depth buffer: D3D12_DEPTH_STENCIL_DESC::DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ ZERO.
    // b. Disable writes to the back buffer: D3D12_RENDER_TARGET_BLEND_DESC::RenderTargetWriteMask = 0 in the blend state.
    // c. To render the mirror to the stencil buffer:
    // 	  i. Make the stencil test succeed always (D3D12_COMPARISON_ALWAYS), specifying
    //       that the stencil buffer pixel be replaced (D3D12_STENCIL_OP_REPLACE) with 1
    //       if the test passes later (for the objects that we'll test for reflection later).
    //       This 1 is StencilRef in the test.
    // 	 ii. If the mirror fails the depth test for a particular pixel (i.e. because another
    //       object occludes it), set D3D12_STENCIL_OP_KEEP so that the stencil buffer isn't changed.
    // 
    // At the end, the stencil buffer will have 1s where the mirror is visible to the camera and 0s
    // where the mirror is occluded or where the mirror isn't.
    mCommandList->OMSetStencilRef(1);
	mCommandList->SetPipelineState(mPSOs["markStencilMirrors"].Get());
	DrawRenderItems(mCommandList.Get(), mRenderItemLayer[(int)RenderLayer::Mirrors]);

	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRenderItemLayer[(int)RenderLayer::Transparent]);

    CD3DX12_RESOURCE_BARRIER backBufferPresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT
    );
	mCommandList->ResourceBarrier(1, &backBufferPresentBarrier);

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrentBackBuffer = (mCurrentBackBuffer + 1) % SwapChainBufferCount;

    mCurrFrameResource->Fence = ++mCurrentFence;

    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void StencilingApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems) {
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    for (size_t i = 0; i < ritems.size(); ++i) {
        auto ri = ritems[i];
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = ri->Geo->VertexBufferView();
        D3D12_INDEX_BUFFER_VIEW indexBufferView = ri->Geo->IndexBufferView();
        cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
        cmdList->IASetIndexBuffer(&indexBufferView);
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);
        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);
        if (ri->Mat->DiffuseSrvHeapIndex > -1) {
            CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		    tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);
            cmdList->SetGraphicsRootDescriptorTable(0, tex);
        }
        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> StencilingApp::GetStaticSamplers() {
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

	return { 
		pointWrap, 
        pointClamp,
		linearWrap,
        linearClamp, 
		anisotropicWrap,
        anisotropicClamp
    };
}

int WINAPI WinMain(
  HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd
) {
#if defined(DEBUG) | defined(_DEBUG)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  try {
    StencilingApp app(hInstance);
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
