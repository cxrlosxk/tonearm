// SPDX-License-Identifier: GPL-2.0-or-later
#include "wic_decode.hpp"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>

namespace tonearm {

namespace {

// WICBitmapInterpolationModeHighQualityCubic (Windows 10+). Spelled numerically so
// older SDK headers, including MinGW's, still compile.
constexpr auto kHighQualityCubic = static_cast<WICBitmapInterpolationMode>(4);

template <typename T> struct ComPtr {
	T *p = nullptr;
	~ComPtr()
	{
		if (p)
			p->Release();
	}
	T **put() { return &p; }
	T *operator->() const { return p; }
	explicit operator bool() const { return p != nullptr; }
};

} // namespace

Image decodeWithWic(const std::vector<uint8_t> &bytes, int maxSide)
{
	if (bytes.empty() || bytes.size() > 64u * 1024u * 1024u)
		return {};

	ComPtr<IWICImagingFactory> factory;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()))))
		return {};

	ComPtr<IWICStream> stream;
	if (FAILED(factory->CreateStream(stream.put())) ||
	    FAILED(stream->InitializeFromMemory(const_cast<BYTE *>(bytes.data()), static_cast<DWORD>(bytes.size()))))
		return {};

	ComPtr<IWICBitmapDecoder> decoder;
	if (FAILED(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnDemand, decoder.put())))
		return {};

	ComPtr<IWICBitmapFrameDecode> frame;
	if (FAILED(decoder->GetFrame(0, frame.put())))
		return {};

	UINT w = 0, h = 0;
	if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0)
		return {};

	IWICBitmapSource *source = frame.p;
	ComPtr<IWICBitmapScaler> scaler;
	const UINT longest = std::max(w, h);
	if (maxSide > 0 && longest > static_cast<UINT>(maxSide)) {
		const double k = static_cast<double>(maxSide) / longest;
		const UINT sw = std::max<UINT>(1, static_cast<UINT>(std::lround(w * k)));
		const UINT sh = std::max<UINT>(1, static_cast<UINT>(std::lround(h * k)));
		if (SUCCEEDED(factory->CreateBitmapScaler(scaler.put())) &&
		    SUCCEEDED(scaler->Initialize(frame.p, sw, sh, kHighQualityCubic))) {
			source = scaler.p;
			w = sw;
			h = sh;
		}
	}

	// Convert straight to premultiplied BGRA, the layout Tonearm uses everywhere.
	ComPtr<IWICFormatConverter> converter;
	if (FAILED(factory->CreateFormatConverter(converter.put())) ||
	    FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
					 WICBitmapPaletteTypeCustom)))
		return {};

	Image img(static_cast<int>(w), static_cast<int>(h));
	const UINT stride = w * 4;
	if (FAILED(converter->CopyPixels(nullptr, stride, stride * h, img.data())))
		return {};
	return img;
}

} // namespace tonearm
