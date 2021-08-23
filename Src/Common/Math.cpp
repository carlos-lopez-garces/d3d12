#include "Math.h"

DirectX::XMFLOAT4X4 Math::Identity4x4() {
	static DirectX::XMFLOAT4X4 I(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);

	return I;
}

DirectX::XMMATRIX Math::InverseTranspose(DirectX::CXMMATRIX M) {
	DirectX::XMMATRIX A = M;
	// TODO: explain.
	A.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	DirectX::XMVECTOR determinant = DirectX::XMMatrixDeterminant(A);
	return DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(&determinant, A));
}

DirectX::XMVECTOR Math::SphericalToCartesian(float rho, float theta, float phi) {
	// Polar radius.
	float r = rho * sinf(phi);
	float x = r * cosf(theta);
	// The y-axis points up in this coordinate system.
	float y = rho * cosf(phi);
	float z = r * sinf(theta);
	return DirectX::XMVectorSet(x, y, z, 1.0f);
}