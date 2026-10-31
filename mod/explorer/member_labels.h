// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

// Persistent, per-game names for managed types and members. Obfuscators rename
// members to strings nobody can read or type - Phasmophobia ships nine-letter
// Malayalam identifiers - so working out what one does is wasted the moment the
// game restarts. A label is that finding, written down: a readable name and an
// optional note, kept next to the Explorer DLL and shared by the UI and MCP.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer::Labels {

enum class Kind : std::uint8_t { Type, Field, Property, Method };

struct Label {
    std::string name;
    std::string note;
    // "user" when typed in the overlay, "mcp" when an MCP client wrote it.
    std::string source;
    std::int64_t updated = 0;
    // Written against a different game build. A fresh obfuscation pass may have
    // moved the name to another member, so the UI marks it instead of trusting it.
    bool stale = false;
};

struct Entry {
    Kind kind = Kind::Field;
    // Full name of the type, or of the member's declaring type.
    std::string type_name;
    // Empty for Kind::Type.
    std::string member;
    // Methods only: comma-joined parameter type names. Empty labels every
    // overload that has no exact label of its own.
    std::string signature;
    Label label;
};

inline constexpr std::size_t kMaxNameBytes = 128;
inline constexpr std::size_t kMaxNoteBytes = 2048;

const char *kind_name(Kind kind);
std::optional<Kind> parse_kind(std::string_view text);
std::string method_signature(const std::vector<std::string> &parameter_types);

// Reads the store for the running game. Later calls are no-ops.
void initialize();

std::optional<Label> find(Kind kind, std::string_view type_name, std::string_view member = {},
                          std::string_view signature = {});
// Only a label written for exactly this signature, without the overload fallback.
std::optional<Label> find_exact(Kind kind, std::string_view type_name, std::string_view member = {},
                                std::string_view signature = {});
bool set(Entry entry, std::string *error);
bool remove(Kind kind, std::string_view type_name, std::string_view member, std::string_view signature,
            std::string *error);
std::vector<Entry> list();
// Bumped by every change, so a cached view can tell it is out of date.
std::uint64_t revision();
std::string storage_path();
std::string last_error();

// True when `name` cannot be read or typed as written: invalid UTF-8, control
// or invisible characters, or a script the overlay cannot shape. ASCII, Latin
// letters and CJK stay readable.
bool unreadable(std::string_view name);
// A short, stable stand-in for an unreadable name, e.g. "m_7c1e2a".
std::string alias(Kind kind, std::string_view name);
// `text` with every unreadable run replaced by its alias, so a type such as
// List<ഫഞഭഞ> reads as List<t_3a91f0> instead of a row of broken glyphs.
std::string humanize(std::string_view text, Kind kind = Kind::Type);
// A parameter name, aliased as "a_..." when unreadable.
std::string parameter_display(std::string_view name);

// What to show for a type: its label, else its humanized name.
std::string type_display(std::string_view type_name);
// What to show for a member: its label, else its alias when unreadable, else the name.
std::string member_display(Kind kind, std::string_view type_name, std::string_view member,
                           std::string_view signature = {});
// Extra text a filter should match: label, note and alias.
std::string search_text(Kind kind, std::string_view type_name, std::string_view member = {},
                        std::string_view signature = {});

} // namespace Explorer::Labels
