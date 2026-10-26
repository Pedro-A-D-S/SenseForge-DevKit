// ini_config.cpp — implementation (SF-3).

#include "ini_config.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace senseforge {
namespace {

std::string Trim(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t' ||
                           s[begin] == '\r')) {
        ++begin;
    }
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                           s[end - 1] == '\r')) {
        --end;
    }
    return s.substr(begin, end - begin);
}

// Strips a comment: a ';' at line start or preceded by whitespace.
std::string StripComment(const std::string& line) {
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == ';' && (i == 0 || line[i - 1] == ' ' ||
                               line[i - 1] == '\t')) {
            return line.substr(0, i);
        }
    }
    return line;
}

bool HasAny(const std::string& text, const char* chars) {
    return text.find_first_of(chars) != std::string::npos;
}

}  // namespace

bool IniDocument::Load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    sections_.clear();

    std::string current = "._global_";  // keys before any [section]
    std::string raw;
    while (std::getline(in, raw)) {
        const std::string line = Trim(StripComment(raw));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            current = Trim(line.substr(1, line.size() - 2));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;  // malformed line — tolerate and move on
        }
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        if (!key.empty()) {
            sections_[current][key] = value;
        }
    }
    return true;
}

bool IniDocument::Save(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    for (const auto& [section, entries] : sections_) {
        if (section != "._global_") {
            out << '[' << section << "]\n";
        }
        for (const auto& [key, value] : entries) {
            out << key << '=' << value << '\n';
        }
        out << '\n';
    }
    return true;
}

std::string IniDocument::GetString(const std::string& section,
                                   const std::string& key,
                                   const std::string& fallback) const {
    const auto sec = sections_.find(section);
    if (sec == sections_.end()) {
        return fallback;
    }
    const auto kv = sec->second.find(key);
    return kv == sec->second.end() ? fallback : kv->second;
}

long long IniDocument::GetInt(const std::string& section,
                              const std::string& key,
                              long long fallback) const {
    const std::string raw = GetString(section, key);
    if (raw.empty()) {
        return fallback;
    }
    // std::from_chars does not accept base 0; strtoll does and auto-detects
    // "0x…" (spec §3.2: overlay_toggle_key=0x74).
    char* end = nullptr;
    const long long parsed = std::strtoll(raw.c_str(), &end, 0);
    return (end != raw.c_str() && *end == '\0') ? parsed : fallback;
}

bool IniDocument::GetBool(const std::string& section, const std::string& key,
                          bool fallback) const {
    const std::string raw = GetString(section, key);
    if (raw == "true" || raw == "1" || raw == "yes") {
        return true;
    }
    if (raw == "false" || raw == "0" || raw == "no") {
        return false;
    }
    return fallback;
}

double IniDocument::GetFloat(const std::string& section,
                             const std::string& key, double fallback) const {
    const std::string raw = GetString(section, key);
    if (raw.empty()) {
        return fallback;
    }
    try {
        size_t used = 0;
        const double value = std::stod(raw, &used);
        return used == raw.size() ? value : fallback;
    } catch (...) {
        return fallback;
    }
}

bool IniDocument::Set(const std::string& section, const std::string& key,
                      const std::string& value) {
    if (section.empty() || HasAny(section, "\r\n[]") || key.empty() ||
        HasAny(key, "\r\n[]=;") || HasAny(value, "\r\n")) {
        return false;
    }
    sections_[section][key] = value;
    return true;
}

void IniDocument::SetFloat(const std::string& section, const std::string& key,
                           double value) {
    std::ostringstream out;
    out.precision(6);
    out << std::defaultfloat << value;
    Set(section, key, out.str());
}

bool IniDocument::HasSection(const std::string& section) const {
    return sections_.find(section) != sections_.end();
}

bool IniDocument::HasKey(const std::string& section,
                         const std::string& key) const {
    const auto sec = sections_.find(section);
    return sec != sections_.end() &&
           sec->second.find(key) != sec->second.end();
}

}  // namespace senseforge
