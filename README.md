# coap-pp

[![CI](https://github.com/jushar/coap-pp/actions/workflows/ci.yml/badge.svg)](https://github.com/jushar/coap-pp/actions/workflows/ci.yml)

A C++17 implementation of the CoAP protocol ([RFC 7252](https://www.rfc-editor.org/rfc/rfc7252)) targeting small embedded systems (e.g. STM32 Cortex-M) in functional-safety contexts.

**Features:**
- CoAP messaging (reliable/unreliable messages, request/response, retransmission)
- CoAP server, with semi-dynamic registration of endpoints
- Async responses
- Resource observation, server side ([RFC 7641](https://www.rfc-editor.org/rfc/rfc7641))
- Block-wise transfers, server side ([RFC 7959](https://www.rfc-editor.org/rfc/rfc7959))

**Goals:**
- No heap allocations — all buffers are statically sized or provided via `MemoryPool`
- Platform-agnostic core; platform-specific code lives only in transport implementations
- C++17, Google C++ Style Guide

**Non-goals:**
- Hardened implementation that is exposed to the internet. The goal is to provide an implementation for internal MCU networks.
  Therefore, the implementation migth be prone to DoS.

## Library layout

| CMake target | Description |
|---|---|
| `coap-pp` | Core library: PDU (de)serialization, `Messenger`, `CoapServer`, routing |
| `coap-pp-transport-posix` | POSIX IPv4 UDP transport (Linux / macOS) |
| `coap-pp-transport-udp-ip-slip` | UDP/IP/SLIP transport over a serial port (platform-agnostic) |
| `coap-pp-serde-nanopb` | NanoPB Serialization Layer |
| `coap-pp-serde-json` | nlohmann/json Serialization Layer |

## Requirements

- CMake ≥ 3.30
- A C++17 compiler (tested: GCC 14, Clang 18)

## Building

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

CMake options:

| Option | Default | Description |
|---|---|---|
| `COAP_PP_BUILD_POSIX_TRANSPORT` | `OFF` | Build the POSIX UDP transport (UNIX only) |
| `COAP_PP_BUILD_TRANSPORT_UDP_IP_SLIP` | `OFF` | Build the UDP/IP/SLIP serial transport |
| `COAP_PP_BUILD_SERDE_NANOPB` | `OFF` | Build the NanoPB serialization layer |
| `COAP_PP_BUILD_SERDE_JSON` | `OFF` | Build the nlohmann/json serialization layer |
| `COAP_PP_BUILD_EXAMPLES` | `ON` | Build example programs (requires `COAP_PP_BUILD_POSIX_TRANSPORT`) |
| `COAP_PP_BUILD_TESTS` | `ON` | Build GoogleTest suite |
| `COAP_PP_LOG_LEVEL` | `0` | Minimum compiled-in log level (0=Debug, 1=Info, 2=Warning, 3=Error) |
| `COAP_PP_MAX_RESPONSE_OPTIONS` | `4` | Maximum number of additional options a response can carry (besides Content-Format) |
| `COAP_PP_DUPLICATE_CACHE_SIZE` | `8` | Number of recent non-idempotent requests remembered for duplicate detection (RFC 7252 §4.5) |
| `COAP_PP_USE_INPLACE_FUNCTION` | `OFF` | Use a fixed-buffer `inplace_function` instead of `std::function` for `RequestHandler` (no heap allocation) |
| `COAP_PP_INPLACE_FUNCTION_CAPACITY` | `32` | Buffer capacity in bytes for the `inplace_function` storage (only when `COAP_PP_USE_INPLACE_FUNCTION=ON`) |

### Running tests

```sh
cd build/tests && ctest --output-on-failure --parallel
```

## Quick start

```cpp
#include "coap_pp/content_formats.hpp"
#include "coap_pp/log.hpp"
#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/server/coap_server.hpp"
#include "coap_pp/server/router.hpp"
#include "coap_pp_transport_posix/udp_transport.hpp"

using namespace coap_pp;

int main() {
    SetLogHandler([](LogLevel level, std::string_view message) {
        std::cout << message << std::endl;
    });
    SetPanicHandler([](const char* reason) {
        std::cerr << reason << std::endl;
        std::abort();
    });

    // 1. Transport
    PosixUdpTransport transport{5683};

    // 2. Messenger (tracks CON retransmissions; pool size = max in-flight CONs)
    MemoryPool<Messenger::PendingSlot, 4> con_pool{};
    Messenger messenger{transport, con_pool};

    // 3. Server
    CoapServer server{messenger};

    // 4. Routes
    static const std::array<Route, 1> kRoutes{{{
        codes::kGet, "/hello",
        Router<>::Bind([](const RawRequest&) {
            static constexpr std::string_view kBody = "Hello, CoAP!";
            return Response{codes::kContent,
                            as_bytes(span{kBody.data(), kBody.size()}),
                            ContentFormat::kTextPlain};
        })
    }}};

    // 5. Router
    static Router<> api{"", kRoutes};
    server.AddRouter(api);

    // 6. Start + tick loop
    transport.Start();
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        messenger.Tick(100);
    }
    transport.Stop();
}
```

See [examples/serde_nanopb/serde_nanopb.cpp](examples/serde_nanopb/serde_nanopb.cpp) for a complete example including async (deferred) responses.

## Threading model

coap-pp uses a **single-context execution model**: the core has no internal locking, and all entry points into the library must run in the same execution context:

- `Messenger::Tick()` and `Messenger::Send()`
- datagram delivery into the CoAP layer (`TransportReceiverIF::OnReceive()`, i.e. the transport's receive path)
- `AsyncResponse::Send()` for deferred replies

On a microcontroller this is the natural model: feed received datagrams and drive `Tick()` from the same superloop or RTOS task, and deliver deferred responses from that context as well.

## Response options

Besides `Content-Format` (which has its own field), responses can carry additional CoAP options such as ETag, Max-Age, or Location-Path:

```cpp
auto HandleGet(const RawRequest&) {
    Response resp{codes::kContent,
                  as_bytes(span{kBody.data(), kBody.size()}),
                  ContentFormat::kTextPlain};
    resp.AddOption(OptionNumber::kMaxAge, uint32_t{60u})
        .AddOption(OptionNumber::kLocationPath, std::string_view{"created"});
    return resp;
}
```

String and opaque option values are non-owning views — the referenced data must stay alive until the response has been sent (for deferred replies: until `AsyncResponse::Send()` returns). A response holds at most `COAP_PP_MAX_RESPONSE_OPTIONS` (default 4) additional options; exceeding the limit panics. Use the `content_format` field for Content-Format rather than `AddOption`, otherwise the option is emitted twice.

## Transports

### POSIX UDP (`coap-pp-transport-posix`)

Standard Berkeley sockets. The receive loop runs in a background thread.

```cpp
PosixUdpTransport transport{5683};          // listen on UDP port 5683
auto ep = PosixUdpTransport::MakeEndpoint("192.168.1.10", 5683);
```

### UDP/IP/SLIP (`coap-pp-transport-udp-ip-slip`)

Sends CoAP over UDP wrapped in a minimal IPv4 header, SLIP-framed over a serial port. Useful for microcontrollers connected to a host via UART.

```cpp
// Implement SerialPortIF for your platform
MySerialPort serial{};
UdpIpSlipTransport transport{serial, {192, 168, 1, 1}, 5683};
auto ep = UdpIpSlipTransport::MakeEndpoint({192, 168, 1, 2}, 5683);
```

## NanoPB serialization (`coap-pp-serde-nanopb`)

The `coap-pp-serde-nanopb` layer automatically deserializes protobuf request payloads and serializes protobuf response payloads using [NanoPB](https://jpa.kapsi.fi/nanopb/).

**1. Generate `NanopbFields` specializations from your proto file:**

Use `coap_pp_nanopb_generate_cpp` in your `CMakeLists.txt` instead of the bare `nanopb_generate_cpp`. It runs the bundled `protoc-gen-coap_pp_fields` plugin to produce a `<name>.coap_pp_fields.hpp` header alongside the usual `<name>.pb.h`:

```cmake
coap_pp_nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS PROTO_FIELDS my_message.proto)

add_executable(my_app main.cpp ${PROTO_SRCS} ${PROTO_FIELDS})
target_include_directories(my_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(my_app PRIVATE coap-pp-serde-nanopb)
```

Then include the generated header — it contains a `NanopbFields` specialization for every message in the file:

```cpp
#include "my_message.coap_pp_fields.hpp"  // auto-generated; also includes my_message.pb.h
```

<details>
<summary>Manual specialization (without the plugin)</summary>

```cpp
#include "coap_pp_serde_nanopb/router.hpp"
#include "my_message.pb.h"

template <>
struct coap_pp::NanopbFields<MyMessage> {
    static constexpr const pb_msgdesc_t* kFields = MyMessage_fields;
};
```
</details>

**2. Write a typed handler — the payload is decoded before your handler runs:**

```cpp
auto HandleMyMessage(const Request<MyRequest>& request) {
    const MyRequest& msg = request.Body();
    // use msg.field_name ...
    MyResponse response{.greeting = "Hello!"};
    return Response{codes::kContent, response};
}
```

The return type `Response<MyResponse>` is automatically serialized by the router via NanoPB. If decoding the request fails, the server automatically responds with `4.00 Bad Request` before calling your handler.

**3. Register routes with `NanopbRouter`:**

```cpp
static std::array<Route, 1> routes{{
    {codes::kPost, "/my-endpoint",
     NanopbRouter::Bind<&MyController::HandleMyMessage>(this)},
}};
static NanopbRouter router{"", routes};
server.AddRouter(router);
```

Lambdas are also supported:

```cpp
NanopbRouter::Bind([](const Request<MyRequest>& request) {
    std::cout << request.Body().name << "\n";
    return Response{codes::kContent, MyResponse{.greeting = "hello from lambda"}};
})
```

See [examples/serde_nanopb/serde_nanopb.cpp](examples/serde_nanopb/serde_nanopb.cpp) for a complete working example.

## nlohmann/json serialization (`coap-pp-serde-json`)

The `coap-pp-serde-json` layer automatically deserializes JSON request payloads and serializes JSON response payloads using [nlohmann/json](https://github.com/nlohmann/json).

**1. Define your request/response types and wire up nlohmann/json:**

```cpp
#include <nlohmann/json.hpp>

struct GreetRequest {
    std::string name{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GreetRequest, name)

struct GreetResponse {
    std::string greeting{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GreetResponse, greeting)
```

Any type that nlohmann/json can convert via `from_json`/`to_json` ADL works here.

**2. Write a typed handler — the payload is decoded before your handler runs:**

```cpp
auto HandleGreet(const Request<GreetRequest>& request) {
    GreetResponse response{.greeting = "Hello, " + request.Body().name + "!"};
    return Response{codes::kContent, response};
}
```

If JSON parsing fails, the server automatically responds with `4.00 Bad Request`.

**3. Register routes with `JsonRouter`:**

```cpp
#include "coap_pp_serde_json/router.hpp"

static std::array<Route, 1> routes{{
    {codes::kPost, "/greet",
     JsonRouter::Bind<&MyController::HandleGreet>(this)},
}};
static JsonRouter router{"", routes};
server.AddRouter(router);
```

Lambdas are also supported:

```cpp
JsonRouter::Bind([](const Request<GreetRequest>& request) {
    return Response{codes::kContent,
                    GreetResponse{.greeting = "hello from lambda"}};
})
```

See [examples/serde_json/serde_json.cpp](examples/serde_json/serde_json.cpp) for a complete working example.

## Async responses

Handlers can defer their reply — useful for slow operations. The server sends an empty ACK immediately (stopping CON retransmissions) and the handler delivers the real response later via `AsyncResponse::Send()`. Note that `Send()` is a library entry point: if the deferred work completes on another thread (as in the sketch below), the call must be serialized with the rest of the library per the [threading model](#threading-model):

```cpp
auto HandleSlow(const RawRequest& req) {
    auto async = req.MakeAsync();
    std::thread([a = async]() mutable {
        std::this_thread::sleep_for(std::chrono::seconds{2});
        a.Send(Response{codes::kContent,
                        as_bytes(span{kSlowText.data(), kSlowText.size()}),
                        ContentFormat::kTextPlain});
    }).detach();
    return async;
}
```

## Observing resources (RFC 7641, server side)

A resource becomes observable by pairing its GET route with an application-owned `Observable`. `HandleGet()` processes the request's Observe option (registration, re-registration, deregistration) and, for registrations, adds the Observe option to the response so it doubles as the initial notification. `Notify()` pushes a new representation to every registered observer:

```cpp
#include "coap_pp/server/observable.hpp"

Observable<4> observable{server};  // up to 4 concurrent observers

const std::array<Route, 1> routes{{
    {codes::kGet, "/counter", RawRouter::Bind([&](const RawRequest& req) {
       Response resp{codes::kContent, CounterPayload(),
                     ContentFormat::kTextPlain};
       observable.HandleGet(req, resp.options);
       return resp;
     })},
}};

// Whenever the resource state changes:
observable.Notify(Response{codes::kContent, CounterPayload(),
                           ContentFormat::kTextPlain});

// When the resource disappears:
observable.CancelAll(codes::kNotFound);
```

Like `AsyncResponse`, `Observable<MaxObservers, Serializer>` takes an optional serializer type so `Notify()` accepts typed payloads (e.g. `Observable<4, NanopbSerializer>`).

Behaviour details:

- Notifications are sent non-confirmable by default; every Nth notification per observer (CMake `COAP_PP_OBSERVE_CON_EVERY`, default 24) is sent confirmable so dead observers are detected. `Notify()` also takes an explicit `NotifyType::kCon`/`kNon` override. RFC 7641 §4.5's wall-clock rule (a CON at least every 24 h) is the application's responsibility, since the library has no time source.
- Observers are removed when they deregister (Observe=1), reject a notification with RST, or let a confirmable notification time out. A non-2.xx `Notify()`/`CancelAll()` code is sent without an Observe option and ends all observations (§4.2).
- When the observer list is full, registrations are silently ignored and the request is served as a plain GET (allowed by §4.1).
- Not implemented: NSTART/RTT congestion control (§4.5.1), updating the Observe value of an already-queued CON retransmission (§4.4), proactive Max-Age refresh (§4.3.1), ETag validation with 2.03 notifications (§4.3.2), transmission superseding (§4.5.2).

See [examples/raw/raw.cpp](examples/raw/raw.cpp) for a complete working example (`GET /counter`).

## Block-wise transfers (RFC 7959, server side)

Bodies larger than one datagram are transferred in blocks. The API is split by direction — *downloads* (a client GETs a large response body; RFC 7959 Block2) and *uploads* (a client PUTs/POSTs a large request body; RFC 7959 Block1) — and, on both sides, by how the body is accessed:

| | `ServeDownload(options, body)` | `ServeDownload(options, size, source)` | `UploadTransfer` | `UploadAssembler` |
|---|---|---|---|---|
| Direction | Response body → client | Response body → client | Request body ← client | Request body ← client |
| RFC 7959 option | Block2 | Block2 | Block1 | Block1 |
| Kind | Free function (span overload) | Free function (streaming overload) | Stateful class (one per resource) | Stateful class (one per resource) |
| Server-side state | None — each request names its block | None — each request names its block | Transfer position only (~64 bytes) | Transfer position + body buffer |
| Body in RAM | Yes — one contiguous `span` (RAM or memory-mapped flash), served as zero-copy slices | No — the source callback writes each block directly into the outgoing message buffer | No — handler consumes each block at its byte offset | Yes — reassembled into a caller-provided buffer |
| Typical use | Logs, in-RAM reports | External flash, data computed on the fly | Firmware written straight to flash | Bodies that must be parsed as a whole (JSON, protobuf) |

**Downloads — `ServeDownload()`.** Stateless by design: every Block2 request names the exact block it wants, so the handler simply runs once per block and `ServeDownload()` slices out the requested window, negotiating the block size and attaching the Block2/Size2/ETag options:

```cpp
#include "coap_pp/server/blockwise.hpp"

{codes::kGet, "/image", RawRouter::Bind([&](const RawRequest& req) {
   auto resp = ServeDownload(req.options, span<const std::byte>{image});
   resp.content_format = ContentFormat::kOctetStream;
   return resp;
 })},
```

For representations that are not addressable as one contiguous span (external flash, computed data) there is a streaming overload, `ServeDownload(options, total_size, source)`. The source callback is invoked once per response with the byte offset of the requested block and an output window of exactly the block's length, pointing directly into the outgoing message buffer — no intermediate copy. It must fill the window completely and return the number of bytes written:

```cpp
{codes::kGet, "/log", RawRouter::Bind([&](const RawRequest& req) {
   auto resp = ServeDownload(req.options, ext_flash.Size(),
                             [&](std::size_t offset, span<std::byte> out) {
                               ext_flash.Read(offset, out);
                               return out.size();
                             });
   resp.content_format = ContentFormat::kOctetStream;
   return resp;
 })},
```

**Uploads, streamed — `UploadTransfer`.** A per-resource state machine that hands each block to the handler as `(Offset(), Data())` for immediate consumption, so the full body never has to fit into RAM:

```cpp
UploadTransfer upload{4096};  // advertise ≤ 4096-byte blocks (e.g. flash page size)

{codes::kPut, "/firmware", RawRouter::Bind([&](const RawRequest& req) {
   switch (upload.Accept(req)) {
     case UploadTransfer::Status::kRejected:
       return upload.Reject();                    // 4.00 / 4.08
     case UploadTransfer::Status::kIntermediate:
       flash.Write(upload.Offset(), upload.Data());
       return upload.Continue();                  // 2.31 Continue
     case UploadTransfer::Status::kFinal:
       flash.Write(upload.Offset(), upload.Data());
       return upload.Finish(Response<span<const std::byte>>{codes::kChanged});
   }
 })},
```

**Uploads, assembled — `UploadAssembler`.** A convenience wrapper around `UploadTransfer` for handlers that need the complete body at once. It collects the blocks into a caller-provided buffer and answers all intermediate/error responses itself; the handler only runs its own logic when the body is complete:

```cpp
std::array<std::byte, 2048> config_buf;
UploadAssembler upload{config_buf};

{codes::kPost, "/config", RawRouter::Bind([&](const RawRequest& req) {
   if (upload.Accept(req) == UploadAssembler::Status::kReply) {
     return upload.Reply();  // 2.31 / 4.00 / 4.08 / 4.13
   }
   ApplyConfig(upload.Body());
   return upload.Finish(Response<span<const std::byte>>{codes::kChanged});
 })},
```

Behaviour details:

- A download handler runs once per block, so the representation must be stable across the transfer — supply `DownloadConfig::etag` so clients can detect a change between blocks. `Size2` (total body size) is attached to the first block; a request without a Block2 option is answered block-wise automatically when the body exceeds one block.
- Uploads follow the "atomic" style of RFC 7959 §2.5: in-sequence blocks are answered with 2.31 Continue, out-of-order blocks with 4.08 Request Entity Incomplete, and `UploadAssembler` rejects bodies beyond its buffer with 4.13 Request Entity Too Large (carrying `Size1` = capacity). A handler written against `UploadTransfer`/`UploadAssembler` transparently handles plain (non-block-wise) requests as well: they complete immediately as a single block.
- One instance supports one upload at a time. A client starting over supersedes an unfinished transfer (last writer wins); a stray request from another client is rejected with 4.08 without disturbing the active transfer. A retransmitted block (lost ACK) is re-accepted at the same offset, so offset-based sinks stay idempotent.

See [examples/blockwise/blockwise.cpp](examples/blockwise/blockwise.cpp) for a complete working example of all three (`GET /image`, `PUT /firmware`, `POST /config`), and [examples/blockwise/blockwise_client.py](examples/blockwise/blockwise_client.py) for an aiocoap client driving them.

## Integrating via CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(coap-pp
    GIT_REPOSITORY https://github.com/jushar/coap-pp.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(coap-pp)

target_link_libraries(my-target PRIVATE coap-pp)
```

When `COAP_PP_BUILD_SERDE_NANOPB` is `ON`, `FetchContent_MakeAvailable` also defines the `coap_pp_nanopb_generate_cpp` CMake function. See the [NanoPB deserialization](#nanopb-deserialization-coap-pp-serde-nanopb) section above for usage.
