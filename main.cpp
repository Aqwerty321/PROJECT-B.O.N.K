// ---------------------------------------------------------------------------
// CASCADE (Project BONK) — API server
// ---------------------------------------------------------------------------

#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <limits>

#if PROJECTBONK_ENABLE_JULIA_RUNTIME
#include <jluna.hpp>
#endif
#include <httplib.h>
#include <boost/version.hpp>

#include "http/api_server.hpp"
#include "engine_runtime.hpp"

namespace {

int resolve_listen_port() noexcept
{
    constexpr int kDefaultPort = 8000;
    const char* raw = std::getenv("PROJECTBONK_PORT");
    if (raw == nullptr || *raw == '\0') {
        return kDefaultPort;
    }

    char* end = nullptr;
    const long parsed = std::strtol(raw, &end, 10);
    if (end == raw || (end != nullptr && *end != '\0')) {
        return kDefaultPort;
    }
    if (parsed <= 0 || parsed > 65535) {
        return kDefaultPort;
    }
    return static_cast<int>(parsed);
}

} // namespace

int main()
{
#if PROJECTBONK_ENABLE_JULIA_RUNTIME
    jluna::initialize();
#endif

    std::cout << "CASCADE (Project BONK) SYSTEM ONLINE\n";
    std::cout << "Boost version : " << BOOST_LIB_VERSION << "\n";
    const int listen_port = resolve_listen_port();
    std::cout << "Starting HTTP server on 0.0.0.0:" << listen_port << " ...\n";

    cascade::EngineRuntime runtime;
    httplib::Server svr;

    const cascade::http::ApiServerConfig api_cfg = cascade::http::api_server_config_from_env();
    cascade::http::register_routes(svr, runtime, api_cfg);

    // Serve static frontend assets if the directory exists.
    // In Docker the built React app lives at /app/static; during local dev
    // the Vite dev-server is used instead so this gracefully becomes a no-op.
    const std::string static_dir = "static";
    if (std::filesystem::is_directory(static_dir)) {
        svr.set_mount_point("/", static_dir);
        std::cout << "Serving static frontend from ./" << static_dir << "\n";
    }

    if (!svr.listen("0.0.0.0", listen_port)) {
        std::cerr << "FATAL: failed to bind to 0.0.0.0:" << listen_port << "\n";
        return 1;
    }

    return 0;
}
