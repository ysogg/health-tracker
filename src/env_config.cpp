#include "healthtracker/env_config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace healthtracker {

namespace {

std::string Trim(const std::string& s) {
    const char* whitespace = " \t\r\n";
    const auto start = s.find_first_not_of(whitespace);
    if (start == std::string::npos) return "";
    const auto end = s.find_last_not_of(whitespace);
    return s.substr(start, end - start + 1);
}

std::string StripQuotes(const std::string& s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                          (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

bool ParseLine(const std::string& raw_line, std::string& key, std::string& value) {
    const std::string line = Trim(raw_line);
    if (line.empty() || line[0] == '#') return false;

    const auto eq = line.find('=');
    if (eq == std::string::npos) return false;

    key = Trim(line.substr(0, eq));
    value = StripQuotes(Trim(line.substr(eq + 1)));
    return !key.empty();
}

}  // namespace

std::optional<EnvConfig> EnvConfig::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    EnvConfig config;
    std::string line, key, value;
    while (std::getline(file, line)) {
        if (ParseLine(line, key, value)) {
            config.values_[key] = value;
        }
    }
    return config;
}

std::optional<std::string> EnvConfig::Get(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

std::string EnvConfig::GetOr(const std::string& key,
                              const std::string& fallback) const {
    return Get(key).value_or(fallback);
}

std::string EnvConfig::Require(const std::string& key) const {
    auto value = Get(key);
    if (!value) {
        throw std::runtime_error(
            "Missing required setting '" + key +
            "' in .env. See .env.example for the full list, or run with "
            "--auth to generate credentials.");
    }
    return *value;
}

void EnvConfig::Set(const std::string& key, const std::string& value) {
    values_[key] = value;
}

void EnvConfig::UpsertFileValue(const std::string& path, const std::string& key,
                                 const std::string& value) {
    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }

    bool replaced = false;
    std::string existing_key, existing_value;
    for (auto& line : lines) {
        if (ParseLine(line, existing_key, existing_value) && existing_key == key) {
            line = key + "=" + value;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        lines.push_back(key + "=" + value);
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Could not write to " + path);
    }
    for (const auto& line : lines) {
        out << line << "\n";
    }
}

}  // namespace healthtracker
