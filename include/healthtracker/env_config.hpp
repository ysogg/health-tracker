#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace healthtracker {

// Loads KEY=VALUE pairs from a .env-style file. Lines starting with '#' and
// blank lines are ignored; values may optionally be wrapped in quotes.
class EnvConfig {
public:
    // Returns std::nullopt if the file does not exist or can't be read.
    static std::optional<EnvConfig> LoadFromFile(const std::string& path);

    std::optional<std::string> Get(const std::string& key) const;
    std::string GetOr(const std::string& key, const std::string& fallback) const;
    std::string Require(const std::string& key) const;  // throws if missing

    // Overwrites a key in memory only.
    void Set(const std::string& key, const std::string& value);

    // Rewrites a single KEY=value line in-place in an existing .env file
    // (appending it if the key isn't present yet), leaving every other line
    // untouched.
    static void UpsertFileValue(const std::string& path, const std::string& key,
                                 const std::string& value);

private:
    std::unordered_map<std::string, std::string> values_;
};

}  // namespace healthtracker
