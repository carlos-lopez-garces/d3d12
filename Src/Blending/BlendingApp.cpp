#include "../Common/d3dApp.h"
#include "../Common/Math.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"
#include "../Common/Camera.h"
#include "FrameResource.h"
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

    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    void LoadTextures();

    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildGeometry();
    void BuildPSOs();

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();
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
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildGeometry();
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

void BlendingApp::BuildRootSignature() {
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

void BlendingApp::BuildDescriptorHeaps() {
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

void BlendingApp::BuildShadersAndInputLayout() {
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

void BlendingApp::BuildGeometry() {
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

void BlendingApp::BuildPSOs() {
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

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> BlendingApp::GetStaticSamplers() {
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
