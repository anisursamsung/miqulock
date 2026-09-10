#include "config.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <vector>

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

std::string Config::get_user_config_path() {
    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && *xdg_config) {
        return std::string(xdg_config) + "/miqulock/miqulock.conf";
    }
    const char* home = getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.config/miqulock/miqulock.conf";
    }
    return "";
}

std::string Config::ensure_user_config() {
    std::string user_conf = get_user_config_path();
    if (user_conf.empty()) return "";

    if (fs::exists(user_conf)) {
        return user_conf;
    }

    fs::path user_path(user_conf);
    std::error_code ec;
    fs::create_directories(user_path.parent_path(), ec);

    std::vector<std::string> defaults = {
        "/usr/share/miqulock/miqulock.conf",
        "assets/miqulock.conf",
        "/usr/local/share/miqulock/miqulock.conf"
    };

    for (const auto& cand : defaults) {
        if (fs::exists(cand)) {
            fs::copy_file(cand, user_conf, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                std::cout << "[miqulock] Initialized user configuration: copied default to " 
                          << user_conf << "\n";
                return user_conf;
            }
        }
    }

    return user_conf;
}

void Config::load(const std::string& custom_path) {
    set_defaults();

    if (!custom_path.empty() && fs::exists(custom_path)) {
        m_config_path = custom_path;
        load_file(m_config_path);
        return;
    }

    std::string user_conf = ensure_user_config();
    if (!user_conf.empty() && fs::exists(user_conf)) {
        m_config_path = user_conf;
        load_file(m_config_path);
    } else if (fs::exists("/usr/share/miqulock/miqulock.conf")) {
        m_config_path = "/usr/share/miqulock/miqulock.conf";
        load_file(m_config_path);
    } else if (fs::exists("assets/miqulock.conf")) {
        m_config_path = "assets/miqulock.conf";
        load_file(m_config_path);
    }
}

void Config::reload() {
    if (!m_config_path.empty() && fs::exists(m_config_path)) {
        set_defaults();
        load_file(m_config_path);
        std::cout << "[miqulock] Configuration reloaded live from " << m_config_path << "\n";
    }
}

} // namespace miqulock
