#include "../Common/d3dApp.h"
#include "../Common/Math.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"
#include "../Common/Camera.h"
#include "../Common/GLTFLoader.h"
#include "FrameResource.h"
#include "RenderItem.h"
#include <iostream>
#include <memory>

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

    std::vector<std::unique_ptr<MeshGeometry>> mUnnamedGeometries;

    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    RenderItem *mMainObjRenderItem = nullptr;
    RenderItem *mReflectedMainObjRenderItem = nullptr;
    RenderItem *mShadowedMainObjRenderItem = nullptr;

    std::vector<RenderItem*> mRenderItemLayer[(int)RenderLayer::Count];

    std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;

    int mCurrFrameResourceIndex = 0;

    FrameResource* mCurrFrameResource = nullptr;

    XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };

	XMFLOAT4X4 mView = Math::Identity4x4();

	XMFLOAT4X4 mProj = Math::Identity4x4();

    XMFLOAT3 mMainObjTranslation = { 0.0f, 1.0f, -5.0f };

    float mTheta = 1.30f*XM_PI;

    float mPhi = 0.5f*XM_PI;

    float mRadius = 12.0f;

    POINT mLastMousePos;

    void LoadTextures();

    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildGeometry();
    void BuildMainModelGeometry();
    void BuildGeometryFromGLTF();
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
    void UpdateReflectedPassCB(const GameTimer& gt);
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
    BuildGeometryFromGLTF();
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
        "bricksTex",
        "checkboardTex",
        "iceTex",
        "white1x1Tex"
    };

    std::vector<std::wstring> texFilenames = {
        L"Assets/cosmic_sky.dds",
        L"Assets/checkboard.dds",
        L"Assets/ice.dds",
        L"Assets/white1x1.dds"
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
    srvHeapDesc.NumDescriptors = 4;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    // Input: descriptor of heap to create. Output: the heap.
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)
    ));

    CD3DX12_CPU_DESCRIPTOR_HANDLE heapHandle(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    auto bricksTex = mTextures["bricksTex"]->Resource;
	auto checkboardTex = mTextures["checkboardTex"]->Resource;
	auto iceTex = mTextures["iceTex"]->Resource;
    auto white1x1Tex = mTextures["white1x1Tex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = -1;

    srvDesc.Format = bricksTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, heapHandle);

    heapHandle.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = checkboardTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(checkboardTex.Get(), &srvDesc, heapHandle);

    heapHandle.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = iceTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, heapHandle);

    heapHandle.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = white1x1Tex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(white1x1Tex.Get(), &srvDesc, heapHandle);
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

    mShaders["standardVS"] = d3dUtil::CompileShader(L"Src/Stenciling/Stenciling.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Src/Stenciling/Stenciling.hlsl", defines, "PS", "ps_5_1");
    mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Src/Stenciling/Stenciling.hlsl", alphaTestDefines, "PS", "ps_5_1");

    mInputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void StencilingApp::BuildGeometry() {
    std::array<Vertex, 20> vertices = {
		Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f),
		Vertex(-3.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
		Vertex(7.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
		Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f),

		Vertex(-3.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f),
		Vertex(-3.0f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
		Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),
		Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), 
		Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(3.0f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f),
		Vertex(3.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 2.0f),
		Vertex(-3.0f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f),
		Vertex(-3.0f, 4.7f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(3.0f, 4.7f, 0.0f, 0.0f, 0.0f, -1.0f, 4.7f, 0.0f),
		Vertex(3.0f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 4.7f, 1.0f),

        Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f),
		Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
		Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)
	};

	std::array<std::int16_t, 30> indices = {
		// Floor
		0, 1, 2,	
		0, 2, 3,

		// Walls
		4, 5, 6,
		4, 6, 7,

		8, 9, 10,
		8, 10, 11,

		12, 13, 14,
		12, 14, 15,

		// Mirror
		16, 17, 18,
		16, 18, 19
	};

	SubmeshGeometry floorSubmesh;
	floorSubmesh.IndexCount = 6;
	floorSubmesh.StartIndexLocation = 0;
	floorSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry wallSubmesh;
	wallSubmesh.IndexCount = 18;
	wallSubmesh.StartIndexLocation = 6;
	wallSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry mirrorSubmesh;
	mirrorSubmesh.IndexCount = 6;
	mirrorSubmesh.StartIndexLocation = 24;
	mirrorSubmesh.BaseVertexLocation = 0;

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "roomGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
		mCommandList.Get(),
        vertices.data(),
        vbByteSize, geo->VertexBufferUploader
    );

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
		mCommandList.Get(),
        indices.data(),
        ibByteSize, geo->IndexBufferUploader
    );

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["floor"] = floorSubmesh;
	geo->DrawArgs["wall"] = wallSubmesh;
	geo->DrawArgs["mirror"] = mirrorSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void StencilingApp::BuildMainModelGeometry() {
    std::ifstream fin("Assets/car.txt");
	
	if(!fin) {
		MessageBox(0, L"Assets/car.txt not found.", 0, 0);
		return;
	}

	UINT vcount = 0;
	UINT tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;
	
	std::vector<Vertex> vertices(vcount);
	for(UINT i = 0; i < vcount; ++i) {
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
		vertices[i].TexC = { 0.0f, 0.0f };
	}

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(3 * tcount);
	for(UINT i = 0; i < tcount; ++i) {
		fin >> indices[i*3+0] >> indices[i*3+1] >> indices[i*3+2];
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
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["mainModel"] = submesh;

	mGeometries[geo->Name] = std::move(geo);
}

void StencilingApp::BuildGeometryFromGLTF() {
    std::unique_ptr<GLTFLoader> gltfLoader = std::make_unique<GLTFLoader>(string("C:/Users/carlo/Code/src/github.com/carlos-lopez-garces/d3d12/Assets/Sponza/Sponza.gltf"));
    gltfLoader->LoadModel();

    unsigned int primCount = gltfLoader->getPrimitiveCount();
    mUnnamedGeometries.resize(primCount);

    for (int primIdx = 0; primIdx < primCount; ++primIdx) {
        GLTFPrimitiveData loadedData = gltfLoader->LoadPrimitive(0, primIdx);

        std::vector<std::uint16_t> &indices = loadedData.indices;
        std::vector<Vertex> vertices(loadedData.positions.size());

        float scale = 0.005;
        for (int i = 0; i < loadedData.positions.size(); ++i) {
            vertices[i].Pos.x = loadedData.positions[i].x * scale;
            vertices[i].Pos.y = loadedData.positions[i].y * scale;
            vertices[i].Pos.z = loadedData.positions[i].z * scale;
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

        geo->DrawArgs["mainModel"] = submesh;

        mUnnamedGeometries[primIdx] = std::move(geo);
    }
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
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

    // Stencil marking of mirrors PSO.
    CD3DX12_BLEND_DESC mirrorBlendState(D3D12_DEFAULT);
    mirrorBlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    D3D12_DEPTH_STENCIL_DESC mirrorDSDesc;
    mirrorDSDesc.DepthEnable = true;
    mirrorDSDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    mirrorDSDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    mirrorDSDesc.StencilEnable = true;
    mirrorDSDesc.StencilReadMask = 0xff;
    mirrorDSDesc.StencilWriteMask = 0xff;
    mirrorDSDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    mirrorDSDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    mirrorDSDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    mirrorDSDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    // The backface configuration doesn't matter given that we don't render back faces.
    mirrorDSDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDSDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorsPsoDesc = opaquePsoDesc;
    markMirrorsPsoDesc.BlendState = mirrorBlendState;
    markMirrorsPsoDesc.DepthStencilState = mirrorDSDesc;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&markMirrorsPsoDesc, IID_PPV_ARGS(&mPSOs["markStencilMirrors"])));

    // Mirror reflections PSO.
    D3D12_DEPTH_STENCIL_DESC reflectionsDSDesc;
	reflectionsDSDesc.DepthEnable = true;
	reflectionsDSDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	reflectionsDSDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	reflectionsDSDesc.StencilEnable = true;
	reflectionsDSDesc.StencilReadMask = 0xff;
	reflectionsDSDesc.StencilWriteMask = 0xff;
	reflectionsDSDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
    // The backface configuration doesn't matter given that we don't render back faces.
	reflectionsDSDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC drawReflectionsPsoDesc = opaquePsoDesc;
	drawReflectionsPsoDesc.DepthStencilState = reflectionsDSDesc;
	drawReflectionsPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	drawReflectionsPsoDesc.RasterizerState.FrontCounterClockwise = true;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&drawReflectionsPsoDesc, IID_PPV_ARGS(&mPSOs["drawStencilReflections"])));

    D3D12_DEPTH_STENCIL_DESC shadowDSDesc;
	shadowDSDesc.DepthEnable = true;
	shadowDSDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	shadowDSDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	shadowDSDesc.StencilEnable = true;
	shadowDSDesc.StencilReadMask = 0xff;
	shadowDSDesc.StencilWriteMask = 0xff;
	shadowDSDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
    // The backface configuration doesn't matter given that we don't render back faces.
	shadowDSDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = transparentPsoDesc;
	shadowPsoDesc.DepthStencilState = shadowDSDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&mPSOs["shadow"])));
}

void StencilingApp::BuildMaterials() {
    auto brickMaterial = std::make_unique<Material>();
    brickMaterial->Name = "bricks";
    brickMaterial->MatCBIndex = 0;
    brickMaterial->DiffuseSrvHeapIndex = 0;
    brickMaterial->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    brickMaterial->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    brickMaterial->Roughness = 0.25f;

    auto checkersMaterial = std::make_unique<Material>();
	checkersMaterial->Name = "checkertile";
	checkersMaterial->MatCBIndex = 1;
	checkersMaterial->DiffuseSrvHeapIndex = 1;
	checkersMaterial->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	checkersMaterial->FresnelR0 = XMFLOAT3(0.07f, 0.07f, 0.07f);
	checkersMaterial->Roughness = 0.3f;

	auto mirrorMaterial = std::make_unique<Material>();
	mirrorMaterial->Name = "mirror";
	mirrorMaterial->MatCBIndex = 2;
	mirrorMaterial->DiffuseSrvHeapIndex = 2;
    // Note that alpah is 0.3: 30% of the mirror's albedo will be blended with 70% of
    // the reflected object's albedo.
	mirrorMaterial->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
	mirrorMaterial->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	mirrorMaterial->Roughness = 0.5f;

    auto mainModelMaterial = std::make_unique<Material>();
    mainModelMaterial->Name = "mainModelMat";
    mainModelMaterial->MatCBIndex = 3;
    mainModelMaterial->DiffuseSrvHeapIndex = 3;
    mainModelMaterial->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    mainModelMaterial->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    mainModelMaterial->Roughness = 0.3f;

    auto shadowMat = std::make_unique<Material>();
	shadowMat->Name = "shadowMat";
	shadowMat->MatCBIndex = 4;
	shadowMat->DiffuseSrvHeapIndex = 3;
	shadowMat->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);
	shadowMat->FresnelR0 = XMFLOAT3(0.001f, 0.001f, 0.001f);
	shadowMat->Roughness = 0.0f;

    mMaterials["bricks"] = std::move(brickMaterial);
    mMaterials["checkertile"] = std::move(checkersMaterial);
    mMaterials["mirror"] = std::move(mirrorMaterial);
    mMaterials["mainModelMat"] = std::move(mainModelMaterial);
    mMaterials["shadowMat"] = std::move(shadowMat);
}

void StencilingApp::BuildRenderItems() {
    auto floorRitem = std::make_unique<RenderItem>(gNumFrameResources);
	floorRitem->World = Math::Identity4x4();
	floorRitem->TexTransform = Math::Identity4x4();
	floorRitem->ObjCBIndex = 0;
	floorRitem->Mat = mMaterials["checkertile"].get();
	floorRitem->Geo = mGeometries["roomGeo"].get();
	floorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	floorRitem->IndexCount = floorRitem->Geo->DrawArgs["floor"].IndexCount;
	floorRitem->StartIndexLocation = floorRitem->Geo->DrawArgs["floor"].StartIndexLocation;
	floorRitem->BaseVertexLocation = floorRitem->Geo->DrawArgs["floor"].BaseVertexLocation;
	// mRenderItemLayer[(int)RenderLayer::Opaque].push_back(floorRitem.get());

    auto wallsRitem = std::make_unique<RenderItem>(gNumFrameResources);
	wallsRitem->World = Math::Identity4x4();
	wallsRitem->TexTransform = Math::Identity4x4();
	wallsRitem->ObjCBIndex = 1;
	wallsRitem->Mat = mMaterials["bricks"].get();
	wallsRitem->Geo = mGeometries["roomGeo"].get();
	wallsRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallsRitem->IndexCount = wallsRitem->Geo->DrawArgs["wall"].IndexCount;
	wallsRitem->StartIndexLocation = wallsRitem->Geo->DrawArgs["wall"].StartIndexLocation;
	wallsRitem->BaseVertexLocation = wallsRitem->Geo->DrawArgs["wall"].BaseVertexLocation;
	mRenderItemLayer[(int)RenderLayer::Opaque].push_back(wallsRitem.get());

	auto mainModelRenderItem = std::make_unique<RenderItem>(gNumFrameResources);
	mainModelRenderItem->World = Math::Identity4x4();
	mainModelRenderItem->TexTransform = Math::Identity4x4();
	mainModelRenderItem->ObjCBIndex = 2;
	mainModelRenderItem->Mat = mMaterials["mainModelMat"].get();
	mainModelRenderItem->Geo = mGeometries["mainModelGeo"].get();
	mainModelRenderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mainModelRenderItem->IndexCount = mainModelRenderItem->Geo->DrawArgs["mainModel"].IndexCount;
	mainModelRenderItem->StartIndexLocation = mainModelRenderItem->Geo->DrawArgs["mainModel"].StartIndexLocation;
	mainModelRenderItem->BaseVertexLocation = mainModelRenderItem->Geo->DrawArgs["mainModel"].BaseVertexLocation;
	mMainObjRenderItem = mainModelRenderItem.get();
	mRenderItemLayer[(int)RenderLayer::Opaque].push_back(mainModelRenderItem.get());

	// Reflected main object will have different world matrix, so it needs to be its own render item.
	auto reflectedmainModelRenderItem = std::make_unique<RenderItem>(gNumFrameResources);
	*reflectedmainModelRenderItem = *mainModelRenderItem;
	reflectedmainModelRenderItem->ObjCBIndex = 3;
	mReflectedMainObjRenderItem = reflectedmainModelRenderItem.get();
	mRenderItemLayer[(int)RenderLayer::Reflected].push_back(reflectedmainModelRenderItem.get());

	// Shadowed main object will have different world matrix, so it needs to be its own render item.
	auto shadowedMainModelRenderItem = std::make_unique<RenderItem>(gNumFrameResources);
	*shadowedMainModelRenderItem = *mainModelRenderItem;
	shadowedMainModelRenderItem->ObjCBIndex = 4;
	shadowedMainModelRenderItem->Mat = mMaterials["shadowMat"].get();
	mShadowedMainObjRenderItem = shadowedMainModelRenderItem.get();
	// mRenderItemLayer[(int)RenderLayer::Shadow].push_back(shadowedMainModelRenderItem.get());

	auto mirrorRenderItem = std::make_unique<RenderItem>(gNumFrameResources);
	mirrorRenderItem->World = Math::Identity4x4();
	mirrorRenderItem->TexTransform = Math::Identity4x4();
	mirrorRenderItem->ObjCBIndex = 5;
	mirrorRenderItem->Mat = mMaterials["mirror"].get();
	mirrorRenderItem->Geo = mGeometries["roomGeo"].get();
	mirrorRenderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mirrorRenderItem->IndexCount = mirrorRenderItem->Geo->DrawArgs["mirror"].IndexCount;
	mirrorRenderItem->StartIndexLocation = mirrorRenderItem->Geo->DrawArgs["mirror"].StartIndexLocation;
	mirrorRenderItem->BaseVertexLocation = mirrorRenderItem->Geo->DrawArgs["mirror"].BaseVertexLocation;
	mRenderItemLayer[(int)RenderLayer::Mirrors].push_back(mirrorRenderItem.get());
	mRenderItemLayer[(int)RenderLayer::Transparent].push_back(mirrorRenderItem.get());

	mAllRenderItems.push_back(std::move(floorRitem));
	mAllRenderItems.push_back(std::move(wallsRitem));
	mAllRenderItems.push_back(std::move(mainModelRenderItem));
	mAllRenderItems.push_back(std::move(reflectedmainModelRenderItem));
	mAllRenderItems.push_back(std::move(shadowedMainModelRenderItem));
	mAllRenderItems.push_back(std::move(mirrorRenderItem));

    for (int i = 0; i < mUnnamedGeometries.size(); ++i) {
        auto mainModelRenderItem = std::make_unique<RenderItem>(gNumFrameResources);
        mainModelRenderItem->World = Math::Identity4x4();
        mainModelRenderItem->TexTransform = Math::Identity4x4();
        mainModelRenderItem->ObjCBIndex = 2;
        mainModelRenderItem->Mat = mMaterials["mainModelMat"].get();
        mainModelRenderItem->Geo = mUnnamedGeometries[i].get();
        mainModelRenderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        mainModelRenderItem->IndexCount = mainModelRenderItem->Geo->DrawArgs["mainModel"].IndexCount;
        mainModelRenderItem->StartIndexLocation = mainModelRenderItem->Geo->DrawArgs["mainModel"].StartIndexLocation;
        mainModelRenderItem->BaseVertexLocation = mainModelRenderItem->Geo->DrawArgs["mainModel"].BaseVertexLocation;
        mMainObjRenderItem = mainModelRenderItem.get();
        mRenderItemLayer[(int)RenderLayer::Opaque].push_back(mainModelRenderItem.get());
        mAllRenderItems.push_back(std::move(mainModelRenderItem));
    }
}

void StencilingApp::BuildFrameResources() {
    for (int i = 0; i < gNumFrameResources; ++i) {
        mFrameResources.push_back(std::make_unique<FrameResource>(
            md3dDevice.Get(), 2, (UINT) mAllRenderItems.size(), (UINT) mMaterials.size()
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
	mEyePos.y = mRadius*cosf(mPhi)-1.5f;

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y+3, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorSet(1.5f, 2.0f, 0.0f, 1.0f);
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
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void StencilingApp::UpdateReflectedPassCB(const GameTimer& gt) {
    mReflectedPassCB = mMainPassCB;

    // Mirror plane's normal.
    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    XMMATRIX R = XMMatrixReflect(mirrorPlane);

    // Direction of lights has to be reflected.
    for(int i = 0; i < 3; ++i) {
		XMVECTOR lightDir = XMLoadFloat3(&mMainPassCB.Lights[i].Direction);
		XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
		XMStoreFloat3(&mReflectedPassCB.Lights[i].Direction, reflectedLightDir);
	}

    auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(1, mReflectedPassCB);
}

void StencilingApp::AnimateMaterials(const GameTimer& gt) {}

void StencilingApp::OnKeyboardInput(const GameTimer& gt) {
    const float dt = gt.DeltaTime();

    // Update main object translation vector.
    if(GetAsyncKeyState('A') & 0x8000) {
		mMainObjTranslation.x -= 1.0f*dt;
    }
	if(GetAsyncKeyState('D') & 0x8000) {
		mMainObjTranslation.x += 1.0f*dt;
    }
	if(GetAsyncKeyState('W') & 0x8000) {
		mMainObjTranslation.y += 1.0f*dt;
    }
	if(GetAsyncKeyState('S') & 0x8000) {
		mMainObjTranslation.y -= 1.0f*dt;
    }

    // Prevent the object from moving below the floor.
    mMainObjTranslation.y = Math::Max(mMainObjTranslation.y, 0.0f);

    // Update main object's world matrix.
    XMMATRIX mainObjRotate = XMMatrixRotationY(0.5f*Math::Pi);
    XMMATRIX mainObjScale = XMMatrixScaling(0.45f, 0.45f, 0.45f);
    XMMATRIX mainObjTranslate = XMMatrixTranslation(mMainObjTranslation.x, mMainObjTranslation.y, mMainObjTranslation.z);
    XMMATRIX mainObjWorld = mainObjRotate * mainObjScale * mainObjTranslate;
    XMStoreFloat4x4(&mMainObjRenderItem->World, mainObjWorld);
    
    // Update main object's reflection world matrix.
    // Mirror plane's normal.
    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    XMMATRIX R = XMMatrixReflect(mirrorPlane);
    XMStoreFloat4x4(&mReflectedMainObjRenderItem->World, mainObjWorld * R);

    // Update main object's shadow  world matrix.
    XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR toMainLight = -XMLoadFloat3(&mMainPassCB.Lights[0].Direction);
	XMMATRIX S = XMMatrixShadow(shadowPlane, toMainLight);
	XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 0.001f, 0.0f);
	XMStoreFloat4x4(&mShadowedMainObjRenderItem->World, mainObjWorld * S * shadowOffsetY);

	mMainObjRenderItem->NumFramesDirty = gNumFrameResources;
	mReflectedMainObjRenderItem->NumFramesDirty = gNumFrameResources;
	mShadowedMainObjRenderItem->NumFramesDirty = gNumFrameResources;
}

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

    // Render reflected objects to the back buffer only if the stencil test passes; we set up the test
    // to pass if the value in the stencil buffer is 1 (Value in the test would be whatever was set while
    // drawing the mirrors layer and StencilRef would be 1). This way, the reflected objects will only be
    // rendered to the back buffer on the visible surface of the mirror.
    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress() + 1 * passCBByteSize);
    mCommandList->SetPipelineState(mPSOs["drawStencilReflections"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[(int)RenderLayer::Reflected]);

    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
	mCommandList->OMSetStencilRef(0);

    // Draw transparent layer on the back buffer. The mirror is blended with the scene reflections.
	// mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	// DrawRenderItems(mCommandList.Get(), mRenderItemLayer[(int)RenderLayer::Transparent]);

    // Draw shadows.
    mCommandList->SetPipelineState(mPSOs["shadow"].Get());
	DrawRenderItems(mCommandList.Get(), mRenderItemLayer[(int)RenderLayer::Shadow]);

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
        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);
        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;
        cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);
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
