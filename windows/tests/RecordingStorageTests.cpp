#include "recording/RecordingStorage.h"
#include <mfapi.h>
#include <mferror.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace llcv::recording;
using Microsoft::WRL::ComPtr;
void Check(bool ok,const char* why){if(!ok){printf("FAIL %s\n",why);std::exit(1);}}
void Hr(HRESULT hr,const char* why){if(FAILED(hr)){printf("HRESULT %08lX ",static_cast<unsigned long>(hr));Check(false,why);}}
enum class Fault {None,DiskFull,ShortWrite,Flush,Seek,Close};
class File final : public IMFByteStream {
    std::atomic<ULONG> refs_{1};
    ComPtr<IMFByteStream> bytes_;
public:
    Fault fault=Fault::None;
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    bool blockFlush=false,blockWrite=false;
    std::atomic<unsigned> flushes{0},writes{0},closes{0};
    File(){Hr(MFCreateTempFile(MF_ACCESSMODE_READWRITE,MF_OPENMODE_DELETE_IF_EXIST,MF_FILEFLAGS_NONE,&bytes_),"temporary backend");}
    ~File(){CloseHandle(entered);CloseHandle(release);}
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;
        if(iid!=IID_IUnknown&&iid!=__uuidof(IMFByteStream))return E_NOINTERFACE;*out=static_cast<IMFByteStream*>(this);AddRef();return S_OK;}
    STDMETHODIMP_(ULONG) AddRef() override{return ++refs_;}
    STDMETHODIMP_(ULONG) Release() override{auto left=--refs_;if(!left)delete this;return left;}
    STDMETHODIMP GetCapabilities(DWORD* v) override{return bytes_->GetCapabilities(v);}
    STDMETHODIMP GetLength(QWORD* v) override{return bytes_->GetLength(v);}
    STDMETHODIMP SetLength(QWORD v) override{return bytes_->SetLength(v);}
    STDMETHODIMP GetCurrentPosition(QWORD* v) override{return bytes_->GetCurrentPosition(v);}
    STDMETHODIMP SetCurrentPosition(QWORD v) override{return fault==Fault::Seek?HRESULT_FROM_WIN32(ERROR_SEEK):bytes_->SetCurrentPosition(v);}
    STDMETHODIMP IsEndOfStream(BOOL* v) override{return bytes_->IsEndOfStream(v);}
    STDMETHODIMP Read(BYTE* b,ULONG c,ULONG* n) override{return bytes_->Read(b,c,n);}
    STDMETHODIMP BeginRead(BYTE* b,ULONG c,IMFAsyncCallback* cb,IUnknown* s) override{return bytes_->BeginRead(b,c,cb,s);}
    STDMETHODIMP EndRead(IMFAsyncResult* r,ULONG* n) override{return bytes_->EndRead(r,n);}
    STDMETHODIMP Write(const BYTE* b,ULONG c,ULONG* n) override{
        ++writes;if(blockWrite){SetEvent(entered);WaitForSingleObject(release,5000);}
        if(fault==Fault::DiskFull){*n=0;return HRESULT_FROM_WIN32(ERROR_DISK_FULL);}
        if(fault==Fault::ShortWrite){*n=0;return S_OK;}return bytes_->Write(b,c,n);
    }
    STDMETHODIMP BeginWrite(const BYTE* b,ULONG c,IMFAsyncCallback* cb,IUnknown* s) override{return bytes_->BeginWrite(b,c,cb,s);}
    STDMETHODIMP EndWrite(IMFAsyncResult* r,ULONG* n) override{return bytes_->EndWrite(r,n);}
    STDMETHODIMP Seek(MFBYTESTREAM_SEEK_ORIGIN o,LONGLONG p,DWORD f,QWORD* n) override{return bytes_->Seek(o,p,f,n);}
    STDMETHODIMP Flush() override{
        ++flushes;if(blockFlush){SetEvent(entered);WaitForSingleObject(release,5000);}
        return fault==Fault::Flush?HRESULT_FROM_WIN32(ERROR_WRITE_FAULT):bytes_->Flush();
    }
    STDMETHODIMP Close() override{++closes;const auto hr=bytes_->Close();return fault==Fault::Close?HRESULT_FROM_WIN32(ERROR_WRITE_FAULT):hr;}
};
class Callback final : public IMFAsyncCallback {
    std::atomic<ULONG> refs_{1};
public:
    HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);ComPtr<IMFAsyncResult> result;
    ~Callback(){CloseHandle(done);}
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override{if(!out)return E_POINTER;*out=nullptr;
        if(iid!=IID_IUnknown&&iid!=__uuidof(IMFAsyncCallback))return E_NOINTERFACE;*out=static_cast<IMFAsyncCallback*>(this);AddRef();return S_OK;}
    STDMETHODIMP_(ULONG) AddRef() override{return ++refs_;}
    STDMETHODIMP_(ULONG) Release() override{auto left=--refs_;if(!left)delete this;return left;}
    STDMETHODIMP GetParameters(DWORD*,DWORD*) override{return E_NOTIMPL;}
    STDMETHODIMP Invoke(IMFAsyncResult* value) override{result=value;SetEvent(done);return S_OK;}
};
ComPtr<IMFByteStream> Buffered(File* file,StorageLimits limits={}){ComPtr<IMFByteStream> stream;Hr(BufferRecordingStorage(file,nullptr,&stream,limits),"buffer storage");return stream;}
void Write(IMFByteStream* stream,std::initializer_list<BYTE> values){ULONG n=0;Hr(stream->Write(values.begin(),static_cast<ULONG>(values.size()),&n),"enqueue write");Check(n==values.size(),"accepted byte count");}
void Blocking(bool writeBlock){
    ComPtr<File> file;file.Attach(new File());file->blockWrite=writeBlock;file->blockFlush=!writeBlock;
    auto stream=Buffered(file.Get(),{8,4});
    if(writeBlock)Write(stream.Get(),{1,2,3,4});else Hr(RequestRecordingFlush(stream.Get()),"request blocked flush");
    Check(WaitForSingleObject(file->entered,2000)==WAIT_OBJECT_0,"storage worker blocked in backend");
    // These calls must return while physical storage is still held by the test.
    if(!writeBlock)Write(stream.Get(),{1,2,3,4});
    Write(stream.Get(),{5,6,7,8});
    ULONG n=99;const BYTE byte=9;
    Check(stream->Write(&byte,1,&n)==HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)&&n==0,"bounded byte queue refuses overflow immediately");
    for(int i=0;i<100;++i)Hr(RequestRecordingFlush(stream.Get()),"coalesced periodic flush");
    QWORD size=0;Hr(stream->GetLength(&size),"logical length");Check(size==8,"logical size includes queued writes");
    SetEvent(file->release);Hr(stream->Flush(),"physical flush barrier");
    Hr(stream->SetCurrentPosition(2),"seek accepted");Write(stream.Get(),{9,10});
    Hr(stream->SetCurrentPosition(0),"seek to read");BYTE bytes[8]{};Hr(stream->Read(bytes,8,&n),"ordered read barrier");
    const BYTE expected[]{1,2,9,10,5,6,7,8};Check(n==8&&memcmp(bytes,expected,8)==0,"queued data and patches preserved in order");
    Hr(stream->SetLength(6),"queued truncation");Hr(stream->SetCurrentPosition(0),"read truncated prefix");
    Hr(stream->Read(bytes,8,&n),"resize precedes subsequent reads");Check(n==6&&memcmp(bytes,expected,6)==0,"resize preserves the intended prefix");
    Check(stream->Seek(msoBegin,-1,0,nullptr)==E_INVALIDARG,"negative seek rejected");
    Hr(RetireRecordingStorage(stream.Get()),"retire without waiting");Hr(stream->Close(),"close waits for already retired file");
    Check(file->closes==1,"exactly one physical close");
}
void Siblings(){
    ComPtr<File> first,second;first.Attach(new File());second.Attach(new File());first->blockFlush=true;
    auto a=Buffered(first.Get());ComPtr<IMFByteStream> b;Hr(BufferRecordingStorage(second.Get(),a.Get(),&b),"sibling shares queue");
    Write(a.Get(),{1,2});Hr(RetireRecordingStorage(a.Get()),"retire first");
    Check(WaitForSingleObject(first->entered,2000)==WAIT_OBJECT_0,"old file flush blocks");
    Write(b.Get(),{3,4}); // Opening the next segment must not wait for the old file.
    Check(second->writes==0,"physical segment order retained");
    SetEvent(first->release);Hr(b->Flush(),"sibling barrier includes retired writes");
    Check(first->closes==1&&second->writes==1,"old part sealed and new part delivered");Hr(b->Close(),"close sibling");
}
void OperationBound(){
    ComPtr<File> file;file.Attach(new File());file->blockFlush=true;auto stream=Buffered(file.Get(),{1024,2});
    Hr(RequestRecordingFlush(stream.Get()),"blocked operation-bound flush");
    Check(WaitForSingleObject(file->entered,2000)==WAIT_OBJECT_0,"operation-bound backend blocked");
    Write(stream.Get(),{1});const BYTE byte=2;ULONG count=0;
    Check(stream->Write(&byte,1,&count)==HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW),"operation count bounded independently of bytes");
    SetEvent(file->release);Hr(stream->Close(),"bounded queue still drains and closes");
}
void Errors(){
    for(auto fault:{Fault::DiskFull,Fault::ShortWrite,Fault::Flush,Fault::Seek,Fault::Close}){
        ComPtr<File> file;file.Attach(new File());file->fault=fault;auto stream=Buffered(file.Get());Write(stream.Get(),{1});
        const HRESULT expected=fault==Fault::DiskFull?HRESULT_FROM_WIN32(ERROR_DISK_FULL):
            fault==Fault::Seek?HRESULT_FROM_WIN32(ERROR_SEEK):HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        if(fault!=Fault::Close)Check(stream->Flush()==expected,"first physical failure reaches flush and caller");
        Check(stream->Close()==expected,"close preserves physical failure");
        Check(file->closes==1&&RecordingStorageResult(stream.Get())==expected,"failed recording releases file and retains cause");
    }
}
void Async(){
    ComPtr<File> file;file.Attach(new File());auto stream=Buffered(file.Get());ComPtr<Callback> cb;cb.Attach(new Callback());
    const BYTE bytes[]{7,8};Hr(stream->BeginWrite(bytes,2,cb.Get(),file.Get()),"begin buffered write");
    Check(WaitForSingleObject(cb->done,2000)==WAIT_OBJECT_0,"write callback dispatched");ULONG count=0;
    Hr(stream->EndWrite(cb->result.Get(),&count),"end buffered write");Check(count==2,"async byte count");
    ComPtr<IUnknown> state;Hr(cb->result->GetState(&state),"async state");Check(state.Get()==static_cast<IMFByteStream*>(file.Get()),"callback state retained");
    Hr(stream->Flush(),"async physical durability");Hr(stream->SetCurrentPosition(0),"async read position");
    ComPtr<Callback> readCb;readCb.Attach(new Callback());BYTE read[2]{};
    Hr(stream->BeginRead(read,2,readCb.Get(),nullptr),"begin buffered read");
    Check(WaitForSingleObject(readCb->done,2000)==WAIT_OBJECT_0,"read callback dispatched");
    Check(stream->EndWrite(readCb->result.Get(),&count)==E_INVALIDARG,"wrong asynchronous completion method rejected");
    Hr(stream->EndRead(readCb->result.Get(),&count),"end buffered read");Check(count==2&&memcmp(read,bytes,2)==0,"asynchronous read bytes");
    Hr(stream->Close(),"async close");cb.Reset();readCb.Reset();
}
int main(){
    Hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM");Hr(MFStartup(MF_VERSION),"MF");
    Blocking(false);Blocking(true);Siblings();OperationBound();Errors();Async();
    MFShutdown();CoUninitialize();puts("Recording storage blocking, bounds, ordering, faults and async contract passed.");
}
