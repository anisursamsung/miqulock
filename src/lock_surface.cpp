#include "lock_surface.hpp"
#include "lock_app.hpp"
#include "config.hpp"
#include "ext-session-lock-v1-client-protocol.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctime>
#include <cmath>
#include <cstring>
#include <iostream>

namespace miqulock {

static int create_anonymous_shm_file(off_t size) {
    int fd = memfd_create("miqulock-shm", MFD_CLOEXEC);
    if (fd < 0) {
        char template_name[] = "/tmp/miqulock-shm-XXXXXX";
        fd = mkstemp(template_name);
        if (fd >= 0) {
            unlink(template_name);
        }
    }
    if (fd >= 0) {
        if (ftruncate(fd, size) < 0) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
    .configure = [](void* data, struct ext_session_lock_surface_v1*, uint32_t serial, uint32_t width, uint32_t height) {
        auto* surface = static_cast<LockSurface*>(data);
        surface->handle_configure(serial, width, height);
    }
};

static const struct wl_callback_listener frame_listener = {
    .done = [](void* data, struct wl_callback* callback, uint32_t time) {
        auto* surface = static_cast<LockSurface*>(data);
        LockSurface::handle_frame_done(surface, callback, time);
    }
};

LockSurface::LockSurface(LockApp* app, struct wl_output* output)
    : m_app(app), m_wl_output(output)
{
    m_last_frame_time = std::chrono::steady_clock::now();
}

LockSurface::~LockSurface() {
    if (m_frame_callback) {
        wl_callback_destroy(m_frame_callback);
        m_frame_callback = nullptr;
    }
    destroy_shm_buffer();
    if (m_lock_surface) {
        ext_session_lock_surface_v1_destroy(m_lock_surface);
        m_lock_surface = nullptr;
    }
    if (m_wl_surface) {
        wl_surface_destroy(m_wl_surface);
        m_wl_surface = nullptr;
    }
}

void LockSurface::init() {
    m_wl_surface = wl_compositor_create_surface(m_app->get_compositor());
    m_lock_surface = ext_session_lock_v1_get_lock_surface(m_app->get_lock(), m_wl_surface, m_wl_output);
    ext_session_lock_surface_v1_add_listener(m_lock_surface, &lock_surface_listener, this);
}

void LockSurface::set_lock_surface(struct ext_session_lock_surface_v1* lock_surface) {
    m_lock_surface = lock_surface;
}

void LockSurface::handle_configure(uint32_t serial, uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
    m_configured = true;

    ext_session_lock_surface_v1_ack_configure(m_lock_surface, serial);
    create_shm_buffer();
    render();
}

void LockSurface::create_shm_buffer() {
    destroy_shm_buffer();

    int stride = m_width * 4;
    m_shm_size = stride * m_height;

    m_shm_fd = create_anonymous_shm_file(m_shm_size);
    if (m_shm_fd < 0) {
        std::cerr << "Failed to allocate SHM file" << std::endl;
        return;
    }

    m_shm_data = mmap(nullptr, m_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_fd, 0);
    if (m_shm_data == MAP_FAILED) {
        close(m_shm_fd);
        m_shm_fd = -1;
        m_shm_data = nullptr;
        return;
    }

    m_shm_pool = wl_shm_create_pool(m_app->get_shm(), m_shm_fd, m_shm_size);
    m_buffer = wl_shm_pool_create_buffer(m_shm_pool, 0, m_width, m_height, stride, WL_SHM_FORMAT_ARGB8888);
    m_cairo_surface = cairo_image_surface_create_for_data(
        static_cast<unsigned char*>(m_shm_data),
        CAIRO_FORMAT_ARGB32,
        m_width,
        m_height,
        stride
    );
}

void LockSurface::destroy_shm_buffer() {
    if (m_cairo_surface) {
        cairo_surface_destroy(m_cairo_surface);
        m_cairo_surface = nullptr;
    }
    if (m_buffer) {
        wl_buffer_destroy(m_buffer);
        m_buffer = nullptr;
    }
    if (m_shm_pool) {
        wl_shm_pool_destroy(m_shm_pool);
        m_shm_pool = nullptr;
    }
    if (m_shm_data && m_shm_data != MAP_FAILED) {
        munmap(m_shm_data, m_shm_size);
        m_shm_data = nullptr;
    }
    if (m_shm_fd >= 0) {
        close(m_shm_fd);
        m_shm_fd = -1;
    }
}

void LockSurface::handle_frame_done(void* data, struct wl_callback* callback, uint32_t) {
    auto* surface = static_cast<LockSurface*>(data);
    wl_callback_destroy(callback);
    surface->m_frame_callback = nullptr;

    surface->render();
}

void LockSurface::schedule_frame() {
    if (!m_frame_callback && m_wl_surface) {
        m_frame_callback = wl_surface_frame(m_wl_surface);
        wl_callback_add_listener(m_frame_callback, &frame_listener, this);
    }
}

void LockSurface::draw_rounded_rectangle(cairo_t* cr, double x, double y, double w, double h, double r) {
    if (w <= 0 || h <= 0) return;
    r = std::min(r, std::min(w / 2.0, h / 2.0));
    double deg = M_PI / 180.0;

    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -90 * deg, 0 * deg);
    cairo_arc(cr, x + w - r, y + h - r, r, 0 * deg, 90 * deg);
    cairo_arc(cr, x + r, y + h - r, r, 90 * deg, 180 * deg);
    cairo_arc(cr, x + r, y + r, r, 180 * deg, 270 * deg);
    cairo_close_path(cr);
}

void LockSurface::draw(cairo_t* cr) {
    const auto& config = Config::get();
    const auto& primary = config.get_primary_color();
    const auto& bg = config.get_background_color();
    const auto& surface_col = config.get_surface_color();
    const auto& on_surface = config.get_on_surface_color();
    const auto& outline = config.get_outline_color();
    const auto& err_col = config.get_error_color();

    // 1. Dark Modern Background Gradient
    cairo_pattern_t* pat = cairo_pattern_create_linear(0, 0, 0, m_height);
    cairo_pattern_add_color_stop_rgba(pat, 0.0, bg.r * 1.2, bg.g * 1.2, bg.b * 1.2, 1.0);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, bg.r * 0.7, bg.g * 0.7, bg.b * 0.7, 1.0);
    cairo_set_source(cr, pat);
    cairo_paint(cr);
    cairo_pattern_destroy(pat);

    // 2. Subtle Glow behind center card
    double center_x = m_width / 2.0;
    double center_y = m_height / 2.0;

    cairo_pattern_t* glow = cairo_pattern_create_radial(center_x, center_y, 50, center_x, center_y, 400);
    cairo_pattern_add_color_stop_rgba(glow, 0.0, primary.r, primary.g, primary.b, 0.12);
    cairo_pattern_add_color_stop_rgba(glow, 1.0, 0, 0, 0, 0.0);
    cairo_set_source(cr, glow);
    cairo_paint(cr);
    cairo_pattern_destroy(glow);

    // 3. Time & Date
    time_t raw_time;
    time(&raw_time);
    struct tm* time_info = localtime(&raw_time);

    char time_str[64];
    strftime(time_str, sizeof(time_str), config.get_time_format().c_str(), time_info);

    char date_str[128];
    strftime(date_str, sizeof(date_str), config.get_date_format().c_str(), time_info);

    // Clock
    cairo_select_font_face(cr, config.get_font_family().c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 88.0);
    cairo_text_extents_t time_ext;
    cairo_text_extents(cr, time_str, &time_ext);

    double time_y = center_y - 120;
    cairo_set_source_rgba(cr, on_surface.r, on_surface.g, on_surface.b, 0.95);
    cairo_move_to(cr, center_x - (time_ext.width / 2.0) - time_ext.x_bearing, time_y);
    cairo_show_text(cr, time_str);

    // Date
    cairo_select_font_face(cr, config.get_font_family().c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 20.0);
    cairo_text_extents_t date_ext;
    cairo_text_extents(cr, date_str, &date_ext);

    double date_y = time_y + 40;
    cairo_set_source_rgba(cr, on_surface.r, on_surface.g, on_surface.b, 0.70);
    cairo_move_to(cr, center_x - (date_ext.width / 2.0) - date_ext.x_bearing, date_y);
    cairo_show_text(cr, date_str);

    // 4. User Profile Info
    std::string username = m_app->get_username();
    std::string user_display = username.empty() ? "User" : username;

    double user_y = date_y + 60;
    cairo_select_font_face(cr, config.get_font_family().c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 18.0);
    cairo_text_extents_t user_ext;
    cairo_text_extents(cr, user_display.c_str(), &user_ext);

    // User Avatar Circle
    double avatar_r = 18.0;
    double avatar_x = center_x - (user_ext.width / 2.0) - 20;
    cairo_arc(cr, avatar_x, user_y - 6, avatar_r, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, primary.r, primary.g, primary.b, 0.25);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, primary.r, primary.g, primary.b, 0.80);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    // User Initial
    std::string initial = user_display.substr(0, 1);
    cairo_text_extents_t init_ext;
    cairo_text_extents(cr, initial.c_str(), &init_ext);
    cairo_set_source_rgba(cr, primary.r, primary.g, primary.b, 1.0);
    cairo_move_to(cr, avatar_x - (init_ext.width / 2.0) - init_ext.x_bearing, user_y - 6 + (init_ext.height / 2.0));
    cairo_show_text(cr, initial.c_str());

    // Username Label
    cairo_set_source_rgba(cr, on_surface.r, on_surface.g, on_surface.b, 0.90);
    cairo_move_to(cr, avatar_x + avatar_r + 10, user_y);
    cairo_show_text(cr, user_display.c_str());

    // 5. Password Pill Input Field
    double pill_w = 320.0;
    double pill_h = 52.0;
    double shake = m_app->get_shake_offset();
    double pill_x = center_x - (pill_w / 2.0) + shake;
    double pill_y = user_y + 40.0;
    double pill_r = pill_h / 2.0;

    // Pill background
    draw_rounded_rectangle(cr, pill_x, pill_y, pill_w, pill_h, pill_r);
    cairo_set_source_rgba(cr, surface_col.r, surface_col.g, surface_col.b, 0.85);
    cairo_fill_preserve(cr);

    // Pill border (glowing primary or error red)
    if (m_app->is_auth_failed()) {
        cairo_set_source_rgba(cr, err_col.r, err_col.g, err_col.b, 0.95);
        cairo_set_line_width(cr, 2.0);
    } else if (m_app->is_verifying()) {
        cairo_set_source_rgba(cr, primary.r, primary.g, primary.b, 0.95);
        cairo_set_line_width(cr, 2.5);
    } else if (!m_app->get_password().empty()) {
        cairo_set_source_rgba(cr, primary.r, primary.g, primary.b, 0.90);
        cairo_set_line_width(cr, 2.0);
    } else {
        cairo_set_source_rgba(cr, outline.r, outline.g, outline.b, 0.40);
        cairo_set_line_width(cr, 1.5);
    }
    cairo_stroke(cr);

    // Content inside the pill
    const std::string& pwd = m_app->get_password();
    if (m_app->is_verifying()) {
        std::string vtext = "Authenticating...";
        cairo_set_font_size(cr, 15.0);
        cairo_text_extents_t vext;
        cairo_text_extents(cr, vtext.c_str(), &vext);
        cairo_set_source_rgba(cr, primary.r, primary.g, primary.b, 1.0);
        cairo_move_to(cr, pill_x + (pill_w / 2.0) - (vext.width / 2.0), pill_y + (pill_h / 2.0) + (vext.height / 2.0));
        cairo_show_text(cr, vtext.c_str());
    } else if (pwd.empty()) {
        std::string placeholder = "Enter password...";
        cairo_set_font_size(cr, 15.0);
        cairo_text_extents_t pext;
        cairo_text_extents(cr, placeholder.c_str(), &pext);
        cairo_set_source_rgba(cr, on_surface.r, on_surface.g, on_surface.b, 0.40);
        cairo_move_to(cr, pill_x + (pill_w / 2.0) - (pext.width / 2.0), pill_y + (pill_h / 2.0) + (pext.height / 2.0));
        cairo_show_text(cr, placeholder.c_str());
    } else {
        // Draw bullets
        size_t count = pwd.length();
        double dot_spacing = 14.0;
        double dot_radius = 4.5;
        double total_dots_w = (count - 1) * dot_spacing;
        double start_dot_x = pill_x + (pill_w / 2.0) - (total_dots_w / 2.0);

        for (size_t i = 0; i < count; ++i) {
            double dx = start_dot_x + i * dot_spacing;
            double dy = pill_y + (pill_h / 2.0);
            cairo_arc(cr, dx, dy, dot_radius, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, primary.r, primary.g, primary.b, 0.95);
            cairo_fill(cr);
        }
    }

    // Status Message below input
    if (m_app->is_auth_failed()) {
        std::string err_msg = "Incorrect Password. Try again.";
        cairo_set_font_size(cr, 14.0);
        cairo_text_extents_t eext;
        cairo_text_extents(cr, err_msg.c_str(), &eext);
        cairo_set_source_rgba(cr, err_col.r, err_col.g, err_col.b, 0.95);
        cairo_move_to(cr, center_x - (eext.width / 2.0), pill_y + pill_h + 30);
        cairo_show_text(cr, err_msg.c_str());
    }
}

void LockSurface::render() {
    if (!m_configured || !m_cairo_surface || !m_buffer || !m_wl_surface) return;

    cairo_t* cr = cairo_create(m_cairo_surface);
    draw(cr);
    cairo_destroy(cr);

    wl_surface_attach(m_wl_surface, m_buffer, 0, 0);
    wl_surface_damage_buffer(m_wl_surface, 0, 0, m_width, m_height);

    schedule_frame();
    wl_surface_commit(m_wl_surface);
}

} // namespace miqulock
