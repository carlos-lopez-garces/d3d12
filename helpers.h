#pragma once

#define WIN32_LEAN_AND_MEAN
// Contains HRESULT.
#include <Windows.h>
#include <exception>

// From DXSampleHelper.h, github.com/Microsoft/DirectX-Graphics-Samples.
inline void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) {
        throw std::exception();
    }
}