// Included inside UpdateCheckerTests' namespace. All WinHTTP calls are fakes:
// even cancellation/late-callback tests never resolve a host or open a socket.
enum class TransportStage { None, Send, Receive, Read };

struct FakeTransport;
static FakeTransport* transport = nullptr;

struct FakeTransport {
    WINHTTP_STATUS_CALLBACK callback = nullptr;
    DWORD_PTR context = 0;
    TransportStage stall = TransportStage::None, asyncError = TransportStage::None;
    bool deferClosing = false, valid = true;
    unsigned failCall = 0, calls = 0;
    std::vector<uintptr_t> opened, closed;
    std::string body = R"({"tag_name":"v9.0.0","assets":[]})";
    size_t position = 0, chunkSize = 13;
    DWORD status = 200;
    void* pendingRead = nullptr;
    DWORD pendingReadCapacity = 0;
    std::promise<void> entered;

    static HINTERNET Handle(uintptr_t id) { return reinterpret_cast<HINTERNET>(id); }
    bool Succeed() { return ++calls != failCall; }
    void Complete(DWORD notification, void* information = nullptr, DWORD length = 0) {
        callback(Handle(3), context, notification, information, length);
    }
    void FinishClosing() {
        if (callback && context) {
            Complete(WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING);
            context = 0;
        }
    }
    bool HoldOrFail(TransportStage stage) {
        if (asyncError == stage) {
            WINHTTP_ASYNC_RESULT failure{};
            failure.dwError = ERROR_WINHTTP_CONNECTION_ERROR;
            Complete(WINHTTP_CALLBACK_STATUS_REQUEST_ERROR, &failure, sizeof(failure));
            return true;
        }
        if (stall != stage) return false;
        entered.set_value();
        return true;
    }
    void DeliverAfterCancellation() {
        // The owner, stop flag, and UpdateCheckTask may no longer be used by
        // transport callbacks. Only the callback-owned event/buffer may live.
        if (stall == TransportStage::Read) {
            valid &= pendingRead && pendingReadCapacity >= 4;
            std::memcpy(pendingRead, "late", 4);
            Complete(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, pendingRead, 4);
        } else Complete(stall == TransportStage::Send
            ? WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE
            : WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
        WINHTTP_ASYNC_RESULT cancelled{};
        cancelled.dwError = ERROR_WINHTTP_OPERATION_CANCELLED;
        Complete(WINHTTP_CALLBACK_STATUS_REQUEST_ERROR, &cancelled, sizeof(cancelled));
    }

    static HINTERNET WINAPI Open(LPCWSTR, DWORD access, LPCWSTR, LPCWSTR, DWORD flags) {
        transport->valid &= access == WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY && (flags & WINHTTP_FLAG_ASYNC);
        if (!transport->Succeed()) return nullptr;
        transport->opened.push_back(1); return Handle(1);
    }
    static BOOL WINAPI Timeouts(HINTERNET, int, int, int, int) { return transport->Succeed(); }
    static HINTERNET WINAPI Connect(HINTERNET, LPCWSTR host, INTERNET_PORT port, DWORD) {
        transport->valid &= std::wstring(host) == L"api.github.com" && port == INTERNET_DEFAULT_HTTPS_PORT;
        if (!transport->Succeed()) return nullptr;
        transport->opened.push_back(2); return Handle(2);
    }
    static HINTERNET WINAPI Request(HINTERNET, LPCWSTR verb, LPCWSTR path, LPCWSTR,
                                    LPCWSTR, LPCWSTR*, DWORD flags) {
        transport->valid &= std::wstring(verb) == L"GET" &&
            std::wstring(path) == L"/repos/seria-aa/LowLatencyCaptureViewer/releases/latest" &&
            flags == WINHTTP_FLAG_SECURE;
        if (!transport->Succeed()) return nullptr;
        transport->opened.push_back(3); return Handle(3);
    }
    static BOOL WINAPI Headers(HINTERNET, LPCWSTR text, DWORD, DWORD) {
        transport->valid &= std::wstring(text) == L"Accept: application/vnd.github+json\r\n";
        return transport->Succeed();
    }
    static WINHTTP_STATUS_CALLBACK WINAPI Callback(HINTERNET, WINHTTP_STATUS_CALLBACK value,
                                                    DWORD flags, DWORD_PTR) {
        transport->valid &= (flags & WINHTTP_CALLBACK_FLAG_HANDLES) != 0;
        if (!transport->Succeed()) return WINHTTP_INVALID_STATUS_CALLBACK;
        transport->callback = value; return nullptr;
    }
    static BOOL WINAPI Option(HINTERNET, DWORD option, void* value, DWORD bytes) {
        transport->valid &= option == WINHTTP_OPTION_CONTEXT_VALUE && bytes == sizeof(DWORD_PTR);
        if (!transport->Succeed()) return FALSE;
        transport->context = *static_cast<DWORD_PTR*>(value); return TRUE;
    }
    static BOOL WINAPI Send(HINTERNET, LPCWSTR, DWORD, void*, DWORD, DWORD, DWORD_PTR context) {
        transport->valid &= context == transport->context && context != 0;
        if (!transport->Succeed()) return FALSE;
        if (!transport->HoldOrFail(TransportStage::Send))
            transport->Complete(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
        return TRUE;
    }
    static BOOL WINAPI Receive(HINTERNET, void*) {
        if (!transport->Succeed()) return FALSE;
        if (!transport->HoldOrFail(TransportStage::Receive))
            transport->Complete(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
        return TRUE;
    }
    static BOOL WINAPI Query(HINTERNET, DWORD flags, LPCWSTR, void* value, DWORD* bytes, DWORD*) {
        transport->valid &= flags == (WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER) &&
            *bytes == sizeof(DWORD);
        if (!transport->Succeed()) return FALSE;
        *static_cast<DWORD*>(value) = transport->status; return TRUE;
    }
    static BOOL WINAPI Read(HINTERNET, void* buffer, DWORD bytes, DWORD* immediateRead) {
        transport->valid &= immediateRead == nullptr && bytes == 8192;
        if (!transport->Succeed()) return FALSE;
        transport->pendingRead = buffer; transport->pendingReadCapacity = bytes;
        if (transport->HoldOrFail(TransportStage::Read)) return TRUE;
        const size_t count = (std::min)({static_cast<size_t>(bytes), transport->chunkSize,
                                         transport->body.size() - transport->position});
        std::memcpy(buffer, transport->body.data() + transport->position, count);
        transport->position += count;
        transport->Complete(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, buffer, static_cast<DWORD>(count));
        return TRUE;
    }
    static BOOL WINAPI Close(HINTERNET handle) {
        transport->closed.push_back(reinterpret_cast<uintptr_t>(handle));
        if (handle == Handle(3) && !transport->deferClosing) transport->FinishClosing();
        return TRUE;
    }
    llcv::update::detail::WinHttpApi Api() {
        transport = this;
        return {Open, Timeouts, Connect, Request, Headers, Callback, Option,
                Send, Receive, Query, Read, Close};
    }
    bool ReleasedHandles() const {
        auto reverse = opened;
        std::reverse(reverse.begin(), reverse.end());
        return closed == reverse;
    }
};

void TestAsyncTransport() {
    using namespace llcv::update;
    using namespace std::chrono_literals;
    for (unsigned fail = 0; fail <= 11; ++fail) {
        FakeTransport fake; fake.failCall = fail;
        const auto api = fake.Api();
        std::string json;
        const bool result = detail::FetchReleaseJson(L"LLCV/test", json, nullptr, api);
        Expect(result == (fail == 0) && fake.valid && fake.ReleasedHandles() && !fake.context,
               "async transport preserves official TLS endpoint and closes every acquired handle on setup/I/O failure");
        if (!fail) Expect(json == fake.body, "inline asynchronous completions assemble a multi-chunk response");
    }
    for (auto stage : {TransportStage::Send, TransportStage::Receive, TransportStage::Read}) {
        FakeTransport fake; fake.asyncError = stage;
        const auto api = fake.Api(); std::string json;
        Expect(!detail::FetchReleaseJson(L"LLCV/test", json, nullptr, api) &&
               fake.ReleasedHandles() && !fake.context,
               "REQUEST_ERROR terminates each asynchronous network phase without waiting for timeout");
    }
    for (DWORD code : {403u, 404u, 500u}) {
        FakeTransport fake; fake.status = code;
        const auto api = fake.Api(); std::string json;
        Expect(!detail::FetchReleaseJson(L"LLCV/test", json, nullptr, api) && fake.position == 0,
               "HTTP errors do not become release responses");
    }
    for (size_t extra : {size_t{0}, size_t{1}}) {
        FakeTransport fake; fake.chunkSize = 8192;
        fake.body.assign(2 * 1024 * 1024 + extra, 'a');
        const auto api = fake.Api(); std::string json;
        Expect(detail::FetchReleaseJson(L"LLCV/test", json, nullptr, api) == (extra == 0) &&
               json.size() <= 2 * 1024 * 1024 && fake.ReleasedHandles(),
               "response size remains bounded at exactly two MiB");
    }
    {
        FakeTransport fake; const auto api = fake.Api(); std::string json;
        std::atomic<bool> stop{true};
        Expect(!detail::FetchReleaseJson(L"LLCV/test", json, &stop, api) && fake.opened.empty(),
               "pre-cancelled transport does not even create a session");
    }

    for (auto stage : {TransportStage::Send, TransportStage::Receive, TransportStage::Read}) {
        FakeTransport fake; fake.stall = stage; fake.deferClosing = true;
        const auto api = fake.Api();
        auto entered = fake.entered.get_future();
        std::atomic<unsigned> notices{0};
        {
            UpdateCheckTask task([&](const wchar_t*, CheckResult&, const std::atomic<bool>* stop) {
                std::string json;
                return detail::FetchReleaseJson(L"LLCV/test", json, stop, api);
            });
            Expect(task.Start(L"v1.0.0", [&] { ++notices; }), "fake stalled transport task starts");
            Expect(entered.wait_for(2s) == std::future_status::ready, "request reached selected pending phase");
            const auto start = std::chrono::steady_clock::now();
            task.CancelAndWait();
            Expect(std::chrono::steady_clock::now() - start < 500ms &&
                   notices == 0 && !task.TakeResult() && fake.ReleasedHandles(),
                   "cancellation joins promptly during send/receive/read instead of waiting for network timeout");
        } // task and its stop flag are destroyed BEFORE WinHTTP late notifications
        DWORD beforeClose = 0, afterClose = 0;
        GetProcessHandleCount(GetCurrentProcess(), &beforeClose);
        fake.DeliverAfterCancellation();
        fake.FinishClosing();
        GetProcessHandleCount(GetCurrentProcess(), &afterClose);
        Expect(fake.valid && notices == 0 && !fake.context && beforeClose == afterClose + 1,
               "late callbacks use isolated owned storage and HANDLE_CLOSING releases the last event handle");
    }
    {
        FakeTransport fake; fake.stall = TransportStage::Send;
        const auto api = fake.Api(); std::string json;
        const auto start = std::chrono::steady_clock::now();
        Expect(!detail::FetchReleaseJson(L"LLCV/test", json, nullptr, api, 1) &&
               std::chrono::steady_clock::now() - start < 500ms && fake.ReleasedHandles(),
               "overall deadline bounds a request that never completes");
    }
    transport = nullptr;
}
