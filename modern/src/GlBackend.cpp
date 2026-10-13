// OpenGL ES 3.0 / WebGL 2: one textured quad per draw command, lit at its corners, blended as the command says, with a
// stencil that marks what has been drawn so that shadows fall only on it. It draws what D3D11Backend.cpp draws, pixel for
// pixel. Only what WebGL 2 also has is used: no clamp-to-border sampling (the shader drops what lies outside the
// picture), no mapped buffers, and every function is looked up through SDL, so no GL headers or loader are needed.
//
// The frame is drawn into an offscreen framebuffer upside down (clip space y negated) and flipped onto the window when it
// is presented. OpenGL counts rows from the bottom and Direct3D from the top; drawn this way the rows are rasterised in
// Direct3D's order, so where a pixel centre falls exactly on a texel edge both pick the same texel. It also lets
// readback read rows top down.
#include "GlBackend.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace d2 {
namespace {
// The few OpenGL ES 3.0 names this file uses (values from the Khronos registry).
using GLenum=unsigned;using GLuint=unsigned;using GLint=int;using GLsizei=int;using GLfloat=float;using GLbitfield=unsigned;
using GLchar=char;
constexpr GLenum GL_TEXTURE_2D=0x0DE1,GL_RGBA=0x1908,GL_RGBA8=0x8058,GL_UNSIGNED_BYTE=0x1401,GL_TEXTURE_MIN_FILTER=0x2801,
    GL_TEXTURE_MAG_FILTER=0x2800,GL_TEXTURE_WRAP_S=0x2802,GL_TEXTURE_WRAP_T=0x2803,GL_NEAREST=0x2600,GL_CLAMP_TO_EDGE=0x812F,
    GL_UNPACK_ALIGNMENT=0x0CF5,GL_PACK_ALIGNMENT=0x0D05,GL_COLOR_BUFFER_BIT=0x4000,GL_STENCIL_BUFFER_BIT=0x0400,GL_BLEND=0x0BE2,
    GL_STENCIL_TEST=0x0B90,GL_DEPTH_TEST=0x0B71,GL_CULL_FACE=0x0B44,GL_SCISSOR_TEST=0x0C11,GL_ONE=1,GL_ZERO=0,
    GL_SRC_ALPHA=0x0302,GL_ONE_MINUS_SRC_ALPHA=0x0303,GL_SRC_COLOR=0x0300,GL_DST_COLOR=0x0306,
    GL_FUNC_ADD=0x8006,GL_ALWAYS=0x0207,GL_EQUAL=0x0202,GL_KEEP=0x1E00,GL_REPLACE=0x1E01,GL_TRIANGLES=0x0004,
    GL_VERTEX_SHADER=0x8B31,GL_FRAGMENT_SHADER=0x8B30,GL_COMPILE_STATUS=0x8B81,GL_LINK_STATUS=0x8B82,GL_TEXTURE0=0x84C0,
    GL_VERSION=0x1F02,GL_FRAMEBUFFER=0x8D40,GL_READ_FRAMEBUFFER=0x8CA8,GL_DRAW_FRAMEBUFFER=0x8CA9,GL_RENDERBUFFER=0x8D41,
    GL_COLOR_ATTACHMENT0=0x8CE0,GL_DEPTH_STENCIL_ATTACHMENT=0x821A,GL_DEPTH24_STENCIL8=0x88F0,
    GL_FRAMEBUFFER_COMPLETE=0x8CD5;
#ifdef _WIN32
#define D2_GLAPI __stdcall
#else
#define D2_GLAPI
#endif
// The functions, looked up once the context exists.
#define D2_GL_FUNCTIONS(X) \
    X(void,Viewport,(GLint,GLint,GLsizei,GLsizei)) X(void,ClearColor,(GLfloat,GLfloat,GLfloat,GLfloat)) \
    X(void,ClearStencil,(GLint)) X(void,Clear,(GLbitfield)) X(void,Enable,(GLenum)) X(void,Disable,(GLenum)) \
    X(void,BlendFuncSeparate,(GLenum,GLenum,GLenum,GLenum)) X(void,BlendEquation,(GLenum)) \
    X(void,StencilFunc,(GLenum,GLint,GLuint)) \
    X(void,StencilOp,(GLenum,GLenum,GLenum)) X(void,StencilMask,(GLuint)) X(void,GenTextures,(GLsizei,GLuint*)) \
    X(void,DeleteTextures,(GLsizei,const GLuint*)) X(void,BindTexture,(GLenum,GLuint)) X(void,ActiveTexture,(GLenum)) \
    X(void,TexParameteri,(GLenum,GLenum,GLint)) X(void,PixelStorei,(GLenum,GLint)) \
    X(void,TexImage2D,(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*)) \
    X(GLuint,CreateShader,(GLenum)) X(void,ShaderSource,(GLuint,GLsizei,const GLchar* const*,const GLint*)) \
    X(void,CompileShader,(GLuint)) X(void,GetShaderiv,(GLuint,GLenum,GLint*)) \
    X(void,GetShaderInfoLog,(GLuint,GLsizei,GLsizei*,GLchar*)) X(GLuint,CreateProgram,()) \
    X(void,AttachShader,(GLuint,GLuint)) X(void,LinkProgram,(GLuint)) X(void,GetProgramiv,(GLuint,GLenum,GLint*)) \
    X(void,GetProgramInfoLog,(GLuint,GLsizei,GLsizei*,GLchar*)) X(void,DeleteShader,(GLuint)) X(void,UseProgram,(GLuint)) \
    X(GLint,GetUniformLocation,(GLuint,const GLchar*)) X(void,Uniform4fv,(GLint,GLsizei,const GLfloat*)) \
    X(void,Uniform1i,(GLint,GLint)) X(void,Uniform1f,(GLint,GLfloat)) X(void,GenVertexArrays,(GLsizei,GLuint*)) \
    X(void,BindVertexArray,(GLuint)) \
    X(void,DrawArrays,(GLenum,GLint,GLsizei)) X(void,ReadPixels,(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*)) \
    X(const unsigned char*,GetString,(GLenum)) X(void,BindFramebuffer,(GLenum,GLuint)) \
    X(void,GenFramebuffers,(GLsizei,GLuint*)) X(void,DeleteFramebuffers,(GLsizei,const GLuint*)) \
    X(void,GenRenderbuffers,(GLsizei,GLuint*)) X(void,DeleteRenderbuffers,(GLsizei,const GLuint*)) \
    X(void,BindRenderbuffer,(GLenum,GLuint)) X(void,RenderbufferStorage,(GLenum,GLenum,GLsizei,GLsizei)) \
    X(void,FramebufferRenderbuffer,(GLenum,GLenum,GLenum,GLuint)) X(GLenum,CheckFramebufferStatus,(GLenum)) \
    X(void,FramebufferTexture2D,(GLenum,GLenum,GLenum,GLuint,GLint)) \
    X(void,BlitFramebuffer,(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum))
struct Gl {
#define D2_GL_MEMBER(ret,name,args) ret (D2_GLAPI* name) args=nullptr;
    D2_GL_FUNCTIONS(D2_GL_MEMBER)
#undef D2_GL_MEMBER
    void load() {
#define D2_GL_LOAD(ret,name,args) \
        name=reinterpret_cast<ret (D2_GLAPI*) args>(SDL_GL_GetProcAddress("gl" #name)); \
        if(!name)throw std::runtime_error("OpenGL function gl" #name " is missing");
        D2_GL_FUNCTIONS(D2_GL_LOAD)
#undef D2_GL_LOAD
    }
};
// The shaders, for "#version 300 es" (OpenGL ES 3, WebGL 2) or "#version 330 core" (desktop OpenGL); the bodies are the
// same. The quad is one triangle twice its size whose texture coordinates run 0..2; what lies past 1 is dropped, as
// Direct3D's transparent border colour drops it. y is negated: the frame is drawn upside down (see the top).
const char* vertexBody=R"(
uniform vec4 quad; // half width, half height, centre x, centre y (clip space)
uniform float ySign; // -1: offscreen FBO (Direct3D row order); +1: the window
out vec2 uv;
void main() {
    uv=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    vec2 p=(uv*vec2(2.0,-2.0)+vec2(-1.0,1.0))*quad.xy+quad.zw;
    gl_Position=vec4(p.x,p.y*ySign,0.0,1.0);
}
)";
const char* pictureBody=R"(
uniform sampler2D artwork;
uniform vec4 light[4]; // top left, top right, bottom left, bottom right
uniform float amount;  // Mix blend opacity; 1 for every other blend
in vec2 uv;
out vec4 colour;
void main() {
    if(uv.x>1.0||uv.y>1.0)discard;
    vec4 c=texture(artwork,uv);
    if(c.a<0.5)discard;
    vec2 t=clamp(uv,0.0,1.0);
    c.rgb*=mix(mix(light[0].rgb,light[1].rgb,t.x),mix(light[2].rgb,light[3].rgb,t.x),t.y);
    colour=vec4(c.rgb,amount);
}
)";
const char* shadowBody=R"(
uniform sampler2D artwork;
in vec2 uv;
out vec4 colour;
void main() {
    if(uv.x>1.0||uv.y>1.0)discard;
    if(texture(artwork,uv).a<0.5)discard;
    colour=vec4(0.0,0.0,0.0,0.5);
}
)";
// Copy the offscreen frame to the window. WebGL 2 rejects glBlitFramebuffer when the canvas format
// (often RGB, sometimes MSAA) does not match the RGBA8 render target, which left a black screen.
const char* presentVertex=R"(
out vec2 uv;
void main() {
    vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    uv=vec2(p.x,1.0-p.y);
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
)";
const char* presentBody=R"(
uniform sampler2D artwork;
in vec2 uv;
out vec4 colour;
void main() { colour=texture(artwork,uv); }
)";
class GlBackend final:public GpuBackend {
    SDL_Window* window_;
    SDL_GLContext context_=nullptr;
    Gl gl;
    bool es_=true;
    std::string name_;
    GLuint picture=0,shadow=0,presentProg=0,vertexArray=0;
    GLint pictureQuad=-1,pictureLight=-1,pictureAmount=-1,pictureYSign=-1,shadowQuad=-1,shadowYSign=-1;
    GLuint frame=0,frameColour=0,frameStencil=0; // the offscreen frame (upside down); colour is a texture
    bool direct_=false; // WebGL: draw into the canvas. Blit/copy from an FBO is rejected or stays black.
    std::map<const Image*,GLuint> textures; // by the picture's address
    std::map<uint32_t,GLuint> solids;       // plain colours, 1x1
    uint32_t width_=0,height_=0;
    GLuint upload(const Image& image) {
        GLuint id=0;gl.GenTextures(1,&id);
        gl.BindTexture(GL_TEXTURE_2D,id);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        gl.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,GLsizei(image.width),GLsizei(image.height),0,GL_RGBA,GL_UNSIGNED_BYTE,image.rgba.data());
        return id;
    }
    GLuint texture(const Image& image) {
        auto found=textures.find(&image);
        if(found==textures.end())found=textures.emplace(&image,upload(image)).first;
        return found->second;
    }
    GLuint solid(const std::array<uint8_t,3>& c) {
        const uint32_t key=(uint32_t(c[0])<<16)|(uint32_t(c[1])<<8)|c[2];
        auto found=solids.find(key);
        if(found==solids.end())found=solids.emplace(key,upload(Image{1,1,{c[0],c[1],c[2],255}})).first;
        return found->second;
    }
    GLuint compile(GLenum type,const char* body) {
        const std::string source=std::string(es_?"#version 300 es\nprecision highp float;\nprecision highp int;\n":"#version 330 core\n")+body;
        const GLuint shader=gl.CreateShader(type);
        const char* text=source.c_str();gl.ShaderSource(shader,1,&text,nullptr);gl.CompileShader(shader);
        GLint ok=0;gl.GetShaderiv(shader,GL_COMPILE_STATUS,&ok);
        if(!ok){char log[2048]{};gl.GetShaderInfoLog(shader,sizeof log,nullptr,log);throw std::runtime_error(std::string("Compile shader: ")+log);}
        return shader;
    }
    GLuint link(const char* vertexSrc,const char* fragmentBody) {
        const GLuint vs=compile(GL_VERTEX_SHADER,vertexSrc),fs=compile(GL_FRAGMENT_SHADER,fragmentBody);
        const GLuint program=gl.CreateProgram();
        gl.AttachShader(program,vs);gl.AttachShader(program,fs);gl.LinkProgram(program);
        gl.DeleteShader(vs);gl.DeleteShader(fs);
        GLint ok=0;gl.GetProgramiv(program,GL_LINK_STATUS,&ok);
        if(!ok){char log[2048]{};gl.GetProgramInfoLog(program,sizeof log,nullptr,log);throw std::runtime_error(std::string("Link shaders: ")+log);}
        return program;
    }
    GLuint link(const char* fragmentBody) {return link(vertexBody,fragmentBody);}
    void setLight(const std::array<float,12>& l) {
        const float values[16]={l[0],l[1],l[2],1,l[3],l[4],l[5],1,l[6],l[7],l[8],1,l[9],l[10],l[11],1};
        gl.Uniform4fv(pictureLight,4,values);
    }
    // The offscreen frame at the window's size: colour and the stencil the shadows need.
    void makeFrame(uint32_t w,uint32_t h) {
        if(frame){gl.DeleteFramebuffers(1,&frame);gl.DeleteTextures(1,&frameColour);gl.DeleteRenderbuffers(1,&frameStencil);}
        gl.GenFramebuffers(1,&frame);gl.BindFramebuffer(GL_FRAMEBUFFER,frame);
        gl.GenTextures(1,&frameColour);gl.BindTexture(GL_TEXTURE_2D,frameColour);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        gl.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,GLsizei(w),GLsizei(h),0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,frameColour,0);
        gl.GenRenderbuffers(1,&frameStencil);gl.BindRenderbuffer(GL_RENDERBUFFER,frameStencil);
        gl.RenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,GLsizei(w),GLsizei(h));
        gl.FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,frameStencil);
        if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("OpenGL: the offscreen frame cannot be drawn into");
        width_=w;height_=h;
    }
    // OpenGL ES 3.0 where the system has it (Android, browsers, most desktop drivers); desktop OpenGL 3.3 otherwise.
    bool createContext(bool es) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,es?SDL_GL_CONTEXT_PROFILE_ES:SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,es?0:3);
        context_=SDL_GL_CreateContext(window_);
        es_=es;
        return context_!=nullptr;
    }
public:
    explicit GlBackend(SDL_Window* window):window_(window) {
        const char* prefer=SDL_getenv("D2MODERN_GL"); // test hook: "core" skips OpenGL ES on desktops
        const bool tryEs=!(prefer&&std::string(prefer)=="core");
#ifdef __EMSCRIPTEN__
        if(!createContext(true))throw std::runtime_error(std::string("WebGL 2 context: ")+SDL_GetError());
#else
        if(!(tryEs&&createContext(true))&&!createContext(false))throw std::runtime_error(std::string("OpenGL context: ")+SDL_GetError());
#endif
        SDL_GL_MakeCurrent(window_,context_);
        SDL_GL_SetSwapInterval(1);
        gl.load();
        name_=std::string(es_?"":"OpenGL ")+reinterpret_cast<const char*>(gl.GetString(GL_VERSION));
        picture=link(pictureBody);shadow=link(shadowBody);presentProg=link(presentVertex,presentBody);
        pictureQuad=gl.GetUniformLocation(picture,"quad");pictureLight=gl.GetUniformLocation(picture,"light");
        pictureAmount=gl.GetUniformLocation(picture,"amount");pictureYSign=gl.GetUniformLocation(picture,"ySign");
        shadowQuad=gl.GetUniformLocation(shadow,"quad");shadowYSign=gl.GetUniformLocation(shadow,"ySign");
        gl.UseProgram(shadow);gl.Uniform1i(gl.GetUniformLocation(shadow,"artwork"),0);
        gl.UseProgram(presentProg);gl.Uniform1i(gl.GetUniformLocation(presentProg,"artwork"),0);
        gl.UseProgram(picture);gl.Uniform1i(gl.GetUniformLocation(picture,"artwork"),0);gl.Uniform1f(pictureAmount,1.f);
        gl.GenVertexArrays(1,&vertexArray); // the vertices come from gl_VertexID, but desktop core profiles need one bound
        gl.PixelStorei(GL_UNPACK_ALIGNMENT,1);gl.PixelStorei(GL_PACK_ALIGNMENT,1);
#ifdef __EMSCRIPTEN__
        direct_=true; // the canvas itself has the stencil; copying an offscreen frame onto it stayed black
#endif
        const float ySign=direct_?1.f:-1.f;
        if(pictureYSign>=0){gl.UseProgram(picture);gl.Uniform1f(pictureYSign,ySign);}
        if(shadowYSign>=0){gl.UseProgram(shadow);gl.Uniform1f(shadowYSign,ySign);}
        gl.UseProgram(picture);
    }
    ~GlBackend() override {if(context_)SDL_GL_DestroyContext(context_);}
    bool begin(const std::array<float,4>& clear) override {
        SDL_GL_MakeCurrent(window_,context_);
        int w=0,h=0;SDL_GetWindowSizeInPixels(window_,&w,&h);
        if((SDL_GetWindowFlags(window_)&SDL_WINDOW_MINIMIZED)||w<=0||h<=0) {
            static bool once=false;
            if(!once){once=true;std::printf("GL: window %dx%d flags=0x%x, skip draw\n",w,h,(unsigned)SDL_GetWindowFlags(window_));std::fflush(stdout);}
            return false;
        }
        if(direct_) {
            width_=uint32_t(w);height_=uint32_t(h);
            gl.BindFramebuffer(GL_FRAMEBUFFER,0);
        } else {
            if(!frame||width_!=uint32_t(w)||height_!=uint32_t(h))makeFrame(uint32_t(w),uint32_t(h));
            gl.BindFramebuffer(GL_FRAMEBUFFER,frame);
        }
        gl.Viewport(0,0,w,h);
        gl.Disable(GL_DEPTH_TEST);gl.Disable(GL_CULL_FACE);gl.Disable(GL_SCISSOR_TEST);
        gl.StencilMask(0xff);
        gl.ClearColor(clear[0],clear[1],clear[2],clear[3]);gl.ClearStencil(0);
        gl.Clear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
        gl.Enable(GL_STENCIL_TEST);
        gl.BindVertexArray(vertexArray);gl.ActiveTexture(GL_TEXTURE0);
        gl.UseProgram(picture);setLight({1,1,1,1,1,1,1,1,1,1,1,1});gl.Uniform1f(pictureAmount,1.f);
        static bool logged=false;
        if(!logged){logged=true;std::printf("GL: %s %dx%d%s\n",name_.c_str(),w,h,direct_?" canvas":" FBO");std::fflush(stdout);}
        return true;
    }
    uint32_t width() const override {return width_;}
    uint32_t height() const override {return height_;}
    void draw(const DrawList& list) override {
        std::array<float,12> light{};bool lightSet=false;GLuint program=0;float amountSet=-1.f;
        for(const auto& c:list) {
            gl.BindTexture(GL_TEXTURE_2D,c.image?texture(*c.image):solid(c.color));
            const bool dark=c.blend==DrawCommand::Blend::Darken||c.blend==DrawCommand::Blend::Shadow;
            const GLuint want=dark?shadow:picture;
            if(program!=want){gl.UseProgram(want);program=want;lightSet=false;amountSet=-1.f;}
            gl.Uniform4fv(dark?shadowQuad:pictureQuad,1,c.quad.data());
            if(!dark&&(!lightSet||light!=c.light)){light=c.light;lightSet=true;setLight(light);}
            if(!dark) {
                // Mix opacity lives in the shader. glBlendColor / GL_CONSTANT_COLOR is ignored on some
                // GLES drivers (Pixel / Mali), which left the default constant of 0 and made see-through
                // HUD (the phone controls) hittable but invisible.
                const float mixAmount=c.blend==DrawCommand::Blend::Mix?(c.amount<0.f?0.f:c.amount>1.f?1.f:c.amount):1.f;
                if(amountSet!=mixAmount){amountSet=mixAmount;gl.Uniform1f(pictureAmount,mixAmount);}
            }
            switch(c.blend) {
            case DrawCommand::Blend::Normal:gl.Disable(GL_BLEND);break;
            // Fire and glows are added onto what is below them.
            case DrawCommand::Blend::Add:gl.Enable(GL_BLEND);gl.BlendEquation(GL_FUNC_ADD);gl.BlendFuncSeparate(GL_ONE,GL_ONE,GL_ZERO,GL_ONE);break;
            // See-through: the picture and what is below it, by the shader's alpha (`amount`).
            case DrawCommand::Blend::Mix:gl.Enable(GL_BLEND);gl.BlendEquation(GL_FUNC_ADD);
                gl.BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);break;
            case DrawCommand::Blend::Darken:case DrawCommand::Blend::Shadow:gl.Enable(GL_BLEND);gl.BlendEquation(GL_FUNC_ADD);
                gl.BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);break;
            // Brightness: what is below times twice the colour drawn.
            case DrawCommand::Blend::Scale:gl.Enable(GL_BLEND);gl.BlendEquation(GL_FUNC_ADD);gl.BlendFuncSeparate(GL_DST_COLOR,GL_SRC_COLOR,GL_ZERO,GL_ONE);break;
            }
            // Everything drawn marks the stencil; shadows only darken what is marked.
            if(c.blend==DrawCommand::Blend::Shadow){gl.StencilMask(0);gl.StencilFunc(GL_EQUAL,1,0xff);gl.StencilOp(GL_KEEP,GL_KEEP,GL_KEEP);}
            else {gl.StencilMask(0xff);gl.StencilFunc(GL_ALWAYS,1,0xff);gl.StencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);}
            gl.DrawArrays(GL_TRIANGLES,0,3);
        }
        gl.Disable(GL_BLEND);
        gl.UseProgram(picture);setLight({1,1,1,1,1,1,1,1,1,1,1,1});gl.Uniform1f(pictureAmount,1.f);
    }
    void present() override {
        if(!direct_) {
            gl.BindFramebuffer(GL_FRAMEBUFFER,0);
            gl.Viewport(0,0,GLint(width_),GLint(height_));
            gl.Disable(GL_STENCIL_TEST);gl.Disable(GL_BLEND);gl.Disable(GL_DEPTH_TEST);
            gl.UseProgram(presentProg);
            gl.BindTexture(GL_TEXTURE_2D,frameColour);
            gl.BindVertexArray(vertexArray);
            gl.DrawArrays(GL_TRIANGLES,0,3);
        }
        SDL_GL_SwapWindow(window_);
    }
    size_t readback(const std::filesystem::path& capture) override {
        // The offscreen frame's first row is the top of the picture.
        std::vector<uint8_t> rgba(size_t(width_)*height_*4);
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER,frame);
        gl.ReadPixels(0,0,GLsizei(width_),GLsizei(height_),GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
        std::vector<uint8_t> bgra(rgba.size());
        size_t changed=0;
        for(size_t i=0;i<rgba.size();i+=4) {
            if(memcmp(&rgba[i],rgba.data(),3)!=0)++changed;
            bgra[i]=rgba[i+2];bgra[i+1]=rgba[i+1];bgra[i+2]=rgba[i];bgra[i+3]=255;
        }
        if(!capture.empty()) {
            // A 32-bit top-down BMP, as the Direct3D backend writes it.
            std::vector<uint8_t> header;
            auto put16=[&](uint32_t v){header.push_back(uint8_t(v));header.push_back(uint8_t(v>>8));};
            auto put32=[&](uint32_t v){put16(v&0xffff);put16(v>>16);};
            put16(0x4d42);put32(uint32_t(14+40+bgra.size()));put32(0);put32(14+40);
            put32(40);put32(width_);put32(uint32_t(-int32_t(height_)));put16(1);put16(32);
            put32(0);put32(0);put32(0);put32(0);put32(0);put32(0);
            std::ofstream out(capture,std::ios::binary);
            out.write(reinterpret_cast<const char*>(header.data()),std::streamsize(header.size()));
            out.write(reinterpret_cast<const char*>(bgra.data()),std::streamsize(bgra.size()));
            if(!out)throw std::runtime_error("Cannot write capture file");
        }
        return changed;
    }
    void forgetTextures() override {
        for(auto& [image,id]:textures)gl.DeleteTextures(1,&id);
        textures.clear();
    }
    void forgetTexture(const Image* image) override {
        if(auto found=textures.find(image);found!=textures.end()){gl.DeleteTextures(1,&found->second);textures.erase(found);}
    }
    bool software() const override {return false;}
    std::string name() const override {return name_;}
};
}
std::unique_ptr<GpuBackend> makeGlBackend(void* sdlWindow) {
    return std::make_unique<GlBackend>(static_cast<SDL_Window*>(sdlWindow));
}
}
