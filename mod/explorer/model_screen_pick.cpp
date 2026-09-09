// Copyright (c) 2026 Jadis0x. All rights reserved.
// Screen picking: turn a click into a Hierarchy selection.
#include "model_shared.h"
#include "screen_picker.h"

#include "support/mod_log.h"

using namespace URK::Unity;
namespace Inspect = URK::Unity::Inspect;

namespace Explorer {

void RuntimeModel::clear_screen_pick() {
    working_.screen_pick = {};
}

void RuntimeModel::pick_at_screen_point(const Command &command) {
    ScreenPicker::Options options{};
    options.screen_height = command.float_value;
    options.include_inactive = command.bool_value;
    const URK::Unity::Vector2 point{command.vector_value.x, command.vector_value.y};

    const ScreenPicker::Result picked = ScreenPicker::pick(point, options);

    ScreenPickResult result{};
    result.valid = true;
    result.point_x = point.x;
    result.point_y = point.y;
    result.camera_name = picked.camera_name;
    result.status = picked.diagnostic;
    result.scanned_ui = picked.scanned_ui;
    result.scanned_world = picked.scanned_world;
    result.revision = working_.screen_pick.revision + 1;
    result.hits.reserve(picked.hits.size());
    for (const Pick::Candidate &hit : picked.hits) {
        ScreenPickHit entry{};
        entry.instance_id = hit.instance_id;
        entry.name = hit.name;
        entry.path = hit.path;
        entry.source_type = hit.source_type;
        entry.ui = hit.layer == Pick::Layer::UI;
        entry.distance = hit.distance;
        entry.min_x = hit.rect.min_x;
        entry.min_y = hit.rect.min_y;
        entry.max_x = hit.rect.max_x;
        entry.max_y = hit.rect.max_y;
        result.hits.push_back(std::move(entry));
    }
    working_.screen_pick = std::move(result);

    if (working_.screen_pick.hits.empty()) {
        set_status(picked.diagnostic.empty() ? "Nothing is under that point" : picked.diagnostic);
        return;
    }

    // The topmost hit becomes the selection, exactly as clicking in the editor's
    // scene view does. Everything else stays in the list so a crowded click can
    // be resolved by hand.
    //
    // A hit can measure on screen and still fail to resolve - the renderer may
    // belong to an object the GameObject scan cannot reach, which is what makes
    // picking look dead in some games. Walk down the list rather than giving up
    // on the whole click.
    for (std::size_t index = 0; index < working_.screen_pick.hits.size(); ++index) {
        const ScreenPickHit &hit = working_.screen_pick.hits[index];
        Inspect::ObjectHandle root{};
        const GameObject object = resolve_live_game_object(hit.instance_id, root);
        if (!object) {
            Inspect::FreeObjectHandle(root);
            ModLog::warn("screen pick: %s (instance %d) is on screen but could not be resolved to a live GameObject",
                         hit.name.c_str(), hit.instance_id);
            continue;
        }
        select_object(object, root);
        const std::string others =
            working_.screen_pick.hits.size() > 1
                ? " (" + std::to_string(working_.screen_pick.hits.size() - 1) + " more under the cursor)"
                : std::string{};
        const std::string skipped =
            index > 0 ? " after skipping " + std::to_string(index) + " unresolvable hit(s)" : std::string{};
        set_status("Selected " + (hit.path.empty() ? hit.name : hit.path) + " [" + hit.source_type + "]" + others +
                   skipped);
        return;
    }
    set_status("Found " + std::to_string(working_.screen_pick.hits.size()) +
               " object(s) under the cursor, but none of them could be resolved to a live GameObject");
}

} // namespace Explorer
