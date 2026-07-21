#pragma once

#include <string>

#include "server/router/request.hpp"
#include "server/router/response.hpp"

namespace cxxprobe::server::handlers {

// Serves the embedded developer UI's static assets by request path — no
// runtime filesystem access, so `--ui` keeps working from a relocated or
// read-only binary. Owns zero business logic: every dynamic action the UI
// takes is a fetch()/EventSource call to the real public API, not this
// handler — the one exception is index.html, which gets a single string
// substitution (the API's actual base URL) since the UI and API listen on
// two different ports and the page needs to know where to send requests.
class UiAssetHandler {
public:
    explicit UiAssetHandler(std::string api_base_url);

    void serve(cxxprobe::server::router::Request& req, cxxprobe::server::router::Response& res);

private:
    std::string api_base_url_;
};

}  // namespace cxxprobe::server::handlers
