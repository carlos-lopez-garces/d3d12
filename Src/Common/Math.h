#pragma once

#include <Windows.h>
#include <DirectXMath.h>
#include <cstdint>

namespace Math {
	static const float Pi = 3.1415926535f;

	DirectX::XMFLOAT4X4 Identity4x4();

	DirectX::XMMATRIX InverseTranspose(DirectX::CXMMATRIX M);

	DirectX::XMVECTOR SphericalToCartesian(float rho, float theta, float phi);

	template<typename T> T Clamp(const T& x, const T& low, const T& high) {
		return (x < low) ? low : (x > high ? high : x);
	}

	static const float Infinity;
}