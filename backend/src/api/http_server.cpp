#include "pulsenet/api/http_server.hpp"

#include "pulsenet/database/incident_repository.hpp"
#include "pulsenet/database/result_repository.hpp"
#include "pulsenet/database/target_repository.hpp"
#include "pulsenet/models/validation.hpp"
#include "pulsenet/monitoring/diagnostics.hpp"
#include "pulsenet/utilities/logger.hpp"
#include "pulsenet/utilities/time.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace pulsenet {
namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;
using json = nlohmann::json;

struct HttpResponse {
    int status{200};
    std::string content_type{"application/json; charset=utf-8"};
    std::string body;
};

json target_json(const MonitorTarget& target, const std::optional<MonitorResult>& latest) {
    json document = {
        {"id", target.id},
        {"name", target.name},
        {"host", target.host},
        {"port", target.port ? json(*target.port) : json(nullptr)},
        {"type", std::string(to_string(target.type))},
        {"interval_seconds", target.interval.count()},
        {"timeout_ms", target.timeout.count()},
        {"enabled", target.enabled},
        {"latency_threshold_ms", target.latency_threshold.count()},
        {"failure_threshold", target.failure_threshold},
        {"created_at_ms", to_unix_ms(target.created_at)},
        {"last_status", latest ? std::string(to_string(latest->status)) : "unknown"},
        {"last_latency_ms", latest && latest->response_time ? json(latest->response_time->count()) : json(nullptr)},
        {"last_checked_at_ms", latest ? json(to_unix_ms(latest->checked_at)) : json(nullptr)},
        {"last_error", latest && latest->error_message ? json(*latest->error_message) : json(nullptr)},
    };
    return document;
}

json result_json(const MonitorResult& result) {
    return {
        {"id", result.id},
        {"target_id", result.target_id},
        {"checked_at_ms", to_unix_ms(result.checked_at)},
        {"success", result.success},
        {"response_time_ms", result.response_time ? json(result.response_time->count()) : json(nullptr)},
        {"http_status", result.http_status ? json(*result.http_status) : json(nullptr)},
        {"error_message", result.error_message ? json(*result.error_message) : json(nullptr)},
        {"status", std::string(to_string(result.status))},
    };
}

json incident_json(const Incident& incident) {
    return {
        {"id", incident.id},
        {"target_id", incident.target_id},
        {"severity", std::string(to_string(incident.severity))},
        {"reason", incident.reason},
        {"started_at_ms", to_unix_ms(incident.started_at)},
        {"resolved_at_ms", incident.resolved_at ? json(to_unix_ms(*incident.resolved_at)) : json(nullptr)},
        {"status", std::string(to_string(incident.status))},
    };
}

HttpResponse json_response(const int status, const json& document) {
    return HttpResponse{status, "application/json; charset=utf-8", document.dump()};
}

HttpResponse error_response(const Error& error) {
    int status = 500;
    if (error.code == ErrorCode::Validation) {
        status = 400;
    } else if (error.code == ErrorCode::NotFound) {
        status = 404;
    } else if (error.code == ErrorCode::Constraint) {
        status = 409;
    }
    return json_response(status, json{{"error", error.message}});
}

std::optional<std::int64_t> parse_id(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const auto value = std::stoll(text, &consumed, 10);
        if (consumed != text.size() || value <= 0) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string mime_type(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension == ".html") {
        return "text/html; charset=utf-8";
    }
    if (extension == ".js" || extension == ".mjs") {
        return "text/javascript; charset=utf-8";
    }
    if (extension == ".css") {
        return "text/css; charset=utf-8";
    }
    if (extension == ".svg") {
        return "image/svg+xml";
    }
    if (extension == ".json" || extension == ".map") {
        return "application/json";
    }
    if (extension == ".woff2") {
        return "font/woff2";
    }
    if (extension == ".ico") {
        return "image/x-icon";
    }
    return "application/octet-stream";
}

std::vector<std::string> split_path(std::string target) {
    const auto query = target.find('?');
    if (query != std::string::npos) {
        target.resize(query);
    }
    std::vector<std::string> parts;
    std::stringstream stream(target);
    std::string part;
    while (std::getline(stream, part, '/')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

class WsSession;

class Hub {
public:
    void join(const std::shared_ptr<WsSession>& session);
    void leave(const WsSession* session);
    void broadcast(const std::string& message);

private:
    std::mutex mutex_;
    std::vector<std::weak_ptr<WsSession>> sessions_;
};

class WsSession : public std::enable_shared_from_this<WsSession> {
public:
    WsSession(beast::tcp_stream stream, beast::flat_buffer buffer, Hub& hub)
        : ws_(std::move(stream)), buffer_(std::move(buffer)), hub_(hub) {}

    void run(http::request<http::string_body> request) {
        hub_.join(shared_from_this());
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.async_accept(request, [self = shared_from_this()](const beast::error_code& ec) {
            if (ec) {
                self->hub_.leave(self.get());
                return;
            }
            self->do_read();
        });
    }

    void send(std::string message) {
        net::post(ws_.get_executor(), [self = shared_from_this(), message = std::move(message)]() mutable {
            self->queue_.push_back(std::move(message));
            if (self->queue_.size() == 1) {
                self->do_write();
            }
        });
    }

private:
    void do_read() {
        ws_.async_read(buffer_, [self = shared_from_this()](const beast::error_code& ec, std::size_t) {
            if (ec) {
                self->hub_.leave(self.get());
                return;
            }
            self->buffer_.consume(self->buffer_.size());
            self->do_read();
        });
    }

    void do_write() {
        ws_.async_write(net::buffer(queue_.front()), [self = shared_from_this()](const beast::error_code& ec, std::size_t) {
            if (ec) {
                self->hub_.leave(self.get());
                return;
            }
            self->queue_.pop_front();
            if (!self->queue_.empty()) {
                self->do_write();
            }
        });
    }

    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    Hub& hub_;
    std::deque<std::string> queue_;
};

void Hub::join(const std::shared_ptr<WsSession>& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.push_back(session);
}

void Hub::leave(const WsSession* session) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(sessions_, [&](const std::weak_ptr<WsSession>& weak) {
        const auto live = weak.lock();
        return !live || live.get() == session;
    });
}

void Hub::broadcast(const std::string& message) {
    std::vector<std::shared_ptr<WsSession>> live;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& weak : sessions_) {
            if (auto session = weak.lock()) {
                live.push_back(std::move(session));
            }
        }
    }
    for (const auto& session : live) {
        session->send(message);
    }
}

class App {
public:
    App(TargetRepository& targets, ResultRepository& results, IncidentRepository& incidents, Scheduler& scheduler,
        std::filesystem::path web_root)
        : targets_(targets),
          results_(results),
          incidents_(incidents),
          scheduler_(scheduler),
          web_root_(std::move(web_root)) {}

    HttpResponse handle(const std::string& method, const std::string& target, const std::string& body) {
        if (method == "OPTIONS") {
            return HttpResponse{204, "text/plain", {}};
        }
        const auto parts = split_path(target);
        try {
            if (!parts.empty() && parts[0] == "api") {
                return route_api(method, parts, body);
            }
            if (method == "GET") {
                return serve_file(parts.empty() ? "/" : target);
            }
            return json_response(404, json{{"error", "Not found"}});
        } catch (const json::exception& ex) {
            return json_response(400, json{{"error", ex.what()}});
        } catch (const std::exception& ex) {
            return json_response(500, json{{"error", ex.what()}});
        }
    }

private:
    HttpResponse route_api(const std::string& method, const std::vector<std::string>& parts, const std::string& body) {
        if (parts.size() == 2 && parts[1] == "health" && method == "GET") {
            return json_response(200, json{{"status", "ok"}});
        }
        if (parts.size() == 2 && parts[1] == "targets" && method == "GET") {
            return list_targets();
        }
        if (parts.size() == 2 && parts[1] == "targets" && method == "POST") {
            return create_target(json::parse(body));
        }
        if (parts.size() == 2 && parts[1] == "incidents" && method == "GET") {
            return list_incidents();
        }
        if (parts.size() >= 3 && parts[1] == "targets") {
            const auto id = parse_id(parts[2]);
            if (!id) {
                return json_response(400, json{{"error", "Invalid target id"}});
            }
            if (parts.size() == 3 && method == "GET") {
                return read_target(*id);
            }
            if (parts.size() == 3 && method == "PATCH") {
                return patch_target(*id, json::parse(body));
            }
            if (parts.size() == 3 && method == "DELETE") {
                return delete_target(*id);
            }
            if (parts.size() == 4 && parts[3] == "results" && method == "GET") {
                return list_results(*id);
            }
            if (parts.size() == 4 && parts[3] == "check" && method == "POST") {
                return run_check(*id);
            }
            if (parts.size() == 4 && parts[3] == "diagnostics" && method == "GET") {
                return read_diagnostics(*id);
            }
            if (parts.size() == 4 && parts[3] == "diagnose" && method == "POST") {
                return run_check(*id);
            }
        }
        return json_response(404, json{{"error", "Not found"}});
    }

    std::optional<MonitorResult> latest_of(const std::int64_t id) {
        const auto history = results_.list_for_target(id, 1);
        if (!history || history.value().empty()) {
            return std::nullopt;
        }
        return history.value().front();
    }

    HttpResponse list_targets() {
        const auto listed = targets_.list_all();
        if (!listed) {
            return error_response(listed.error());
        }
        json rows = json::array();
        int online = 0;
        int degraded = 0;
        int offline = 0;
        int unknown = 0;
        for (const auto& target : listed.value()) {
            const auto latest = latest_of(target.id);
            const auto status = latest ? latest->status : TargetStatus::Unknown;
            if (status == TargetStatus::Online) {
                ++online;
            } else if (status == TargetStatus::Degraded) {
                ++degraded;
            } else if (status == TargetStatus::Offline) {
                ++offline;
            } else {
                ++unknown;
            }
            rows.push_back(target_json(target, latest));
        }
        const auto open = incidents_.list_open(1000);
        return json_response(200, json{{"targets", std::move(rows)},
                                        {"summary",
                                         {{"online", online},
                                          {"degraded", degraded},
                                          {"offline", offline},
                                          {"unknown", unknown},
                                          {"open_incidents", open ? static_cast<int>(open.value().size()) : 0}}}});
    }

    HttpResponse read_target(const std::int64_t id) {
        const auto found = targets_.find_by_id(id);
        if (!found) {
            return error_response(found.error());
        }
        return json_response(200, target_json(found.value(), latest_of(id)));
    }

    MonitorTarget target_from_json(const json& document, const MonitorTarget& base) {
        MonitorTarget target = base;
        if (document.contains("name")) {
            target.name = document.at("name").get<std::string>();
        }
        if (document.contains("host")) {
            target.host = document.at("host").get<std::string>();
        }
        if (document.contains("type")) {
            const auto parsed = monitor_type_from_string(document.at("type").get<std::string>());
            if (!parsed) {
                throw std::invalid_argument("invalid monitor type");
            }
            target.type = *parsed;
        }
        if (document.contains("port")) {
            if (document.at("port").is_null()) {
                target.port.reset();
            } else {
                target.port = document.at("port").get<int>();
            }
        }
        if (document.contains("interval_seconds")) {
            target.interval = std::chrono::seconds{document.at("interval_seconds").get<int>()};
        }
        if (document.contains("timeout_ms")) {
            target.timeout = std::chrono::milliseconds{document.at("timeout_ms").get<int>()};
        }
        if (document.contains("enabled")) {
            target.enabled = document.at("enabled").get<bool>();
        }
        if (document.contains("latency_threshold_ms")) {
            target.latency_threshold = std::chrono::milliseconds{document.at("latency_threshold_ms").get<int>()};
        }
        if (document.contains("failure_threshold")) {
            target.failure_threshold = document.at("failure_threshold").get<int>();
        }
        return target;
    }

    HttpResponse create_target(const json& document) {
        const auto draft = target_from_json(document, MonitorTarget{});
        const auto inserted = targets_.insert(draft);
        if (!inserted) {
            return error_response(inserted.error());
        }
        return read_target(inserted.value());
    }

    HttpResponse patch_target(const std::int64_t id, const json& document) {
        const auto found = targets_.find_by_id(id);
        if (!found) {
            return error_response(found.error());
        }
        const auto updated = targets_.update(target_from_json(document, found.value()));
        if (!updated) {
            return error_response(updated.error());
        }
        return read_target(id);
    }

    HttpResponse delete_target(const std::int64_t id) {
        const auto removed = targets_.remove(id);
        if (!removed) {
            return error_response(removed.error());
        }
        return HttpResponse{204, "application/json", {}};
    }

    HttpResponse list_results(const std::int64_t id) {
        const auto found = targets_.find_by_id(id);
        if (!found) {
            return error_response(found.error());
        }
        const auto history = results_.list_for_target(id, 50);
        if (!history) {
            return error_response(history.error());
        }
        json rows = json::array();
        for (const auto& result : history.value()) {
            rows.push_back(result_json(result));
        }
        return json_response(200, json{{"results", std::move(rows)}});
    }

    HttpResponse list_incidents() {
        const auto open = incidents_.list_recent(50);
        if (!open) {
            return error_response(open.error());
        }
        json rows = json::array();
        for (const auto& incident : open.value()) {
            rows.push_back(incident_json(incident));
        }
        return json_response(200, json{{"incidents", std::move(rows)}});
    }

    HttpResponse read_diagnostics(const std::int64_t id) {
        const auto found = targets_.find_by_id(id);
        if (!found) {
            return error_response(found.error());
        }
        const auto latest = latest_of(id);
        return json_response(200, json{{"summary", explain_result(found.value(), latest)},
                                        {"result", latest ? result_json(*latest) : json(nullptr)}});
    }

    HttpResponse run_check(const std::int64_t id) {
        const auto found = targets_.find_by_id(id);
        if (!found) {
            return error_response(found.error());
        }
        const auto report = scheduler_.check_now(found.value(), std::chrono::system_clock::now());
        if (!report) {
            return error_response(report.error());
        }
        if (report.value().results.empty()) {
            return json_response(409, json{{"error", "A check for this target is already running"}});
        }
        const auto& result = report.value().results.front();
        return json_response(200, json{{"summary", explain_result(found.value(), result)}, {"result", result_json(result)}});
    }

    HttpResponse serve_file(std::string target) {
        const auto query = target.find('?');
        if (query != std::string::npos) {
            target.resize(query);
        }
        if (web_root_.empty()) {
            return json_response(404, json{{"error", "Dashboard files are not installed"}});
        }
        if (target.find("..") != std::string::npos || target.find('%') != std::string::npos) {
            return json_response(400, json{{"error", "Invalid path"}});
        }
        const auto root = web_root_.lexically_normal();
        std::filesystem::path relative = target == "/" ? std::filesystem::path("index.html") : std::filesystem::path(target.substr(1));
        auto full = (root / relative).lexically_normal();
        const auto rel = full.lexically_relative(root).generic_string();
        if (rel.empty() || rel == ".." || rel.rfind("../", 0) == 0) {
            return json_response(400, json{{"error", "Invalid path"}});
        }
        if (!std::filesystem::is_regular_file(full)) {
            full = root / "index.html";
        }
        std::ifstream input(full, std::ios::binary);
        if (!input) {
            return json_response(404, json{{"error", "Not found"}});
        }
        std::ostringstream contents;
        contents << input.rdbuf();
        return HttpResponse{200, mime_type(full), contents.str()};
    }

    TargetRepository& targets_;
    ResultRepository& results_;
    IncidentRepository& incidents_;
    Scheduler& scheduler_;
    std::filesystem::path web_root_;
};

}  // namespace

struct HttpServer::Impl {
    Impl(HttpServerOptions options, TargetRepository& targets, ResultRepository& results, IncidentRepository& incidents,
         Scheduler& scheduler)
        : app(targets, results, incidents, scheduler, std::move(options.web_root)), acceptor(io) {
        boost::system::error_code ec;
        const auto address = net::ip::make_address(options.host, ec);
        if (ec) {
            throw std::runtime_error("invalid listen address: " + options.host);
        }
        acceptor.open(tcp::v4(), ec);
        if (ec) {
            throw std::runtime_error(ec.message());
        }
        acceptor.set_option(net::socket_base::reuse_address(true), ec);
        acceptor.bind({address, options.port}, ec);
        if (ec) {
            throw std::runtime_error("cannot listen: " + ec.message());
        }
        acceptor.listen(net::socket_base::max_listen_connections, ec);
        if (ec) {
            throw std::runtime_error(ec.message());
        }
        bound_port = acceptor.local_endpoint().port();
        work.emplace(net::make_work_guard(io));
    }

    void start() {
        if (started) {
            return;
        }
        started = true;
        do_accept();
        for (int index = 0; index < 2; ++index) {
            threads.emplace_back([this] { io.run(); });
        }
    }

    void stop() {
        if (!started) {
            work.reset();
            return;
        }
        boost::system::error_code ec;
        acceptor.close(ec);
        work.reset();
        io.stop();
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threads.clear();
        started = false;
    }

    void publish(const SchedulerReport& report) {
        for (const auto& result : report.results) {
            hub.broadcast(json{{"type", "check"}, {"result", result_json(result)}}.dump());
        }
        for (const auto& incident : report.opened) {
            hub.broadcast(json{{"type", "incident_opened"}, {"incident", incident_json(incident)}}.dump());
        }
        for (const auto id : report.resolved_ids) {
            hub.broadcast(json{{"type", "incident_resolved"}, {"id", id}}.dump());
        }
    }

    void do_accept() {
        auto stream = std::make_shared<beast::tcp_stream>(net::make_strand(io));
        acceptor.async_accept(stream->socket(), [this, stream](const beast::error_code& ec) {
            if (ec) {
                return;
            }
            std::make_shared<HttpSession>(*this, std::move(*stream))->run();
            if (acceptor.is_open()) {
                do_accept();
            }
        });
    }

    class HttpSession : public std::enable_shared_from_this<HttpSession> {
    public:
        HttpSession(Impl& server, beast::tcp_stream stream) : server_(server), stream_(std::move(stream)) {
            parser_.emplace();
            parser_->body_limit(1024 * 1024);
        }

        void run() {
            http::async_read(stream_, buffer_, *parser_, [self = shared_from_this()](const beast::error_code& ec, std::size_t) {
                self->on_read(ec);
            });
        }

    private:
        void on_read(const beast::error_code& ec) {
            if (ec) {
                return;
            }
            auto request = parser_->release();
            const auto path = request.target();
            if (websocket::is_upgrade(request) && path.size() >= 9 && path.substr(0, 9) == "/api/live") {
                std::make_shared<WsSession>(std::move(stream_), std::move(buffer_), server_.hub)->run(std::move(request));
                return;
            }
            const auto produced = server_.app.handle(std::string(request.method_string()), std::string(request.target()), request.body());
            auto response = std::make_shared<http::response<http::string_body>>(static_cast<http::status>(produced.status), request.version());
            response->set(http::field::server, "PulseNet");
            response->set(http::field::access_control_allow_origin, "*");
            response->set(http::field::access_control_allow_methods, "GET, POST, PATCH, DELETE, OPTIONS");
            response->set(http::field::access_control_allow_headers, "Content-Type");
            response->keep_alive(request.keep_alive());
            if (!produced.content_type.empty()) {
                response->set(http::field::content_type, produced.content_type);
            }
            response->body() = produced.body;
            response->prepare_payload();
            http::async_write(stream_, *response, [self = shared_from_this(), response, keep = request.keep_alive()](const beast::error_code& write_ec, std::size_t) {
                if (write_ec || !keep) {
                    return;
                }
                self->buffer_.consume(self->buffer_.size());
                self->parser_.emplace();
                self->parser_->body_limit(1024 * 1024);
                self->run();
            });
        }

        Impl& server_;
        beast::tcp_stream stream_;
        beast::flat_buffer buffer_;
        std::optional<http::request_parser<http::string_body>> parser_;
    };

    App app;
    Hub hub;
    net::io_context io;
    tcp::acceptor acceptor;
    std::optional<net::executor_work_guard<net::io_context::executor_type>> work;
    std::vector<std::thread> threads;
    unsigned short bound_port{0};
    bool started{false};
};

HttpServer::HttpServer(HttpServerOptions options, TargetRepository& targets, ResultRepository& results,
                       IncidentRepository& incidents, Scheduler& scheduler)
    : impl_(std::make_unique<Impl>(std::move(options), targets, results, incidents, scheduler)) {}

HttpServer::~HttpServer() {
    if (impl_) {
        impl_->stop();
    }
}

void HttpServer::start() { impl_->start(); }

void HttpServer::stop() { impl_->stop(); }

unsigned short HttpServer::port() const { return impl_->bound_port; }

void HttpServer::publish(const SchedulerReport& report) { impl_->publish(report); }

}  // namespace pulsenet
