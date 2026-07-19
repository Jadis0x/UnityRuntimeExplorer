#pragma once

#include "runtime_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace URK::network {
struct Header {
  std::string name;
  std::string value;
};

struct Response {
  bool completed = false;
  std::int32_t status = 0;
  std::string body;
  std::string error;
  bool body_truncated = false;
  bool error_truncated = false;

  bool ok() const { return completed && status >= 200 && status < 300; }
};

inline bool available() {
  const URK::ModContext *ctx = URK::context();
  return ctx && URK::has_network() &&
         ctx->size >= offsetof(URK::ModContext, network) + sizeof(ctx->network) &&
         ctx->network &&
         ctx->network->size >= offsetof(URK::NetworkApi, json_request) +
                                  sizeof(ctx->network->json_request) &&
         ctx->network->json_request;
}

inline Response request_json(URK::NetworkHttpMethod method,
                             std::string_view url,
                             std::string_view json_body = {},
                             const std::vector<Header> &headers = {},
                             std::uint32_t timeout_ms = 5000,
                             std::string_view pinned_public_key = {},
                             std::size_t response_capacity = 65536) {
  Response response;
  if (!available()) {
    response.error = "URKit network API is unavailable";
    return response;
  }
  if (url.rfind("https://", 0) != 0) {
    response.error = "HTTPS URL is required";
    return response;
  }

  if (response_capacity == 0)
    response_capacity = 1;
  if (response_capacity > 1024 * 1024)
    response_capacity = 1024 * 1024;

  std::string url_storage(url);
  std::string body_storage(json_body);
  std::string pin_storage(pinned_public_key);
  std::vector<URK::NetworkHeader> raw_headers;
  raw_headers.reserve(headers.size());
  for (const Header &header : headers)
    raw_headers.push_back({header.name.c_str(), header.value.c_str()});

  std::string body_buffer(response_capacity, '\0');
  std::array<char, 1024> error_buffer{};

  URK::NetworkRequest request{};
  request.size = sizeof(request);
  request.method = static_cast<std::uint32_t>(method);
  request.url = url_storage.c_str();
  request.jsonBody = body_storage.empty() ? nullptr : body_storage.c_str();
  request.headers = raw_headers.empty() ? nullptr : raw_headers.data();
  request.headerCount = raw_headers.size();
  request.timeoutMs = timeout_ms;
  request.pinnedPublicKey =
      pin_storage.empty() ? nullptr : pin_storage.c_str();

  URK::NetworkResponse raw_response{};
  raw_response.size = sizeof(raw_response);
  raw_response.body = body_buffer.data();
  raw_response.bodyCapacity = body_buffer.size();
  raw_response.error = error_buffer.data();
  raw_response.errorCapacity = error_buffer.size();

  const URK::ModContext *ctx = URK::context();
  response.completed =
      ctx->network->json_request(&request, &raw_response) != 0;
  response.status = raw_response.statusCode;
  response.body_truncated =
      (raw_response.flags & URK::network_result_body_truncated) != 0;
  response.error_truncated =
      (raw_response.flags & URK::network_result_error_truncated) != 0;
  if (raw_response.body && raw_response.bodyCapacity) {
    const std::size_t copied =
        raw_response.bodyLength < raw_response.bodyCapacity
            ? raw_response.bodyLength
            : raw_response.bodyCapacity - 1;
    response.body.assign(raw_response.body, copied);
  }
  if (raw_response.error)
    response.error = raw_response.error;
  return response;
}

inline Response get_json(std::string_view url,
                         const std::vector<Header> &headers = {},
                         std::uint32_t timeout_ms = 5000,
                         std::string_view pinned_public_key = {}) {
  return request_json(URK::network_http_get, url, {}, headers, timeout_ms,
                      pinned_public_key);
}

inline Response post_json(std::string_view url, std::string_view body,
                          const std::vector<Header> &headers = {},
                          std::uint32_t timeout_ms = 5000,
                          std::string_view pinned_public_key = {}) {
  return request_json(URK::network_http_post, url, body, headers, timeout_ms,
                      pinned_public_key);
}

inline Response put_json(std::string_view url, std::string_view body,
                         const std::vector<Header> &headers = {},
                         std::uint32_t timeout_ms = 5000,
                         std::string_view pinned_public_key = {}) {
  return request_json(URK::network_http_put, url, body, headers, timeout_ms,
                      pinned_public_key);
}

inline Response update_json(std::string_view url, std::string_view body,
                            const std::vector<Header> &headers = {},
                            std::uint32_t timeout_ms = 5000,
                            std::string_view pinned_public_key = {}) {
  return request_json(URK::network_http_update, url, body, headers,
                      timeout_ms, pinned_public_key);
}

inline Response patch_json(std::string_view url, std::string_view body,
                           const std::vector<Header> &headers = {},
                           std::uint32_t timeout_ms = 5000,
                           std::string_view pinned_public_key = {}) {
  return request_json(URK::network_http_patch, url, body, headers, timeout_ms,
                      pinned_public_key);
}

inline Response delete_json(std::string_view url,
                            const std::vector<Header> &headers = {},
                            std::uint32_t timeout_ms = 5000,
                            std::string_view pinned_public_key = {}) {
  return request_json(URK::network_http_delete, url, {}, headers, timeout_ms,
                      pinned_public_key);
}
} // namespace URK::network