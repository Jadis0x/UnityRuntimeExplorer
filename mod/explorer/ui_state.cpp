// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "ui_state.h"

namespace Explorer::UI {

UiState &ui_state() {
    static UiState state;
    return state;
}

} // namespace Explorer::UI
