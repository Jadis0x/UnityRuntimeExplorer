// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "explorer_types.h"

namespace Explorer {

enum class CommandKind {
    Select = 0,
    ClearSelection = 1,
    Refresh = 2,
    DeleteObject = 3,
    DuplicateObject = 4,
    Rename = 5,
    SetTag = 6,
    SetLayer = 7,
    SetStatic = 8,
    SetActive = 9,
    SetLocalPosition = 10,
    SetLocalRotation = 11,
    SetLocalScale = 12,
    AddComponent = 13,
    DeleteComponent = 14,
    SetComponentEnabled = 15,
    SetLiveData = 16,
    LoadComponentMetadata = 17,
    LoadComponentClassCatalog = 18,
    LoadClassBrowserCatalog = 19,
    FindClassInstances = 20,
    LoadClassBrowserStaticState = 21,
    LoadClassBrowserMembers = 22,
    SetFieldValue = 23,
    SetPropertyValue = 24,
    SampleMemberValue = 25,
    SetArrayPage = 26,
    InvokeMethod = 27,
    SetMethodTrace = 28,
    ClearMethodTrace = 29,
    CloseMethodTrace = 30,
    SetFieldWatch = 31,
    ClearFieldWatch = 32,
    CloseFieldWatch = 33,
    InspectReference = 34,
    InspectRawReference = 35,
    CloseObjectInspectorTab = 36,
    SceneHint = 37,
    ObjectDestroyRequested = 38,
    ClearDiagnostics = 39,
    SetHighlightDistance = 40,
    FocusSelected = 41,
    RestoreCamera = 42,
    SetHighlightEnabled = 43,
    ClearFlightRecorder = 44,
    SetCameraFocusDistance = 45,
    SetCameraFocusTopDown = 46,
    SetCameraFocusTilt = 47,
    SetCameraFocusOffset = 48,
    SetClassBrowserStaticField = 49,
    LoadScene = 50,
    PinManagedReference = 51,
    ReleaseManagedReference = 52,
    ClearManagedReferences = 53,
    CreateClassInstance = 54,
    RefreshByteArrayInspection = 55,
    PasteLocalTransform = 56,
    CopyLocalTransform = 57,
    ConfigureFieldWatch = 58,
    ExportDiagnosticBundle = 59,
    BuildReferenceGraph = 60,
    ClearReferenceGraph = 61,
};

struct Command {
    CommandKind kind = CommandKind::Refresh;
    int instance_id = 0;
    int int_value = 0;
    int member_index = -1;
    std::uint64_t reference_token = 0;
    // Scopes nested Object Inspector and Class Browser command results.
    std::uint64_t object_inspector_token = 0;
    // Captured from the immutable UI snapshot. Zero means that the producer is
    // a lifecycle hook rather than a hierarchy/inspector interaction.
    std::uint64_t scene_generation = 0;
    std::uint64_t hierarchy_revision = 0;
    std::uintptr_t expected_object_address = 0;
    std::uint64_t sequence = 0;
    bool bool_value = false;
    bool object_inspector_target = false;
    bool class_browser_target = false;
    bool lock_value = false;
    bool unlock_value = false;
    bool member_is_property = false;
    bool class_is_unity_object = false;
    float float_value = 0.0f;
    URK::Unity::Vector3 vector_value{};
    URK::Unity::Vector3 vector_value_secondary{};
    URK::Unity::Vector3 vector_value_tertiary{};
    std::string text;
    std::string image;
    std::string namespc;
    std::string class_name;
    std::vector<std::string> method_arguments;
    std::vector<std::string> generic_type_arguments;
};

} // namespace Explorer
