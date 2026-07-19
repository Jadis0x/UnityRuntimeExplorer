#include "mod/explorer/explorer_model.h"

#include <cassert>
#include <type_traits>

int main() {
    using Explorer::CommandKind;
    using URK::Unity::GameObject;
    using URK::Unity::detail::RootedObjectArray;

    static_assert(static_cast<int>(CommandKind::Select) == 0);
    static_assert(static_cast<int>(CommandKind::ClearSelection) == 1);
    static_assert(static_cast<int>(CommandKind::SetLayer) == 7);
    static_assert(static_cast<int>(CommandKind::LoadComponentMetadata) == 17);
    static_assert(static_cast<int>(CommandKind::SceneHint) == 37);
    static_assert(static_cast<int>(CommandKind::ObjectDestroyRequested) == 38);
    static_assert(static_cast<int>(CommandKind::ClearDiagnostics) == 39);

    static_assert(!std::is_copy_constructible_v<RootedObjectArray<GameObject>>);
    static_assert(std::is_nothrow_move_constructible_v<RootedObjectArray<GameObject>>);
    static_assert(URK::Unity::Inspect::kMaxMetadataInheritanceDepth <= 128);
    static_assert(URK::Unity::Inspect::kMaxMethodParameters <= 256);

#if defined(_WIN32)
    assert(URK::Unity::Inspect::metadata_exception_filter(0xC0000005ul) == 1);
    assert(URK::Unity::Inspect::metadata_exception_filter(0xC0000006ul) == 1);
    assert(URK::Unity::Inspect::metadata_exception_filter(0xC000001Dul) == 0);
#endif
    return 0;
}
