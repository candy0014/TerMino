#include "tetr/network/websocket_socket.h"
#include "tetr/network/udp_socket.h"
#include "tetr/network/event_stream.h"
#include "curl_runtime.h"

#include <curl/curl.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>
#include <cctype>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Network {
namespace {
constexpr std::size_t maxMessage = 65507;
constexpr std::size_t maxBuffered = 2 * 1024 * 1024;
constexpr std::size_t maxQueuedMessages = 1024;

}

struct WebSocketSocket::Impl {
    // Keep curl alive until this socket's worker has stopped, even when the
    // application owns sockets with static storage duration.
    std::shared_ptr<Detail::CurlRuntime> runtime;
    std::string url;
    std::atomic<bool> stopping{false};
    std::atomic<bool> eventStream{false};
    bool negotiatedStream=false;
    std::mutex mutex;
    std::condition_variable available;
    EventStreamQueue outgoing, incoming;
    std::size_t pendingBytes = 0;
    std::string error;
    std::unique_ptr<CURLM,decltype(&curl_multi_cleanup)> wake{nullptr,curl_multi_cleanup};
    std::thread worker;

    explicit Impl(std::string address) : runtime(Detail::initialize_curl()), url(std::move(address)),
        wake(curl_multi_init(),curl_multi_cleanup) {
        if (!wake) throw UdpException("WebSocket initialization failed.");
        worker = std::thread([this] { run(); });
    }
    ~Impl() { stop(); }
    void stop() noexcept {
        stopping = true;
        available.notify_all();
        curl_multi_wakeup(wake.get());
        if (worker.joinable()) worker.join();
    }
    static int progress(void* self, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
        return static_cast<Impl*>(self)->stopping ? 1 : 0;
    }
    static std::size_t header(char* data,std::size_t size,std::size_t count,void* self) {
        std::string_view line(data,size*count),name="sec-websocket-protocol:";
        if (line.size()>=name.size() && std::equal(name.begin(),name.end(),line.begin(),
            [](unsigned char a,unsigned char b){return a==std::tolower(b);})) {
            line.remove_prefix(name.size());
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.remove_prefix(1);
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.remove_suffix(1);
            static_cast<Impl*>(self)->negotiatedStream=line==kEventStreamProtocol;
        }
        return size*count;
    }
    void fail(const char* message) {
        std::lock_guard<std::mutex> lock(mutex);
        error = message;
        available.notify_all();
    }
    void run() noexcept {
        try {
            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
            if (!curl) throw UdpException("WebSocket initialization failed.");
            curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "TerMino");
            curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "ws,wss");
            curl_easy_setopt(curl.get(), CURLOPT_CONNECT_ONLY, 2L);
            curl_easy_setopt(curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
            curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 3500L);
            curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 4500L);
            curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_TCP_NODELAY, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
            curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, this);
            std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)> headers(
                curl_slist_append(nullptr,"Sec-WebSocket-Protocol: termino.events.v1"),curl_slist_free_all);
            if (!headers) throw UdpException("WebSocket initialization failed.");
            curl_easy_setopt(curl.get(),CURLOPT_HTTPHEADER,headers.get());
            curl_easy_setopt(curl.get(),CURLOPT_HEADERFUNCTION,header);
            curl_easy_setopt(curl.get(),CURLOPT_HEADERDATA,this);
            // System trust store on Windows (Schannel); standard CA files on
            // Linux also work with the statically linked portable TLS backend.
#ifdef __linux__
            for (const auto* path : {"/etc/ssl/certs/ca-certificates.crt", "/etc/ssl/cert.pem"}) {
                if (std::filesystem::exists(path)) {
                    curl_easy_setopt(curl.get(), CURLOPT_CAINFO, path);
                    break;
                }
            }
#endif
#ifdef _WIN32
            // Wine's Schannel does not implement automatic credential
            // validation. A CA bundle makes curl validate the certificate
            // chain and hostname explicitly through CryptoAPI instead.
            const auto ntdll = GetModuleHandleW(L"ntdll.dll");
            if (url.rfind("wss://", 0) == 0 && ntdll &&
                GetProcAddress(ntdll, "wine_get_version")) {
                bool found = false;
                for (const auto* path : {"Z:\\etc\\ssl\\certs\\ca-certificates.crt", "Z:\\etc\\ssl\\cert.pem"}) {
                    if (std::filesystem::exists(path)) {
                        curl_easy_setopt(curl.get(), CURLOPT_CAINFO, path);
                        found = true;
                        break;
                    }
                }
                if (!found) throw UdpException("Wine TLS requires the host CA certificate bundle.");
            }
#endif
            const auto connected = curl_easy_perform(curl.get());
            if (stopping) return;
            if (connected != CURLE_OK) {
                fail(connected == CURLE_PEER_FAILED_VERIFICATION
                    ? "WebSocket TLS verification failed." : "Cannot connect to WebSocket server.");
                return;
            }
            eventStream=negotiatedStream;
            curl_socket_t descriptor=CURL_SOCKET_BAD;
            if (curl_easy_getinfo(curl.get(),CURLINFO_ACTIVESOCKET,&descriptor)!=CURLE_OK || descriptor==CURL_SOCKET_BAD)
                throw UdpException("WebSocket socket unavailable.");
            std::string pending, received;
            std::size_t sentOffset = 0;
            bool hasPending = false;
            while (!stopping) {
                for (int burst = 0; burst < 32; ++burst) {
                    if (!hasPending) {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (outgoing.empty()) break;
                        auto events=outgoing.take(eventStream);
                        pendingBytes=0; for (const auto& event:events) pendingBytes+=event.size();
                        pending=eventStream?*encode_event_stream(events):std::move(events.front());
                        sentOffset = 0; hasPending = true;
                    }
                    std::size_t sent = 0;
                    const auto result = curl_ws_send(curl.get(), pending.data() + sentOffset,
                        pending.size() - sentOffset, &sent, 0, CURLWS_BINARY);
                    sentOffset += sent;
                    if (result != CURLE_OK && result != CURLE_AGAIN)
                        throw UdpException(std::string("WebSocket send failed: ")+curl_easy_strerror(result));
                    if (sentOffset == pending.size() && result == CURLE_OK) {
                        std::lock_guard<std::mutex> lock(mutex);
                        pendingBytes=0;
                        hasPending = false;
                    } else break;
                }
                bool receiveDrained=false;
                for (int burst = 0; burst < 64; ++burst) {
                    std::array<char, 8192> buffer{};
                    std::size_t size = 0;
                    const curl_ws_frame* metadata = nullptr;
                    const auto result = curl_ws_recv(curl.get(), buffer.data(), buffer.size(), &size, &metadata);
                    if (result == CURLE_AGAIN) {receiveDrained=true;break;}
                    if (result != CURLE_OK || !metadata)
                        throw UdpException(std::string("WebSocket receive failed: ")+curl_easy_strerror(result));
                    if(metadata->flags & CURLWS_CLOSE) {
                        const int code=size>=2?static_cast<unsigned char>(buffer[0])*256+static_cast<unsigned char>(buffer[1]):1005;
                        throw UdpException("WebSocket server disconnected (close code "+std::to_string(code)+").");
                    }
                    if (metadata->flags & (CURLWS_PING | CURLWS_PONG)) continue;
                    const auto frameLimit=eventStream?kEventStreamMaxFrame:maxMessage;
                    if (!(metadata->flags & (CURLWS_BINARY | CURLWS_TEXT)) ||
                        received.size() + size > frameLimit || metadata->bytesleft > static_cast<curl_off_t>(frameLimit))
                        throw UdpException("Invalid WebSocket message.");
                    received.append(buffer.data(), size);
                    if (metadata->bytesleft == 0 && !(metadata->flags & CURLWS_CONT)) {
                        std::lock_guard<std::mutex> lock(mutex);
                        auto decoded=eventStream?decode_event_stream(received):
                            std::optional<std::vector<std::string_view>>{std::vector<std::string_view>{received}};
                        if (!decoded) throw UdpException("Invalid WebSocket event stream.");
                        for (auto event:*decoded) {
                            if (!incoming.push(std::string(event),EventStreamQueue::Clock::now(),eventStream,maxBuffered,maxQueuedMessages))
                                throw UdpException("Too much pending WebSocket data.");
                        }
                        received.clear(); available.notify_all();
                    }
                }
                // Drain curl/TLS's internal buffer before polling the OS: it
                // may hold more complete messages even when the fd is quiet.
                if (!receiveDrained) continue;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (!outgoing.empty() && !hasPending) continue;
                }
                // Wait on real socket readiness, not a 2/10ms sampling timer.
                // Enqueue and stop wake this poll, including the race just
                // before entering it. Only ask for writability under pressure.
                curl_waitfd ready{descriptor,static_cast<short>(CURL_WAIT_POLLIN|
                    (hasPending?CURL_WAIT_POLLOUT:0)),0};
                const auto polled=curl_multi_poll(wake.get(),&ready,1,1000,nullptr);
                if (polled!=CURLM_OK) throw UdpException("WebSocket readiness wait failed.");
            }
        } catch (const std::exception& exception) {
            if (!stopping) fail(exception.what());
        } catch (...) {
            if (!stopping) fail("WebSocket initialization failed.");
        }
    }
};

bool WebSocketSocket::recognizes(const std::string& address) noexcept {
    return address.rfind("ws://", 0) == 0 || address.rfind("wss://", 0) == 0;
}
WebSocketSocket::WebSocketSocket(std::string url) {
    if (!recognizes(url) || url.size() > 2048 ||
        url.find_first_of("\r\n\t ") != std::string::npos ||
        url.find('\0') != std::string::npos)
        throw UdpException("Invalid WebSocket URL.");
    impl_ = std::make_unique<Impl>(std::move(url));
}
WebSocketSocket::~WebSocketSocket() = default;
void WebSocketSocket::close() noexcept { impl_->stop(); }
bool WebSocketSocket::uses_event_stream() const noexcept { return impl_->eventStream; }
void WebSocketSocket::send(const std::string& message) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->error.empty()) throw UdpException(impl_->error);
    if (impl_->stopping) throw UdpException("WebSocket server disconnected.");
    if (message.size() > maxMessage) throw UdpException("Invalid WebSocket message.");
    if (!impl_->outgoing.push(message,EventStreamQueue::Clock::now(),impl_->eventStream,
                              maxBuffered-impl_->pendingBytes,maxQueuedMessages))
        throw UdpException("Too much pending WebSocket data.");
    curl_multi_wakeup(impl_->wake.get());
}
std::optional<std::string> WebSocketSocket::receive_for(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(impl_->mutex);
    // Polling must never enter a timed kernel wait. In particular MinGW's
    // condition_variable wait_for(0) can still cost a timer tick under Wine.
    // A gameplay frame polls until the queue is empty, so that empty wait
    // otherwise delays every local input arriving during a network frame.
    if (timeout.count() > 0) {
        impl_->available.wait_for(lock, timeout, [this] {
            return !impl_->incoming.empty() || !impl_->error.empty() || impl_->stopping;
        });
    }
    if (!impl_->incoming.empty()) {
        return impl_->incoming.pop();
    }
    if (!impl_->error.empty()) throw UdpException(impl_->error);
    if (impl_->stopping) throw UdpException("WebSocket server disconnected.");
    return std::nullopt;
}
} // namespace Network
