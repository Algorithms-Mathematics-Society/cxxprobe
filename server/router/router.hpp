#pragma once

#include <functional>
#include <string>
#include <vector>

#include "server/router/request.hpp"
#include "server/router/response.hpp"
#include "server/router/route_pattern.hpp"

namespace cxxprobe::server::router {

using Handler = std::function<void(Request&, Response&)>;

// The dispatcher Beast itself doesn't provide. Routes are registered once
// at startup (see server/app.cpp) and never mutated after the HTTP
// listener starts accepting connections, so dispatch() needs no
// synchronization despite running concurrently across the connection
// thread pool. Deliberately has no knowledge of Middleware — the
// composition root wraps a call to dispatch() in a MiddlewareChain, rather
// than Router owning that chain itself.
class Router {
public:
    void add_route(beast_http::verb method, const std::string& pattern, Handler handler);

    // Matches method+path against the registered routes and invokes the
    // matched handler, or writes 404/405 directly if nothing matches.
    void dispatch(Request& req, Response& res) const;

private:
    struct Route {
        beast_http::verb method;
        RoutePattern pattern;
        Handler handler;
    };

    std::vector<Route> routes_;
};

}  // namespace cxxprobe::server::router
