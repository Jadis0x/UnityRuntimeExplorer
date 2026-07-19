#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace URK::events {
struct Subscription {
  std::uint64_t id{};
  explicit operator bool() const noexcept { return id != 0; }
};

template <class... Args>
class Signal {
public:
  using Callback = void (*)(Args..., void*);

  Subscription subscribe(Callback callback, void* user = nullptr) {
    if (!callback)
      return {};
    const Subscription token{next_id_++};
    slots_.push_back({token.id, callback, user, true});
    return token;
  }

  bool unsubscribe(Subscription token) {
    if (!token)
      return false;
    bool removed = false;
    for (Slot& slot : slots_) {
      if (slot.id == token.id && slot.active) {
        slot.active = false;
        removed = true;
      }
    }
    if (emitting_ == 0)
      compact();
    return removed;
  }

  void emit(Args... args) {
    ++emitting_;
    for (Slot& slot : slots_) {
      if (slot.active)
        slot.callback(args..., slot.user);
    }
    --emitting_;
    if (emitting_ == 0)
      compact();
  }

  void clear() {
    for (Slot& slot : slots_)
      slot.active = false;
    if (emitting_ == 0)
      compact();
  }

  bool empty() const {
    return std::none_of(slots_.begin(), slots_.end(),
                        [](const Slot& slot) { return slot.active; });
  }

private:
  struct Slot {
    std::uint64_t id{};
    Callback callback{};
    void* user{};
    bool active{};
  };

  void compact() {
    slots_.erase(std::remove_if(slots_.begin(), slots_.end(),
                                [](const Slot& slot) { return !slot.active; }),
                 slots_.end());
  }

  std::uint64_t next_id_ = 1;
  std::uint32_t emitting_ = 0;
  std::vector<Slot> slots_;
};

template <class T, class Equal = std::equal_to<T>>
class Changed {
public:
  using SignalType = Signal<const T&, const T&>;

  Changed() = default;
  explicit Changed(T value) : value_(std::move(value)) {}

  const T& get() const noexcept { return value_; }
  SignalType& changed() noexcept { return changed_; }
  const SignalType& changed() const noexcept { return changed_; }

  bool assign(T value) {
    if (equal_(value_, value))
      return false;
    T previous = value_;
    value_ = std::move(value);
    changed_.emit(previous, value_);
    return true;
  }

private:
  T value_{};
  Equal equal_{};
  SignalType changed_{};
};
} // namespace URK::events
