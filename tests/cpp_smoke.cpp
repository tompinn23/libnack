/*
 * Exercises the C++ header against the running library.
 *
 * What matters here is not that libnack works - console_smoke.c settles that -
 * but that the wrapper is a faithful and leak-free face on it: handles free
 * themselves and free exactly once, a moved-from handle does not double free,
 * views and owners are interchangeable where a helper takes one, and the
 * variant carries the arm the event type says it does.
 */
#include <nack/nack.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

static int failures;

static void check(bool ok, const char *what)
{
    std::printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        ++failures;
}

/*
 * The point of console_view: one helper, either kind of console. If this
 * stopped compiling the wrapper would have lost the property the C API has.
 */
static void draw_panel(nack::console_view console, nack::color fg)
{
    console.draw_box(0, 0, 10, 4, fg, nack::black, "panel");
    console.print(1, 1, fg, nack::black, "hi");
}

int main()
{
    nack::config config = nack::default_config();
    config.title = "nack c++ smoke";
    config.columns = 40;
    config.rows = 20;
    config.vsync = false;

    auto app = nack::app::try_create(config);
    if (!app) {
        const char *why = ::nack_get_error();
        if (!why)
            why = "no reason given";
        if (std::strncmp(why, "cannot open a window", 20) == 0 ||
            std::strncmp(why, "cannot create a window", 22) == 0 ||
            std::strstr(why, "cannot create an OpenGL 3.3 context") != nullptr) {
            std::printf("skipping: %s\n", why);
            return 77;
        }
        std::fprintf(stderr, "nack::app failed: %s\n", why);
        return 1;
    }
    check(true, "app constructed");

    auto [columns, rows] = app->console().size();
    check(columns == 40 && rows == 20, "size comes back as a pair");

    /* Drawing through a view, on the root. */
    app->console().clear();
    app->console().put(2, 3, 'A', nack::red, nack::blue);
    nack::cell cell = app->console().at(2, 3);
    check(cell.glyph == 'A' && cell.fg.r == nack::red.r,
          "put and at round trip");

    check(app->console().print(0, 0, nack::white, nack::black, "hello") == 5,
          "print returns the cell count");

    /* A string_view that is not NUL-terminated must not read past its end. */
    {
        std::string backing = "abcdefgh";
        std::string_view slice(backing.data(), 3);
        int written = app->console().print(0, 6, nack::white, nack::black,
                                           slice);
        check(written == 3, "an unterminated string_view prints its length");
        check(app->console().at(2, 6).glyph == 'c' &&
              app->console().at(3, 6).glyph != 'd',
              "and stops where the view stops");
    }

    /* {fmt}: the format string is checked against the arguments at compile
     * time, and a runtime string still prints as it stands. */
    check(app->console().print(0, 7, nack::cyan, nack::black, "{}-{}", 42,
                               "x") == 4,
          "print formats with fmt");
    check(app->console().at(0, 7).glyph == '4' &&
          app->console().at(3, 7).glyph == 'x',
          "and the formatted text lands in the cells");
    {
        std::string runtime = "{not a placeholder}";
        int written = app->console().print(0, 8, nack::grey, nack::black,
                                           runtime);
        check(written == static_cast<int>(runtime.size()),
              "a runtime string is text, not a format string");
    }
    check(app->console().print(0, 9, nack::white, nack::black, "{:>6.2f}|",
                               3.14159) == 7,
          "fmt's own formatting reaches the console");
    /*
     * A format string that does not match its arguments cannot be tested from
     * here any more, because under C++20 it does not compile - which is the
     * point. tests/cpp_bad_format.cpp holds that case and the
     * cpp_format_rejected test builds it and expects the build to fail.
     */
    static_assert(__cplusplus >= 202002L,
                  "the C++ header wants C++20 so {fmt} checks format strings "
                  "at compile time");

    /* Owning consoles free themselves, and work with the same helper. */
    {
        nack::console panel(10, 4);
        draw_panel(panel, nack::green);
        draw_panel(app->console(), nack::grey);
        check(panel.at(0, 0).glyph == 0x250C,
              "a helper takes an owned console");
        check(app->console().at(0, 0).glyph == 0x250C,
              "and the root, through the same type");

        panel.blit_to(app->console(), 20, 15);
        check(app->console().at(21, 16).glyph == 'h', "blit through the view");

        /* Moving must not double free when both go out of scope. */
        nack::console moved = std::move(panel);
        check(!panel && moved, "move leaves the source empty");
        check(moved.at(0, 0).glyph == 0x250C, "and the target usable");
    }
    check(true, "consoles freed without a double free");

    /* Many consoles created and destroyed: a leak here shows under ASan. */
    {
        std::vector<nack::console> consoles;
        for (int i = 0; i < 32; ++i)
            consoles.emplace_back(4, 4);
        consoles.clear();
        check(true, "32 consoles created and destroyed");
    }

    check(!nack::console::try_create(0, 0).has_value(),
          "try_create reports a bad size rather than throwing");

    /* Modifiers are a real bitmask now, not a bare integer. */
    {
        nack::mod set = nack::mod::shift | nack::mod::ctrl;
        check(nack::holds(set, nack::mod::shift), "holds finds a set modifier");
        check(!nack::holds(set, nack::mod::alt), "and misses an unset one");
        check(nack::holds(set, nack::mod::shift | nack::mod::ctrl),
              "and requires every modifier asked for");
        check(!nack::any(nack::mod::none), "none is empty");
    }

    check(std::strcmp(nack::key_name(nack::key::escape), "Escape") == 0,
          "keys keep their names");

    /* The event variant must carry the arm the type says it does. */
    app->wake();
    {
        bool woke = false;
        for (int i = 0; i < 32 && !woke; ++i) {
            auto ev = app->wait_for(0.3);
            if (!ev)
                break;
            if (std::holds_alternative<nack::wakeup_event>(*ev))
                woke = true;
        }
        check(woke, "wakeup arrives as the wakeup_event alternative");
    }

    {
        /* Built by hand rather than waited for: input cannot be synthesised. */
        ::nack_event raw{};
        raw.type = NACK_EVENT_KEY_DOWN;
        raw.data.key.key = NACK_KEY_ESCAPE;
        raw.data.key.mods = NACK_MOD_SHIFT | NACK_MOD_CTRL;
        raw.data.key.repeat = true;
        auto ev = nack::detail::to_event(raw);
        check(ev && std::holds_alternative<nack::key_event>(*ev),
              "a key event becomes key_event");
        const auto *key = std::get_if<nack::key_event>(&*ev);
        check(key && key->key == nack::key::escape && key->repeat &&
              nack::holds(key->mods, nack::mod::shift | nack::mod::ctrl),
              "with its key, mods and repeat intact");

        raw = ::nack_event{};
        raw.type = NACK_EVENT_TEXT;
        std::memcpy(raw.data.text.utf8, "\xE2\x94\x80", 4);   /* with the NUL */
        ev = nack::detail::to_event(raw);
        const auto *text = std::get_if<nack::text_event>(&*ev);
        check(text && text->text() == "\xE2\x94\x80",
              "text arrives as a view into the event");

        raw = ::nack_event{};
        raw.type = NACK_EVENT_BLUR;
        ev = nack::detail::to_event(raw);
        const auto *focus = std::get_if<nack::focus_event>(&*ev);
        check(focus && !focus->focused,
              "focus and blur collapse into one alternative");

        raw = ::nack_event{};
        raw.type = NACK_EVENT_NONE;
        check(!nack::detail::to_event(raw).has_value(),
              "an empty event yields nothing");
    }

    app->present();
    check(app->delta_time() >= 0.0, "delta time is sane");
    check(!app->should_close(), "not asked to close");
    app->set_title("renamed");
    check(true, "set_title survived");

    /* Tilesets are handles too. The built-in font is not one we own. */
    {
        auto size = nack::tileset::dimensions{ 0, 0, 0 };
        ::nack_tileset_size(::nack_get_font(), &size.width, &size.height,
                            &size.count);
        check(size.width == 8 && size.count == 256,
              "the built-in font is 256 8x8 tiles");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
