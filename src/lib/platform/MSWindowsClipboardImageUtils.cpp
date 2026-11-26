/*
 * InputLeap -- mouse and keyboard sharing utility
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "platform/MSWindowsClipboardImageUtils.h"

#include "base/Log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace inputleap {
namespace mswindows_image {

namespace {

// keeps COM initialized on the current thread while in scope
class ComScope {
public:
    ComScope()
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // RPC_E_CHANGED_MODE: already initialized differently, still usable
        m_uninit = SUCCEEDED(hr);
    }
    ~ComScope()
    {
        if (m_uninit) {
            CoUninitialize();
        }
    }
private:
    bool m_uninit;
};

template<class T>
class ComPtr {
public:
    ComPtr() : m_p(nullptr) { }
    ~ComPtr() { if (m_p) m_p->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T** operator&() { return &m_p; }
    T* operator->() const { return m_p; }
    T* get() const { return m_p; }
private:
    T* m_p;
};

bool create_factory(ComPtr<IWICImagingFactory>& factory)
{
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        LOG_WARN("clipboard image: can't create WIC factory: 0x%08lx", hr);
        return false;
    }
    return true;
}

// creates a WIC stream over memory that must outlive the stream
bool memory_stream(IWICImagingFactory* factory, const void* data, std::size_t size,
                   ComPtr<IWICStream>& stream)
{
    if (size > std::numeric_limits<DWORD>::max()) {
        return false;
    }
    if (FAILED(factory->CreateStream(&stream))) {
        return false;
    }
    return SUCCEEDED(stream->InitializeFromMemory(
                const_cast<BYTE*>(static_cast<const BYTE*>(data)), static_cast<DWORD>(size)));
}

} // namespace

std::string decode_to_dib(const std::string& encoded)
{
    if (encoded.empty()) {
        return {};
    }
    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    if (!create_factory(factory)) {
        return {};
    }

    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (!memory_stream(factory.get(), encoded.data(), encoded.size(), stream) ||
        FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr,
                                                WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        LOG_WARN("clipboard image: can't decode %zu byte image", encoded.size());
        return {};
    }

    UINT width = 0, height = 0;
    if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 ||
        width > 32768 || height > 32768) {
        return {};
    }

    const UINT stride = width * 4;
    std::vector<BYTE> pixels(static_cast<std::size_t>(stride) * height);
    if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()),
                                     pixels.data()))) {
        return {};
    }

    BITMAPINFOHEADER info = {};
    info.biSize = sizeof(BITMAPINFOHEADER);
    info.biWidth = static_cast<LONG>(width);
    info.biHeight = static_cast<LONG>(height);     // bottom-up
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = static_cast<DWORD>(pixels.size());

    std::string dib(reinterpret_cast<const char*>(&info), sizeof(info));
    dib.resize(sizeof(info) + pixels.size());
    char* dst = &dib[sizeof(info)];
    for (UINT y = 0; y < height; ++y) {
        const BYTE* src_row = pixels.data() + static_cast<std::size_t>(height - 1 - y) * stride;
        BYTE* dst_row = reinterpret_cast<BYTE*>(dst) + static_cast<std::size_t>(y) * stride;
        for (UINT x = 0; x < width; ++x) {
            const BYTE* s = src_row + x * 4;
            BYTE* d = dst_row + x * 4;
            unsigned a = s[3];
            for (int c = 0; c < 3; ++c) {
                d[c] = static_cast<BYTE>((s[c] * a + 255 * (255 - a) + 127) / 255);
            }
            d[3] = 255;
        }
    }
    return dib;
}

std::string dib_to_png(const void* dib, std::size_t size)
{
    if (size < sizeof(BITMAPINFOHEADER)) {
        return {};
    }
    const auto* header = static_cast<const BITMAPINFOHEADER*>(dib);
    if (header->biSize < sizeof(BITMAPINFOHEADER) || header->biSize > size) {
        return {};
    }

    // compute where the pixels start so that the DIB can be wrapped into a
    // BMP file which the WIC BMP decoder understands
    std::size_t offset = header->biSize;
    if (header->biSize == sizeof(BITMAPINFOHEADER) && header->biCompression == BI_BITFIELDS) {
        offset += 3 * sizeof(DWORD);
    }
    if (header->biBitCount <= 8) {
        std::size_t colors = header->biClrUsed != 0 ? header->biClrUsed
                                                    : (std::size_t(1) << header->biBitCount);
        offset += colors * sizeof(RGBQUAD);
    } else if (header->biClrUsed != 0) {
        offset += header->biClrUsed * sizeof(RGBQUAD);
    }
    if (offset > size) {
        return {};
    }

    BITMAPFILEHEADER file = {};
    file.bfType = 0x4d42;   // "BM"
    file.bfSize = static_cast<DWORD>(sizeof(file) + size);
    file.bfOffBits = static_cast<DWORD>(sizeof(file) + offset);
    std::string bmp(reinterpret_cast<const char*>(&file), sizeof(file));
    bmp.append(static_cast<const char*>(dib), size);

    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    if (!create_factory(factory)) {
        return {};
    }

    ComPtr<IWICStream> in_stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (!memory_stream(factory.get(), bmp.data(), bmp.size(), in_stream) ||
        FAILED(factory->CreateDecoder(GUID_ContainerFormatBmp, nullptr, &decoder)) ||
        FAILED(decoder->Initialize(in_stream.get(), WICDecodeMetadataCacheOnDemand)) ||
        FAILED(decoder->GetFrame(0, &frame))) {
        LOG_WARN("clipboard image: can't read clipboard bitmap");
        return {};
    }

    // encode into a growable in-memory COM stream
    IStream* raw_out = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &raw_out))) {
        return {};
    }
    std::string png;
    {
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> out_frame;
        ComPtr<IPropertyBag2> props;
        bool ok = SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
                  SUCCEEDED(encoder->Initialize(raw_out, WICBitmapEncoderNoCache)) &&
                  SUCCEEDED(encoder->CreateNewFrame(&out_frame, &props)) &&
                  SUCCEEDED(out_frame->Initialize(props.get())) &&
                  SUCCEEDED(out_frame->WriteSource(frame.get(), nullptr)) &&
                  SUCCEEDED(out_frame->Commit()) &&
                  SUCCEEDED(encoder->Commit());

        HGLOBAL mem = nullptr;
        if (ok && SUCCEEDED(GetHGlobalFromStream(raw_out, &mem))) {
            STATSTG stat = {};
            if (SUCCEEDED(raw_out->Stat(&stat, STATFLAG_NONAME))) {
                const void* p = GlobalLock(mem);
                if (p != nullptr) {
                    png.assign(static_cast<const char*>(p),
                               static_cast<std::size_t>(stat.cbSize.QuadPart));
                    GlobalUnlock(mem);
                }
            }
        }
        if (!ok) {
            LOG_WARN("clipboard image: can't encode PNG");
        }
    }
    raw_out->Release();
    return png;
}

} // namespace mswindows_image
} // namespace inputleap
