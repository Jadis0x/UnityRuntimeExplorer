// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "member_labels.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace Explorer::Labels {
namespace {

constexpr int kFormatVersion = 1;
constexpr char kKeySeparator = '\x1f';

struct Stored {
    Entry entry;
    // The game build the label was written against.
    std::string build;
};

std::shared_mutex g_mutex;
std::unordered_map<std::string, Stored> g_entries;
std::atomic<std::uint64_t> g_revision{1};
std::once_flag g_initialized;
std::mutex g_save_mutex;
std::filesystem::path g_file;
std::string g_build;
std::string g_last_error;
std::mutex g_error_mutex;
int g_module_anchor = 0;

void set_last_error(std::string text) {
    std::scoped_lock lock(g_error_mutex);
    g_last_error = std::move(text);
}

std::string key_for(Kind kind, std::string_view type_name, std::string_view member, std::string_view signature) {
    std::string key;
    key.reserve(type_name.size() + member.size() + signature.size() + 4);
    key.push_back(static_cast<char>('0' + static_cast<int>(kind)));
    key.push_back(kKeySeparator);
    key.append(type_name);
    key.push_back(kKeySeparator);
    key.append(member);
    key.push_back(kKeySeparator);
    key.append(signature);
    return key;
}

std::string utf8(const std::filesystem::path &path) {
    const std::wstring &wide = path.native();
    if (wide.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string text(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0)
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), size, nullptr,
                            nullptr);
    return text;
}

std::filesystem::path module_directory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&g_module_anchor), &module))
        return {};
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return {};
    return std::filesystem::path(path.data(), path.data() + length).parent_path();
}

std::filesystem::path game_executable() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return {};
    return std::filesystem::path(path.data(), path.data() + length);
}

// Size and write time of the assembly that obfuscation rewrites. Any patch
// changes at least one of them; nothing else about a build is cheap to read.
std::string read_build_fingerprint(const std::filesystem::path &executable) {
    if (executable.empty())
        return {};
    const std::filesystem::path directory = executable.parent_path();
    const std::filesystem::path candidates[] = {
        directory / L"GameAssembly.dll",
        directory / (executable.stem().wstring() + L"_Data") / L"Managed" / L"Assembly-CSharp.dll",
    };
    for (const std::filesystem::path &candidate : candidates) {
        std::error_code error;
        const auto size = std::filesystem::file_size(candidate, error);
        if (error)
            continue;
        const auto written = std::filesystem::last_write_time(candidate, error);
        if (error)
            continue;
        return utf8(candidate.filename()) + ":" + std::to_string(size) + ":" +
               std::to_string(static_cast<long long>(written.time_since_epoch().count()));
    }
    return {};
}

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool stale_build(const std::string &build) {
    return !build.empty() && !g_build.empty() && build != g_build;
}

// Decodes one UTF-8 code point. Returns 0 bytes consumed on malformed input.
std::size_t decode_utf8(std::string_view text, std::size_t offset, char32_t *code_point) {
    const auto byte = [&](std::size_t index) { return static_cast<unsigned char>(text[index]); };
    const unsigned char lead = byte(offset);
    std::size_t length = 0;
    char32_t value = 0;
    if (lead < 0x80) {
        *code_point = lead;
        return 1;
    } else if ((lead & 0xE0) == 0xC0) {
        length = 2;
        value = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        value = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        value = lead & 0x07;
    } else {
        return 0;
    }
    if (offset + length > text.size())
        return 0;
    for (std::size_t index = 1; index < length; ++index) {
        const unsigned char next = byte(offset + index);
        if ((next & 0xC0) != 0x80)
            return 0;
        value = (value << 6) | (next & 0x3F);
    }
    *code_point = value;
    return length;
}

// Scripts the overlay font covers and draws without shaping. Combining marks,
// zero-width and bidi controls, and scripts that need shaping (Malayalam,
// Arabic, Devanagari...) come out as broken or invisible glyphs.
bool readable_code_point(char32_t value) {
    if (value >= 0x20 && value <= 0x7E)
        return true;
    if (value >= 0xC0 && value <= 0x24F && value != 0xD7 && value != 0xF7)
        return true; // Latin-1 letters, Latin Extended-A/B
    if (value >= 0x370 && value <= 0x4FF)
        return true; // Greek, Cyrillic
    if ((value >= 0x3040 && value <= 0x30FF) || (value >= 0x3400 && value <= 0x4DBF) ||
        (value >= 0x4E00 && value <= 0x9FFF) || (value >= 0xFF01 && value <= 0xFF5E))
        return true; // Kana, CJK ideographs, full-width ASCII
    return false;
}

std::uint32_t fnv1a(std::string_view text) {
    std::uint32_t hash = 2166136261u;
    for (const char character : text) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 16777619u;
    }
    return hash;
}

std::string alias_with_prefix(char prefix, std::string_view name) {
    const std::uint32_t hash = fnv1a(name);
    char text[16]{};
    std::snprintf(text, sizeof(text), "%c_%06x", prefix, static_cast<unsigned>((hash ^ (hash >> 24)) & 0xFFFFFFu));
    return text;
}

char kind_prefix(Kind kind) {
    switch (kind) {
    case Kind::Type:
        return 't';
    case Kind::Field:
        return 'f';
    case Kind::Property:
        return 'p';
    case Kind::Method:
        return 'm';
    }
    return 'x';
}

std::string humanize_with_prefix(std::string_view text, char prefix) {
    std::string result;
    result.reserve(text.size());
    std::size_t offset = 0;
    std::size_t run_start = std::string_view::npos;
    const auto flush_run = [&](std::size_t end) {
        if (run_start == std::string_view::npos)
            return;
        result += alias_with_prefix(prefix, text.substr(run_start, end - run_start));
        run_start = std::string_view::npos;
    };
    while (offset < text.size()) {
        char32_t code_point = 0;
        const std::size_t length = decode_utf8(text, offset, &code_point);
        const std::size_t step = length == 0 ? 1 : length;
        if (length != 0 && readable_code_point(code_point)) {
            flush_run(offset);
            result.append(text.substr(offset, step));
        } else if (run_start == std::string_view::npos) {
            run_start = offset;
        }
        offset += step;
    }
    flush_run(text.size());
    return result;
}

bool valid_label_name(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameBytes)
        return false;
    for (const char character : name) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (value < 0x20 || value == 0x7F)
            return false;
    }
    return true;
}

std::string trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

nlohmann::json to_json(const Stored &stored) {
    const Entry &entry = stored.entry;
    nlohmann::json value = {{"kind", kind_name(entry.kind)}, {"type", entry.type_name}, {"label", entry.label.name}};
    if (!entry.member.empty())
        value["member"] = entry.member;
    if (!entry.signature.empty())
        value["signature"] = entry.signature;
    if (!entry.label.note.empty())
        value["note"] = entry.label.note;
    value["source"] = entry.label.source;
    value["updated"] = entry.label.updated;
    if (!stored.build.empty())
        value["build"] = stored.build;
    return value;
}

std::optional<Stored> from_json(const nlohmann::json &value) {
    if (!value.is_object())
        return std::nullopt;
    const auto text = [&](const char *name) {
        const auto found = value.find(name);
        return found != value.end() && found->is_string() ? found->get<std::string>() : std::string{};
    };
    const std::optional<Kind> kind = parse_kind(text("kind"));
    Stored stored;
    if (!kind)
        return std::nullopt;
    stored.entry.kind = *kind;
    stored.entry.type_name = text("type");
    stored.entry.member = text("member");
    stored.entry.signature = text("signature");
    stored.entry.label.name = text("label");
    stored.entry.label.note = text("note");
    stored.entry.label.source = text("source");
    if (const auto updated = value.find("updated"); updated != value.end() && updated->is_number_integer())
        stored.entry.label.updated = updated->get<std::int64_t>();
    stored.build = text("build");
    if (stored.entry.type_name.empty() || !valid_label_name(stored.entry.label.name) ||
        (stored.entry.kind != Kind::Type && stored.entry.member.empty()))
        return std::nullopt;
    return stored;
}

bool save_locked_copy(std::string *error) {
    if (g_file.empty()) {
        if (error)
            *error = "The label file location could not be resolved";
        return false;
    }
    nlohmann::json labels = nlohmann::json::array();
    {
        std::shared_lock lock(g_mutex);
        for (const auto &[key, stored] : g_entries)
            labels.push_back(to_json(stored));
    }
    // Sorted so the file diffs cleanly when it is kept under version control.
    const auto order = [](const nlohmann::json &value) {
        return std::make_tuple(value.value("type", std::string{}), value.value("member", std::string{}),
                               value.value("kind", std::string{}), value.value("signature", std::string{}));
    };
    std::sort(labels.begin(), labels.end(),
              [&](const nlohmann::json &left, const nlohmann::json &right) { return order(left) < order(right); });
    const nlohmann::json document = {{"format", kFormatVersion},
                                     {"game", utf8(game_executable().stem())},
                                     {"build", g_build},
                                     {"labels", std::move(labels)}};
    const std::string text = document.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);

    std::scoped_lock save_lock(g_save_mutex);
    const std::filesystem::path &path = g_file;
    std::error_code directory_error;
    std::filesystem::create_directories(path.parent_path(), directory_error);
    std::filesystem::path temporary = path;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            if (error)
                *error = "Could not write " + utf8(temporary);
            return false;
        }
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output) {
            if (error)
                *error = "Could not write " + utf8(temporary);
            return false;
        }
    }
    // Replace in one step so a crash mid-save never leaves half a file behind.
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error)
            *error = "Could not replace " + utf8(path) + " (Win32 " + std::to_string(GetLastError()) + ")";
        return false;
    }
    return true;
}

void load_from_disk() {
    const std::filesystem::path executable = game_executable();
    g_build = read_build_fingerprint(executable);
    const std::filesystem::path directory = module_directory();
    if (directory.empty() || executable.empty()) {
        set_last_error("The label file location could not be resolved");
        return;
    }
    g_file = directory / L"URK_Explorer_Labels" / (executable.stem().wstring() + L".json");
    const std::filesystem::path &path = g_file;

    std::ifstream input(path, std::ios::binary);
    if (!input)
        return; // No labels yet.
    std::stringstream buffer;
    buffer << input.rdbuf();
    nlohmann::json document = nlohmann::json::parse(buffer.str(), nullptr, false);
    if (document.is_discarded() || !document.is_object() || !document.contains("labels") ||
        !document["labels"].is_array()) {
        set_last_error("Ignored " + utf8(g_file) + ": it is not a label file");
        return;
    }
    std::unique_lock lock(g_mutex);
    for (const nlohmann::json &value : document["labels"]) {
        std::optional<Stored> stored = from_json(value);
        if (!stored)
            continue;
        stored->entry.label.stale = stale_build(stored->build);
        g_entries[key_for(stored->entry.kind, stored->entry.type_name, stored->entry.member,
                          stored->entry.signature)] = std::move(*stored);
    }
    g_revision.fetch_add(1, std::memory_order_release);
}

const Stored *find_locked(Kind kind, std::string_view type_name, std::string_view member,
                          std::string_view signature) {
    const auto found = g_entries.find(key_for(kind, type_name, member, signature));
    if (found != g_entries.end())
        return &found->second;
    // A label written without a signature covers every overload of that name.
    if (kind == Kind::Method && !signature.empty()) {
        const auto any = g_entries.find(key_for(kind, type_name, member, {}));
        if (any != g_entries.end())
            return &any->second;
    }
    return nullptr;
}

} // namespace

const char *kind_name(Kind kind) {
    switch (kind) {
    case Kind::Type:
        return "type";
    case Kind::Field:
        return "field";
    case Kind::Property:
        return "property";
    case Kind::Method:
        return "method";
    }
    return "unknown";
}

std::optional<Kind> parse_kind(std::string_view text) {
    if (text == "type")
        return Kind::Type;
    if (text == "field")
        return Kind::Field;
    if (text == "property")
        return Kind::Property;
    if (text == "method")
        return Kind::Method;
    return std::nullopt;
}

std::string method_signature(const std::vector<std::string> &parameter_types) {
    std::string signature;
    for (std::size_t index = 0; index < parameter_types.size(); ++index) {
        if (index)
            signature += ',';
        signature += parameter_types[index];
    }
    return signature;
}

void initialize() {
    std::call_once(g_initialized, load_from_disk);
}

std::optional<Label> find(Kind kind, std::string_view type_name, std::string_view member,
                          std::string_view signature) {
    initialize();
    if (type_name.empty())
        return std::nullopt;
    std::shared_lock lock(g_mutex);
    const Stored *stored = find_locked(kind, type_name, member, signature);
    if (!stored)
        return std::nullopt;
    return stored->entry.label;
}

std::optional<Label> find_exact(Kind kind, std::string_view type_name, std::string_view member,
                                std::string_view signature) {
    initialize();
    std::shared_lock lock(g_mutex);
    const auto found = g_entries.find(key_for(kind, type_name, member, signature));
    if (found == g_entries.end())
        return std::nullopt;
    return found->second.entry.label;
}

bool set(Entry entry, std::string *error) {
    initialize();
    entry.label.name = trimmed(entry.label.name);
    entry.label.note = trimmed(entry.label.note);
    if (entry.type_name.empty() || entry.type_name.size() > 1024) {
        if (error)
            *error = "A type name of at most 1024 bytes is required";
        return false;
    }
    if (entry.kind == Kind::Type) {
        entry.member.clear();
        entry.signature.clear();
    } else if (entry.member.empty() || entry.member.size() > 1024) {
        if (error)
            *error = "A member name of at most 1024 bytes is required";
        return false;
    }
    if (entry.kind != Kind::Method)
        entry.signature.clear();
    if (entry.signature.size() > 4096) {
        if (error)
            *error = "The method signature is longer than 4096 bytes";
        return false;
    }
    if (!valid_label_name(entry.label.name)) {
        if (error)
            *error = "A label is 1-128 bytes on one line";
        return false;
    }
    if (entry.label.note.size() > kMaxNoteBytes) {
        if (error)
            *error = "A note is at most 2048 bytes";
        return false;
    }
    if (entry.label.source.empty())
        entry.label.source = "user";
    entry.label.updated = unix_now();
    entry.label.stale = false;
    {
        std::unique_lock lock(g_mutex);
        Stored stored{std::move(entry), g_build};
        g_entries[key_for(stored.entry.kind, stored.entry.type_name, stored.entry.member, stored.entry.signature)] =
            std::move(stored);
        g_revision.fetch_add(1, std::memory_order_release);
    }
    std::string save_error;
    if (!save_locked_copy(&save_error)) {
        set_last_error(save_error);
        if (error)
            *error = "Kept for this session only: " + save_error;
        return false;
    }
    return true;
}

bool remove(Kind kind, std::string_view type_name, std::string_view member, std::string_view signature,
            std::string *error) {
    initialize();
    {
        std::unique_lock lock(g_mutex);
        if (g_entries.erase(key_for(kind, type_name, kind == Kind::Type ? std::string_view{} : member,
                                    kind == Kind::Method ? signature : std::string_view{})) == 0) {
            if (error)
                *error = "No such label";
            return false;
        }
        g_revision.fetch_add(1, std::memory_order_release);
    }
    std::string save_error;
    if (!save_locked_copy(&save_error)) {
        set_last_error(save_error);
        if (error)
            *error = "Removed for this session only: " + save_error;
        return false;
    }
    return true;
}

std::vector<Entry> list() {
    initialize();
    std::vector<Entry> entries;
    std::shared_lock lock(g_mutex);
    entries.reserve(g_entries.size());
    for (const auto &[key, stored] : g_entries)
        entries.push_back(stored.entry);
    return entries;
}

std::uint64_t revision() {
    initialize();
    return g_revision.load(std::memory_order_acquire);
}

std::string storage_path() {
    initialize();
    return utf8(g_file);
}

std::string last_error() {
    std::scoped_lock lock(g_error_mutex);
    return g_last_error;
}

bool unreadable(std::string_view name) {
    std::size_t offset = 0;
    while (offset < name.size()) {
        char32_t code_point = 0;
        const std::size_t length = decode_utf8(name, offset, &code_point);
        if (length == 0 || !readable_code_point(code_point))
            return true;
        offset += length;
    }
    return false;
}

std::string alias(Kind kind, std::string_view name) {
    return alias_with_prefix(kind_prefix(kind), name);
}

std::string humanize(std::string_view text, Kind kind) {
    return unreadable(text) ? humanize_with_prefix(text, kind_prefix(kind)) : std::string(text);
}

std::string parameter_display(std::string_view name) {
    return unreadable(name) ? humanize_with_prefix(name, 'a') : std::string(name);
}

std::string type_display(std::string_view type_name) {
    if (const std::optional<Label> label = find(Kind::Type, type_name))
        return label->name;
    return humanize(type_name, Kind::Type);
}

std::string member_display(Kind kind, std::string_view type_name, std::string_view member,
                           std::string_view signature) {
    if (const std::optional<Label> label = find(kind, type_name, member, signature))
        return label->name;
    return unreadable(member) ? alias(kind, member) : std::string(member);
}

std::string search_text(Kind kind, std::string_view type_name, std::string_view member, std::string_view signature) {
    const std::string_view name = kind == Kind::Type ? type_name : member;
    std::string text = unreadable(name) ? humanize(name, kind) : std::string{};
    if (const std::optional<Label> label = find(kind, type_name, member, signature)) {
        text += ' ';
        text += label->name;
        text += ' ';
        text += label->note;
    }
    return text;
}

} // namespace Explorer::Labels
