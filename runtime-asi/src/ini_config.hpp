// ini_config.hpp — senseforge_user.ini parser/writer (SF-3).
//
// SENSEFORGE_SPEC.md §3.2: preferences edited by both the in-game overlay
// (SF-6) and SenseForge Studio (SF-7). Tolerant parser: ';'-comments (full
// line or trailing after whitespace), [sections], key=value, hex ints
// ("0x74"), unknown keys preserved through save.

#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace senseforge {

class IniDocument {
public:
    // Parses as much as possible; returns false only on I/O error.
    bool Load(const std::filesystem::path& path);
    bool Save(const std::filesystem::path& path) const;

    std::string GetString(const std::string& section, const std::string& key,
                          const std::string& fallback = "") const;
    // base-0 autodetect: "0x74" parses as hex (spec §3.2 overlay_toggle_key).
    long long GetInt(const std::string& section, const std::string& key,
                     long long fallback = 0) const;
    bool GetBool(const std::string& section, const std::string& key,
                 bool fallback = false) const;
    double GetFloat(const std::string& section, const std::string& key,
                    double fallback = 0.0) const;

    // SF-22: returns false (and writes nothing) when the section, key or
    // value would change the file structure on the next Load: a CR/LF, or a
    // '[', ']', '=' or ';' in a key, or a bracket in a section name. Ids that
    // come from a game manifest reach this call, so it must not trust them.
    bool Set(const std::string& section, const std::string& key,
             const std::string& value);
    void SetInt(const std::string& section, const std::string& key,
                long long value) {
        Set(section, key, std::to_string(value));
    }
    void SetBool(const std::string& section, const std::string& key,
                 bool value) {
        Set(section, key, value ? "true" : "false");
    }
    void SetFloat(const std::string& section, const std::string& key,
                  double value);

    bool HasSection(const std::string& section) const;
    bool HasKey(const std::string& section, const std::string& key) const;

    const std::map<std::string, std::map<std::string, std::string>>&
    Sections() const {
        return sections_;
    }

private:
    std::map<std::string, std::map<std::string, std::string>> sections_;
};

}  // namespace senseforge
