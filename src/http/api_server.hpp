// ---------------------------------------------------------------------------
// api_server.hpp — route registration for CASCADE HTTP API
// ---------------------------------------------------------------------------
#pragma once

#include "engine_runtime.hpp"

#include <httplib.h>

#include <string>

namespace cascade::http {

struct ApiServerConfig {
    bool cors_enabled = false;
    bool cors_allow_credentials = false;
    std::string cors_allow_origin;
    std::string cors_allow_methods = "GET, POST, OPTIONS";
    std::string cors_allow_headers = "Content-Type, X-Source-Id";
};

ApiServerConfig api_server_config_from_env();

void register_routes(httplib::Server& server,
                     EngineRuntime& runtime,
                     const ApiServerConfig& config);

} // namespace cascade::http
