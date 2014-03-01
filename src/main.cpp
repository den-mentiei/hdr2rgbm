#include <iostream>
#include <d3d11.h>
#include <d3dx11.h>

#include "com_ptr.h"
#include "image_rgbe.h"

class Application {
public:
	bool init() {
		const D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
		const unsigned n_feature_levels = sizeof(feature_levels) / sizeof(feature_levels[0]);
		D3D_FEATURE_LEVEL current_feature_level;

		UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED;
	#ifdef _DEBUG
		flags |= D3D11_CREATE_DEVICE_DEBUG;
	#endif

		HRESULT hr = ::D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE, 0,
			flags, feature_levels, n_feature_levels,
			D3D11_SDK_VERSION, &_device, &current_feature_level, &_immediate_device);
		if (FAILED(hr)) {
			std::cerr << "Failed to create DirectX device." << std::endl;
			return false;
		}

		return true;
	}

	bool convert(const wchar_t* src_path, const wchar_t* dst_path) {
		const image_rgbe::Data rgbe = image_rgbe::load(src_path);
		if (!rgbe.valid) {
			std::cerr << "Can not load source image." << std::endl;
			return false;
		}
		ComPtr<ID3D11Texture2D> hdr_texture, rgba_texture;
		ComPtr<ID3D11ShaderResourceView> hdr_srv;
		if (!create_hdr_texture(rgbe, hdr_texture, hdr_srv)) {
			delete[] rgbe.rgb;
			return false;
		}
		ComPtr<ID3D11RenderTargetView> rgba_rtv;
		if (!create_rgba_texture(rgbe.w, rgbe.h, rgba_texture, rgba_rtv)) {
			return false;
		}
		delete[] rgbe.rgb;

		// TODO:

		return true;
	}
private:
	bool create_hdr_texture(const image_rgbe::Data& data, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& srv) {
		D3D11_TEXTURE2D_DESC desc;
		desc.Width = data.w;
		desc.Height = data.h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA texture_data;
		texture_data.pSysMem = data.rgb;
		texture_data.SysMemPitch = data.w * sizeof(float) * 3;
		texture_data.SysMemSlicePitch = 0;
		HRESULT hr = _device->CreateTexture2D(&desc, &texture_data, &texture);
		if (FAILED(hr)) {
			std::cerr << "Can not create HDR texture." << std::endl;
			return false;
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
		srv_desc.Format = desc.Format;
		srv_desc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Texture2D.MipLevels = 1;

		hr = _device->CreateShaderResourceView(texture.get(), &srv_desc, &srv);
		if (FAILED(hr)) {
			std::cerr << "Can not create SRV for HDR texture." << std::endl;
			return false;
		}

		return true;
	}

	bool create_rgba_texture(const unsigned w, const unsigned h, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11RenderTargetView>& rtv) {
		D3D11_TEXTURE2D_DESC desc;
		desc.Width = w;
		desc.Height = h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		HRESULT hr = _device->CreateTexture2D(&desc, 0, &texture);
		if (FAILED(hr)) {
			std::cerr << "Can not create RGBA8 texture." << std::endl;
			return false;
		}

		D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
		rtv_desc.Format = desc.Format;
		rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtv_desc.Texture2D.MipSlice = 0;

		ID3D10RenderTargetView *pRenderTargetView = NULL;
		hr = _device->CreateRenderTargetView(texture.get(), &rtv_desc, &rtv);
		if (FAILED(hr)) {
			std::cerr << "Can not create RTV for RGBA8 texture." << std::endl;
			return false;
		}

		return true;
	}

	ComPtr<ID3D11Device> _device;
	ComPtr<ID3D11DeviceContext> _immediate_device;
};

int wmain(int argc, wchar_t* argv[]) {
	if (argc != 3) {
		std::cout << "Usage:" << std::endl;
		std::cout << "rgbm-tool.exe <hdr-image> <tga-output>" << std::endl;
		return 1;
	}
	Application app;
	if (!app.init()) {
		return false;
	}
	return app.convert(argv[1], argv[2]) ? 0 : 1;
}
