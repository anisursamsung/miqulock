#pragma once

#include <miqutoolkit/core/color.hpp>
#include <string>
#include <cstdint>

namespace miqulock {

using Color = miqu::Color;

class Config {
public:
    static Config& get();

    void load(const std::string& custom_path = "");
    void reload();
    const std::string& get_config_path() const { return m_config_path; }

    static std::string get_user_config_path();
    static std::string ensure_user_config();

    const Color& get_primary_color() const { return m_primary; }
    const Color& get_on_primary_color() const { return m_on_primary; }
    const Color& get_primary_container_color() const { return m_primary_container; }
    const Color& get_background_color() const { return m_background; }
    const Color& get_surface_color() const { return m_surface; }
    const Color& get_on_surface_color() const { return m_on_surface; }
    const Color& get_outline_color() const { return m_outline; }
    const Color& get_error_color() const { return m_error; }

    bool show_power_actions() const { return m_show_power_actions; }
    int get_corner_radius() const { return m_corner_radius; }
    const std::string& get_font_family() const { return m_font_family; }
    const std::string& get_time_format() const { return m_time_format; }
    const std::string& get_date_format() const { return m_date_format; }
    const std::string& get_background_image() const { return m_background_image; }
    float get_background_scale() const { return m_background_scale; }

    static bool parse_hex_color(const std::string& hex, Color& out_color);

private:
    Config();
    void set_defaults();
    void load_file(const std::string& path, int depth = 0);
    std::string resolve_path(const std::string& path) const;
    std::string resolve_default_background() const;
    void sync_toolkit_config();

    std::string m_config_path;

    Color m_primary;
    Color m_on_primary;
    Color m_primary_container;
    Color m_background;
    Color m_surface;
    Color m_on_surface;
    Color m_outline;
    Color m_error;

    bool m_show_power_actions = true;
    int m_corner_radius = 24;
    std::string m_font_family = "Sans";
    std::string m_time_format = "%H:%M";
    std::string m_date_format = "%A, %B %d";
    std::string m_background_image;
    float m_background_scale = 0.90f;
};

} // namespace miqulock
