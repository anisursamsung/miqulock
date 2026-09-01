#include "config.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <iostream>

namespace miqulock {

namespace fs = std::filesystem;

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n\"'");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\"'");
    return str.substr(first, (last - first + 1));
}

bool Config::parse_hex_color(const std::string& hex, Color& out_color) {
    std::string s = trim(hex);
    if (s.empty()) return false;
    if (s[0] == '#') s = s.substr(1);

    uint32_t val = 0;
    try {
        if (s.length() == 6) {
            val = std::stoul(s, nullptr, 16);
            out_color.r = ((val >> 16) & 0xFF) / 255.0;
            out_color.g = ((val >> 8) & 0xFF) / 255.0;
            out_color.b = (val & 0xFF) / 255.0;
            out_color.a = 1.0;
            return true;
        } else if (s.length() == 8) {
            val = std::stoul(s, nullptr, 16);
            out_color.r = ((val >> 24) & 0xFF) / 255.0;
            out_color.g = ((val >> 16) & 0xFF) / 255.0;
            out_color.b = ((val >> 8) & 0xFF) / 255.0;
            out_color.a = (val & 0xFF) / 255.0;
            return true;
        }
    } catch (...) {}
    return false;
}

Config& Config::get() {
    static Config instance;
    return instance;
}

Config::Config() {
    set_defaults();
    load();
}

void Config::set_defaults() {
    parse_hex_color("#0066ff", m_primary);
    parse_hex_color("#ffffff", m_on_primary);
    parse_hex_color("#cce5ff", m_primary_container);
    parse_hex_color("#0b0f19", m_background);
    parse_hex_color("#161f30", m_surface);
    parse_hex_color("#f8fafc", m_on_surface);
    parse_hex_color("#3b82f6", m_outline);
    parse_hex_color("#ef4444", m_error);
    m_corner_radius = 24;
    m_font_family = "Sans";
    m_time_format = "%H:%M";
    m_date_format = "%A, %B %d";
}

std::string Config::resolve_path(const std::string& path) const {
    if (path.empty()) return "";
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            return std::string(home) + path.substr(1);
        }
    }
    return path;
}

void Config::load_file(const std::string& path, int depth) {
    if (depth > 5) return;
    std::string resolved = resolve_path(path);
    if (!fs::exists(resolved)) return;

    std::ifstream file(resolved);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        if (key == "source" || key == "include") {
            load_file(value, depth + 1);
        } else if (key == "color_primary" || key == "primary") {
            parse_hex_color(value, m_primary);
        } else if (key == "color_on_primary" || key == "on_primary") {
            parse_hex_color(value, m_on_primary);
        } else if (key == "color_primary_container" || key == "primary_container") {
            parse_hex_color(value, m_primary_container);
        } else if (key == "color_background" || key == "background" || key == "bg_color") {
            parse_hex_color(value, m_background);
        } else if (key == "color_surface" || key == "surface") {
            parse_hex_color(value, m_surface);
        } else if (key == "color_on_surface" || key == "on_surface" || key == "text_color") {
            parse_hex_color(value, m_on_surface);
        } else if (key == "color_outline" || key == "outline" || key == "border_color") {
            parse_hex_color(value, m_outline);
        } else if (key == "color_error" || key == "error") {
            parse_hex_color(value, m_error);
        } else if (key == "window_border_radius" || key == "corner_radius" || key == "radius") {
            try {
                m_corner_radius = std::max(0, std::stoi(value));
            } catch (...) {}
        } else if (key == "font" || key == "font_family") {
            m_font_family = value;
        } else if (key == "time_format") {
            m_time_format = value;
        } else if (key == "date_format") {
            m_date_format = value;
        }
    }
}

void Config::load() {
    set_defaults();
    // 1. Load from miquland compositor config
    load_file("~/.config/miquland/miquland.conf");
    // 2. Load from theme mode if present
    load_file("~/.config/miquland/theme/theme_mode.conf");
    // 3. Override with miqulock specific config if present
    load_file("~/.config/miqulock/miqulock.conf");
}

} // namespace miqulock
