#include <iostream>
#include <string>
#include <d3d11.h>
#include <d3dx11.h>

#include "com_ptr.h"
#include "image_rgbe.h"
#include "image_tga.h"

static const std::string vs_shader_code = ""
"struct VS_Input {\n"
"	float3 pos : POSITION;\n"
"	float2 uv : TEXCOORD;\n"
"};\n\n"
"struct PS_Input {\n"
"	float4 pos : SV_POSITION;\n"
"	float2 uv : TEXCOORD;\n"
"};\n\n"
"PS_Input vs_main(VS_Input input) {\n"
"	PS_Input o;\n"
"	o.pos = float4(input.pos.x, input.pos.y, 0.0f, 1.0f);\n"
"	o.uv = input.uv;\n"
"	return o;\n"
"}";

static const std::string ps_shader_code = ""
"Texture2D input_texture : register(t0);\n"
"SamplerState sampler_aniso : register(s0);\n\n"
"struct PS_Input {\n"
"	float4 pos : SV_POSITION;\n"
"	float2 uv : TEXCOORD;\n"
"};\n\n"
"float4 rgbm_encode(float3 color) {\n"
"	float4 rgbm;\n"
"	color *= 1.0 / 6.0;\n"
"	rgbm.a = saturate(max(max(color.r, color.g), max(color.b, 1e-6)));\n"
"	rgbm.a = ceil(rgbm.a * 255.0) / 255.0;\n"
"	rgbm.rgb = color / rgbm.a;\n"
"	return rgbm;\n"
"}\n\n"
"float4 ps_main(PS_Input input) : SV_TARGET0 {\n"
"	float4 rgbm = rgbm_encode(input_texture.Sample(sampler_aniso, input.uv).rgb);\n"
"	return rgbm;\n"
"}";

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

		if (!init_states()) {
			return false;
		}

		if (!init_shaders()) {
			return false;
		}

		if (!init_buffers()) {
			return false;
		}

		if (!init_sampler()) {
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

		ID3D11RenderTargetView* rtvs[] = { rgba_rtv.get() };
		_immediate_device->OMSetRenderTargets(1, rtvs, nullptr);

		D3D11_VIEWPORT viewport;

		viewport.Width = float(rgbe.w);
		viewport.Height = float(rgbe.h);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;

		_immediate_device->RSSetViewports(1, &viewport);

		UINT stride = sizeof(Vertex);
		UINT offset = 0;
		ID3D11Buffer* vbs[] = { _vb.get() };
		_immediate_device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		_immediate_device->IASetInputLayout(_ia.get());
		_immediate_device->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
		_immediate_device->IASetIndexBuffer(_ib.get(), DXGI_FORMAT_R32_UINT, 0);

		ID3D11ShaderResourceView* srvs[] = { hdr_srv.get() };
		_immediate_device->PSSetShaderResources(0, 1, srvs);
		
		ID3D11SamplerState* samplers[] = { _sampler.get() };
		_immediate_device->PSSetSamplers(0, 1, samplers);

		_immediate_device->VSSetShader(_vs.get(), 0, 0);
		_immediate_device->GSSetShader(0, 0, 0);
		_immediate_device->PSSetShader(_ps.get(), 0, 0);

		_immediate_device->RSSetState(_rs.get());
		_immediate_device->OMSetBlendState(_bs.get(), 0, 0xFFFFFFFF);
		_immediate_device->OMSetDepthStencilState(_ds.get(), 0);

		_immediate_device->DrawIndexed(6, 0, 0);

		_immediate_device->Flush();

		if (!save(rgba_texture, dst_path)) {
			return false;
		}

		return true;
	}
private:
	bool init_states() {
		CD3D11_RASTERIZER_DESC rs_description(D3D11_DEFAULT);
		rs_description.DepthClipEnable = false;

		HRESULT hr = _device->CreateRasterizerState(&rs_description, &_rs);
		if (FAILED(hr)) {
			std::cerr << "Can not create RS." << std::endl;
			return false;
		}

		D3D11_RENDER_TARGET_BLEND_DESC rt_bs_description;
		rt_bs_description.BlendEnable = false;
		rt_bs_description.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		rt_bs_description.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		rt_bs_description.BlendOp = D3D11_BLEND_OP_ADD;
		rt_bs_description.SrcBlendAlpha = D3D11_BLEND_ONE;
		rt_bs_description.DestBlendAlpha = D3D11_BLEND_ZERO;
		rt_bs_description.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rt_bs_description.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		D3D11_BLEND_DESC bs_description;
		bs_description.AlphaToCoverageEnable = false;
		bs_description.IndependentBlendEnable = false;
		bs_description.RenderTarget[0] = rt_bs_description;

		hr = _device->CreateBlendState(&bs_description, &_bs);
		if (FAILED(hr)) {
			std::cerr << "Can not create BS." << std::endl;
			return false;
		}

		CD3D11_DEPTH_STENCIL_DESC dst_description(D3D11_DEFAULT);
		dst_description.DepthEnable = false;

		hr = _device->CreateDepthStencilState(&dst_description, &_ds);
		if (FAILED(hr)) {
			std::cerr << "Can not create DSS." << std::endl;
			return false;
		}

		return true;
	}

	bool init_shaders() {
		ComPtr<ID3DBlob> vs_blob;
		HRESULT hr = D3DX11CompileFromMemory(
			vs_shader_code.c_str(), vs_shader_code.length(),
			0, 0, 0,
			"vs_main", "vs_4_0",
			0, 0, 0, 
			&vs_blob, 0, 0);
		if (FAILED(hr)) {
			std::cerr << "Can not compile VS." << std::endl;
			return false;
		}
		hr = _device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), 0, &_vs);
		if (FAILED(hr)) {
			std::cerr << "Can not load VS." << std::endl;
			return false;
		}

		ComPtr<ID3DBlob> ps_blob;
		hr = D3DX11CompileFromMemory(
			ps_shader_code.c_str(), ps_shader_code.length(),
			0, 0, 0,
			"ps_main", "ps_4_0",
			0, 0, 0, 
			&ps_blob, 0, 0);
		if (FAILED(hr)) {
			std::cerr << "Can not compile PS." << std::endl;
			return false;
		}
		hr = _device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), 0, &_ps);
		if (FAILED(hr)) {
			std::cerr << "Can not load PS." << std::endl;
			return false;
		}

		D3D11_INPUT_ELEMENT_DESC layout[2];
		layout[0].SemanticName = "POSITION";
		layout[0].SemanticIndex = 0;
		layout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		layout[0].InputSlot = 0;
		layout[0].AlignedByteOffset = 0;
		layout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		layout[0].InstanceDataStepRate = 0;

		layout[1].SemanticName = "TEXCOORD";
		layout[1].SemanticIndex = 0;
		layout[1].Format = DXGI_FORMAT_R32G32_FLOAT;
		layout[1].InputSlot = 0;
		layout[1].AlignedByteOffset = sizeof(float) * 3;
		layout[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		layout[1].InstanceDataStepRate = 0;

		hr = _device->CreateInputLayout(layout, 2, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &_ia);
		if (FAILED(hr)) {
			std::cerr << "Can not create IL" << std::endl;
			return false;
		}

		return true;
	}

	struct Vertex {
		float x, y, z;
		float u, v;
	};
	
	bool init_buffers() {
		static const Vertex vertices[] = {
			{ -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
			{ -1.0f,  1.0f, 0.0f, 0.0f, 0.0f },
			{  1.0f,  1.0f, 0.0f, 1.0f, 0.0f },
			{  1.0f, -1.0f, 0.0f, 1.0f, 1.0f },
		};

		D3D11_BUFFER_DESC desc;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.ByteWidth = sizeof(vertices);
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;

		D3D11_SUBRESOURCE_DATA resource;
		resource.pSysMem = vertices;
		resource.SysMemPitch = 0;
		resource.SysMemSlicePitch = 0;

		HRESULT hr = _device->CreateBuffer(&desc, &resource, &_vb);
		if (FAILED(hr)) {
			std::cerr << "Can not create VB." << std::endl;
			return false;
		}

		static const unsigned indices[] = {
			0, 1, 2,
			0, 2, 3
		};

		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.ByteWidth = sizeof(indices);
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;

		resource.pSysMem = indices;
		resource.SysMemPitch = 0;
		resource.SysMemSlicePitch = 0;

		hr = _device->CreateBuffer(&desc, &resource, &_ib);
		if (FAILED(hr)) {
			std::cerr << "Can not create IB." << std::endl;
			return false;
		}

		return true;
	}

	bool init_sampler() {
		CD3D11_SAMPLER_DESC desc(D3D11_DEFAULT);
		desc.Filter = D3D11_FILTER_ANISOTROPIC;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

		HRESULT hr = _device->CreateSamplerState(&desc, &_sampler);
		if (FAILED(hr)) {
			std::cerr << "Can not create Sampler State." << std::endl;
			return false;
		}

		return true;
	}

	bool save(ComPtr<ID3D11Texture2D> texture, const wchar_t* dst_path) {
		ComPtr<ID3D11Texture2D> staging_texture;
	
		D3D11_TEXTURE2D_DESC desc;
		texture->GetDesc(&desc);
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;

		HRESULT hr = _device->CreateTexture2D(&desc, 0, &staging_texture);
		if (FAILED(hr)) {
			std::cerr << "Can not create staging texture." << std::endl;
			return false;
		}
		
		_immediate_device->CopyResource(staging_texture.get(), texture.get());
		D3D11_MAPPED_SUBRESOURCE resource;
		_immediate_device->Map(staging_texture.get(), 0, D3D11_MAP_READ, 0, &resource);
		const unsigned char* rgba = (const unsigned char*)resource.pData;
		const unsigned pitch = resource.RowPitch;
		_immediate_device->Unmap(staging_texture.get(), 0);

		return image_tga::save(dst_path, rgba, desc.Width, desc.Height);
	}

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
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
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

	ComPtr<ID3D11SamplerState> _sampler;

	ComPtr<ID3D11RasterizerState> _rs;
	ComPtr<ID3D11BlendState> _bs;
	ComPtr<ID3D11DepthStencilState> _ds;

	ComPtr<ID3D11VertexShader> _vs;
	ComPtr<ID3D11PixelShader> _ps;

	ComPtr<ID3D11InputLayout> _ia;
	ComPtr<ID3D11Buffer> _ib, _vb;
};

int wmain(int argc, wchar_t* argv[]) {
	if (argc != 3) {
		std::cout << "hdr2rgbm 1.0-rc1 by Denis Mentey (denis@goortom.com)" << std::endl;
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
