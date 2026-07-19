# URKit - IL2CPP Runtime Helpers

Generated IL2CPP runtime-helper headers. Bindings use `ctx->il2cpp`, `URK_Il2CppApi`, runtime capability checks, runtime image/class/member lookup, method signatures, and `last_error` diagnostics. They intentionally do not hardcode addresses, metadata registration data, vtables, or Unity-version-specific offsets. Managed hook targets use the loader's validated first-field `MethodInfo::methodPointer` path.

Runtime-only IL2CPP generation does not emit per-game SDK descriptors.

Mono and IL2CPP generated projects use runtime API helpers. No offline metadata or dump-generated modules are emitted.
