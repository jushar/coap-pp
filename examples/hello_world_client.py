"""
Example CoAP client for the hello_world server.

Requires:
    pip install aiocoap protobuf

hello_world_pb2.py was generated with:
    protoc --python_out=examples examples/hello_world.proto

Start the server first:
    ./build/examples/coap-hello-world

Then run:
    python examples/hello_world_client.py
"""

import asyncio
import aiocoap

import hello_world_pb2

BASE_URI = "coap://127.0.0.1:5683"


async def main():
    ctx = await aiocoap.Context.create_client_context()

    requests = [
        ("GET",  "/hello",          None),                                                        # 2.05 Content: "Hello, CoAP World!"
        ("GET",  "/slow",           None),                                                        # 2.05 Content after ~2 s delay
        ("POST", "/hello",          None),                                                        # 4.05 Method Not Allowed
        ("GET",  "/other",          None),                                                        # 4.04 Not Found
        ("POST", "/hello-world-pb", hello_world_pb2.HelloRequest(name="World").SerializeToString()),  # 2.05 Content
    ]

    for method, path, payload in requests:
        code = aiocoap.GET if method == "GET" else aiocoap.POST
        req = aiocoap.Message(code=code, uri=f"{BASE_URI}{path}", payload=payload or b"")
        print(f"{method} {path} ...", end=" ", flush=True)
        resp = await ctx.request(req).response
        body = resp.payload.decode(errors="replace") if resp.payload else ""
        print(f"{resp.code}  {body!r}")

    await ctx.shutdown()


if __name__ == "__main__":
    asyncio.run(main())
