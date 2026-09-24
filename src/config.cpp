#include "config.hpp"
#include <miqutoolkit/core/config.hpp>
#include <miqutoolkit/core/fs_utils.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <vector>
#include <set>

namespace miqulock {

namespace fs = std::filesystem;

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n\"'");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\"'");
    return str.substr(first, (last - first + 1));
}

bool Config::parse_hex_color(const std::string& hex, Color& out_color) {
    Color parsed = Color::from_hex(hex, Color::transparent());
    if (parsed.a > 0.0f) {
        out_color = parsed;
        return true;
    }
    std::string s = trim(hex);
    if (!s.empty() && (s == "#000" || s == "#000000" || s == "000000" || s == "#000000ff")) {
        out_color = Color(0.0f, 0.0f, 0.0f, 1.0f);
        return true;
    }
    return false;
}

Config& Config::get() {
    static Config instance;
    return instance;
}

Config::Config() {
    sync_defaults_from_toolkit();
}

void Config::sync_defaults_from_toolkit() {
    auto t_cfg = miqu::Config::get();
    if (t_cfg) {
        m_primary = t_cfg->colors.primary;
        m_on_primary = t_cfg->colors.on_primary;
        m_primary_container = t_cfg->colors.primary_container;
        m_background = t_cfg->colors.background.with_alpha(1.0f);
        m_surface = t_cfg->colors.surface;
        m_on_surface = t_cfg->colors.on_surface;
        m_outline = t_cfg->colors.outline;
        m_error = Color::from_hex("#ef4444");
        m_corner_radius = t_cfg->metrics.corner_radius;
        m_font_family = !t_cfg->metrics.font_family.empty() ? t_cfg->metrics.font_family : "Sans";
    }
    m_show_power_actions = true;
    m_time_format = "%H:%M";
    m_date_format = "%A, %B %d";
    m_wallpaper_path = "";
    m_blur_radius = 0;
    m_dim_alpha = 0.0f;
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

bool Config::parse_wallpaper_file(const std::string& path_val, std::string& out_path) const {
    std::string res = resolve_path(path_val);
    std::error_code ec;
    if (!fs::exists(res, ec) || fs::is_directory(res, ec)) {
        return false;
    }
    static const std::set<std::string> img_exts = {".jpg", ".jpeg", ".png", ".webp", ".gif"};
    std::string ext = fs::path(res).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (!img_exts.count(ext)) {
        std::ifstream pf(res);
        std::string forwarded;
        if (std::getline(pf, forwarded)) {
            forwarded = trim(forwarded);
            forwarded = resolve_path(forwarded);
            std::error_code ec2;
            if (!forwarded.empty() && fs::exists(forwarded, ec2) && !fs::is_directory(forwarded, ec2)) {
                res = forwarded;
            }
        }
    }
    out_path = res;
    return true;
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
        } else if (key == "color_background") {
            parse_hex_color(value, m_background);
            m_background = m_background.with_alpha(1.0f);
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
            } catch (const std::exception& e) {
                std::cerr << "[miqulock] Warning: invalid integer value for '" << key << "': " << value << "\n";
            }
        } else if (key == "show_power_actions" || key == "power_actions") {
            std::string v = value;
            std::transform(v.begin(), v.end(), v.begin(), ::tolower);
            m_show_power_actions = (v == "true" || v == "1" || v == "yes" || v == "on");
        } else if (key == "wallpaper" || key == "background_image" || key == "image") {
            if (!parse_wallpaper_file(value, m_wallpaper_path)) {
                std::cerr << "[miqulock] Warning: 'wallpaper' file not found: " << value << "\n";
            }
        } else if (key == "background") {
            // Disambiguation:
            // 1. Valid hex color: toolkit palette background override & solid fallback (forced opaque)
            // 2. Existing file or pointer: backward compatibility with legacy Section 1 'background = ...'
            Color c;
            if (parse_hex_color(value, c)) {
                m_background = c.with_alpha(1.0f);
            } else if (parse_wallpaper_file(value, m_wallpaper_path)) {
                // Backward-compatible wallpaper path
            } else {
                std::cerr << "[miqulock] Warning: 'background' value is neither a valid hex color nor an existing file: " << value << "\n";
            }
        } else if (key == "font" || key == "font_family") {
            m_font_family = value;
        } else if (key == "time_format") {
            m_time_format = value;
        } else if (key == "date_format") {
            m_date_format = value;
        } else if (key == "blur" || key == "blur_radius") {
            try {
                m_blur_radius = std::max(0, std::stoi(value));
            } catch (const std::exception& e) {
                std::cerr << "[miqulock] Warning: invalid integer value for '" << key << "': " << value << "\n";
            }
        } else if (key == "dim" || key == "dim_alpha") {
            try {
                m_dim_alpha = std::clamp(std::stof(value), 0.0f, 1.0f);
            } catch (const std::exception& e) {
                std::cerr << "[miqulock] Warning: invalid float value for '" << key << "': " << value << "\n";
            }
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

std::string Config::init_user_config() {
    return miqu::Config::init_user_config("miqulock", "miqulock.conf");
}

void Config::sync_toolkit_config() {
    auto m_cfg = miqu::Config::get();
    if (!m_cfg) return;
    m_cfg->colors.primary = m_primary;
    m_cfg->colors.on_primary = m_on_primary;
    m_cfg->colors.primary_container = m_primary_container;
    m_cfg->colors.background = m_background;
    m_cfg->colors.surface = m_surface;
    m_cfg->colors.surface_variant = m_surface.darken(0.12f);
    m_cfg->colors.on_surface = m_on_surface;
    m_cfg->colors.on_surface_variant = m_on_surface.with_alpha(0.60f);
    m_cfg->colors.outline = m_outline;
    m_cfg->metrics.corner_radius = m_corner_radius;
    m_cfg->metrics.font_family = m_font_family;
}

void Config::load(const std::string& custom_path) {
    if (!custom_path.empty() && fs::exists(custom_path)) {
        m_config_path = custom_path;
    } else {
        std::string user_cfg_dir = miqu::FsUtils::get_user_config_dir("miqulock");
        if (!user_cfg_dir.empty()) {
            std::string p = user_cfg_dir + "/miqulock.conf";
            if (fs::exists(p)) {
                m_config_path = p;
            }
        }
    }

    if (!m_config_path.empty() && fs::exists(m_config_path)) {
        miqu::Config::get()->load_from_file(m_config_path);
    }

    sync_defaults_from_toolkit();
    if (!m_config_path.empty() && fs::exists(m_config_path)) {
        load_file(m_config_path);
    }
    sync_toolkit_config();
}

void Config::reload() {
    if (!m_config_path.empty() && fs::exists(m_config_path)) {
        miqu::Config::get()->load_from_file(m_config_path);
    }
    sync_defaults_from_toolkit();
    if (!m_config_path.empty() && fs::exists(m_config_path)) {
        load_file(m_config_path);
        std::cout << "[miqulock] Configuration reloaded live from " << m_config_path << "\n";
    }
    sync_toolkit_config();
}

} // namespace miqulock
