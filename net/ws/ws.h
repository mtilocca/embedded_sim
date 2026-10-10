#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct http_request_ctx;

// Called by the HTTP server (net/ws/service.c) after a client upgraded to WebSocket.
int sim_ws_setup(int ws_socket, struct http_request_ctx *request_ctx, void *user_data);

#ifdef __cplusplus
}

namespace net::ws {

// Start the HTTP server and the telemetry fan-out thread. Call once, after
// the network is up. Live protocol: docs/web.md.
int start();

} // namespace net::ws
#endif
