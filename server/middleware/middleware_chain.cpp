#include "server/middleware/middleware_chain.hpp"

#include <utility>

namespace cxxprobe::server::middleware {

void MiddlewareChain::use(std::shared_ptr<Middleware> mw) { middleware_.push_back(std::move(mw)); }

void MiddlewareChain::run(cxxprobe::server::router::Request& req,
                          cxxprobe::server::router::Response& res, const Terminal& terminal) const {
    // Builds the chain outward from the terminal handler: index 0's
    // middleware runs first, and its `next` ultimately reaches `terminal`.
    std::function<void(std::size_t)> invoke = [&](std::size_t index) {
        if (index >= middleware_.size()) {
            terminal(req, res);
            return;
        }
        middleware_.at(index)->handle(req, res, [&, index] { invoke(index + 1); });
    };
    invoke(0);
}

}  // namespace cxxprobe::server::middleware
