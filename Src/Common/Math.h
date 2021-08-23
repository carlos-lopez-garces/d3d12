#pragma once

#include <Windows.h>
#include <DirectXMath.h>
#include <cstdint>

namespace Math {
	static const float Pi = 3.1415926535f;

	static DirectX::XMFLOAT4X4 Identity4x4();

	static DirectX::XMMATRIX InverseTranspose(DirectX::CXMMATRIX M);

	static DirectX::XMVECTOR SphericalToCartesian(float rho, float theta, float phi);
}