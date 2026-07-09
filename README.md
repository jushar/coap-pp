# coap-pp

[![CI](https://github.com/jushar/coap-pp/actions/workflows/ci.yml/badge.svg)](https://github.com/jushar/coap-pp/actions/workflows/ci.yml)

A C++17 implementation of the CoAP protocol ([RFC 7252](https://www.rfc-editor.org/rfc/rfc7252)) targeting small embedded systems (e.g. STM32 Cortex-M) in functional-safety contexts.

**Features:**
- CoAP messaging (reliable/unreliable messages, request/response, retransmission)
- CoAP server, with semi-dynamic registration of endpoints
- Async responses

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

    // 1. Transport
    PosixUdpTransport transport{5683};

    // 2. Messenger (tracks CON retransmissions; pool size = max in-flight CONs)
    MemoryPool<Messenger::PendingSlot, 4> con_pool{};
    Messenger messenger{transport, con_pool};

    // 3. Server
    std::array<RouterBase*, 4> router_storage{};
    CoapServer server{messenger, router_storage};

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

Handlers can defer their reply — useful for slow operations. The server sends an empty ACK immediately (stopping CON retransmissions) and the handler delivers the real response later from any thread:

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
