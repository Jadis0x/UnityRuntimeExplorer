// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "request_dispatcher.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace Explorer::Mcp {
namespace {
constexpr std::size_t kMaximumQueuedWork = 64;
constexpr std::size_t kMaximumTasks = 64;
constexpr std::uint64_t kMinimumTaskTtlMs = 1000;
constexpr std::uint64_t kMaximumTaskTtlMs = 3600000;
}

RequestDispatcher::RequestDispatcher(Execute execute, Emit emit)
    : execute_(std::move(execute)), emit_(std::move(emit)),
      worker_(&RequestDispatcher::worker_main, this) {}

RequestDispatcher::~RequestDispatcher() {
    stop();
}

RequestDispatcher::Json RequestDispatcher::rpc_error(const Json& id, int code, std::string message) {
    return {{"jsonrpc", "2.0"}, {"id", id},
            {"error", {{"code", code}, {"message", std::move(message)}}}};
}

RequestDispatcher::Json RequestDispatcher::rpc_result(const Json& id, Json result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

std::string RequestDispatcher::request_key(const Json& id) {
    return id.dump();
}

std::string RequestDispatcher::now_iso8601() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_s(&utc, &now);
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::string RequestDispatcher::new_task_id() {
    std::array<std::uint32_t, 4> values{};
    std::random_device random;
    for (std::uint32_t& value : values)
        value = random();
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(8) << values[0] << '-'
           << std::setw(4) << (values[1] >> 16u) << '-' << std::setw(4) << (values[1] & 0xffffu)
           << '-' << std::setw(4) << (values[2] >> 16u) << '-' << std::setw(4)
           << (values[2] & 0xffffu) << std::setw(8) << values[3];
    return stream.str();
}

void RequestDispatcher::submit(Json rpc_id, std::string tool, Json arguments,
                               std::optional<Json> progress_token,
                               std::optional<std::uint64_t> task_ttl_ms) {
    Work work;
    work.rpc_id = std::move(rpc_id);
    work.request_key = request_key(work.rpc_id);
    work.tool = std::move(tool);
    work.arguments = std::move(arguments);
    work.progress_token = std::move(progress_token);
    work.cancellation = std::make_shared<Cancellation>();

    Json immediate;
    {
        std::lock_guard lock(mutex_);
        purge_expired_tasks();
        if (queue_.size() >= kMaximumQueuedWork) {
            immediate = rpc_error(work.rpc_id, -32603, "MCP request queue is full");
        } else if (task_ttl_ms) {
            if (tasks_.size() >= kMaximumTasks) {
                immediate = rpc_error(work.rpc_id, -32603, "MCP task store is full");
            } else {
                work.task_id = new_task_id();
                Task task;
                task.id = work.task_id;
                task.created_at = now_iso8601();
                task.updated_at = task.created_at;
                task.created_monotonic = std::chrono::steady_clock::now();
                task.ttl_ms = std::clamp(*task_ttl_ms, kMinimumTaskTtlMs, kMaximumTaskTtlMs);
                task.cancellation = work.cancellation;
                tasks_.emplace(task.id, task);
                task_order_.push_back(task.id);
                queue_.push_back(work);
                immediate = rpc_result(work.rpc_id, {{"task", task_json(task)},
                    {"_meta", {{"io.modelcontextprotocol/model-immediate-response",
                        "Unity runtime operation accepted as a task."}}}});
            }
        } else {
            active_requests_[work.request_key] = work.cancellation;
            queue_.push_back(work);
        }
    }
    if (!immediate.is_null())
        emit_(immediate);
    condition_.notify_one();
}

void RequestDispatcher::cancel_request(const Json& rpc_id, std::string reason) {
    std::lock_guard lock(mutex_);
    const auto found = active_requests_.find(request_key(rpc_id));
    if (found == active_requests_.end())
        return;
    found->second->reason = std::move(reason);
    found->second->requested.store(true, std::memory_order_release);
}

void RequestDispatcher::emit_progress(const Work& work, double progress, std::string message) {
    if (!work.progress_token || work.cancellation->requested.load(std::memory_order_acquire))
        return;
    emit_({{"jsonrpc", "2.0"}, {"method", "notifications/progress"},
           {"params", {{"progressToken", *work.progress_token}, {"progress", progress},
                       {"total", 1.0}, {"message", std::move(message)}}}});
}

RequestDispatcher::Json RequestDispatcher::task_json(const Task& task) const {
    return {{"taskId", task.id}, {"status", task.status},
            {"statusMessage", task.status_message}, {"createdAt", task.created_at},
            {"lastUpdatedAt", task.updated_at}, {"ttl", task.ttl_ms}, {"pollInterval", 250}};
}

void RequestDispatcher::complete_task(const Work& work, Json result) {
    std::vector<Json> waiters;
    Json state;
    Json final_result;
    {
        std::lock_guard lock(mutex_);
        const auto found = tasks_.find(work.task_id);
        if (found == tasks_.end())
            return;
        Task& task = found->second;
        if (task.status == "cancelled")
            return;
        task.result = std::move(result);
        final_result = task.result;
        const bool failed = task.result.value("isError", false);
        task.status = failed ? "failed" : "completed";
        task.status_message = failed ? "The tool call failed." : "The tool call completed.";
        task.updated_at = now_iso8601();
        waiters.swap(task.result_waiters);
        state = task_json(task);
    }
    emit_({{"jsonrpc", "2.0"}, {"method", "notifications/tasks/status"}, {"params", state}});
    for (const Json& waiter : waiters) {
        Json related = final_result;
        related["_meta"]["io.modelcontextprotocol/related-task"] = {{"taskId", work.task_id}};
        emit_(rpc_result(waiter, std::move(related)));
    }
}

void RequestDispatcher::worker_main() {
    for (;;) {
        Work work;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty())
                return;
            work = std::move(queue_.front());
            queue_.pop_front();
        }
        if (work.cancellation->requested.load(std::memory_order_acquire)) {
            if (!work.task_id.empty()) {
                std::lock_guard lock(mutex_);
                if (auto found = tasks_.find(work.task_id); found != tasks_.end()) {
                    found->second.status = "cancelled";
                    found->second.status_message = "Cancelled before execution.";
                    found->second.updated_at = now_iso8601();
                }
            }
            continue;
        }
        emit_progress(work, 0.25, "Dispatching request to the Unity main thread.");
        Json result;
        try {
            result = execute_(std::move(work.tool), std::move(work.arguments));
        } catch (const std::exception& exception) {
            result = {{"content", {{{"type", "text"}, {"text", std::string("internal_error: ") + exception.what()}}}},
                      {"isError", true}};
        } catch (...) {
            result = {{"content", {{{"type", "text"}, {"text", "internal_error: unknown exception"}}}},
                      {"isError", true}};
        }
        emit_progress(work, 1.0, "Request completed.");
        if (!work.task_id.empty())
            complete_task(work, std::move(result));
        else {
            const bool cancelled = work.cancellation->requested.load(std::memory_order_acquire);
            {
                std::lock_guard lock(mutex_);
                active_requests_.erase(work.request_key);
            }
            if (!cancelled)
                emit_(rpc_result(work.rpc_id, std::move(result)));
        }
    }
}

bool RequestDispatcher::handle_task_request(std::string_view method, const Json& rpc_id,
                                            const Json& params) {
    if (method != "tasks/get" && method != "tasks/list" && method != "tasks/result" &&
        method != "tasks/cancel")
        return false;
    Json response;
    std::vector<Json> cancelled_waiters;
    std::string cancelled_task_id;
    {
        std::lock_guard lock(mutex_);
        purge_expired_tasks();
        if (method == "tasks/list") {
            Json tasks = Json::array();
            std::size_t offset = 0;
            bool cursor_valid = true;
            if (params.contains("cursor")) {
                if (!params["cursor"].is_string())
                    cursor_valid = false;
                else {
                    const std::string cursor = params["cursor"].get<std::string>();
                    constexpr std::string_view prefix = "task-offset:";
                    if (!cursor.starts_with(prefix))
                        cursor_valid = false;
                    else {
                        try {
                            offset = static_cast<std::size_t>(std::stoull(cursor.substr(prefix.size())));
                        } catch (const std::exception&) {
                            cursor_valid = false;
                        }
                    }
                }
            }
            if (!cursor_valid || offset > task_order_.size())
                response = rpc_error(rpc_id, -32602, "Invalid task cursor");
            else {
                constexpr std::size_t page_size = 25;
                std::size_t visited = 0;
                std::size_t emitted = 0;
                for (const std::string& id : task_order_) {
                    if (visited++ < offset)
                        continue;
                    if (emitted >= page_size)
                        break;
                    if (const auto found = tasks_.find(id); found != tasks_.end()) {
                        tasks.push_back(task_json(found->second));
                        ++emitted;
                    }
                }
                Json result{{"tasks", std::move(tasks)}};
                if (offset + emitted < task_order_.size())
                    result["nextCursor"] = "task-offset:" + std::to_string(offset + emitted);
                response = rpc_result(rpc_id, std::move(result));
            }
        } else if (!params.contains("taskId") || !params["taskId"].is_string()) {
            response = rpc_error(rpc_id, -32602, "taskId must be a string");
        } else {
            const std::string id = params["taskId"].get<std::string>();
            const auto found = tasks_.find(id);
            if (found == tasks_.end()) {
                response = rpc_error(rpc_id, -32602, "Task not found");
            } else if (method == "tasks/get") {
                response = rpc_result(rpc_id, task_json(found->second));
            } else if (method == "tasks/result") {
                if (found->second.status == "working" || found->second.status == "input_required")
                    found->second.result_waiters.push_back(rpc_id);
                else {
                    Json result = found->second.result;
                    if (result.is_null())
                        result = {{"content", {{{"type", "text"}, {"text", found->second.status_message}}}},
                                  {"isError", true}};
                    result["_meta"]["io.modelcontextprotocol/related-task"] = {{"taskId", id}};
                    response = rpc_result(rpc_id, std::move(result));
                }
            } else {
                if (found->second.status != "working" && found->second.status != "input_required")
                    response = rpc_error(rpc_id, -32602, "Cannot cancel a terminal task");
                else {
                    found->second.cancellation->requested.store(true, std::memory_order_release);
                    found->second.status = "cancelled";
                    found->second.status_message = "The task was cancelled by request.";
                    found->second.updated_at = now_iso8601();
                    cancelled_waiters.swap(found->second.result_waiters);
                    cancelled_task_id = id;
                    response = rpc_result(rpc_id, task_json(found->second));
                }
            }
        }
    }
    if (!response.is_null())
        emit_(response);
    for (const Json& waiter : cancelled_waiters) {
        Json result{{"content", {{{"type", "text"}, {"text", "Task was cancelled."}}}},
                    {"isError", true},
                    {"_meta", {{"io.modelcontextprotocol/related-task", {{"taskId", cancelled_task_id}}}}}};
        emit_(rpc_result(waiter, std::move(result)));
    }
    return true;
}

void RequestDispatcher::purge_expired_tasks() {
    const auto now = std::chrono::steady_clock::now();
    for (auto iterator = task_order_.begin(); iterator != task_order_.end();) {
        const auto found = tasks_.find(*iterator);
        if (found == tasks_.end()) {
            iterator = task_order_.erase(iterator);
            continue;
        }
        const bool terminal = found->second.status == "completed" || found->second.status == "failed" ||
                              found->second.status == "cancelled";
        if (terminal && now - found->second.created_monotonic >=
                            std::chrono::milliseconds(found->second.ttl_ms)) {
            tasks_.erase(found);
            iterator = task_order_.erase(iterator);
            continue;
        }
        ++iterator;
    }
    while (tasks_.size() >= kMaximumTasks && !task_order_.empty()) {
        const std::string id = task_order_.front();
        const auto found = tasks_.find(id);
        if (found != tasks_.end() && (found->second.status == "working" || found->second.status == "input_required"))
            break;
        tasks_.erase(id);
        task_order_.pop_front();
    }
}

void RequestDispatcher::stop() {
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
            return;
        stopping_ = true;
        for (auto& [_, cancellation] : active_requests_)
            cancellation->requested.store(true, std::memory_order_release);
        for (auto& [_, task] : tasks_)
            if (task.cancellation)
                task.cancellation->requested.store(true, std::memory_order_release);
    }
    condition_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

} // namespace Explorer::Mcp
