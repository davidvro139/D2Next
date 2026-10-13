// Direct3D 11: one textured quad per draw command, lit at its corners, blended as the command says, with a stencil that
// marks what has been drawn so that shadows fall only on it.
#include "D3D11Backend.h"
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace d2 {
namespace {
void check(HRESULT hr,const char* operation) {
    if(FAILED(hr)) throw std::runtime_error(std::string(operation)+" failed (HRESULT "+std::to_string(uint32_t(hr))+")");
}
class D3D11Backend final:public GpuBackend {
    HWND window_;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> coverage;
    ComPtr<ID3D11DepthStencilState> markCoverage,requireCoverage;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11PixelShader> shadowPixel;
    ComPtr<ID3D11BlendState> shadowBlend,additiveBlend,halfBlend,scaleBlend;
    ComPtr<ID3D11Buffer> constants,lightConstants;
    ComPtr<ID3D11SamplerState> sampler;
    std::map<const Image*,ComPtr<ID3D11ShaderResourceView>> textures; // by the picture's address
    std::map<uint32_t,ComPtr<ID3D11ShaderResourceView>> solids;       // plain colours, 1x1
    uint32_t width_=0,height_=0;
    bool software_=false;
    ComPtr<ID3D11ShaderResourceView> upload(const Image& image) {
        D3D11_TEXTURE2D_DESC td{}; td.Width=image.width; td.Height=image.height;
        td.MipLevels=td.ArraySize=td.SampleDesc.Count=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{image.rgba.data(),image.width*4,0};
        ComPtr<ID3D11Texture2D> resource;
        check(device->CreateTexture2D(&td,&data,&resource),"Upload asset texture");
        ComPtr<ID3D11ShaderResourceView> view;
        check(device->CreateShaderResourceView(resource.Get(),nullptr,&view),"Create texture view");
        return view;
    }
    ID3D11ShaderResourceView* texture(const Image& image) {
        auto found=textures.find(&image);
        if(found==textures.end())found=textures.emplace(&image,upload(image)).first;
        return found->second.Get();
    }
    ID3D11ShaderResourceView* solid(const std::array<uint8_t,3>& c) {
        const uint32_t key=(uint32_t(c[0])<<16)|(uint32_t(c[1])<<8)|c[2];
        auto found=solids.find(key);
        if(found==solids.end())found=solids.emplace(key,upload(Image{1,1,{c[0],c[1],c[2],255}})).first;
        return found->second.Get();
    }
    void setLight(const std::array<float,12>& l) {
        const float values[16]={l[0],l[1],l[2],1,l[3],l[4],l[5],1,l[6],l[7],l[8],1,l[9],l[10],l[11],1};
        context->UpdateSubresource(lightConstants.Get(),0,nullptr,values,0,0);
    }
public:
    D3D11Backend(HWND window,bool forceSoftware):window_(window) {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=2; desc.OutputWindow=window; desc.Windowed=TRUE;
        desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
        auto create=[&](D3D_DRIVER_TYPE type) {
            return D3D11CreateDeviceAndSwapChain(nullptr,type,nullptr,0,levels,1,D3D11_SDK_VERSION,
                &desc,&swap,&device,nullptr,&context);
        };
        HRESULT hr=forceSoftware?E_FAIL:create(D3D_DRIVER_TYPE_HARDWARE);
        if(FAILED(hr)) { software_=true; check(create(D3D_DRIVER_TYPE_WARP),"Create D3D11 WARP device"); }
        const char* shader=R"(
cbuffer View : register(b0) { float2 scale; float2 offset; };
struct V { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };
V vs(uint id:SV_VertexID) {
    float2 uv=float2((id<<1)&2,id&2);
    V o; o.pos=float4((uv*float2(2,-2)+float2(-1,1))*scale+offset,0,1); o.uv=uv; return o;
}
Texture2D artwork:register(t0); SamplerState nearest:register(s0);
// Light at the picture's four corners (top left, top right, bottom left, bottom right), blended across it.
cbuffer Light : register(b1) { float4 l00; float4 l10; float4 l01; float4 l11; };
float4 ps(V v):SV_TARGET {
    float4 c=artwork.Sample(nearest,v.uv); clip(c.a-0.5);
    float2 t=saturate(v.uv);
    c.rgb*=lerp(lerp(l00.rgb,l10.rgb,t.x),lerp(l01.rgb,l11.rgb,t.x),t.y);
    return c;
}
float4 shadow(V v):SV_TARGET { clip(artwork.Sample(nearest,v.uv).a-0.5); return float4(0,0,0,0.5); }
)";
        ComPtr<ID3DBlob> vs,ps,error;
        check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&error),"Compile vertex shader");
        check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&error),"Compile pixel shader");
        check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex),"Create vertex shader");
        check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel),"Create pixel shader");
        ps.Reset();error.Reset();
        check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"shadow","ps_5_0",0,0,&ps,&error),"Compile shadow shader");
        check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&shadowPixel),"Create shadow shader");
        D3D11_BLEND_DESC blend{};auto& rt=blend.RenderTarget[0];
        rt.BlendEnable=TRUE;rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.SrcBlendAlpha=D3D11_BLEND_ZERO;rt.DestBlendAlpha=D3D11_BLEND_ONE;
        rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateBlendState(&blend,&shadowBlend),"Create shadow blend");
        // Fire and glows are added onto what is below them.
        rt.SrcBlend=rt.DestBlend=D3D11_BLEND_ONE;rt.SrcBlendAlpha=D3D11_BLEND_ZERO;rt.DestBlendAlpha=D3D11_BLEND_ONE;
        check(device->CreateBlendState(&blend,&additiveBlend),"Create additive blend");
        // See-through: the picture and what is below it, by the blend factor.
        rt.SrcBlend=D3D11_BLEND_BLEND_FACTOR;rt.DestBlend=D3D11_BLEND_INV_BLEND_FACTOR;
        check(device->CreateBlendState(&blend,&halfBlend),"Create half blend");
        // Brightness: what is below times twice the colour drawn (dest*src + src*dest).
        rt.SrcBlend=D3D11_BLEND_DEST_COLOR;rt.DestBlend=D3D11_BLEND_SRC_COLOR;
        check(device->CreateBlendState(&blend,&scaleBlend),"Create scale blend");
        // Shadows darken existing map pixels only, matching the CPU reference.
        D3D11_DEPTH_STENCIL_DESC stencil{};stencil.StencilEnable=TRUE;
        stencil.StencilReadMask=stencil.StencilWriteMask=0xff;
        stencil.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};
        stencil.BackFace=stencil.FrontFace;
        check(device->CreateDepthStencilState(&stencil,&markCoverage),"Create coverage writer");
        stencil.StencilWriteMask=0;
        stencil.FrontFace.StencilPassOp=D3D11_STENCIL_OP_KEEP;
        stencil.FrontFace.StencilFunc=D3D11_COMPARISON_EQUAL;stencil.BackFace=stencil.FrontFace;
        check(device->CreateDepthStencilState(&stencil,&requireCoverage),"Create shadow coverage test");
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&bd,nullptr,&constants),"Create view constants");
        bd.ByteWidth=64;check(device->CreateBuffer(&bd,nullptr,&lightConstants),"Create light constants");
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;
        sd.MaxLOD=D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&sd,&sampler),"Create sampler");
    }
    bool begin(const std::array<float,4>& clear) override {
        RECT r{}; GetClientRect(window_,&r);
        if(IsIconic(window_)||r.right<=0||r.bottom<=0) return false;
        if(width_!=uint32_t(r.right)||height_!=uint32_t(r.bottom)) {
            context->OMSetRenderTargets(0,nullptr,nullptr); target.Reset();coverage.Reset();
            check(swap->ResizeBuffers(0,r.right,r.bottom,DXGI_FORMAT_UNKNOWN,0),"Resize swap chain");
            width_=r.right; height_=r.bottom;
            ComPtr<ID3D11Texture2D> back;
            check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"Get back buffer");
            check(device->CreateRenderTargetView(back.Get(),nullptr,&target),"Create render target");
            D3D11_TEXTURE2D_DESC td{};td.Width=width_;td.Height=height_;
            td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
            td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
            ComPtr<ID3D11Texture2D> mask;
            check(device->CreateTexture2D(&td,nullptr,&mask),"Create coverage texture");
            check(device->CreateDepthStencilView(mask.Get(),nullptr,&coverage),"Create coverage view");
        }
        context->ClearRenderTargetView(target.Get(),clear.data());
        context->ClearDepthStencilView(coverage.Get(),D3D11_CLEAR_STENCIL,0,0);
        context->OMSetRenderTargets(1,target.GetAddressOf(),coverage.Get());
        const D3D11_VIEWPORT viewport{0,0,float(width_),float(height_),0,1};
        context->RSSetViewports(1,&viewport);
        context->VSSetShader(vertex.Get(),nullptr,0); context->PSSetShader(pixel.Get(),nullptr,0);
        context->VSSetConstantBuffers(0,1,constants.GetAddressOf());
        context->PSSetConstantBuffers(1,1,lightConstants.GetAddressOf());
        setLight({1,1,1,1,1,1,1,1,1,1,1,1});
        context->PSSetSamplers(0,1,sampler.GetAddressOf());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        return true;
    }
    uint32_t width() const override {return width_;}
    uint32_t height() const override {return height_;}
    void draw(const DrawList& list) override {
        std::array<float,12> light{};bool lightSet=false;
        for(const auto& c:list) {
            ID3D11ShaderResourceView* srv=c.image?texture(*c.image):solid(c.color);
            context->UpdateSubresource(constants.Get(),0,nullptr,c.quad.data(),0,0);
            if(!lightSet||light!=c.light){light=c.light;lightSet=true;setLight(light);}
            context->PSSetShaderResources(0,1,&srv);
            const bool shadow=c.blend==DrawCommand::Blend::Darken||c.blend==DrawCommand::Blend::Shadow;
            context->PSSetShader(shadow?shadowPixel.Get():pixel.Get(),nullptr,0);
            const float factor[4]={c.amount,c.amount,c.amount,c.amount};
            switch(c.blend) {
            case DrawCommand::Blend::Normal:context->OMSetBlendState(nullptr,nullptr,0xffffffff);break;
            case DrawCommand::Blend::Add:context->OMSetBlendState(additiveBlend.Get(),nullptr,0xffffffff);break;
            case DrawCommand::Blend::Mix:context->OMSetBlendState(halfBlend.Get(),factor,0xffffffff);break;
            case DrawCommand::Blend::Scale:context->OMSetBlendState(scaleBlend.Get(),nullptr,0xffffffff);break;
            case DrawCommand::Blend::Darken:case DrawCommand::Blend::Shadow:context->OMSetBlendState(shadowBlend.Get(),nullptr,0xffffffff);break;
            }
            context->OMSetDepthStencilState(c.blend==DrawCommand::Blend::Shadow?requireCoverage.Get():markCoverage.Get(),1);
            context->Draw(3,0);
        }
        context->OMSetBlendState(nullptr,nullptr,0xffffffff);
        setLight({1,1,1,1,1,1,1,1,1,1,1,1});
    }
    void present() override {check(swap->Present(1,0),"Present");}
    size_t readback(const std::filesystem::path& capture) override {
        ComPtr<ID3D11Texture2D> back,staging;
        check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"Get capture buffer");
        D3D11_TEXTURE2D_DESC desc{}; back->GetDesc(&desc);
        desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&desc,nullptr,&staging),"Create capture texture");
        context->CopyResource(staging.Get(),back.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read rendered pixels");
        std::vector<uint8_t> bgra(size_t(width_)*height_*4);
        size_t changed=0;
        const auto* corner=static_cast<const uint8_t*>(mapped.pData);
        for(uint32_t y=0;y<height_;++y) for(uint32_t x=0;x<width_;++x) {
            auto* src=static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch+x*4;
            auto* dst=bgra.data()+(size_t(y)*width_+x)*4;
            if(memcmp(src,corner,3)!=0) ++changed;
            dst[0]=src[2]; dst[1]=src[1]; dst[2]=src[0]; dst[3]=255;
        }
        context->Unmap(staging.Get(),0);
        if(!capture.empty()) {
            BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
            file.bfSize=file.bfOffBits+DWORD(bgra.size());
            BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=width_; info.biHeight=-int32_t(height_);
            info.biPlanes=1; info.biBitCount=32; info.biCompression=BI_RGB;
            std::ofstream out(capture,std::ios::binary);
            out.write(reinterpret_cast<const char*>(&file),sizeof(file));
            out.write(reinterpret_cast<const char*>(&info),sizeof(info));
            out.write(reinterpret_cast<const char*>(bgra.data()),bgra.size());
            if(!out) throw std::runtime_error("Cannot write capture file");
        }
        return changed;
    }
    void forgetTextures() override {textures.clear();}
    void forgetTexture(const Image* image) override {textures.erase(image);}
    bool software() const override {return software_;}
    std::string name() const override {return software_?"WARP":"D3D11 hardware";}
};
}
std::unique_ptr<GpuBackend> makeD3D11Backend(void* window,bool forceSoftware) {
    return std::make_unique<D3D11Backend>(static_cast<HWND>(window),forceSoftware);
}
}
