#include "HdrScreenshotReference.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

// Independent numerical oracle: double precision, no production LUT or matrix.
// RGB matrices are derived from CIE xy primaries and D65 by Gaussian elimination.
// PQ/BT.2020: ITU-R BT.2100-3; sRGB/BT.709 primaries share the D65 white point.
// Alternative curve ONLY: OBS libobs/data/color.effect eetf_0_Lmax, inspected
// 2026-09-29. This is not an emulation of OBS's complete capture/screenshot path.
// https://www.itu.int/rec/R-REC-BT.2100-3-202502-I/en
// https://github.com/obsproject/obs-studio/blob/master/libobs/data/color.effect
namespace {
using namespace llcv::screenshot;
using V = std::array<double,3>;
using M = std::array<V,3>;
std::size_t pixelsChecked=0;
int maximumError=0;
void Check(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr,"HDR reference FAIL: %s\n",name); std::exit(1); }
}
V Multiply(const M& a, const V& b) {
    V out{};
    for (unsigned r=0;r<3;++r) for (unsigned c=0;c<3;++c) out[r]+=a[r][c]*b[c];
    return out;
}
V Solve(M a, V b) {
    for (unsigned c=0;c<3;++c) {
        unsigned pivot=c;
        for (unsigned r=c+1;r<3;++r) if (std::abs(a[r][c])>std::abs(a[pivot][c])) pivot=r;
        std::swap(a[c],a[pivot]); std::swap(b[c],b[pivot]);
        const double divisor=a[c][c]; Check(std::abs(divisor)>1e-12,"nonsingular reference matrix");
        for (double& x:a[c]) x/=divisor;
        b[c]/=divisor;
        for (unsigned r=0;r<3;++r) if (r!=c) {
            const double factor=a[r][c];
            for (unsigned k=0;k<3;++k) a[r][k]-=factor*a[c][k];
            b[r]-=factor*b[c];
        }
    }
    return b;
}
M Primaries(const std::array<std::array<double,2>,3>& xy) {
    M m{};
    for (unsigned c=0;c<3;++c) {
        m[0][c]=xy[c][0]/xy[c][1]; m[1][c]=1;
        m[2][c]=(1-xy[c][0]-xy[c][1])/xy[c][1];
    }
    const V scale=Solve(m,{.3127/.3290,1,(1-.3127-.3290)/.3290});
    for (unsigned r=0;r<3;++r) for (unsigned c=0;c<3;++c) m[r][c]*=scale[c];
    return m;
}
const M bt2020=Primaries({{{.708,.292},{.170,.797},{.131,.046}}});
const M bt709=Primaries({{{.640,.330},{.300,.600},{.150,.060}}});
// Rows encode nonconstant-luminance Y, Cb, Cr from nonlinear RGB.
const M ycbcr={{{.2627,.6780,.0593},
    {-.2627/(2*(1-.0593)),-.6780/(2*(1-.0593)),.5},
    {.5,-.6780/(2*(1-.2627)),-.0593/(2*(1-.2627))}}};
double Pq(double nits) {
    const double p=std::pow(std::clamp(nits/10000,0.0,1.0),2610.0/16384);
    return std::pow((3424.0/4096+2413.0/128*p)/(1+2392.0/128*p),2523.0/32);
}
double Nits(double pq) {
    const double p=std::pow(std::clamp(pq,0.0,1.0),32.0/2523);
    return 10000*std::pow(std::max(0.0,p-3424.0/4096)/(2413.0/128-2392.0/128*p),16384.0/2610);
}
double Srgb(double linear) {
    linear=std::clamp(linear,0.0,1.0);
    return linear<=.0031308 ? 12.92*linear : 1.055*std::pow(linear,1/2.4)-.055;
}
double MappedPeak(double nits) {
    // Independent algebraic form of the documented export curve. White is
    // approached asymptotically; no source peak or per-frame analysis needed.
    return nits<=100 ? nits/203 : 1-10609/(203*(nits+3));
}
V Reference(double y, double u, double v) {
    auto rgb=Solve(ycbcr,{(y-64)/876,(u-512)/896,(v-512)/896});
    for (double& channel:rgb) channel=Nits(channel);
    rgb=Solve(bt709,Multiply(bt2020,rgb));
    const double low=*std::min_element(rgb.begin(),rgb.end());
    if (low<0) {
        // Policy definition uses rounded Rec.709 luma, independently from
        // the matrix derivation. This is gamut compression, not PQ decoding.
        const double gray=std::max(0.0,.2126*rgb[0]+.7152*rgb[1]+.0722*rgb[2]);
        const double saturation=gray/(gray-low);
        for (double& channel:rgb) channel=gray+(channel-gray)*saturation;
    }
    const double peak=*std::max_element(rgb.begin(),rgb.end());
    const double scale=peak>0 ? MappedPeak(peak)/peak : 1/203.0;
    for (double& channel:rgb) channel=Srgb(channel*scale);
    return rgb;
}
void Put(std::vector<std::uint8_t>& raw, std::size_t word, unsigned code, unsigned junk=0) {
    const unsigned value=(code<<6)|(junk&63);
    raw[word*2]=static_cast<std::uint8_t>(value);
    raw[word*2+1]=static_cast<std::uint8_t>(value>>8);
}
unsigned Get(const std::vector<std::uint8_t>& raw, std::size_t word) {
    return (raw[word*2]+256u*raw[word*2+1])/64;
}
std::vector<std::uint8_t> Convert(const Description& d, const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> result(std::size_t(d.width)*d.height*4);
    Check(ConvertRows(d,raw,0,d.height,result),"production conversion");
    return result;
}
void Compare(const std::uint8_t* pixel, const V& expected) {
    for (unsigned c=0;c<3;++c) {
        Check(std::isfinite(expected[c]),"finite oracle");
        const int want=static_cast<int>(std::lround(expected[c]*255));
        const int error=std::abs(int(pixel[2-c])-want);
        maximumError=std::max(maximumError,error);
        if (error>1) std::fprintf(stderr,"pixel=%zu channel=%u actual=%u reference=%d\n",pixelsChecked,c,pixel[2-c],want);
        Check(error<=1,"independent RGB oracle within one 8-bit code");
    }
    Check(pixel[3]==255,"opaque alpha"); ++pixelsChecked;
}
void Constant(unsigned y, unsigned u, unsigned v) {
    Description d{2,2,Format::P010}; std::vector<std::uint8_t> raw(PackedSize(d));
    for (unsigned i=0;i<4;++i) Put(raw,i,y);
    Put(raw,4,u); Put(raw,5,v);
    const auto actual=Convert(d,raw); const auto ref=Reference(y,u,v);
    for (unsigned i=0;i<4;++i) Compare(actual.data()+4*i,ref);
}
void EncodedPatch(V linear2020) {
    for (double& c:linear2020) c=Pq(c);
    const auto encoded=Multiply(ycbcr,linear2020);
    const auto y=static_cast<unsigned>(std::lround(64+876*encoded[0]));
    const auto u=static_cast<unsigned>(std::lround(512+896*encoded[1]));
    const auto v=static_cast<unsigned>(std::lround(512+896*encoded[2]));
    Check(y>=64 && y<=940 && u>=64 && u<=960 && v>=64 && v<=960,"legal encoded physical patch");
    Constant(y,u,v); // Compare quantized signal, not unquantized original.
}
double Chroma(const Description& d,const std::vector<std::uint8_t>& raw,unsigned x,unsigned y,unsigned c) {
    // Independent physical-position tent convolution. Extend the image by
    // replicating edge samples, instead of copying production lerp indices.
    double value=0,weight=0;
    const int cw=static_cast<int>(d.width/2),ch=static_cast<int>(d.height/2);
    for (int sy=-1;sy<=ch;++sy) for (int sx=-1;sx<=cw;++sx) {
        const double wx=std::max(0.0,1-std::abs(double(x)-2*sx)/2);
        const double wy=std::max(0.0,1-std::abs(double(y)-(2*sy+(d.topLeftChroma ? 0 : .5)))/2);
        const auto at=std::size_t(d.width)*d.height+
            std::size_t(std::clamp(sy,0,ch-1))*d.width+2*std::clamp(sx,0,cw-1)+c;
        value+=wx*wy*Get(raw,at); weight+=wx*wy;
    }
    Check(std::abs(weight-1)<1e-12,"normalized reference chroma kernel");
    return value;
}
void Spatial(Description d) {
    auto raw=HdrScreenshotPattern(d); const auto out=Convert(d,raw);
    for (unsigned y=0;y<d.height;++y) for (unsigned x=0;x<d.width;++x) {
        const auto at=std::size_t(y)*d.width+x;
        Compare(out.data()+at*4,Reference(Get(raw,at),Chroma(d,raw,x,y,0),Chroma(d,raw,x,y,1)));
    }
    // Dirty low six bits must not leak into P010 conversion.
    auto dirty=raw;
    for (std::size_t i=0;i<raw.size()/2;++i) Put(dirty,i,Get(raw,i),static_cast<unsigned>(i*17+63));
    Check(Convert(d,dirty)==out,"P010 low bits ignored");
    const unsigned row=d.width*2,stride=row+13,rows=d.height*3/2;
    std::vector<std::uint8_t> padded(std::size_t(stride)*rows,0xcd),packed(raw.size());
    for (unsigned y=0;y<rows;++y) std::memcpy(padded.data()+std::size_t(y)*stride,raw.data()+std::size_t(y)*row,row);
    Check(CopyFrame(d,padded.data(),padded.size(),stride,packed) && packed==raw,"colored padded frame");
    std::vector<std::uint8_t> guarded(out.size()+32,0xad);
    for (unsigned y=0;y<d.height;) {
        const unsigned count=std::min(3u,d.height-y);
        Check(ConvertRows(d,packed,y,count,std::span(guarded).subspan(16+std::size_t(y)*d.width*4,std::size_t(count)*d.width*4)),"odd row chunks");
        y+=count;
    }
    Check(std::equal(out.begin(),out.end(),guarded.begin()+16),"colored chunk identity");
    Check(std::all_of(guarded.begin(),guarded.begin()+16,[](auto b){return b==0xad;}) &&
        std::all_of(guarded.end()-16,guarded.end(),[](auto b){return b==0xad;}),"output canaries intact");
}
double AlternativeEetf(double nits,double sourcePeak) {
    // Hermite shoulder in PQ space, as in OBS's EETF helper, with black=0
    // and target peak=203 nits. Output is normalized to that SDR peak.
    const double white=Pq(sourcePeak),target=Pq(203)/white;
    const double knee=1.5*target-.5;
    double e=std::clamp(Pq(nits)/white,0.0,1.0);
    if (e>knee) {
        const double t=(e-knee)/(1-knee);
        e=(2*t*t*t-3*t*t+1)*knee+(t*t*t-2*t*t+t)*(1-knee)+(-2*t*t*t+3*t*t)*target;
    }
    return Srgb(Nits(e*white)/203);
}
void BrightnessPolicy() {
    double previous=-1;
    for (unsigned i=0;i<=100000;++i) {
        const double nits=i/10.0, mapped=MappedPeak(nits);
        Check(std::isfinite(mapped) && mapped>=previous && mapped<1,"bounded monotonic shoulder through 10000 nit");
        Check(mapped+1e-12>=nits/(203+nits),"no neutral darkening versus previous curve");
        if (nits<=100) Check(std::abs(mapped-nits/203)<1e-12,"linear midtone identity");
        previous=mapped;
    }
    // Check the junction itself, not just quantized output far from it.
    constexpr double step=.001;
    const double left=(MappedPeak(100)-MappedPeak(100-step))/step;
    const double right=(MappedPeak(100+step)-MappedPeak(100))/step;
    Check(std::abs(left-right)<1e-7,"continuous slope at 100-nit knee");
    // Regression anchors independent of the generic RGB oracle: enforce the
    // intended brightness using actual P010 quantization and production code.
    constexpr std::array<double,9> nits{0,1,10,50,100,203,1000,4000,10000};
    constexpr std::array<int,9> bytes{0,15,63,136,186,224,249,254,254};
    for (unsigned i=0;i<nits.size();++i) {
        Description d{2,2,Format::P010}; std::vector<std::uint8_t> raw(PackedSize(d));
        const unsigned code=static_cast<unsigned>(std::lround(64+876*Pq(nits[i])));
        for (unsigned p=0;p<4;++p) Put(raw,p,code);
        Put(raw,4,512); Put(raw,5,512);
        const auto out=Convert(d,raw);
        Check(std::abs(int(out[0])-bytes[i])<=1,"production brightness anchor");
    }
}
}

std::vector<std::uint8_t> HdrScreenshotPattern(const llcv::screenshot::Description& d) {
    Check(d.format==Format::P010 && PackedSize(d)>0,"P010 pattern dimensions");
    std::vector<std::uint8_t> raw(PackedSize(d));
    for (unsigned y=0;y<d.height;++y) for (unsigned x=0;x<d.width;++x)
        Put(raw,std::size_t(y)*d.width+x,64+(x*43+y*97)%877);
    for (unsigned y=0;y<d.height/2;++y) for (unsigned x=0;x<d.width/2;++x) {
        const auto at=std::size_t(d.width)*d.height+std::size_t(y)*d.width+x*2;
        // Asymmetric alternating steps, ramps and isolated chroma extrema.
        Put(raw,at,(x+y)%3==0 ? 64 : 256+(x*127+y*53)%705);
        Put(raw,at+1,(x+2*y)%4==0 ? 960 : 64+(x*31+y*197)%897);
    }
    return raw;
}

void HdrScreenshotReferenceTests() {
    BrightnessPolicy();
    Check(std::abs(Pq(100)-.508078421517399)<1e-12,"known PQ 100-nit anchor");
    Check(std::abs(Nits(1)-10000)<1e-6,"PQ endpoint");
    const auto red=Solve(bt709,Multiply(bt2020,{1,0,0}));
    Check(std::abs(red[0]-1.6604910021084345)<1e-12,"derived gamut anchor");
    for (double nits:{.001,.01,.1,1.,10.,100.,203.,1000.,4000.,10000.}) {
        Check(std::abs(Nits(Pq(nits))-nits)<1e-6,"PQ independent roundtrip");
        for (const V& hue:std::array<V,10>{{{1,0,0},{0,1,0},{0,0,1},{1,1,0},{0,1,1},{1,0,1},{1,1,1},{1,.25,.01},{.01,.25,1},{.18,.18,.18}}}) {
            V rgb=hue; for (double& c:rgb) c*=nits;
            EncodedPatch(rgb);
            EncodedPatch(Solve(bt2020,Multiply(bt709,rgb)));
        }
    }
    // Nominal + foot/headroom + invalid chroma combinations: bounded behavior,
    // not a claim that every YCbCr triplet describes an in-gamut HDR color.
    for (unsigned y:{0u,1u,63u,64u,65u,128u,256u,400u,512u,700u,876u,939u,940u,941u,960u,1022u,1023u})
        for (unsigned u:{0u,1u,63u,64u,65u,128u,256u,400u,511u,512u,513u,700u,939u,940u,960u,1022u,1023u})
            for (unsigned v:{0u,1u,63u,64u,65u,128u,256u,400u,511u,512u,513u,700u,939u,940u,960u,1022u,1023u}) Constant(y,u,v);
    std::uint32_t seed=0x20200929;
    const auto next=[&]() { seed=1664525u*seed+1013904223u; return seed>>22; };
    for (unsigned i=0;i<8192;++i) { const auto y=next(),u=next(),v=next(); Constant(y,u,v); }
    for (bool top:{false,true}) for (auto size:std::array<std::array<unsigned,2>,5>{{{2,2},{2,18},{34,2},{8,8},{34,18}}}) {
        Description d{size[0],size[1],Format::P010}; d.topLeftChroma=top; Spatial(d);
    }
    int previous=-1; std::set<unsigned> levels,darkLevels;
    for (unsigned code=0;code<1024;++code) {
        Description d{2,2,Format::P010}; std::vector<std::uint8_t> raw(PackedSize(d));
        for (unsigned i=0;i<4;++i) Put(raw,i,code);
        Put(raw,4,512); Put(raw,5,512); const auto out=Convert(d,raw);
        Compare(out.data(),Reference(code,512,512));
        Check(out[0]==out[1] && out[1]==out[2] && out[0]>=previous,"all-code neutral monotonic ramp");
        if (code<=64) Check(out[0]==0,"nominal black and footroom");
        previous=out[0]; levels.insert(out[0]);
        if (code>=64 && code<=940 && Nits((code-64)/876.0)<=1) darkLevels.insert(out[0]);
    }
    std::printf("HDR oracle: %zu pixels, maximum error %d/255; gray levels=%zu, <=1-nit levels=%zu\n",pixelsChecked,maximumError,levels.size(),darkLevels.size());
    std::puts("Policy comparison, 8-bit neutral sRGB: nits / old / soft-knee / EETF peak1000 / peak4000 / peak10000");
    for (double nits:{.1,1.,10.,100.,203.,1000.,4000.,10000.})
        std::printf("%.1f / %.0f / %.0f / %.0f / %.0f / %.0f\n",nits,255*Srgb(nits/(nits+203)),255*Srgb(MappedPeak(nits)),
            255*AlternativeEetf(nits,1000),255*AlternativeEetf(nits,4000),255*AlternativeEetf(nits,10000));
    for (double peak:{1000.,4000.,10000.}) {
        double prior=-1;
        for (unsigned i=0;i<=1000;++i) {
            const double actual=AlternativeEetf(peak*i/1000,peak);
            Check(std::isfinite(actual) && actual+1e-12>=prior && actual<=1,"comparison curve finite and monotonic"); prior=actual;
        }
    }
}
