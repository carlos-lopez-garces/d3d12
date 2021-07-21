#pragma once

#include <Windows.h>
#include <DirectXMath.h>
#include <cstdint>

namespace Math {

	static const float Pi = 3.1415926535f;

	DirectX::XMFLOAT4X4 Identity4x4() {
		static DirectX::XMFLOAT4X4 I(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		);

		return I;
	}

}