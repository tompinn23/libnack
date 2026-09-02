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
 * The point of ConsoleView: one helper, either kind of console. If this
 * stopped compiling the wrapper would have lost the property the C API has.
 */
static void drawPanel(nack::ConsoleView console, nack::Color fg)
{
    console.drawBox(0, 0, 10, 4, fg, nack::black, "panel");
    console.print(1, 1, "hi", fg, nack::black);
}

int main()
{
    nack::Config config = nack::defaultConfig();
    config.title = "nack c++ smoke";
    config.columns = 40;
    config.rows = 20;
    config.vsync = false;

    auto app = nack::App::tryCreate(config);
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
        std::fprintf(stderr, "nack::App failed: %s\n", why);
        return 1;
    }
    check(true, "App constructed");

    auto [columns, rows] = app->console().size();
    check(columns == 40 && rows == 20, "size comes back as a pair");

    /* Drawing through a view, on the root. */
    app->console().clear();
    app->console().put(2, 3, 'A', nack::red, nack::blue);
    nack::Cell cell = app->console().at(2, 3);
    check(cell.glyph == 'A' && cell.fg.r == nack::red.r,
          "put and at round trip");

    check(app->console().print(0, 0, "hello", nack::white, nack::black) == 5,
          "print returns the cell count");

    /* A string_view that is not NUL-terminated must not read past its end. */
    {
        std::string backing = "abcdefgh";
        std::string_view slice(backing.data(), 3);
        int written = app->console().print(0, 6, slice, nack::white,
                                           nack::black);
        check(written == 3, "an unterminated string_view prints its length");
        check(app->console().at(2, 6).glyph == 'c' &&
              app->console().at(3, 6).glyph != 'd',
              "and stops where the view stops");
    }

    check(app->console().printf(0, 7, nack::cyan, nack::black, "%d-%s", 42,
                                "x") == 4,
          "printf forwards to the C varargs");

    /* Owning consoles free themselves, and work with the same helper. */
    {
        nack::Console panel(10, 4);
        drawPanel(panel, nack::green);
        drawPanel(app->console(), nack::grey);
        check(panel.at(0, 0).glyph == 0x250C,
              "a helper takes an owned console");
        check(app->console().at(0, 0).glyph == 0x250C,
              "and the root, through the same type");

        panel.blitTo(app->console(), 20, 15);
        check(app->console().at(21, 16).glyph == 'h', "blit through the view");

        /* Moving must not double free when both go out of scope. */
        nack::Console moved = std::move(panel);
        check(!panel && moved, "move leaves the source empty");
        check(moved.at(0, 0).glyph == 0x250C, "and the target usable");
    }
    check(true, "consoles freed without a double free");

    /* Many consoles created and destroyed: a leak here shows under ASan. */
    {
        std::vector<nack::Console> consoles;
        for (int i = 0; i < 32; ++i)
            consoles.emplace_back(4, 4);
        consoles.clear();
        check(true, "32 consoles created and destroyed");
    }

    check(!nack::Console::tryCreate(0, 0).has_value(),
          "tryCreate reports a bad size rather than throwing");

    /* Modifiers are a real bitmask now, not a bare integer. */
    {
        nack::Mod set = nack::Mod::Shift | nack::Mod::Ctrl;
        check(nack::holds(set, nack::Mod::Shift), "holds finds a set modifier");
        check(!nack::holds(set, nack::Mod::Alt), "and misses an unset one");
        check(nack::holds(set, nack::Mod::Shift | nack::Mod::Ctrl),
              "and requires every modifier asked for");
        check(!nack::any(nack::Mod::None), "None is empty");
    }

    check(std::strcmp(nack::keyName(nack::Key::Escape), "Escape") == 0,
          "keys keep their names");

    /* The event variant must carry the arm the type says it does. */
    app->wakeup();
    {
        bool woke = false;
        for (int i = 0; i < 32 && !woke; ++i) {
            auto ev = app->waitFor(0.3);
            if (!ev)
                break;
            if (std::holds_alternative<nack::Wakeup>(*ev))
                woke = true;
        }
        check(woke, "wakeup arrives as the Wakeup alternative");
    }

    {
        /* Built by hand rather than waited for: input cannot be synthesised. */
        ::nack_event raw{};
        raw.type = NACK_EVENT_KEY_DOWN;
        raw.data.key.key = NACK_KEY_ESCAPE;
        raw.data.key.mods = NACK_MOD_SHIFT | NACK_MOD_CTRL;
        raw.data.key.repeat = true;
        auto ev = nack::detail::toEvent(raw);
        check(ev && std::holds_alternative<nack::KeyEvent>(*ev),
              "a key event becomes KeyEvent");
        const auto *key = std::get_if<nack::KeyEvent>(&*ev);
        check(key && key->key == nack::Key::Escape && key->repeat &&
              nack::holds(key->mods, nack::Mod::Shift | nack::Mod::Ctrl),
              "with its key, mods and repeat intact");

        raw = ::nack_event{};
        raw.type = NACK_EVENT_TEXT;
        std::strcpy(raw.data.text.utf8, "\xE2\x94\x80");
        ev = nack::detail::toEvent(raw);
        const auto *text = std::get_if<nack::TextEvent>(&*ev);
        check(text && text->text() == "\xE2\x94\x80",
              "text arrives as a view into the event");

        raw = ::nack_event{};
        raw.type = NACK_EVENT_BLUR;
        ev = nack::detail::toEvent(raw);
        const auto *focus = std::get_if<nack::FocusEvent>(&*ev);
        check(focus && !focus->focused,
              "focus and blur collapse into one alternative");

        raw = ::nack_event{};
        raw.type = NACK_EVENT_NONE;
        check(!nack::detail::toEvent(raw).has_value(),
              "an empty event yields nothing");
    }

    app->present();
    check(app->deltaTime() >= 0.0, "delta time is sane");
    check(!app->shouldClose(), "not asked to close");
    app->setTitle("renamed");
    check(true, "set_title survived");

    /* Tilesets are handles too. The built-in font is not one we own. */
    {
        auto size = nack::Tileset::Size{ 0, 0, 0 };
        ::nack_tileset_size(::nack_get_font(), &size.width, &size.height,
                            &size.count);
        check(size.width == 8 && size.count == 256,
              "the built-in font is 256 8x8 tiles");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
