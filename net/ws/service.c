/*
 * HTTP service for the device-hosted web UI. Plain C on purpose: Zephyr's
 * HTTP_SERVICE_DEFINE / HTTP_RESOURCE_DEFINE macros are C-only constructs.
 *
 *   /          index.html  \
 *   /app.js    app.js       } gzip-embedded from web/dist (or web/fallback)
 *   /app.css   app.css     /
 *   /ws        WebSocket, live telemetry (net/ws/ws.cpp)
 */
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>

#include "net/ws/ws.h"

static const uint8_t index_html_gz[] = {
#include "index.html.gz.inc"
};
static const uint8_t app_js_gz[] = {
#include "app.js.gz.inc"
};
static const uint8_t app_css_gz[] = {
#include "app.css.gz.inc"
};

#define STATIC_DETAIL(name, data, mime)                                                            \
	static struct http_resource_detail_static name = {                                         \
		.common = {                                                                        \
			.type = HTTP_RESOURCE_TYPE_STATIC,                                         \
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),                        \
			.content_encoding = "gzip",                                                \
			.content_type = mime,                                                      \
		},                                                                                 \
		.static_data = data,                                                               \
		.static_data_len = sizeof(data),                                                   \
	}

STATIC_DETAIL(index_detail, index_html_gz, "text/html");
STATIC_DETAIL(js_detail, app_js_gz, "text/javascript");
STATIC_DETAIL(css_detail, app_css_gz, "text/css");

static uint8_t ws_buffer[128];

static struct http_resource_detail_websocket ws_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_WEBSOCKET,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = sim_ws_setup,
	.data_buffer = ws_buffer,
	.data_buffer_len = sizeof(ws_buffer),
	.user_data = NULL,
};

static uint16_t sim_http_port = CONFIG_SIM_WEBSERVER_PORT;
HTTP_SERVICE_DEFINE(sim_http, NULL, &sim_http_port, CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL,
		    NULL);

HTTP_RESOURCE_DEFINE(index_res, sim_http, "/", &index_detail);
HTTP_RESOURCE_DEFINE(js_res, sim_http, "/app.js", &js_detail);
HTTP_RESOURCE_DEFINE(css_res, sim_http, "/app.css", &css_detail);
HTTP_RESOURCE_DEFINE(ws_res, sim_http, "/ws", &ws_detail);
