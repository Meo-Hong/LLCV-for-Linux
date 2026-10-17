#include "recording/EncoderSupport.h"
#include <strmif.h>
#include <codecapi.h>
#include <cstdio>
#include <cstdlib>
using namespace llcv::recording;
static void Check(bool ok,const char* text){if(!ok){printf("FAIL %s\n",text);std::abort();}}
// Driver readback deliberately independent of the requested value. A driver
// which accepts a setting but ignores it must not pass recording preflight.
struct FakeCodec final:ICodecAPI {
    ULONG mode=3,bitrate=20000000;ULONGLONG qp=23;
    bool missing=false,wrongType=false,wideMode=false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return 1;}
    ULONG STDMETHODCALLTYPE Release() override{return 1;}
    HRESULT STDMETHODCALLTYPE IsSupported(const GUID*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE IsModifiable(const GUID*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetParameterRange(const GUID*,VARIANT*,VARIANT*,VARIANT*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetParameterValues(const GUID*,VARIANT**,ULONG*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDefaultValue(const GUID*,VARIANT*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetValue(const GUID* key,VARIANT* v) override{
        if(missing)return E_NOTIMPL;
        v->vt=VT_UI4;v->ullVal=0xDEADBEEF00000000ull; // High union bytes are unspecified for UI4.
        if(*key==CODECAPI_AVEncCommonRateControlMode){if(wideMode){v->vt=VT_UI8;v->ullVal=mode;}else v->ulVal=mode;}
        else if(*key==CODECAPI_AVEncVideoEncodeQP){v->vt=VT_UI8;v->ullVal=qp;}
        else if(*key==CODECAPI_AVEncCommonMeanBitRate)v->ulVal=bitrate;
        else return E_NOTIMPL;
        if(wrongType){v->vt=VT_BSTR;v->bstrVal=SysAllocString(L"23");}return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetValue(const GUID*,VARIANT*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE RegisterForEvent(const GUID*,LONG_PTR) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE UnregisterForEvent(const GUID*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetAllDefaults() override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetValueWithNotify(const GUID*,VARIANT*,GUID**,ULONG*) override{return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetAllDefaultsWithNotify(GUID**,ULONG*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetAllSettings(IStream*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetAllSettings(IStream*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetAllSettingsWithNotify(IStream*,GUID**,ULONG*) override{return E_NOTIMPL;}
};
int main(){
    FakeCodec driver;Options options;
    Check(SUCCEEDED(VerifyRateControl(&driver,options)),"UI4 mode with unspecified upper bytes and UI8 QP accepted");
    driver.wideMode=true;Check(SUCCEEDED(VerifyRateControl(&driver,options)),"UI8 mode accepted");
    driver.mode=0;Check(FAILED(VerifyRateControl(&driver,options)),"silently ignored quality mode rejected");
    driver.mode=3;driver.qp=26;Check(FAILED(VerifyRateControl(&driver,options)),"silently ignored QP rejected");
    driver.qp=23;driver.missing=true;Check(FAILED(VerifyRateControl(&driver,options)),"unreadable properties fail closed");
    driver.missing=false;driver.wrongType=true;Check(FAILED(VerifyRateControl(&driver,options)),"incorrect property type rejected");
    driver.wrongType=false;options.rateControl=RateControl::Bitrate;driver.mode=0;
    Check(SUCCEEDED(VerifyRateControl(&driver,options)),"target bitrate verified");
    driver.bitrate=8000000;Check(FAILED(VerifyRateControl(&driver,options)),"ignored target bitrate rejected");
    options.rateControl=static_cast<RateControl>(9);Check(FAILED(VerifyRateControl(&driver,options)),"invalid mode rejected");
    Check(FAILED(VerifyRateControl(nullptr,Options{})),"missing codec rejected");
    puts("Rate control verification: mode, QP, bitrate, types and driver failures passed.");
}
