#include "core/Config.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <map>
#include <sstream>

#include "core/Log.h"

namespace nhn {
namespace {

std::string Trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

std::string ToLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

}  // namespace

bool Config::LoadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return true;  // Absent config is the normal case.
    }

    std::string line;
    int32 lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        const auto separator = trimmed.find('=');
        if (separator == std::string::npos) {
            LOG_WARN("{}:{}: ignoring line without '='", path, lineNumber);
            continue;
        }

        Set(ToLower(Trim(std::string_view(trimmed).substr(0, separator))),
            Trim(std::string_view(trimmed).substr(separator + 1)));
    }

    LOG_INFO("loaded config from {}", path);
    return true;
}

void Config::ApplyCommandLine(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view argument(argv[i]);
        if (!argument.starts_with("--")) {
            continue;
        }
        argument.remove_prefix(2);

        const auto separator = argument.find('=');
        if (separator == std::string_view::npos) {
            Set(ToLower(std::string(argument)), "true");
        } else {
            Set(ToLower(std::string(argument.substr(0, separator))),
                std::string(argument.substr(separator + 1)));
        }
    }
}

void Config::Set(const std::string& key, std::string value) {
    _values[key] = std::move(value);
}

bool Config::Has(const std::string& key) const {
    return _values.find(ToLower(key)) != _values.end();
}

std::string Config::GetString(const std::string& key, const std::string& fallback) const {
    const auto it = _values.find(ToLower(key));
    return it != _values.end() ? it->second : fallback;
}

int32 Config::GetInt(const std::string& key, int32 fallback) const {
    const auto it = _values.find(ToLower(key));
    if (it == _values.end()) {
        return fallback;
    }

    int32 parsed = 0;
    const char* begin = it->second.data();
    const char* end = begin + it->second.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        LOG_WARN("config '{}' is not an integer ('{}'), using {}", key, it->second, fallback);
        return fallback;
    }
    return parsed;
}

uint16 Config::GetPort(const std::string& key, uint16 fallback) const {
    const int32 value = GetInt(key, static_cast<int32>(fallback));
    if (value <= 0 || value > 65535) {
        LOG_WARN("config '{}' is not a valid port ({}), using {}", key, value, fallback);
        return fallback;
    }
    return static_cast<uint16>(value);
}

bool Config::GetBool(const std::string& key, bool fallback) const {
    const auto it = _values.find(ToLower(key));
    if (it == _values.end()) {
        return fallback;
    }
    const std::string value = ToLower(it->second);
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    LOG_WARN("config '{}' is not a boolean ('{}'), using {}", key, it->second, fallback);
    return fallback;
}

std::string Config::Dump() const {
    // Ordered so the startup banner is stable between runs.
    const std::map<std::string, std::string> sorted(_values.begin(), _values.end());
    std::string result;
    for (const auto& [key, value] : sorted) {
        result += std::format("  {} = {}\n", key, value);
    }
    return result;
}

}  // namespace nhn
