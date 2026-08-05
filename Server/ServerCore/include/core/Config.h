#pragma once

#include <string>
#include <unordered_map>

#include "core/Types.h"

namespace nhn {

/// Flat key/value settings, read from an optional `key = value` file and then
/// overridden by `--key=value` arguments.
///
/// Deliberately dependency-free: the servers need a handful of ports and
/// endpoints, and pulling in a JSON or INI library for that would be the
/// largest dependency in the project.
class Config {
public:
    /// Missing files are not an error — every setting has a default.
    /// @return false only when the file exists but cannot be read.
    bool LoadFile(const std::string& path);

    /// Accepts `--key=value`, and `--flag` as shorthand for `--flag=true`.
    /// Arguments override file values.
    void ApplyCommandLine(int argc, char** argv);

    void Set(const std::string& key, std::string value);
    bool Has(const std::string& key) const;

    std::string GetString(const std::string& key, const std::string& fallback) const;
    int32 GetInt(const std::string& key, int32 fallback) const;
    uint16 GetPort(const std::string& key, uint16 fallback) const;
    bool GetBool(const std::string& key, bool fallback) const;

    /// Every resolved setting, for the startup banner.
    std::string Dump() const;

private:
    std::unordered_map<std::string, std::string> _values;
};

}  // namespace nhn
