#pragma once

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <utility>
#include <vector>

namespace URK::coroutines {
class Task;
struct FlowState;

using Clock = std::chrono::steady_clock;
using Predicate = bool (*)(void*);
using ErrorHandler = void (*)(std::exception_ptr, void*);

enum class WaitKind {
  none,
  frame,
  time,
  predicate,
};

struct Promise {
  FlowState* state{};
  WaitKind wait = WaitKind::none;
  std::uint64_t wake_frame{};
  Clock::time_point wake_time{};
  Predicate predicate{};
  void* predicate_user{};
  std::exception_ptr error{};

  Task get_return_object();
  std::suspend_always initial_suspend() noexcept { return {}; }
  std::suspend_always final_suspend() noexcept { return {}; }
  void return_void() noexcept {}
  void unhandled_exception() noexcept { error = std::current_exception(); }
};

class Task {
public:
  using promise_type = Promise;
  using Handle = std::coroutine_handle<promise_type>;

  Task() = default;
  explicit Task(Handle handle) : handle_(handle) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
  Task& operator=(Task&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }
  ~Task() { reset(); }

  explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

private:
  friend struct FlowState;
  friend void spawn(FlowState&, Task&&);

  Handle release() noexcept { return std::exchange(handle_, {}); }
  void reset() noexcept {
    if (handle_)
      handle_.destroy();
    handle_ = {};
  }

  Handle handle_{};
};

inline Task Promise::get_return_object() {
  return Task{std::coroutine_handle<Promise>::from_promise(*this)};
}

struct FlowState {
  std::vector<Task::Handle> tasks;
  Clock::time_point now = Clock::now();
  std::uint64_t frame = 0;
  ErrorHandler error_handler{};
  void* error_user{};
};

struct NextFrameAwaiter {
  bool await_ready() const noexcept { return false; }
  void await_suspend(Task::Handle handle) const noexcept {
    Promise& promise = handle.promise();
    promise.wait = WaitKind::frame;
    promise.wake_frame = promise.state ? promise.state->frame + 1 : 1;
  }
  void await_resume() const noexcept {}
};

struct WaitForAwaiter {
  Clock::duration duration{};
  bool await_ready() const noexcept { return duration <= Clock::duration::zero(); }
  void await_suspend(Task::Handle handle) const noexcept {
    Promise& promise = handle.promise();
    promise.wait = WaitKind::time;
    promise.wake_time = promise.state ? promise.state->now + duration : Clock::now() + duration;
  }
  void await_resume() const noexcept {}
};

struct WaitUntilAwaiter {
  Predicate predicate{};
  void* user{};
  bool await_ready() const noexcept { return predicate && predicate(user); }
  void await_suspend(Task::Handle handle) const noexcept {
    Promise& promise = handle.promise();
    promise.wait = WaitKind::predicate;
    promise.predicate = predicate;
    promise.predicate_user = user;
  }
  void await_resume() const noexcept {}
};

inline NextFrameAwaiter next_frame() noexcept { return {}; }

template <class Rep, class Period>
WaitForAwaiter wait_for(std::chrono::duration<Rep, Period> duration) noexcept {
  return {std::chrono::duration_cast<Clock::duration>(duration)};
}

inline WaitUntilAwaiter wait_until(Predicate predicate, void* user = nullptr) noexcept {
  return {predicate, user};
}

inline void set_error_handler(FlowState& state, ErrorHandler handler,
                              void* user = nullptr) noexcept {
  state.error_handler = handler;
  state.error_user = user;
}

inline void spawn(FlowState& state, Task&& task) {
  Task::Handle handle = task.release();
  if (!handle)
    return;
  Promise& promise = handle.promise();
  promise.state = &state;
  promise.wait = WaitKind::none;
  state.tasks.push_back(handle);
}

inline bool ready(const FlowState& state, const Promise& promise) {
  switch (promise.wait) {
  case WaitKind::none:
    return true;
  case WaitKind::frame:
    return state.frame >= promise.wake_frame;
  case WaitKind::time:
    return state.now >= promise.wake_time;
  case WaitKind::predicate:
    return promise.predicate && promise.predicate(promise.predicate_user);
  default:
    return false;
  }
}

inline void cancel_all(FlowState& state) noexcept {
  for (Task::Handle handle : state.tasks) {
    if (handle)
      handle.destroy();
  }
  state.tasks.clear();
}

inline void tick(FlowState& state, Clock::time_point now = Clock::now()) {
  state.now = now;
  ++state.frame;

  const std::size_t count = state.tasks.size();
  for (std::size_t index = 0; index < count && index < state.tasks.size(); ++index) {
    Task::Handle handle = state.tasks[index];
    if (!handle || handle.done() || !ready(state, handle.promise()))
      continue;
    handle.promise().wait = WaitKind::none;
    handle.resume();
    if (handle.promise().error) {
      if (state.error_handler)
        state.error_handler(handle.promise().error, state.error_user);
      else
        std::terminate();
    }
  }

  state.tasks.erase(std::remove_if(state.tasks.begin(), state.tasks.end(),
                                   [](Task::Handle handle) {
                                     if (!handle)
                                       return true;
                                     if (!handle.done() && !handle.promise().error)
                                       return false;
                                     handle.destroy();
                                     return true;
                                   }),
                    state.tasks.end());
}
} // namespace URK::coroutines
