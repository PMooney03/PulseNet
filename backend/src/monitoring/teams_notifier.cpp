#include "pulsenet/monitoring/teams_notifier.hpp"

#include "pulsenet/monitoring/diagnostics.hpp"
#include "pulsenet/utilities/logger.hpp"

#include <nlohmann/json.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

#include <chrono>
#include <cstdint>
#include <optional>

namespace pulsenet {
namespace {

namespace net = boost::asio;
namespace ssl = net::ssl;
namespace beast = boost::beast;
namespace http = beast::http;

struct HttpsUrl {
    std::string host;
    std::string port{"443"};
    std::string target{"/"};
};

std::optional<HttpsUrl> parse_https_url(std::string_view text) {
    constexpr std::string_view prefix = "https://";
    if (!text.starts_with(prefix)) {
        return std::nullopt;
    }
    text.remove_prefix(prefix.size());
    if (text.empty() || text.find('@') != std::string_view::npos) {
        return std::nullopt;
    }
    const auto slash = text.find('/');
    const auto authority = slash == std::string_view::npos ? text : text.substr(0, slash);
    if (authority.empty() || authority.front() == '[') {
        return std::nullopt;
    }
    HttpsUrl url;
    url.target = slash == std::string_view::npos ? "/" : std::string(text.substr(slash));
    const auto colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        url.host = std::string(authority.substr(0, colon));
        url.port = std::string(authority.substr(colon + 1));
        if (url.port.empty()) {
            return std::nullopt;
        }
    } else {
        url.host = std::string(authority);
    }
    if (url.host.empty()) {
        return std::nullopt;
    }
    return url;
}

void post_one(const HttpsUrl& url, const std::string& message) {
    net::io_context context;
    ssl::context tls(ssl::context::tls_client);
    tls.set_default_verify_paths();
    tls.set_verify_mode(ssl::verify_peer);

    net::ip::tcp::resolver resolver(context);
    beast::ssl_stream<beast::tcp_stream> stream(context, tls);
    const auto endpoints = resolver.resolve(url.host, url.port);
    beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(5));
    beast::get_lowest_layer(stream).connect(endpoints);
    if (SSL_set_tlsext_host_name(stream.native_handle(), url.host.c_str()) == 0) {
        throw std::runtime_error("could not set TLS server name");
    }
    stream.handshake(ssl::stream_base::client);

    http::request<http::string_body> request{http::verb::post, url.target, 11};
    request.set(http::field::host, url.host);
    request.set(http::field::user_agent, "PulseNet");
    request.set(http::field::content_type, "application/json");
    request.body() = nlohmann::json{{"text", message}}.dump();
    request.prepare_payload();
    http::write(stream, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);
    const auto status = response.result_int();
    if (status < 200 || status >= 300) {
        log_warn("teams notification rejected status=" + std::to_string(status));
        return;
    }
    log_info("teams notification sent");
}

}  // namespace

std::string ticket_number(const std::int64_t id) { return "PN-" + std::to_string(id); }

std::string teams_down_message(const MonitorTarget& target, const Incident& incident, const MonitorResult& result) {
    return "PulseNet " + ticket_number(incident.id) + "\n" + target.name + " is down\n" + incident.reason + "\n" +
           explain_result(target, result);
}

std::string teams_recovered_message(const std::string_view target_name, const Incident& incident) {
    return "PulseNet " + ticket_number(incident.id) + " is resolved\n" + std::string(target_name) + " is back\n" +
           incident.reason;
}

void post_teams_messages(const std::string_view webhook, const std::vector<std::string>& messages) {
    if (webhook.empty() || messages.empty()) {
        return;
    }
    const auto url = parse_https_url(webhook);
    if (!url) {
        log_warn("teams webhook was ignored because it is not an https URL");
        return;
    }
    for (const auto& message : messages) {
        try {
            post_one(*url, message);
        } catch (const std::exception& ex) {
            log_warn(std::string("teams notification failed: ") + ex.what());
        }
    }
}

}  // namespace pulsenet
