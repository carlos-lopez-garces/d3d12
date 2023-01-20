#pragma once

#include <Windows.h>
#include <DirectXMath.h>
#include <cstdint>

namespace Math {
	static const float Pi = 3.1415926535f;
	static const float Infinity = FLT_MAX;

	DirectX::XMFLOAT4X4 Identity4x4();

	DirectX::XMMATRIX InverseTranspose(DirectX::CXMMATRIX M);

	DirectX::XMVECTOR SphericalToCartesian(float rho, float theta, float phi);

	template<typename T> T Clamp(const T& x, const T& low, const T& high) {
		return (x < low) ? low : (x > high ? high : x);
	}

	static float RandF() {
		return (float)(rand()) / (float)RAND_MAX;
	}

	static float RandF(float a, float b) {
		return a + RandF()*(b-a);
	}

    static int Rand(int a, int b) {
        return a + rand() % ((b - a) + 1);
    }

	template<typename T> static T Max(const T& a, const T& b) {
		return a > b ? a : b;
	}
}