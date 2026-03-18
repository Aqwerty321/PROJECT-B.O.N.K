// ---------------------------------------------------------------------------
// api_server.hpp — route registration for CASCADE HTTP API
// ---------------------------------------------------------------------------
#pragma once

#include "engine_runtime.hpp"

#include <httplib.h>

namespace cascade::http {

void register_routes(httplib::Server& server,
                     EngineRuntime& runtime);

} // namespace cascade::http
