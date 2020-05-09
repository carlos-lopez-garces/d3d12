#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

#if defined(CreateWindow)
#undef CreateWindow
#endif

// Windows Runtime Library.
// Defines ComPtr. All DirectX 12 objects are COM objects.
#include <wrl.h>
using namespace Microsoft::WRL;

// DirectX 12.
// Defines Device, CommandQueue, CommandList.
#include <d3d12.h>
// DirectX Graphics Infrastructure. Low-level tasks: enumerating GPU adapters, 
// presenting image to the screen, full-screen transitions, HDR display 
// detection.
#include <dxgi1_6.h>
// Run-time HLSL compilation.
#include <d3dcompiler.h>
#include <DirectXMath.h>

// Not part of the SDK. 
// github.com/Microsoft/DirectX-Graphics-Samples/tree/master/Libraries/D3DX12.
#include <d3dx12.h>

// STL.
#include <algorithm>
#include <cassert>
// Contains chrono::high_resolution_clock.
#include <chrono>

#include <helpers.h>