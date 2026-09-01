#pragma once

#include <wayland-client.h>
#include <cairo.h>
#include <string>
#include <memory>
#include <chrono>

struct ext_session_lock_surface_v1;

namespace miqulock {

class LockApp;

class LockSurface {
public:
    LockSurface(LockApp* app, struct wl_output* output);
    ~LockSurface();

    void init();
    void render();
    void schedule_frame();

    void set_lock_surface(struct ext_session_lock_surface_v1* lock_surface);
    struct ext_session_lock_surface_v1* get_lock_surface() const { return m_lock_surface; }
    struct wl_surface* get_wl_surface() const { return m_wl_surface; }
    struct wl_output* get_wl_output() const { return m_wl_output; }

    void handle_configure(uint32_t serial, uint32_t width, uint32_t height);
    static void handle_frame_done(void* data, struct wl_callback* callback, uint32_t time);

private:
    void create_shm_buffer();
    void destroy_shm_buffer();
    void draw(cairo_t* cr);
    void draw_rounded_rectangle(cairo_t* cr, double x, double y, double w, double h, double r);

    LockApp* m_app = nullptr;
    struct wl_output* m_wl_output = nullptr;
    struct wl_surface* m_wl_surface = nullptr;
    struct ext_session_lock_surface_v1* m_lock_surface = nullptr;
    struct wl_callback* m_frame_callback = nullptr;

    int m_width = 1920;
    int m_height = 1080;
    bool m_configured = false;

    // SHM & Cairo resources
    int m_shm_fd = -1;
    void* m_shm_data = nullptr;
    size_t m_shm_size = 0;
    struct wl_shm_pool* m_shm_pool = nullptr;
    struct wl_buffer* m_buffer = nullptr;
    cairo_surface_t* m_cairo_surface = nullptr;

    std::chrono::steady_clock::time_point m_last_frame_time;
};

} // namespace miqulock
