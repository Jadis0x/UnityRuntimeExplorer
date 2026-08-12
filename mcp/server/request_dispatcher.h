// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Explorer::Mcp {

class RequestDispatcher {
  public:
    using Json = nlohmann::json;
    using Execute = std::function<Json(std::string, Json)>;
    using Emit = std::function<void(const Json&)>;

    RequestDispatcher(Execute execute, Emit emit);
    ~RequestDispatcher();

    void submit(Json rpc_id, std::string tool, Json arguments,
                std::optional<Json> progress_token, std::optional<std::uint64_t> task_ttl_ms);
    void cancel_request(const Json& rpc_id, std::string reason);
    bool handle_task_request(std::string_view method, const Json& rpc_id, const Json& params);
    void stop();

  private:
    struct Cancellation {
        std::atomic<bool> requested{false};
        std::string reason;
    };
    struct Work {
        Json rpc_id;
        std::string request_key;
        std::string tool;
        Json arguments;
        std::optional<Json> progress_token;
        std::shared_ptr<Cancellation> cancellation;
        std::string task_id;
    };
    struct Task {
        std::string id;
        std::string status = "working";
        std::string status_message = "Queued for execution.";
        std::string created_at;
        std::string updated_at;
        std::uint64_t ttl_ms = 300000;
        std::chrono::steady_clock::time_point created_monotonic{};
        Json result;
        std::shared_ptr<Cancellation> cancellation;
        std::vector<Json> result_waiters;
    };

    void worker_main();
    void emit_progress(const Work& work, double progress, std::string message);
    Json task_json(const Task& task) const;
    void complete_task(const Work& work, Json result);
    void purge_expired_tasks();
    static std::string request_key(const Json& id);
    static std::string new_task_id();
    static std::string now_iso8601();
    static Json rpc_error(const Json& id, int code, std::string message);
    static Json rpc_result(const Json& id, Json result);

    Execute execute_;
    Emit emit_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Work> queue_;
    std::unordered_map<std::string, std::shared_ptr<Cancellation>> active_requests_;
    std::unordered_map<std::string, Task> tasks_;
    std::deque<std::string> task_order_;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace Explorer::Mcp
