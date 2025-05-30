#include "WindowsCaptureBase.h"
#include "direct3d11.interop.h"
#include <d3dcompiler.h>


#define HR(x) if (FAILED(x)) { std::cerr << "Error at " << __LINE__ << std::endl; exit(1); }

// 创建离屏渲染目标
void CreateOffscreenTarget(ID3D11Device* device, UINT w, UINT h, winrt::com_ptr<ID3D11Texture2D>& tex, winrt::com_ptr<ID3D11RenderTargetView>& rtv)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = w; desc.Height = h;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	desc.MipLevels = 1; desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;

	winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, tex.put()));
	winrt::check_hresult(device->CreateRenderTargetView(tex.get(), nullptr, rtv.put()));
}

// 编译 Shader（简单版本）
winrt::com_ptr<ID3DBlob> CompileShader(const wchar_t* file, const char* entry, const char* model)
{
	winrt::com_ptr<ID3DBlob> blob, error;
	HRESULT hr = D3DCompileFromFile(file, nullptr, nullptr, entry, model, 0, 0, blob.put(), error.put());
	if (FAILED(hr)) {
		if (error) std::cerr << (char*)error->GetBufferPointer() << std::endl;
		exit(1);
	}
	return blob;
}


bool WindowsCaptureBase::Init()
{
	//Create Direct 3D Device
	winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		nullptr, 0, D3D11_SDK_VERSION, d3dDevice.put(), nullptr, nullptr));

	d3dDevice->GetImmediateContext(d3dContext.put());
	d3dContext->Release();

	//初始化缩放RTV
	CreateOffscreenTarget(d3dDevice.get(), glass_width, glass_height, offscreenTex, offscreenRTV);

	auto vsBlob = CompileShader(L"shaders.hlsl", "VSMain", "vs_5_0");
	auto psBlob = CompileShader(L"shaders.hlsl", "PSMain", "ps_5_0");

	winrt::check_hresult((d3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, vs.put())));
	winrt::check_hresult((d3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, ps.put())));

	D3D11_BUFFER_DESC cbDesc = { sizeof(ScaleBuffer), D3D11_USAGE_DYNAMIC,D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
	winrt::check_hresult(d3dDevice->CreateBuffer(&cbDesc, nullptr, scaleCB.put()));




	const auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
	{
		winrt::com_ptr<::IInspectable> inspectable;
		winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
		device = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
	}
	m_oclCtx = cv::directx::ocl::initializeContextFromD3D11Device(d3dDevice.get());
	return true;
}

//计算缩放比和边空，从UV角度。缩放后宽或高的其中一边充满，另外一边填充，保持画面比例不变。
void WindowsCaptureBase::CaculateScale()
{
	float scaleX = std::min(float(glass_width) / LastSize.Width, float(glass_height) / LastSize.Height);
	sx = LastSize.Width * scaleX / glass_width;
	sy = LastSize.Height * scaleX / glass_height;
	ox = (1.0f - sx) * 0.5f;
	oy = (1.0f - sy) * 0.5f;
}



bool WindowsCaptureBase::SetCaptureTarget(const HMONITOR& hmon)
{
	auto activation_factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
	auto interop_factory = activation_factory.as<IGraphicsCaptureItemInterop>();
	interop_factory->CreateForMonitor(hmon, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), reinterpret_cast<void**>(winrt::put_abi(this->m_item)));
	ItemSize = m_item.Size();
	LastSize = ItemSize;

	CaculateScale();

	// Create framepool, define pixel format (DXGI_FORMAT_B8G8R8A8_UNORM), and frame size. 
	m_framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
		device,
		winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
		2,
		ItemSize);
	m_session = m_framePool.CreateCaptureSession(m_item);

	m_frameArrived = m_framePool.FrameArrived(winrt::auto_revoke, { this, &WindowsCaptureBase::OnFrameArrived });
	m_session.StartCapture();
	return true;
}

cv::Mat WindowsCaptureBase::GetCaptureImage()
{
	std::lock_guard<std::mutex>lck(ImageMutex);
	return resMat.clone();
}

bool WindowsCaptureBase::ScaleImage_GPU(winrt::com_ptr<ID3D11ShaderResourceView>& textureSRV)
{
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	winrt::check_hresult(d3dContext->Map(scaleCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	((ScaleBuffer*)mapped.pData)->scaleX = sx;
	((ScaleBuffer*)mapped.pData)->scaleY = sy;
	((ScaleBuffer*)mapped.pData)->offsetX = ox;
	((ScaleBuffer*)mapped.pData)->offsetY = oy;
	d3dContext->Unmap(scaleCB.get(), 0);

	// 渲染	
	ID3D11RenderTargetView* rtv = offscreenRTV.get();
	d3dContext->OMSetRenderTargets(1, &rtv, nullptr);

	float clear[4] = { 1,0,0,1 };
	d3dContext->ClearRenderTargetView(offscreenRTV.get(), clear);
	d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	d3dContext->VSSetShader(vs.get(), nullptr, 0);
	d3dContext->PSSetShader(ps.get(), nullptr, 0);
	
	d3dContext->PSSetShaderResources(0, 1, textureSRV.put());

	ID3D11Buffer* constbuf = scaleCB.get();

	d3dContext->PSSetConstantBuffers(0, 1, &constbuf);

	d3dContext->Draw(4, 0);
	d3dContext->Flush();

	return true;

}


void WindowsCaptureBase::OnFrameArrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const& args)
{

	auto	t_start = std::chrono::steady_clock::now();
	auto newSize = false;
	{

		auto frame = sender.TryGetNextFrame();
		auto frameContentSize = frame.ContentSize();
		if (frameContentSize.Width != LastSize.Width ||
			frameContentSize.Height != LastSize.Height)
		{

			newSize = true;
			LastSize = frameContentSize;

			//重新计算缩放比
			CaculateScale();
		}

		const auto surface = frame.Surface();
		auto access = surface.as<IDirect3DDxgiInterfaceAccess>();
		//Microsoft::WRL::ComPtr< ID3D11Texture2D> texture;
		winrt::com_ptr<ID3D11Texture2D> texture;
		const auto hr = access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), texture.put_void());

		if (SaveTexture2d_bool)
		{

			winrt::com_ptr<ID3D11ShaderResourceView> srv;
			HR(d3dDevice->CreateShaderResourceView(texture.get(), nullptr, srv.put()));

			ScaleImage_GPU(srv);


			Microsoft::WRL::ComPtr< ID3D11Texture2D> des{ nullptr };
			D3D11_TEXTURE2D_DESC texDesc;
			offscreenTex->GetDesc(&texDesc);
			texDesc.Usage = D3D11_USAGE_STAGING;
			texDesc.BindFlags = 0;
			texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			texDesc.MiscFlags = 0;
			d3dDevice->CreateTexture2D(&texDesc, NULL, &des);

			d3dContext->CopyResource(des.Get(), offscreenTex.get());
			if (des != nullptr)
			{

				std::lock_guard<std::mutex>lck(ImageMutex);

				cv::directx::convertFromD3D11Texture2D(des.Get(), resMat);

			}
		}
	}
	if (m_swapChain != nullptr)
	{
		DXGI_PRESENT_PARAMETERS presentParameters = { 0 };
		//m_swapChain->Present1(1, 0, &presentParameters);
	}



	// 毫秒
	auto t_end = std::chrono::steady_clock::now();
	float time_span = std::chrono::duration<double, std::milli>(t_end - t_start).count();


	if (time_span < TimeCur)
	{
		Sleep(TimeCur - time_span);
	}
	if (newSize)
	{
		m_framePool.Recreate(
			device,
			winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
			2,
			LastSize);
	}

}
