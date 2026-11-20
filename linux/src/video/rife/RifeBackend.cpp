#define VK_NO_PROTOTYPES
#include "video/rife/RifeBackend.h"

#include "video/rife/RifeGraph.h"
#include "video/rife/RifeHistoryPolicy.h"

#include <SDL3/SDL_video.h>
#include <dlfcn.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace llcv::rife_vulkan {
namespace {

void Require(bool ok, const char* stage) {
    if (!ok) throw std::runtime_error(stage);
}

void Check(VkResult result, const char* stage) {
    if (result != VK_SUCCESS) {
        char text[200];
        std::snprintf(text, sizeof(text), "%s: VkResult %d", stage, static_cast<int>(result));
        throw std::runtime_error(text);
    }
}

constexpr auto kMemoryHandle = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
constexpr auto kSemaphoreHandle = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
constexpr VkImageSubresourceRange kColor{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

struct GlInterop {
    PFNGLCREATEMEMORYOBJECTSEXTPROC CreateMemoryObjects = nullptr;
    PFNGLDELETEMEMORYOBJECTSEXTPROC DeleteMemoryObjects = nullptr;
    PFNGLMEMORYOBJECTPARAMETERIVEXTPROC MemoryObjectParameteriv = nullptr;
    PFNGLIMPORTMEMORYFDEXTPROC ImportMemoryFd = nullptr;
    PFNGLTEXSTORAGEMEM2DEXTPROC TexStorageMem2D = nullptr;
    PFNGLGENSEMAPHORESEXTPROC GenSemaphores = nullptr;
    PFNGLDELETESEMAPHORESEXTPROC DeleteSemaphores = nullptr;
    PFNGLIMPORTSEMAPHOREFDEXTPROC ImportSemaphoreFd = nullptr;
    PFNGLSIGNALSEMAPHOREEXTPROC SignalSemaphore = nullptr;
    PFNGLWAITSEMAPHOREEXTPROC WaitSemaphore = nullptr;
    PFNGLGETUNSIGNEDBYTEVEXTPROC GetUnsignedBytev = nullptr;
    PFNGLGETUNSIGNEDBYTEI_VEXTPROC GetUnsignedBytei = nullptr;
    PFNGLCOPYIMAGESUBDATAPROC CopyImageSubData = nullptr;
    void (*Flush)() = nullptr;
    void (*GetIntegerv)(GLenum, GLint*) = nullptr;
    PFNGLGETSTRINGIPROC GetStringi = nullptr;
};

template <typename T>
bool Bind(T& target, const char* name) {
    target = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    return target != nullptr;
}

bool LoadInterop(GlInterop& g, std::string& reason) {
    if (!Bind(g.GetIntegerv, "glGetIntegerv") || !Bind(g.GetStringi, "glGetStringi")) {
        reason = "glGetStringi missing";
        return false;
    }
    GLint count = 0;
    g.GetIntegerv(GL_NUM_EXTENSIONS, &count);
    bool memory = false;
    bool memoryFd = false;
    bool semaphore = false;
    bool semaphoreFd = false;
    bool copyImage = false;
    for (GLint i = 0; i < count; ++i) {
        const auto* name = reinterpret_cast<const char*>(g.GetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (!name) continue;
        memory = memory || std::strcmp(name, "GL_EXT_memory_object") == 0;
        memoryFd = memoryFd || std::strcmp(name, "GL_EXT_memory_object_fd") == 0;
        semaphore = semaphore || std::strcmp(name, "GL_EXT_semaphore") == 0;
        semaphoreFd = semaphoreFd || std::strcmp(name, "GL_EXT_semaphore_fd") == 0;
        copyImage = copyImage || std::strcmp(name, "GL_ARB_copy_image") == 0;
    }
    if (!memory || !memoryFd || !semaphore || !semaphoreFd) {
        reason = "OpenGL lacks GL_EXT_memory_object_fd / GL_EXT_semaphore_fd";
        return false;
    }
    GLint major = 0;
    GLint minor = 0;
    g.GetIntegerv(GL_MAJOR_VERSION, &major);
    g.GetIntegerv(GL_MINOR_VERSION, &minor);
    if (!copyImage && major * 10 + minor < 43) {
        reason = "OpenGL lacks glCopyImageSubData (GL 4.3 or GL_ARB_copy_image)";
        return false;
    }
    const bool bound = Bind(g.CreateMemoryObjects, "glCreateMemoryObjectsEXT") &&
                       Bind(g.DeleteMemoryObjects, "glDeleteMemoryObjectsEXT") &&
                       Bind(g.MemoryObjectParameteriv, "glMemoryObjectParameterivEXT") &&
                       Bind(g.ImportMemoryFd, "glImportMemoryFdEXT") &&
                       Bind(g.TexStorageMem2D, "glTexStorageMem2DEXT") &&
                       Bind(g.GenSemaphores, "glGenSemaphoresEXT") &&
                       Bind(g.DeleteSemaphores, "glDeleteSemaphoresEXT") &&
                       Bind(g.ImportSemaphoreFd, "glImportSemaphoreFdEXT") &&
                       Bind(g.SignalSemaphore, "glSignalSemaphoreEXT") &&
                       Bind(g.WaitSemaphore, "glWaitSemaphoreEXT") &&
                       Bind(g.GetUnsignedBytev, "glGetUnsignedBytevEXT") &&
                       Bind(g.GetUnsignedBytei, "glGetUnsignedBytei_vEXT") &&
                       Bind(g.CopyImageSubData, "glCopyImageSubData") && Bind(g.Flush, "glFlush");
    if (!bound) reason = "OpenGL interop entry points missing";
    return bound;
}

struct Loader {
    void* module = nullptr;
    PFN_vkGetInstanceProcAddr instanceAddress = nullptr;
    Loader() {
        module = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!module) module = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        Require(module != nullptr, "Vulkan loader (libvulkan.so.1) not found");
        instanceAddress = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(module, "vkGetInstanceProcAddr"));
        Require(instanceAddress != nullptr, "Vulkan loader entry point");
    }
    ~Loader() {
        if (module) dlclose(module);
    }
};

struct Sharing {
    Loader& loader;
    GlDevice gl;
    VkInstance instance = VK_NULL_HANDLE;

    static bool Match(void* cookie, void* instance, void* physical) {
        auto& self = *static_cast<Sharing*>(cookie);
        self.instance = static_cast<VkInstance>(instance);
        auto props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            self.loader.instanceAddress(self.instance, "vkGetPhysicalDeviceProperties2"));
        if (!props) {
            props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
                self.loader.instanceAddress(self.instance, "vkGetPhysicalDeviceProperties2KHR"));
        }
        if (!props) return false;
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id};
        props(static_cast<VkPhysicalDevice>(physical), &info);
        return std::memcmp(id.deviceUUID, self.gl.deviceUuid.data(), VK_UUID_SIZE) == 0 &&
               std::memcmp(id.driverUUID, self.gl.driverUuid.data(), VK_UUID_SIZE) == 0;
    }

    static int Extend(void* cookie, uintptr_t call, void* physical, const void* source, const void* allocator,
                      void* device) {
        auto& self = *static_cast<Sharing*>(cookie);
        auto info = *static_cast<const VkDeviceCreateInfo*>(source);
        std::vector<const char*> extensions(info.ppEnabledExtensionNames,
                                            info.ppEnabledExtensionNames + info.enabledExtensionCount);
        const auto enumerate = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
            self.loader.instanceAddress(self.instance, "vkEnumerateDeviceExtensionProperties"));
        std::vector<VkExtensionProperties> available;
        if (enumerate) {
            uint32_t count = 0;
            enumerate(static_cast<VkPhysicalDevice>(physical), nullptr, &count, nullptr);
            available.resize(count);
            enumerate(static_cast<VkPhysicalDevice>(physical), nullptr, &count, available.data());
        }
        const auto supported = [&](const char* name) {
            return std::any_of(available.begin(), available.end(),
                               [&](const VkExtensionProperties& p) { return std::strcmp(p.extensionName, name) == 0; });
        };
        for (const char* name : {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                                 VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
                                 VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
                                 VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME}) {
            if (!supported(name)) continue;
            if (std::none_of(extensions.begin(), extensions.end(),
                             [&](const char* s) { return std::strcmp(s, name) == 0; })) {
                extensions.push_back(name);
            }
        }
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        return reinterpret_cast<PFN_vkCreateDevice>(call)(static_cast<VkPhysicalDevice>(physical), &info,
                                                          static_cast<const VkAllocationCallbacks*>(allocator),
                                                          static_cast<VkDevice*>(device));
    }
};

const char* kPackShader = R"(#version 450
#extension GL_EXT_shader_16bit_storage : require
layout(local_size_x=16,local_size_y=8) in;
layout(binding=0,rgba8) readonly uniform image2D rgba;
layout(binding=1,std430) buffer Full {float f[];};
layout(binding=2,std430) buffer Low {float16_t l[];};
layout(binding=3,std430) writeonly buffer End {float16_t e[];};
layout(push_constant) uniform Steps {int fs,ls,es,os,lw,lh,flipY;} p;
vec3 ReadRgb(ivec2 x){return imageLoad(rgba,ivec2(x.x,p.flipY!=0?1079-x.y:x.y)).rgb;}
void main(){
    ivec2 x=ivec2(gl_GlobalInvocationID.xy);
    if(x.x>=1920||x.y>=1080)return;
    int index=x.y*1920+x.x;
    vec3 c=ReadRgb(x);
    for(int k=0;k<3;++k){
        f[index+k*p.fs]=f[index+(k+3)*p.fs];
        f[index+(k+3)*p.fs]=c[k];
    }
    if(x.x>=p.lw||x.y>=p.lh)return;
    vec2 scale=vec2(1920,1080)/vec2(p.lw,p.lh);
    vec2 q=(vec2(x)+.5)*scale-.5;
    ivec2 a=ivec2(q), b=min(a+1,ivec2(1919,1079));
    vec2 t=q-vec2(a);
    c=mix(mix(ReadRgb(a),ReadRgb(ivec2(b.x,a.y)),t.x),
          mix(ReadRgb(ivec2(a.x,b.y)),ReadRgb(b),t.x),t.y);
    int j=x.y*p.lw+x.x;
    for(int k=0;k<3;++k){
        l[j+k*p.ls]=l[j+(k+3)*p.ls];
        l[j+(k+3)*p.ls]=float16_t(c[k]);
        e[j+k*p.es]=float16_t(c[k]);
    }
    l[j+6*p.ls]=float16_t(.5);
}
)";

const char* kUnpackShader = R"(#version 450
layout(local_size_x=16,local_size_y=8) in;
layout(binding=5,rgba8) writeonly uniform image2D rgba;
layout(binding=4,std430) readonly buffer Result {float rgb[];};
layout(push_constant) uniform Steps {int fs,ls,es,os;} p;
void main(){
    ivec2 x=ivec2(gl_GlobalInvocationID.xy);
    if(x.x>=1920||x.y>=1080)return;
    int i=x.y*1920+x.x;
    imageStore(rgba,x,vec4(clamp(vec3(rgb[i],rgb[i+p.os],rgb[i+2*p.os]),0.,1.),1.));
}
)";

constexpr int kHudStages = 10;

std::string HudShader(int stage, bool chroma) {
    const int n = kHudPlane;
    std::string s = "#version 450\n#define STAGE " + std::to_string(stage) + "\n#define W " +
                    std::to_string(kHudWidth) + "\n#define H " + std::to_string(kHudHeight) + "\n#define N " +
                    std::to_string(n) + "\n#define CHROMA " + (chroma ? "1\n" : "0\n") + "#define CHROMA_BASE " +
                    std::to_string(kHudFloats) + "\n#define TILED 0\n#define SKIP_EMPTY 0\n#define EARLY_CHROMA 0\n";
    s += R"(
layout(local_size_x=16,local_size_y=8) in;
layout(binding=0,rgba8) readonly uniform image2D rgba;
layout(binding=6,std430) buffer State {float s[];};
layout(binding=7,std430) buffer State4 {vec4 s4[];};
layout(binding=8,std430) buffer Counts {uint u[];};
layout(push_constant) uniform Step {int p,reset;float dt;int radius,flipY;} c;
#define L(k) ((k)*N)
#define LUMA(q) L(q)
#define SMOOTH L(2)
#define DIFF L(3)
#define HOR L(4)
#define ACT L(5)
#define OCC L(6)
#define DIRX L(7)
#define DIRY L(8)
#define DIST L(9)
#define WEIGHT L(10)
#define CAND L(11)
#define CORE L(12)
#define F4(q) ((13+(q)*4)*N/4)
#define STRUCT4 (21*N/4)
#define STRUCTH4 (25*N/4)
#define AGE L(29)
#define COUNT (L(29)+4)
#if SKIP_EMPTY && (STAGE==8 || STAGE==9)
bool HasCore() {
    uint merged=u[COUNT+1];
#if CHROMA
    uint coreCount=float(merged)>.12*N?u[COUNT+3]:merged;
#else
    uint coreCount=merged;
#endif
    return coreCount>0u&&float(coreCount)<=.12*N;
}
#endif
#if CHROMA
#define UV CHROMA_BASE
#define UV_PREV (UV+N)
#define UV_AGE (UV_PREV+N)
#define UV_CAND (UV_AGE+N)
#endif
int I(int x,int y){return clamp(y,0,H-1)*W+clamp(x,0,W-1);}
#if CHROMA
vec2 Chroma(int x,int y){return unpackHalf2x16(u[UV+I(x,y)]);}
#endif
shared uint groupCount;
#if TILED && STAGE==3
shared float activityTile[8][40];
#elif TILED && STAGE==4
shared float activityTile[32][16];
#endif
void Count(bool v,int slot){
    if(gl_LocalInvocationIndex==0)groupCount=0;
    barrier();
    if(v)atomicAdd(groupCount,1u);
    barrier();
    if(gl_LocalInvocationIndex==0&&groupCount>0)atomicAdd(u[COUNT+slot],groupCount);
}
void main(){
    ivec2 g=ivec2(gl_GlobalInvocationID.xy);
    bool inside=g.x<W&&g.y<H;
    int x=g.x,y=g.y,i=y*W+x;
#if STAGE==0
    if(!inside)return;
    float v=0.;
#if CHROMA
    vec2 color=vec2(0.);
#endif
    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
        int row=2*y+dy;
        vec3 q=imageLoad(rgba,ivec2(2*x+dx,c.flipY!=0?1079-row:row)).rgb;
        float y0=q.r*.2126+q.g*.7152+q.b*.0722;
        v+=y0;
#if CHROMA
        color+=q.rb-vec2(y0);
#endif
    }
    s[LUMA(c.p)+i]=v*.25;
#if CHROMA
    u[UV+i]=packHalf2x16(color*.25);
#endif
#elif STAGE==1
    float delta=0.;
    if(inside){
        float sum=0.;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)sum+=s[LUMA(c.p)+I(x+dx,y+dy)];
        s[SMOOTH+i]=sum/9.;
        delta=c.reset!=0?0.:abs(s[LUMA(c.p)+i]-s[LUMA(1-c.p)+i]);
        s[DIFF+i]=delta;
    }
    Count(inside&&delta>.12,0);
    Count(inside&&delta>.02,2);
#elif STAGE==2
    if(gl_GlobalInvocationID.x!=0||gl_GlobalInvocationID.y!=0)return;
    bool moving=float(u[COUNT+2])>=.03*N;
    float quiet=c.reset!=0?0.:(moving?0.:s[AGE+1]+c.dt);
    bool clear=c.reset!=0||float(u[COUNT+0])>.70*N||quiet>.5;
    s[AGE+0]=clear?0.:s[AGE+0]+c.dt;s[AGE+1]=clear?0.:quiet;s[AGE+2]=clear?1.:0.;
#elif STAGE==3
#if TILED
    int lx=int(gl_LocalInvocationID.x),ly=int(gl_LocalInvocationID.y);
    for(int j=lx;j<40;j+=16)activityTile[ly][j]=s[DIFF+I(x-lx+j-12,y)];
    barrier();
#endif
    if(!inside)return;
    float sum=0.;
    for(int dx=-12;dx<=12;++dx){
#if TILED
        sum+=activityTile[ly][lx+dx+12];
#else
        sum+=s[DIFF+I(x+dx,y)];
#endif
    }
    s[HOR+i]=sum/25.;
#elif STAGE==4
#if TILED
    int lx=int(gl_LocalInvocationID.x),ly=int(gl_LocalInvocationID.y);
    for(int j=ly;j<32;j+=8)activityTile[j][lx]=s[HOR+I(x,y-ly+j-12)];
    barrier();
#endif
    if(!inside)return;
    float gx=(s[SMOOTH+I(x+1,y)]-s[SMOOTH+I(x-1,y)])*.5;
    float gy=(s[SMOOTH+I(x,y+1)]-s[SMOOTH+I(x,y-1)])*.5;
    float energy=gx*gx+gy*gy,mag=sqrt(energy),cx=(gx*gx-gy*gy)/(energy+1e-8),cy=2.*gx*gy/(energy+1e-8);
    s4[F4(c.p)+i]=vec4(mag,cx,cy,0.);
#if CHROMA
    vec4 grad=vec4(Chroma(x+1,y)-Chroma(x-1,y),Chroma(x,y+1)-Chroma(x,y-1))*.5;
    float lenC=length(grad);
#if EARLY_CHROMA
    float colorAge=0.;
    if(lenC>=.035){
        vec4 unitC=grad/lenC;
        if(s[AGE+2]==0.&&dot(unitC,unpackSnorm4x8(u[UV_PREV+i]))>=.94)
            colorAge=min(s[UV_AGE+i]+c.dt,1.);
        u[UV_PREV+i]=packSnorm4x8(unitC);
    }else{
        u[UV_PREV+i]=0u;
    }
#else
    vec4 unitC=lenC>=.035?grad/lenC:vec4(0.);
    bool colorPair=lenC>=.035&&s[AGE+2]==0.&&dot(unitC,unpackSnorm4x8(u[UV_PREV+i]))>=.94;
    float colorAge=colorPair?min(s[UV_AGE+i]+c.dt,1.):0.;
    u[UV_PREV+i]=packSnorm4x8(unitC);
#endif
    s[UV_AGE+i]=colorAge;
    s[UV_CAND+i]=0.;
#endif
    if(s[AGE+2]!=0.){s[OCC+i]=0.;s[DIRX+i]=0.;s[DIRY+i]=0.;s[ACT+i]=0.;s4[STRUCT4+i]=vec4(0.);s[CAND+i]=0.;return;}
    float rate=1.-exp(-c.dt/.55);bool edge=mag>=.012;
    float occ=s[OCC+i]+rate*(float(edge)-s[OCC+i]);
    float mx=s[DIRX+i]+rate*(cx*float(edge)-s[DIRX+i]);
    float my=s[DIRY+i]+rate*(cy*float(edge)-s[DIRY+i]);
    float local=0.;
    for(int dy=-12;dy<=12;++dy){
#if TILED
        local+=activityTile[ly+dy+12][lx];
#else
        local+=s[HOR+I(x,y+dy)];
#endif
    }
    local/=25.;
    float act=s[ACT+i]+rate*(local-s[ACT+i]);
    s[OCC+i]=occ;s[DIRX+i]=mx;s[DIRY+i]=my;s[ACT+i]=act;
    float len=sqrt(mx*mx+my*my);
    bool persistent=occ>=.72&&len/(occ+1e-6)>=.86;
    vec4 old=s4[F4(1-c.p)+i];
    bool pair=edge&&old.x>=.012&&cx*old.y+cy*old.z>=.75;
#if CHROMA
    s[UV_CAND+i]=(colorAge>=.75&&mag<.012&&old.x<.012&&act>=.008)?1.:0.;
#endif
    s[CAND+i]=(persistent&&pair&&(mx*cx+my*cy)/(len+1e-6)>=.75&&act>=.008)?1.:0.;
    s4[STRUCT4+i]=persistent?vec4(mx,my,occ,1.):vec4(0.);
#elif STAGE==5
    if(!inside)return;
    vec4 sum=vec4(0.);for(int dx=-5;dx<=5;++dx)sum+=s4[STRUCT4+I(x+dx,y)];s4[STRUCTH4+i]=sum/11.;
#elif STAGE==6
    if(!inside)return;
    vec4 sum=vec4(0.);for(int dy=-5;dy<=5;++dy)sum+=s4[STRUCTH4+I(x,y+dy)];
    float variety=sqrt(sum.x*sum.x+sum.y*sum.y)/(sum.z+11e-6);
    bool keep=s[CAND+i]!=0.&&s[AGE+0]>=1.&&sum.w/11.>=.025&&variety<.94&&float(u[COUNT+2])>=.03*N;
#if CHROMA
    bool color=false;
    if(!keep&&s[UV_CAND+i]!=0.&&s[AGE+0]>=1.&&float(u[COUNT+2])>=.03*N){
        int support=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
            if(dx==0&&dy==0)continue;
            int xx=x+dx,yy=y+dy;
            if(xx>=0&&xx<W&&yy>=0&&yy<H&&s[UV_CAND+yy*W+xx]!=0.)++support;
        }
        color=support>=2;
    }
    s[CAND+i]=keep?1.:(color?2.:0.);
#else
    s[CAND+i]=keep?1.:0.;
#endif
#elif STAGE==7
#if CHROMA
    bool luma=false, color=false;
    if(inside)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
        int xx=x+dx,yy=y+dy;
        if(xx>=0&&xx<W&&yy>=0&&yy<H){
            float v=s[CAND+yy*W+xx]; luma=luma||v==1.; color=color||v==2.;
        }
    }
    if(inside)s[CORE+i]=luma?1.:(color?2.:0.);
    Count(inside&&(luma||color),1);
    barrier();
    Count(inside&&luma,3);
#else
    bool v=false;
    if(inside)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
        int xx=x+dx,yy=y+dy;if(xx>=0&&xx<W&&yy>=0&&yy<H)v=v||s[CAND+yy*W+xx]!=0.;}
    if(inside)s[CORE+i]=v?1.:0.;
    Count(inside&&v,1);
#endif
#elif STAGE==8
    if(!inside)return;
#if SKIP_EMPTY
    if(!HasCore())return;
#endif
    int d=c.radius+1;
    for(int step=0;step<=c.radius;++step){
        float left=x>=step?s[CORE+y*W+x-step]:0.;
        float right=x+step<W?s[CORE+y*W+x+step]:0.;
#if CHROMA
        if(float(u[COUNT+1])>.12*N){if(left==1.||right==1.){d=step;break;}}
        else
#endif
        if(left!=0.||right!=0.){d=step;break;}
    }
    s[DIST+i]=float(d);
#elif STAGE==9
    if(!inside)return;
#if SKIP_EMPTY
    if(!HasCore()){s[WEIGHT+i]=0.;return;}
#else
#if CHROMA
    if(float(u[COUNT+3])>.12*N){s[WEIGHT+i]=0.;return;}
#else
    if(float(u[COUNT+1])>.12*N){s[WEIGHT+i]=0.;return;}
#endif
#endif
    float dist=float(c.radius+1);
    for(int dy=-c.radius;dy<=c.radius;++dy)if(y+dy>=0&&y+dy<H)dist=min(dist,max(s[DIST+(y+dy)*W+x],float(abs(dy))));
    float t=clamp(1.-dist/float(c.radius),0.,1.);
    s[WEIGHT+i]=t*t*(3.-2.*t);
#endif
}
)";
    return s;
}

int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}

bool QueryGlDevice(GlDevice& device, std::string& reason) {
    GlInterop g;
    if (!LoadInterop(g, reason)) return false;
    GLint count = 0;
    g.GetIntegerv(GL_NUM_DEVICE_UUIDS_EXT, &count);
    if (count < 1) {
        reason = "OpenGL reports no device UUID";
        return false;
    }
    g.GetUnsignedBytei(GL_DEVICE_UUID_EXT, 0, device.deviceUuid.data());
    g.GetUnsignedBytev(GL_DRIVER_UUID_EXT, device.driverUuid.data());
    return true;
}

struct Backend::Impl {
    Loader loader;
    std::unique_ptr<FastGraph> graph;
    GlInterop glApi;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkGetDeviceProcAddr deviceAddress = nullptr;
    VkImage images[2]{};
    VkDeviceMemory memory[2]{};
    VkDeviceSize memorySize[2]{};
    VkImageView views[2]{};
    VkSemaphore ready = VK_NULL_HANDLE;
    VkSemaphore done = VK_NULL_HANDLE;
    int memoryFd[2]{-1, -1};
    int readyFd = -1;
    int doneFd = -1;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pack = VK_NULL_HANDLE;
    VkPipeline unpack = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer before = VK_NULL_HANDLE;
    VkCommandBuffer after = VK_NULL_HANDLE;
    std::string name;
    NativeGraph native{};
    VkDescriptorSet bound = VK_NULL_HANDLE;
    int steps[7]{};
    bool hud = false;
    bool chroma = false;
    VkPipeline detector[kHudStages]{};
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
    const uint32_t* readbackData = nullptr;
    int parity = 0;
    int64_t lastArrival = 0;
    int64_t jobArrival = 0;
    uint64_t hudJobs = 0;
    uint64_t hudActive = 0;
    double hudCoreSum = 0;

    GLuint glMemory[2]{};
    GLuint glTextures[2]{};
    GLuint glReady = 0;
    GLuint glDone = 0;
    bool attached = false;
    bool outputPending = false;

    std::mutex lock;
    std::condition_variable changed;
    std::condition_variable completed;
    bool stop = false;
    bool busy = false;
    bool queued = false;
    bool finished = false;
    int64_t queuedArrival = 0;
    double lastGpuMs = -1;
    std::exception_ptr error;
    std::thread worker;

    template <typename T>
    T D(const char* fn) const {
        auto value = reinterpret_cast<T>(deviceAddress(device, fn));
        Require(value != nullptr, fn);
        return value;
    }

    template <typename T>
    T I(const char* fn) const {
        auto value = reinterpret_cast<T>(loader.instanceAddress(instance, fn));
        Require(value != nullptr, fn);
        return value;
    }

    uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags wanted) const {
        VkPhysicalDeviceMemoryProperties props{};
        I<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(physical, &props);
        for (uint32_t type = 0; type < props.memoryTypeCount; ++type) {
            if ((bits & (1u << type)) && (props.memoryTypes[type].propertyFlags & wanted) == wanted) return type;
        }
        throw std::runtime_error("no compatible Vulkan memory type");
    }

    void CreateShared(int index) {
        VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
        external.handleTypes = kMemoryHandle;
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &external};
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = VK_FORMAT_R8G8B8A8_UNORM;
        image.extent = {static_cast<uint32_t>(kWidth), static_cast<uint32_t>(kHeight), 1};
        image.mipLevels = image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Check(D<PFN_vkCreateImage>("vkCreateImage")(device, &image, nullptr, &images[index]), "create shared image");
        VkMemoryRequirements requirements{};
        D<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(device, images[index], &requirements);
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated.image = images[index];
        VkExportMemoryAllocateInfo exportInfo{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, &dedicated};
        exportInfo.handleTypes = kMemoryHandle;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &exportInfo};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(D<PFN_vkAllocateMemory>("vkAllocateMemory")(device, &allocation, nullptr, &memory[index]),
              "allocate shared image");
        Check(D<PFN_vkBindImageMemory>("vkBindImageMemory")(device, images[index], memory[index], 0),
              "bind shared image");
        memorySize[index] = requirements.size;
        VkMemoryGetFdInfoKHR get{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
        get.memory = memory[index];
        get.handleType = kMemoryHandle;
        Check(D<PFN_vkGetMemoryFdKHR>("vkGetMemoryFdKHR")(device, &get, &memoryFd[index]), "export shared image");
        const VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                         nullptr,
                                         0,
                                         images[index],
                                         VK_IMAGE_VIEW_TYPE_2D,
                                         VK_FORMAT_R8G8B8A8_UNORM,
                                         {},
                                         kColor};
        Check(D<PFN_vkCreateImageView>("vkCreateImageView")(device, &view, nullptr, &views[index]),
              "shared image view");
    }

    void CreateSemaphore(VkSemaphore& semaphore, int& fd) {
        VkExportSemaphoreCreateInfo exportInfo{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
        exportInfo.handleTypes = kSemaphoreHandle;
        const VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &exportInfo};
        Check(D<PFN_vkCreateSemaphore>("vkCreateSemaphore")(device, &info, nullptr, &semaphore), "shared semaphore");
        VkSemaphoreGetFdInfoKHR get{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
        get.semaphore = semaphore;
        get.handleType = kSemaphoreHandle;
        Check(D<PFN_vkGetSemaphoreFdKHR>("vkGetSemaphoreFdKHR")(device, &get, &fd), "export semaphore");
    }

    VkPipeline Pipeline(const char* text) {
        auto code = graph->Compile(text);
        VkShaderModule module = VK_NULL_HANDLE;
        const VkShaderModuleCreateInfo create{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
                                              code.size() * sizeof(uint32_t), code.data()};
        Check(D<PFN_vkCreateShaderModule>("vkCreateShaderModule")(device, &create, nullptr, &module), "bridge shader");
        const VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                    nullptr,
                                                    0,
                                                    VK_SHADER_STAGE_COMPUTE_BIT,
                                                    module,
                                                    "main",
                                                    nullptr};
        const VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                                  nullptr,
                                                  0,
                                                  stage,
                                                  pipelineLayout,
                                                  VK_NULL_HANDLE,
                                                  -1};
        VkPipeline result = VK_NULL_HANDLE;
        const auto status = D<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(device, VK_NULL_HANDLE, 1,
                                                                                         &compute, nullptr, &result);
        D<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(device, module, nullptr);
        Check(status, "bridge compute pipeline");
        return result;
    }

    void CreateReadback() {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = 64;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        Check(D<PFN_vkCreateBuffer>("vkCreateBuffer")(device, &info, nullptr, &readback), "HUD readback buffer");
        VkMemoryRequirements requirements{};
        D<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(device, readback, &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
            MemoryType(requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        Check(D<PFN_vkAllocateMemory>("vkAllocateMemory")(device, &allocation, nullptr, &readbackMemory),
              "HUD readback memory");
        Check(D<PFN_vkBindBufferMemory>("vkBindBufferMemory")(device, readback, readbackMemory, 0), "bind HUD readback");
        void* mapped = nullptr;
        Check(D<PFN_vkMapMemory>("vkMapMemory")(device, readbackMemory, 0, VK_WHOLE_SIZE, 0, &mapped),
              "map HUD readback");
        readbackData = static_cast<const uint32_t*>(mapped);
    }

    void RecordBefore(int64_t arrival) {
        const auto start = D<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer");
        const auto end = D<PFN_vkEndCommandBuffer>("vkEndCommandBuffer");
        const auto barrier = D<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
        const auto bindPipeline = D<PFN_vkCmdBindPipeline>("vkCmdBindPipeline");
        const auto bindSets = D<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets");
        const auto push = D<PFN_vkCmdPushConstants>("vkCmdPushConstants");
        const auto dispatch = D<PFN_vkCmdDispatch>("vkCmdDispatch");
        const VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        Check(start(before, &begin), "record input pack");
        const VkImageMemoryBarrier acquire[]{
            {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, 0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
             VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_EXTERNAL, native.computeFamily, images[0], kColor},
            {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
             VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_EXTERNAL, native.computeFamily, images[1], kColor}};
        const VkMemoryBarrier history{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_MEMORY_WRITE_BIT,
                                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        barrier(before, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &history, 0,
                nullptr, 2, acquire);
        bindPipeline(before, VK_PIPELINE_BIND_POINT_COMPUTE, pack);
        bindSets(before, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &bound, 0, nullptr);
        steps[6] = 0;
        push(before, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(steps), steps);
        dispatch(before, (kWidth + 15) / 16, (kHeight + 7) / 8, 1);
        if (hud) {
            const bool reset = rife_test::HudHistoryDiscontinuity(lastArrival, arrival);
            struct {
                int p, reset;
                float dt;
                int radius, flipY;
            } step{parity, reset ? 1 : 0, reset ? 0.f : static_cast<float>(static_cast<double>(arrival - lastArrival) / 1e6),
                   8, 0};
            lastArrival = arrival;
            const VkMemoryBarrier between{
                VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                    VK_ACCESS_TRANSFER_WRITE_BIT};
            const auto hudBuffer = reinterpret_cast<VkBuffer>(native.hud.handle);
            const VkDeviceSize countOffset = native.hud.offset + static_cast<VkDeviceSize>(29 * kHudPlane + 4) * 4;
            barrier(before, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &between, 0,
                    nullptr, 0, nullptr);
            D<PFN_vkCmdFillBuffer>("vkCmdFillBuffer")(before, hudBuffer, countOffset, 16, 0);
            for (int stage = 0; stage < kHudStages; ++stage) {
                barrier(before, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &between, 0, nullptr, 0, nullptr);
                bindPipeline(before, VK_PIPELINE_BIND_POINT_COMPUTE, detector[stage]);
                push(before, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(step), &step);
                if (stage == 2) {
                    dispatch(before, 1, 1, 1);
                } else {
                    dispatch(before, (kHudWidth + 15) / 16, (kHudHeight + 7) / 8, 1);
                }
            }
            barrier(before, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &between, 0, nullptr,
                    0, nullptr);
            const VkBufferCopy copy{countOffset, 0, 16};
            D<PFN_vkCmdCopyBuffer>("vkCmdCopyBuffer")(before, hudBuffer, readback, 1, &copy);
            parity = 1 - parity;
        }
        Check(end(before), "finish input pack");
    }

    void Build() {
        const uint32_t count = hud ? 9u : 6u;
        VkDescriptorSetLayoutBinding bindings[9]{};
        for (uint32_t i = 0; i < count; ++i) {
            bindings[i] = {i,
                           (i == 0 || i == 5) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                           VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        }
        const VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
                                                  count, bindings};
        Check(D<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(device, &set, nullptr, &setLayout),
              "bridge layout");
        const VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(steps)};
        const VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &setLayout,
                                                1, &range};
        Check(D<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(device, &layout, nullptr, &pipelineLayout),
              "bridge pipeline layout");
        const VkDescriptorPoolSize types[]{{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7}};
        const VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 2, types};
        Check(D<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(device, &poolInfo, nullptr, &descriptors),
              "bridge descriptors");
        const VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
                                                     descriptors, 1, &setLayout};
        Check(D<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(device, &allocation, &bound),
              "bridge descriptor set");
        const VkDescriptorImageInfo input{VK_NULL_HANDLE, views[0], VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo output{VK_NULL_HANDLE, views[1], VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo buffers[4]{};
        VkWriteDescriptorSet writes[9]{};
        const NativeBuffer spans[]{native.full, native.low, native.endpoint, native.output};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, bound, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                     &input, nullptr, nullptr};
        for (uint32_t i = 0; i < 4; ++i) {
            buffers[i] = {reinterpret_cast<VkBuffer>(spans[i].handle), spans[i].offset, spans[i].bytes};
            writes[i + 1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, bound, i + 1, 0, 1,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buffers[i], nullptr};
        }
        writes[5] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, bound, 5, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                     &output, nullptr, nullptr};
        const VkDescriptorBufferInfo state{reinterpret_cast<VkBuffer>(native.hud.handle), native.hud.offset,
                                           native.hud.bytes};
        for (uint32_t i = 6; i < count; ++i) {
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, bound, i, 0, 1,
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &state, nullptr};
        }
        D<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(device, count, writes, 0, nullptr);
        pack = Pipeline(kPackShader);
        unpack = Pipeline(kUnpackShader);
        if (hud) {
            for (int stage = 0; stage < kHudStages; ++stage) detector[stage] = Pipeline(HudShader(stage, chroma).c_str());
            CreateReadback();
        }

        const VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
                                                  VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, native.computeFamily};
        Check(D<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device, &commandPool, nullptr, &pool),
              "bridge command pool");
        const VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, pool,
                                                      VK_COMMAND_BUFFER_LEVEL_PRIMARY, 2};
        VkCommandBuffer cmds[2]{};
        Check(D<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device, &commandInfo, cmds),
              "bridge commands");
        before = cmds[0];
        after = cmds[1];
        const VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        const auto start = D<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer");
        const auto end = D<PFN_vkEndCommandBuffer>("vkEndCommandBuffer");
        const auto barrier = D<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
        const auto bindPipeline = D<PFN_vkCmdBindPipeline>("vkCmdBindPipeline");
        const auto bindSets = D<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets");
        const auto push = D<PFN_vkCmdPushConstants>("vkCmdPushConstants");
        const auto dispatch = D<PFN_vkCmdDispatch>("vkCmdDispatch");
        const int values[]{native.full.planeStride,     native.low.planeStride, native.endpoint.planeStride,
                           native.output.planeStride,   native.estimateWidth,   native.estimateHeight};
        std::copy(std::begin(values), std::end(values), steps);
        Check(start(after, &begin), "record output unpack");
        const VkMemoryBarrier produced{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        barrier(after, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &produced, 0,
                nullptr, 0, nullptr);
        bindPipeline(after, VK_PIPELINE_BIND_POINT_COMPUTE, unpack);
        bindSets(after, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &bound, 0, nullptr);
        push(after, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(steps), steps);
        dispatch(after, (kWidth + 15) / 16, (kHeight + 7) / 8, 1);
        const VkImageMemoryBarrier release[]{
            {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_READ_BIT, 0, VK_IMAGE_LAYOUT_GENERAL,
             VK_IMAGE_LAYOUT_GENERAL, native.computeFamily, VK_QUEUE_FAMILY_EXTERNAL, images[0], kColor},
            {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_GENERAL,
             VK_IMAGE_LAYOUT_GENERAL, native.computeFamily, VK_QUEUE_FAMILY_EXTERNAL, images[1], kColor}};
        barrier(after, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                nullptr, 2, release);
        Check(end(after), "finish output unpack");
    }

    static int Submit(void* cookie, uintptr_t call, void* queue, uint32_t count, const void* infos, uintptr_t fence) {
        auto& self = *static_cast<Impl*>(cookie);
        const auto& main = *static_cast<const VkSubmitInfo*>(infos);
        if (count != 1 || main.waitSemaphoreCount || main.signalSemaphoreCount || main.pNext ||
            main.commandBufferCount != 1) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        const VkCommandBuffer commands[]{self.before, main.pCommandBuffers[0], self.after};
        constexpr VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        const VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 1, &self.ready, &stage, 3, commands, 1,
                                  &self.done};
        return reinterpret_cast<PFN_vkQueueSubmit>(call)(static_cast<VkQueue>(queue), 1, &submit,
                                                         reinterpret_cast<VkFence>(fence));
    }

    void Work() {
        for (;;) {
            {
                std::unique_lock lk(lock);
                changed.wait(lk, [&] { return queued || stop; });
                if (!queued) return;
                jobArrival = queuedArrival;
                queued = false;
            }
            try {
                RecordBefore(jobArrival);
                const double gpu = graph->Execute(&Impl::Submit, this);
                std::lock_guard lk(lock);
                if (hud && readbackData) {
                    const auto merged = readbackData[1];
                    const auto luma = chroma ? readbackData[3] : merged;
                    const bool overCap = static_cast<float>(merged) > .12f * kHudPlane;
                    const auto corePixels = static_cast<float>(luma) > .12f * kHudPlane ? 0u : overCap ? luma : merged;
                    ++hudJobs;
                    hudCoreSum += static_cast<double>(corePixels) / static_cast<double>(kHudPlane);
                    if (corePixels) ++hudActive;
                }
                lastGpuMs = gpu;
                finished = true;
                busy = false;
            } catch (...) {
                std::lock_guard lk(lock);
                error = std::current_exception();
                busy = false;
            }
            completed.notify_all();
            if (error) return;
        }
    }

    void ReleaseGl() {
        if (glReady) glApi.DeleteSemaphores(1, &glReady);
        if (glDone) glApi.DeleteSemaphores(1, &glDone);
        for (int i = 0; i < 2; ++i) {
            if (glTextures[i]) video::gl.DeleteTextures(1, &glTextures[i]);
            if (glMemory[i]) glApi.DeleteMemoryObjects(1, &glMemory[i]);
            glTextures[i] = glMemory[i] = 0;
        }
        glReady = glDone = 0;
    }

    ~Impl() {
        if (worker.joinable()) {
            {
                std::lock_guard lk(lock);
                stop = true;
            }
            changed.notify_one();
            worker.join();
        }
        if (attached) ReleaseGl();
        for (int& fd : memoryFd) {
            if (fd >= 0) close(fd);
            fd = -1;
        }
        if (readyFd >= 0) close(readyFd);
        if (doneFd >= 0) close(doneFd);
        if (device && deviceAddress) {
            D<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device);
            if (pool) D<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device, pool, nullptr);
            for (auto p : {pack, unpack}) {
                if (p) D<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device, p, nullptr);
            }
            for (auto p : detector) {
                if (p) D<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device, p, nullptr);
            }
            if (readbackMemory) D<PFN_vkFreeMemory>("vkFreeMemory")(device, readbackMemory, nullptr);
            if (readback) D<PFN_vkDestroyBuffer>("vkDestroyBuffer")(device, readback, nullptr);
            if (pipelineLayout) D<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(device, pipelineLayout, nullptr);
            if (descriptors) D<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(device, descriptors, nullptr);
            if (setLayout) {
                D<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(device, setLayout, nullptr);
            }
            if (ready) D<PFN_vkDestroySemaphore>("vkDestroySemaphore")(device, ready, nullptr);
            if (done) D<PFN_vkDestroySemaphore>("vkDestroySemaphore")(device, done, nullptr);
            for (int i = 0; i < 2; ++i) {
                if (views[i]) D<PFN_vkDestroyImageView>("vkDestroyImageView")(device, views[i], nullptr);
                if (images[i]) D<PFN_vkDestroyImage>("vkDestroyImage")(device, images[i], nullptr);
                if (memory[i]) D<PFN_vkFreeMemory>("vkFreeMemory")(device, memory[i], nullptr);
            }
        }
        graph.reset();
    }
};

Backend::Backend() : impl_(std::make_unique<Impl>()) {}

Backend::~Backend() = default;

std::unique_ptr<Backend> Backend::Prepare(const GlDevice& gl, const std::filesystem::path& models, bool hud,
                                          bool chroma) {
    std::unique_ptr<Backend> backend(new Backend());
    auto& s = *backend->impl_;
    Sharing sharing{s.loader, gl};
    s.graph = CreateFastGraph(&Sharing::Match, &Sharing::Extend, &sharing, models, Preset::Fast, hud, chroma);
    const auto native = s.graph->Native();
    s.native = native;
    s.hud = native.hud.handle != 0;
    s.chroma = chroma;
    s.instance = static_cast<VkInstance>(native.instance);
    s.physical = static_cast<VkPhysicalDevice>(native.physical);
    s.device = static_cast<VkDevice>(native.device);
    s.deviceAddress = s.I<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
    VkPhysicalDeviceProperties props{};
    s.I<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(s.physical, &props);
    s.name = props.deviceName;
    s.CreateShared(0);
    s.CreateShared(1);
    s.CreateSemaphore(s.ready, s.readyFd);
    s.CreateSemaphore(s.done, s.doneFd);
    s.Build();
    s.graph->Record();
    return backend;
}

void Backend::AttachGl() {
    auto& s = *impl_;
    std::string reason;
    Require(LoadInterop(s.glApi, reason), reason.c_str());
    s.attached = true;
    for (int i = 0; i < 2; ++i) {
        s.glApi.CreateMemoryObjects(1, &s.glMemory[i]);
        const GLint dedicated = GL_TRUE;
        s.glApi.MemoryObjectParameteriv(s.glMemory[i], GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
        s.glApi.ImportMemoryFd(s.glMemory[i], s.memorySize[i], GL_HANDLE_TYPE_OPAQUE_FD_EXT, s.memoryFd[i]);
        s.memoryFd[i] = -1;
        video::gl.GenTextures(1, &s.glTextures[i]);
        video::gl.BindTexture(GL_TEXTURE_2D, s.glTextures[i]);
        video::gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, GL_OPTIMAL_TILING_EXT);
        s.glApi.TexStorageMem2D(GL_TEXTURE_2D, 1, GL_RGBA8, kWidth, kHeight, s.glMemory[i], 0);
        video::gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        video::gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        video::gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        video::gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        video::gl.BindTexture(GL_TEXTURE_2D, 0);
    }
    s.glApi.GenSemaphores(1, &s.glReady);
    s.glApi.ImportSemaphoreFd(s.glReady, GL_HANDLE_TYPE_OPAQUE_FD_EXT, s.readyFd);
    s.readyFd = -1;
    s.glApi.GenSemaphores(1, &s.glDone);
    s.glApi.ImportSemaphoreFd(s.glDone, GL_HANDLE_TYPE_OPAQUE_FD_EXT, s.doneFd);
    s.doneFd = -1;
    s.worker = std::thread([&s] { s.Work(); });
}

GLuint Backend::InputTexture() const {
    return impl_->glTextures[0];
}

GLuint Backend::OutputTexture() const {
    return impl_->glTextures[1];
}

bool Backend::Free() {
    std::lock_guard lk(impl_->lock);
    if (impl_->error) std::rethrow_exception(impl_->error);
    return !impl_->busy;
}

void Backend::Enqueue(int64_t arrivalUs) {
    auto& s = *impl_;
    const GLenum layouts[] = {GL_LAYOUT_GENERAL_EXT, GL_LAYOUT_GENERAL_EXT};
    if (s.outputPending) {
        s.glApi.WaitSemaphore(s.glDone, 0, nullptr, 2, s.glTextures, layouts);
        s.outputPending = false;
    }
    s.glApi.SignalSemaphore(s.glReady, 0, nullptr, 2, s.glTextures, layouts);
    s.glApi.Flush();
    {
        std::lock_guard lk(s.lock);
        if (s.error) std::rethrow_exception(s.error);
        Require(!s.busy, "a second Vulkan job cannot be queued");
        s.queued = s.busy = true;
        s.queuedArrival = arrivalUs;
    }
    s.outputPending = true;
    s.changed.notify_one();
}

bool Backend::WaitUntil(int64_t deadlineUs) {
    auto& s = *impl_;
    std::unique_lock lk(s.lock);
    const auto deadline = std::chrono::steady_clock::time_point(std::chrono::microseconds(deadlineUs));
    s.completed.wait_until(lk, deadline, [&] { return !s.busy; });
    if (s.error) std::rethrow_exception(s.error);
    return !s.busy && NowUs() <= deadlineUs;
}

void Backend::AcquireOutput() {
    auto& s = *impl_;
    if (!s.outputPending) return;
    const GLenum layouts[] = {GL_LAYOUT_GENERAL_EXT, GL_LAYOUT_GENERAL_EXT};
    s.glApi.WaitSemaphore(s.glDone, 0, nullptr, 2, s.glTextures, layouts);
    s.outputPending = false;
}

bool Backend::TakeGpuMs(double& ms) {
    std::lock_guard lk(impl_->lock);
    if (!impl_->finished) return false;
    impl_->finished = false;
    ms = impl_->lastGpuMs;
    return true;
}

bool Backend::Hud(HudStats& stats) {
    std::lock_guard lk(impl_->lock);
    if (!impl_->hud) return false;
    stats.jobs = impl_->hudJobs;
    stats.active = impl_->hudActive;
    stats.meanCore = impl_->hudJobs ? impl_->hudCoreSum / static_cast<double>(impl_->hudJobs) : 0.0;
    return true;
}

const std::string& Backend::DeviceName() const {
    return impl_->name;
}

}
