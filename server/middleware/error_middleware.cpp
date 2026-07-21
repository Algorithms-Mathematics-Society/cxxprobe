#include "server/middleware/error_middleware.hpp"

#include "server/api/dto.hpp"
#include "server/services/submission_service.hpp"

namespace cxxprobe::server::middleware {

namespace {

void write_error(cxxprobe::server::router::Response& res, unsigned status,
                 const std::string& error_code, const std::string& message) {
    res = cxxprobe::server::router::make_error_response(status, error_code, message);
}

}  // namespace

void ErrorMappingMiddleware::handle(cxxprobe::server::router::Request& req,
                                    cxxprobe::server::router::Response& res, const Next& next) {
    using cxxprobe::server::api::BadRequestError;
    using cxxprobe::server::services::ProblemNotFoundError;
    using cxxprobe::server::services::QueueFullError;
    using cxxprobe::server::services::UnsupportedLanguageError;

    try {
        next();
    } catch (const BadRequestError& ex) {
        write_error(res, 400, "bad_request", ex.what());
    } catch (const ProblemNotFoundError& ex) {
        write_error(res, 404, "problem_not_found", ex.what());
    } catch (const UnsupportedLanguageError& ex) {
        write_error(res, 400, "unsupported_language", ex.what());
    } catch (const QueueFullError& ex) {
        // write_error() replaces the whole Response via assignment, so the
        // extra header must be set afterward — setting it first would be
        // silently discarded.
        write_error(res, 503, "queue_full", ex.what());
        res.set_header(cxxprobe::server::router::beast_http::field::retry_after, "5");
    } catch (const std::exception& ex) {
        write_error(res, 500, "internal_error", ex.what());
    } catch (...) {
        write_error(res, 500, "internal_error", "unknown error");
    }
}

}  // namespace cxxprobe::server::middleware
