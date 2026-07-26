// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <memory>
#include <string>

namespace URK::Unity {
struct GameObject;
}

namespace Explorer::CameraFocus {

struct Settings {
    float distance = 8.0f;
    float top_down_tilt = 3.0f;
    float transition_seconds = 0.32f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float offset_z = 0.0f;
    bool top_down = false;
};

class Controller {
  public:
    Controller();
    ~Controller();

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    bool start(URK::Unity::GameObject target, std::string& error);
    bool update(std::string& error);
    bool stop(std::string& error);

    bool active() const;
    const Settings& settings() const;
    void set_settings(Settings settings);

    // Native-fault recovery cannot safely call back into a possibly corrupt
    // managed runtime. Drop native ownership without dereferencing handles.
    void abandon_after_native_fault();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Explorer::CameraFocus
