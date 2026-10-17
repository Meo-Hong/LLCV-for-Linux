#include "recording/RecordingContainer.h"
#include "recording/FragmentedMp4Sink.h"
#include <mfapi.h>
#include <mferror.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace llcv::recording;
namespace {
void Check(bool value, const char* text) { if (!value) { printf("FAIL %s\n", text); std::exit(1); } }
void Hr(HRESULT hr, const char* text) { if (FAILED(hr)) { printf("FAIL %s %08lX\n", text, static_cast<unsigned long>(hr)); std::exit(1); } }
enum class Fault { None, DiskFull, WriteError, ZeroWrite, SeekError, FlushError };
constexpr HRESULT kDiskFull = HRESULT_FROM_WIN32(ERROR_DISK_FULL);
constexpr HRESULT kWriteError = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
constexpr HRESULT kSeekError = HRESULT_FROM_WIN32(ERROR_SEEK);
class Bytes final : public IMFByteStream {
    std::atomic<ULONG> references_{1};
    std::vector<BYTE> data_;
    QWORD position_ = 0;
public:
    Fault fault = Fault::None;
    unsigned failAfterWrites = 0, writes = 0, seeks = 0, flushes = 0;
    bool closed = false;
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER; *object = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(IMFByteStream)) return E_NOINTERFACE;
        *object = static_cast<IMFByteStream*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override { const auto left = --references_; if (!left) delete this; return left; }
    STDMETHODIMP GetCapabilities(DWORD* capabilities) override {
        if (!capabilities) return E_POINTER;
        *capabilities = MFBYTESTREAM_IS_READABLE | MFBYTESTREAM_IS_WRITABLE | MFBYTESTREAM_IS_SEEKABLE; return S_OK;
    }
    STDMETHODIMP GetLength(QWORD* length) override { if (!length) return E_POINTER; *length = data_.size(); return S_OK; }
    STDMETHODIMP SetLength(QWORD length) override {
        if (length > 16 * 1024 * 1024) return kDiskFull;
        data_.resize(static_cast<size_t>(length)); return S_OK;
    }
    STDMETHODIMP GetCurrentPosition(QWORD* position) override {
        if (!position) return E_POINTER;
        *position = position_; return S_OK;
    }
    STDMETHODIMP SetCurrentPosition(QWORD position) override {
        ++seeks; if (fault == Fault::SeekError) return kSeekError;
        if (position > 16 * 1024 * 1024) return E_INVALIDARG;
        position_ = position; return S_OK;
    }
    STDMETHODIMP IsEndOfStream(BOOL* end) override { if (!end) return E_POINTER; *end = position_ >= data_.size(); return S_OK; }
    STDMETHODIMP Read(BYTE* bytes, ULONG count, ULONG* read) override {
        if (!read || (!bytes && count)) return E_POINTER; *read = 0;
        if (position_ >= data_.size()) return S_OK;
        *read = static_cast<ULONG>((std::min)(static_cast<QWORD>(count), data_.size() - position_));
        memcpy(bytes, data_.data() + position_, *read); position_ += *read; return S_OK;
    }
    STDMETHODIMP BeginRead(BYTE*, ULONG, IMFAsyncCallback*, IUnknown*) override { return E_NOTIMPL; }
    STDMETHODIMP EndRead(IMFAsyncResult*, ULONG*) override { return E_NOTIMPL; }
    STDMETHODIMP Write(const BYTE* bytes, ULONG count, ULONG* written) override {
        if (!written || (!bytes && count)) return E_POINTER; *written = 0;
        if (closed) return MF_E_SHUTDOWN;
        const bool failing = writes++ >= failAfterWrites;
        if (failing && fault == Fault::DiskFull) return kDiskFull;
        if (failing && fault == Fault::WriteError) return kWriteError;
        if (failing && fault == Fault::ZeroWrite) return S_OK;
        if (position_ > 16 * 1024 * 1024 || count > 16 * 1024 * 1024 - position_) return kDiskFull;
        if (position_ + count > data_.size()) data_.resize(static_cast<size_t>(position_ + count));
        memcpy(data_.data() + position_, bytes, count); position_ += count; *written = count; return S_OK;
    }
    STDMETHODIMP BeginWrite(const BYTE*, ULONG, IMFAsyncCallback*, IUnknown*) override { return E_NOTIMPL; }
    STDMETHODIMP EndWrite(IMFAsyncResult*, ULONG*) override { return E_NOTIMPL; }
    STDMETHODIMP Seek(MFBYTESTREAM_SEEK_ORIGIN origin, LONGLONG offset, DWORD, QWORD* position) override {
        const LONGLONG base = origin == msoBegin ? 0 : static_cast<LONGLONG>(position_);
        if (offset < -base || offset > 16 * 1024 * 1024 - base) return E_INVALIDARG;
        const HRESULT hr = SetCurrentPosition(static_cast<QWORD>(base + offset));
        if (position) *position = position_; return hr;
    }
    STDMETHODIMP Flush() override { ++flushes; return fault == Fault::FlushError ? kWriteError : S_OK; }
    STDMETHODIMP Close() override { closed = true; return S_OK; }
};
struct Packet { ComPtr<IMFSample> sample; LONGLONG time = 0; bool video = false; };
struct Fixture { ComPtr<IMFMediaType> video, audio; std::vector<Packet> packets; };
Fixture Load(const wchar_t* path) {
    Fixture result;
    for (const bool video : {true, false}) {
        ComPtr<IMFSourceReader> reader; Hr(MFCreateSourceReaderFromURL(path, nullptr, &reader), "fixture reader");
        const DWORD stream = static_cast<DWORD>(video ? MF_SOURCE_READER_FIRST_VIDEO_STREAM : MF_SOURCE_READER_FIRST_AUDIO_STREAM);
        auto& type = video ? result.video : result.audio;
        Hr(reader->GetNativeMediaType(stream, 0, &type), "fixture native type");
        Hr(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE), "unselect fixture");
        Hr(reader->SetStreamSelection(stream, TRUE), "select fixture");
        Hr(reader->SetCurrentMediaType(stream, nullptr, type.Get()), "fixture encoded passthrough");
        bool ended = false;
        for (unsigned reads = 0; reads < 10000; ++reads) {
            Packet packet; packet.video = video; DWORD flags = 0;
            Hr(reader->ReadSample(stream, 0, nullptr, &flags, &packet.time, &packet.sample), "fixture packet");
            if (packet.sample) result.packets.push_back(std::move(packet));
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { ended = true; break; }
        }
        Check(ended, "fixture reading bounded");
    }
    std::stable_sort(result.packets.begin(), result.packets.end(), [](const Packet& a, const Packet& b) { return a.time < b.time; });
    return result;
}
HRESULT Exercise(const Fixture& fixture, Bytes* bytes, bool invalidTime = false) {
    ComPtr<IMFAttributes> attributes; Hr(MFCreateAttributes(&attributes, 2), "fault attributes");
    attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
    attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    ComPtr<IMFSinkWriter> writer;
    HRESULT hr = CreateRecordingWriter(bytes, attributes.Get(), &writer, Codec::Hevc);
    if (FAILED(hr)) return hr;
    DWORD video = 0, audio = 0;
    if (FAILED(hr = writer->AddStream(fixture.video.Get(), &video)) ||
        FAILED(hr = writer->SetInputMediaType(video, fixture.video.Get(), nullptr)) ||
        FAILED(hr = writer->AddStream(fixture.audio.Get(), &audio)) ||
        FAILED(hr = writer->SetInputMediaType(audio, fixture.audio.Get(), nullptr)) ||
        FAILED(hr = writer->BeginWriting())) return hr;
    for (const auto& packet : fixture.packets) {
        if (invalidTime && packet.video && packet.time > 1000000) {
            // A repeated decode timestamp is invalid even for reordered video.
            // Clone metadata instead of mutating a sample still owned by MF.
            ComPtr<IMFSample> invalid; Hr(MFCreateSample(&invalid), "invalid timestamp sample");
            Hr(packet.sample->CopyAllItems(invalid.Get()), "invalid timestamp attributes");
            DWORD count = 0; Hr(packet.sample->GetBufferCount(&count), "invalid sample buffers");
            for (DWORD i = 0; i < count; ++i) {
                ComPtr<IMFMediaBuffer> buffer; Hr(packet.sample->GetBufferByIndex(i, &buffer), "shared fixture buffer");
                Hr(invalid->AddBuffer(buffer.Get()), "attach shared fixture buffer");
            }
            LONGLONG duration = 0; Hr(packet.sample->GetSampleDuration(&duration), "invalid sample duration");
            Hr(invalid->SetSampleDuration(duration), "copy invalid sample duration");
            Hr(invalid->SetSampleTime(0), "backwards PTS");
            Hr(invalid->SetUINT64(MFSampleExtension_DecodeTimestamp, 0), "backwards DTS");
            hr = writer->WriteSample(video, invalid.Get());
            if (FAILED(hr)) break;
        } else hr = writer->WriteSample(packet.video ? video : audio, packet.sample.Get());
        if (FAILED(hr)) break;
    }
    const HRESULT finalized = writer->Finalize();
    if (FAILED(hr)) return hr;
    if (FAILED(finalized)) return finalized;
    return bytes->Flush(); // Recorder owns the final file flush, not the sink.
}
void Released(Bytes* bytes) {
    ULONG references = 0;
    for (unsigned wait = 0; wait < 100; ++wait) {
        references = bytes->AddRef(); bytes->Release();
        if (references == 2) break;
        Sleep(10); // Allow already queued Media Foundation callbacks to retire.
    }
    printf("byte stream references including temporary probe=%lu\n", references);
    Check(references == 2, "all writer/sink/clock references released; only caller retains file");
}
void TestFault(const Fixture& fixture, Fault fault, HRESULT expected, unsigned after) {
    ComPtr<Bytes> bytes; bytes.Attach(new Bytes()); bytes->fault = fault; bytes->failAfterWrites = after;
    const auto start = std::chrono::steady_clock::now();
    const HRESULT result = Exercise(fixture, bytes.Get());
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    printf("fault=%d after=%u result=%08lX writes=%u seeks=%u flushes=%u duration=%.3fs\n",
        static_cast<int>(fault), after, static_cast<unsigned long>(result), bytes->writes, bytes->seeks, bytes->flushes, elapsed);
    Check(elapsed < 5, "storage failure/finalization/destruction remains bounded");
    Released(bytes.Get());
    if (fault == Fault::SeekError && !bytes->seeks) {
        Check(SUCCEEDED(result), "sequential fragmented mux needs no seek"); return;
    }
    if (fault == Fault::ZeroWrite) Check(FAILED(result), "zero-byte successful write cannot silently truncate a recording");
    else Check(result == expected, "original storage HRESULT retained");
    Check(!bytes->closed, "writer does not close caller-owned byte stream");
}
void DirectLifecycle(const Fixture& fixture) {
    ComPtr<Bytes> bytes; bytes.Attach(new Bytes());
    ComPtr<IMFMediaSink> sink; Hr(CreateFragmentedMp4Sink(bytes.Get(), &sink), "direct lifecycle sink");
    ComPtr<IMFStreamSink> child; Hr(sink->AddStreamSink(0, fixture.video.Get(), &child), "retained child stream");
    ComPtr<IMFPresentationClock> clock; Hr(MFCreatePresentationClock(&clock), "lifecycle presentation clock");
    Hr(sink->SetPresentationClock(clock.Get()), "attach presentation clock");
    sink.Reset(); // No explicit Shutdown: clock must not form a strong COM cycle.
    ComPtr<IMFMediaSink> orphan;
    Check(child->GetMediaSink(&orphan) == MF_E_SHUTDOWN && !orphan,
        "retained child cannot resurrect released parent through presentation clock");
    Released(bytes.Get());
    ULONG written = 0; const BYTE marker = 42;
    Hr(bytes->Write(&marker, 1, &written), "caller stream still usable after sink destruction");
    Check(written == 1 && !bytes->closed, "caller stream remains writable");
}
void Bounds(const Fixture& fixture) {
    for (unsigned problem = 0; problem < 3; ++problem) {
        ComPtr<Bytes> bytes; bytes.Attach(new Bytes());
        ComPtr<IMFMediaSink> sink; Hr(CreateFragmentedMp4Sink(bytes.Get(), &sink), "invalid type sink");
        ComPtr<IMFMediaType> invalid; Hr(MFCreateMediaType(&invalid), "invalid media type");
        Hr(fixture.video->CopyAllItems(invalid.Get()), "invalid type copy");
        if (problem == 0) Hr(MFSetAttributeSize(invalid.Get(), MF_MT_FRAME_SIZE, 16385, 192), "oversized dimensions");
        else if (problem == 1) Hr(MFSetAttributeRatio(invalid.Get(), MF_MT_FRAME_RATE, UINT32_MAX, 1), "overflowed rate");
        else Hr(invalid->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12), "unencoded archive type");
        ComPtr<IMFStreamSink> stream;
        Check(FAILED(sink->AddStreamSink(0, invalid.Get(), &stream)), "invalid/unbounded media type rejected");
        sink.Reset(); Released(bytes.Get());
    }
    for (const bool oversizedHeader : {true, false}) {
        ComPtr<Bytes> bytes; bytes.Attach(new Bytes());
        ComPtr<IMFMediaSink> sink; Hr(CreateFragmentedMp4Sink(bytes.Get(), &sink), "payload bound sink");
        ComPtr<IMFMediaType> type; Hr(MFCreateMediaType(&type), "bounded type");
        Hr(fixture.video->CopyAllItems(type.Get()), "bounded type copy");
        if (oversizedHeader) {
            const std::vector<BYTE> tooLarge(1024 * 1024 + 1, 0);
            Hr(type->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER, tooLarge.data(), static_cast<UINT32>(tooLarge.size())), "oversized sequence header");
        }
        ComPtr<IMFStreamSink> video; Hr(sink->AddStreamSink(0, type.Get(), &video), "bounded video stream");
        ComPtr<IMFSample> sample; Hr(MFCreateSample(&sample), "oversized packet sample");
        ComPtr<IMFMediaBuffer> buffer;
        const DWORD size = oversizedHeader ? 16 : 16 * 1024 * 1024 + 1;
        Hr(MFCreateMemoryBuffer(size, &buffer), "oversized packet allocation");
        Hr(buffer->SetCurrentLength(size), "oversized packet length"); Hr(sample->AddBuffer(buffer.Get()), "oversized packet buffer");
        Hr(sample->SetSampleTime(0), "oversized packet PTS"); Hr(sample->SetSampleDuration(333333), "oversized packet duration");
        Check(FAILED(video->ProcessSample(sample.Get())), oversizedHeader ? "codec header cap enforced" : "encoded packet cap enforced");
        sink.Reset(); Released(bytes.Get());
    }
    {
        ComPtr<Bytes> bytes; bytes.Attach(new Bytes());
        ComPtr<IMFMediaSink> sink; Hr(CreateFragmentedMp4Sink(bytes.Get(), &sink), "preheader bound sink");
        ComPtr<IMFStreamSink> video, audio;
        Hr(sink->AddStreamSink(0, fixture.video.Get(), &video), "unseen video track");
        Hr(sink->AddStreamSink(1, fixture.audio.Get(), &audio), "active audio track");
        HRESULT result = S_OK;
        for (unsigned i = 0; i < 600 && SUCCEEDED(result); ++i) {
            ComPtr<IMFSample> sample; ComPtr<IMFMediaBuffer> buffer;
            Hr(MFCreateSample(&sample), "bounded pending sample");
            Hr(MFCreateMemoryBuffer(16, &buffer), "bounded pending buffer");
            BYTE* data = nullptr; Hr(buffer->Lock(&data, nullptr, nullptr), "pending buffer lock");
            memset(data, 0, 16); Hr(buffer->Unlock(), "pending buffer unlock");
            Hr(buffer->SetCurrentLength(16), "pending packet size"); Hr(sample->AddBuffer(buffer.Get()), "pending packet buffer");
            Hr(sample->SetSampleTime(static_cast<LONGLONG>(i) * 213333), "pending audio time");
            Hr(sample->SetSampleDuration(213333), "pending audio duration");
            result = audio->ProcessSample(sample.Get());
        }
        Check(FAILED(result), "missing video cannot accumulate an unlimited preheader audio queue");
        sink.Reset(); Released(bytes.Get());
    }
}
}
int wmain(int argc, wchar_t** argv) {
    Check(argc == 2, "supply HEVC encoded fixture path");
    Hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM"); Hr(MFStartup(MF_VERSION), "MF");
    const auto fixture = Load(argv[1]);
    DirectLifecycle(fixture);
    Bounds(fixture);
    {
        ComPtr<Bytes> bytes; bytes.Attach(new Bytes()); Hr(Exercise(fixture, bytes.Get()), "control passthrough");
        Check(bytes->writes > 2 && !bytes->closed, "stream ownership/control file");
        Released(bytes.Get());
    }
    {
        ComPtr<Bytes> bytes; bytes.Attach(new Bytes());
        ComPtr<IMFSinkWriter> writer;
        Hr(CreateRecordingWriter(bytes.Get(), nullptr, &writer, Codec::Hevc), "early cancel writer");
        DWORD stream = 0; Hr(writer->AddStream(fixture.video.Get(), &stream), "early cancel video");
        Hr(writer->SetInputMediaType(stream, fixture.video.Get(), nullptr), "early cancel passthrough");
        Hr(writer->BeginWriting(), "early cancel begin");
        writer.Reset(); Released(bytes.Get());
        Check(!bytes->closed, "early cancel leaves caller-owned stream open");
    }
    for (const unsigned after : {0u, 2u}) {
        TestFault(fixture, Fault::DiskFull, kDiskFull, after);
        TestFault(fixture, Fault::WriteError, kWriteError, after);
        TestFault(fixture, Fault::ZeroWrite, E_FAIL, after);
    }
    TestFault(fixture, Fault::SeekError, kSeekError, 0);
    TestFault(fixture, Fault::FlushError, kWriteError, 0);
    {
        ComPtr<Bytes> bytes; bytes.Attach(new Bytes());
        Check(FAILED(Exercise(fixture, bytes.Get(), true)), "backwards timestamp cannot silently produce a malformed recording");
        Released(bytes.Get());
    }
    MFShutdown(); CoUninitialize(); puts("Recording mux storage fault tests passed."); return 0;
}
