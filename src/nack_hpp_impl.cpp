/*
 * The bridge between the public C++ face and the engine underneath it.
 *
 * <nack/nack.hpp> only ever declares nack::app/console_view/console/tileset;
 * it cannot define their methods inline, because a definition would need to
 * see the engine's own vocabulary (nack_config, nack_event,
 * nack_key from src/nack_core.h) and that header is not installed - a
 * caller including only <nack/nack.hpp> would fail to find it. So the
 * declarations live in the public header and the bodies live here, in the
 * one file that includes both worlds and converts between them at the
 * seam.
 *
 * Every method here is a thin conversion in front of a call already living
 * in nack_api.cpp / nack_console.cpp / nack_tileset.cpp - the logic itself
 * did not move, only what name reaches it and from where.
 */
#include "console/nack_console_internal.h"

#include <nack/nack.hpp>

#include <cstdio>
#include <cstdlib>

namespace nack {

/* ------------------------------------------------------------------------ */
/* Errors                                                                    */
/* ------------------------------------------------------------------------ */

static std::string last_error_or(const char *fallback)
{
    const char *why = console_state.last_error();
    return why ? std::string(why) : std::string(fallback);
}

std::string_view last_error()
{
    const char *why = console_state.last_error();
    return why ? std::string_view(why) : std::string_view();
}

namespace detail {

void fail(const std::string &what)
{
#if NACK_HPP_EXCEPTIONS
    throw error(what);
#else
    std::fprintf(stderr, "nack: %s\n", what.c_str());
    std::abort();
#endif
}

}  // namespace detail

/* ------------------------------------------------------------------------ */
/* Type conversion at the seam                                               */
/* ------------------------------------------------------------------------ */

static nack_color to_core(color c) { return { c.r, c.g, c.b, c.a }; }
static color from_core(nack_color c) { return { c.r, c.g, c.b, c.a }; }

static nack_config to_core(const config &c)
{
    nack_config out;
    out.title = c.title;
    out.columns = c.columns;
    out.rows = c.rows;
    out.tileset = c.tileset;
    out.tile_width = c.tile_width;
    out.tile_height = c.tile_height;
    out.tileset_layout = static_cast<nack_tileset_layout>(c.tileset_layout);
    out.scaling = static_cast<nack_scaling>(c.scaling);
    out.letterbox = to_core(c.letterbox);
    out.vsync = c.vsync;
    out.resizable = c.resizable;
    out.fullscreen = c.fullscreen;
    out.auto_resize = c.auto_resize;
    out.window_scale = c.window_scale;
    return out;
}

static nack::mod mods_of(std::uint32_t raw)
{
    return static_cast<nack::mod>(raw);
}

namespace detail {

/*
 * Turns the engine's tagged union into the variant, reading only the arm the
 * type says is live. NACK_EVENT_NONE has no variant member and yields
 * nothing.
 *
 * External linkage and declared for tests/nack_hpp_test_hooks.h: it is what
 * lets cpp_smoke.cpp build a raw event by hand - input cannot be
 * synthesised through the real backends - and check the translation
 * directly, without that requiring a public place in <nack/nack.hpp> for
 * something no real caller ever constructs.
 */
std::optional<event> to_event(const nack_event &ev)
{
    switch (ev.type) {
    case NACK_EVENT_QUIT:
        return event{ quit_event{} };
    case NACK_EVENT_KEY_DOWN:
    case NACK_EVENT_KEY_UP: {
        key_event out;
        out.key = static_cast<nack::key>(ev.data.key.key);
        out.mods = mods_of(ev.data.key.mods);
        out.repeat = ev.data.key.repeat;
        return event{ out };
    }
    case NACK_EVENT_TEXT: {
        text_event out{};
        std::size_t i = 0;
        for (; i + 1 < sizeof out.utf8 && ev.data.text.utf8[i]; ++i)
            out.utf8[i] = ev.data.text.utf8[i];
        out.utf8[i] = '\0';
        return event{ out };
    }
    case NACK_EVENT_MOUSE_MOVE: {
        mouse_move_event out;
        out.x = ev.data.mouse.x;
        out.y = ev.data.mouse.y;
        out.px = ev.data.mouse.px;
        out.py = ev.data.mouse.py;
        out.dx = ev.data.mouse.dx;
        out.dy = ev.data.mouse.dy;
        out.mods = mods_of(ev.data.mouse.mods);
        return event{ out };
    }
    case NACK_EVENT_MOUSE_DOWN:
    case NACK_EVENT_MOUSE_UP: {
        mouse_button_event out;
        out.x = ev.data.mouse.x;
        out.y = ev.data.mouse.y;
        out.px = ev.data.mouse.px;
        out.py = ev.data.mouse.py;
        out.button = static_cast<nack::mouse_button>(ev.data.mouse.button);
        out.down = (ev.type == NACK_EVENT_MOUSE_DOWN);
        out.clicks = ev.data.mouse.clicks;
        out.mods = mods_of(ev.data.mouse.mods);
        return event{ out };
    }
    case NACK_EVENT_MOUSE_SCROLL: {
        scroll_event out;
        out.dx = ev.data.scroll.dx;
        out.dy = ev.data.scroll.dy;
        out.mods = mods_of(ev.data.scroll.mods);
        out.precise = ev.data.scroll.precise;
        return event{ out };
    }
    case NACK_EVENT_RESIZE:
        return event{ resize_event{ ev.data.resize.columns,
                                    ev.data.resize.rows } };
    case NACK_EVENT_FOCUS:
        return event{ focus_event{ true } };
    case NACK_EVENT_BLUR:
        return event{ focus_event{ false } };
    case NACK_EVENT_WAKEUP:
        return event{ wakeup_event{} };
    case NACK_EVENT_NONE:
    default:
        return std::nullopt;
    }
}

}  // namespace detail

/* A NUL right after a string_view's data means whatever put it there also
 * terminated the string, so the engine's utf8 functions can read it as one.
 * An empty view has no data() worth reading at all. */
static bool is_terminated(std::string_view text)
{
    return !text.empty() && text.data()[text.size()] == '\0';
}

/* ------------------------------------------------------------------------ */
/* Keys                                                                      */
/* ------------------------------------------------------------------------ */

std::string_view key_name(key which)
{
    return detail::key_name(static_cast<nack_key>(which));
}

/* ------------------------------------------------------------------------ */
/* Consoles                                                                  */
/* ------------------------------------------------------------------------ */

std::pair<int, int> console_view::size() const
{
    return { handle->columns, handle->rows };
}

void console_view::clear() const { handle->clear(); }

void console_view::clear(color fg, color bg) const
{
    handle->clear_to(to_core(fg), to_core(bg));
}

void console_view::put(int x, int y, std::uint32_t codepoint, color fg,
                       color bg) const
{
    handle->put(x, y, codepoint, to_core(fg), to_core(bg));
}

void console_view::put_tile(int x, int y, tileset &tiles, int index,
                            color tint, color bg) const
{
    handle->put_tile(x, y, tiles.get(), index, to_core(tint), to_core(bg));
}

void console_view::set_glyph(int x, int y, std::uint32_t codepoint) const
{
    handle->set_glyph(x, y, codepoint);
}

void console_view::set_fg(int x, int y, color fg) const
{
    handle->set_fg(x, y, to_core(fg));
}

void console_view::set_bg(int x, int y, color bg) const
{
    handle->set_bg(x, y, to_core(bg));
}

cell console_view::at(int x, int y) const
{
    nack_cell c = handle->get(x, y);
    return cell{ c.glyph, c.tileset, from_core(c.fg), from_core(c.bg) };
}

int console_view::print(int x, int y, color fg, color bg,
                        std::string_view text) const
{
    /*
     * console::print wants a NUL-terminated string. A string_view need not
     * be terminated, so anything that is not already gets copied - which
     * literals and std::strings avoid.
     */
    if (is_terminated(text))
        return handle->print(x, y, to_core(fg), to_core(bg), text.data());
    std::string owned(text);
    return handle->print(x, y, to_core(fg), to_core(bg), owned.c_str());
}

int console_view::print_wrapped(int x, int y, int width, int height,
                                color fg, color bg,
                                std::string_view text) const
{
    if (is_terminated(text))
        return handle->print_wrapped(x, y, width, height, to_core(fg),
                                     to_core(bg), text.data());
    std::string owned(text);
    return handle->print_wrapped(x, y, width, height, to_core(fg),
                                 to_core(bg), owned.c_str());
}

void console_view::fill(int x, int y, int width, int height,
                        std::uint32_t codepoint, color fg, color bg) const
{
    handle->fill(x, y, width, height, codepoint, to_core(fg), to_core(bg));
}

void console_view::draw_box(int x, int y, int width, int height, color fg,
                            color bg, const char *title) const
{
    handle->draw_box(x, y, width, height, to_core(fg), to_core(bg), title);
}

void console_view::blit_to(console_view dst, int dst_x, int dst_y, int src_x,
                          int src_y, int width, int height, float fg_alpha,
                          float bg_alpha) const
{
    handle->blit_to(dst.get(), src_x, src_y, width, height, dst_x, dst_y,
                    fg_alpha, bg_alpha);
}

console::console(int columns, int rows)
{
    handle = nack_console::create(columns, rows);
    if (!handle)
        detail::fail(last_error_or("cannot create a console"));
}

std::optional<console> console::try_create(int columns, int rows)
{
    ::nack_console *raw = nack_console::create(columns, rows);
    if (!raw)
        return std::nullopt;
    return console(raw);
}

console &console::operator=(console &&other) noexcept
{
    if (this != &other) {
        nack_console::destroy(handle);
        handle = std::exchange(other.handle, nullptr);
    }
    return *this;
}

console::~console() { nack_console::destroy(handle); }

bool console::resize(int columns, int rows)
{
    return handle->resize(columns, rows);
}

/* ------------------------------------------------------------------------ */
/* Tilesets                                                                  */
/* ------------------------------------------------------------------------ */

tileset::tileset(const char *path, int tile_width, int tile_height,
                 nack::layout arrangement)
{
    handle = nack_tileset::load(
        path, tile_width, tile_height,
        static_cast<nack_tileset_layout>(arrangement));
    if (!handle)
        detail::fail(last_error_or("cannot load a tileset"));
}

tileset::tileset(const void *data, std::size_t size, int tile_width,
                 int tile_height, nack::layout arrangement)
{
    handle = nack_tileset::load_memory(
        data, size, tile_width, tile_height,
        static_cast<nack_tileset_layout>(arrangement));
    if (!handle)
        detail::fail(last_error_or("cannot load a tileset"));
}

tileset &tileset::operator=(tileset &&other) noexcept
{
    if (this != &other) {
        nack_tileset::destroy(handle);
        handle = std::exchange(other.handle, nullptr);
    }
    return *this;
}

tileset::~tileset() { nack_tileset::destroy(handle); }

tileset::dimensions tileset::size() const
{
    return { handle->tile_width, handle->tile_height, handle->count };
}

bool tileset::map(std::uint32_t codepoint, int index)
{
    return handle->map(codepoint, index);
}

bool tileset::map_range(std::uint32_t first, std::uint32_t last,
                        int first_index)
{
    return handle->map_range(first, last, first_index);
}

/* ------------------------------------------------------------------------ */
/* The application                                                           */
/* ------------------------------------------------------------------------ */

console_view root() { return console_view(console_state.root); }

app::app(const config &settings)
{
    nack_config cfg = to_core(settings);
    if (!console_state.init(&cfg))
        detail::fail(last_error_or("cannot start libnack"));
    active = true;
}

std::optional<app> app::try_create(const config &settings)
{
    nack_config cfg = to_core(settings);
    if (!console_state.init(&cfg))
        return std::nullopt;
    return app(adopt{});
}

app &app::operator=(app &&other) noexcept
{
    if (this != &other) {
        if (active)
            console_state.shutdown();
        active = std::exchange(other.active, false);
    }
    return *this;
}

app::~app()
{
    if (active)
        console_state.shutdown();
}

void app::present() const { console_state.present(); }

bool app::should_close() const { return console_state.should_close(); }

void app::set_should_close(bool value) const
{
    console_state.set_should_close(value);
}

double app::time() const { return console_state.time(); }
double app::delta_time() const { return console_state.delta_time(); }

std::optional<event> app::poll() const
{
    nack_event ev;
    while (console_state.poll_event(&ev)) {
        if (auto out = detail::to_event(ev))
            return out;
    }
    return std::nullopt;
}

std::optional<event> app::wait() const
{
    nack_event ev;
    while (console_state.wait_event(&ev)) {
        if (auto out = detail::to_event(ev))
            return out;
    }
    return std::nullopt;
}

std::optional<event> app::wait_for(double seconds) const
{
    nack_event ev;
    while (console_state.wait_event_timeout(&ev, seconds)) {
        if (auto out = detail::to_event(ev))
            return out;
    }
    return std::nullopt;
}

void app::wake() const { console_state.wakeup(); }

bool app::key_down(nack::key which) const
{
    return console_state.key_down(static_cast<nack_key>(which));
}

nack::mod app::mods() const
{
    return static_cast<nack::mod>(console_state.mods());
}

bool app::mouse_down(nack::mouse_button button) const
{
    return console_state.mouse_down(static_cast<int>(button));
}

std::pair<int, int> app::mouse_cell() const
{
    int x = 0, y = 0;
    console_state.mouse_cell(&x, &y);
    return { x, y };
}

void app::set_title(const char *title) const { console_state.set_title(title); }

void app::set_fullscreen(bool on) const { console_state.set_fullscreen(on); }

bool app::fullscreen() const { return console_state.is_fullscreen(); }

void app::set_vsync(bool on) const { console_state.set_vsync(on); }

void app::set_font(nack::tileset &tiles) const
{
    console_state.set_font(tiles.get());
}

bool app::set_clipboard(const char *utf8) const
{
    return console_state.clipboard_set(utf8);
}

std::string app::clipboard() const
{
    const char *text = console_state.clipboard_get();
    return text ? std::string(text) : std::string();
}

}  // namespace nack
