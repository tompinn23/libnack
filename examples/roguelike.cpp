/*
 * A small roguelike, to show the shape libnack is meant to take.
 *
 * Demonstrates the things a roguelike actually needs: a fixed console scaled
 * to the window, eight-way movement from both the keypad and the vi keys,
 * field of view with remembered terrain, a mouse that reports cells rather
 * than pixels, panels composed offscreen and blitted, and a blocking event
 * loop so a turn-based game uses no CPU while it waits for the player.
 */
#include <nack/nack.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#define MAP_W 60
#define MAP_H 36
#define VIEW_W 60
#define VIEW_H 36
#define SIDEBAR_W 22
#define LOG_H 6

#define CONSOLE_W (VIEW_W + SIDEBAR_W)
#define CONSOLE_H (VIEW_H + LOG_H)

#define FOV_RADIUS 8
#define MAX_ROOMS 14
#define MAX_MONSTERS 24

struct tile {
    bool wall;
    bool visible;
    bool seen;
};

struct monster {
    int x, y;
    int hp;
    char glyph;
    bool alive;
    nack::color colour;
};

static tile map[MAP_H][MAP_W];
static monster monsters[MAX_MONSTERS];
static int monster_count;
static int player_x, player_y, player_hp = 20, player_gold;
static int turn;

static std::string log_lines[LOG_H - 2];
static nack::color log_colours[LOG_H - 2];
static int log_count;

/* ------------------------------------------------------------------ */
/* Message log                                                        */
/* ------------------------------------------------------------------ */

template <class... Args>
static void logf_message(nack::color colour, fmt::format_string<Args...> spec,
                         Args &&...args)
{
    if (log_count == LOG_H - 2) {
        for (int i = 0; i < log_count - 1; ++i) {
            log_lines[i] = log_lines[i + 1];
            log_colours[i] = log_colours[i + 1];
        }
        log_count--;
    }

    log_lines[log_count] = fmt::format(spec, std::forward<Args>(args)...);
    log_colours[log_count] = colour;
    log_count++;
}

/* ------------------------------------------------------------------ */
/* Map generation                                                     */
/* ------------------------------------------------------------------ */

static void carve_room(int x, int y, int w, int h)
{
    for (int iy = y; iy < y + h; ++iy)
        for (int ix = x; ix < x + w; ++ix)
            if (ix > 0 && iy > 0 && ix < MAP_W - 1 && iy < MAP_H - 1)
                map[iy][ix].wall = false;
}

static void carve_corridor(int x0, int y0, int x1, int y1)
{
    int x = x0, y = y0;
    while (x != x1) { map[y][x].wall = false; x += (x1 > x) ? 1 : -1; }
    while (y != y1) { map[y][x].wall = false; y += (y1 > y) ? 1 : -1; }
    map[y][x].wall = false;
}

static void generate_map()
{
    int rooms[MAX_ROOMS][4];
    int room_count = 0, attempt;

    for (auto &row : map)
        for (auto &t : row)
            t = tile{ true, false, false };

    for (attempt = 0; attempt < 200 && room_count < MAX_ROOMS; ++attempt) {
        int w = 5 + rand() % 8;
        int h = 3 + rand() % 5;
        int x = 1 + rand() % (MAP_W - w - 2);
        int y = 1 + rand() % (MAP_H - h - 2);
        bool overlaps = false;

        for (int i = 0; i < room_count; ++i) {
            if (x < rooms[i][0] + rooms[i][2] + 1 &&
                x + w + 1 > rooms[i][0] &&
                y < rooms[i][1] + rooms[i][3] + 1 &&
                y + h + 1 > rooms[i][1]) {
                overlaps = true;
                break;
            }
        }
        if (overlaps)
            continue;

        carve_room(x, y, w, h);
        if (room_count > 0)
            carve_corridor(rooms[room_count - 1][0] + rooms[room_count - 1][2] / 2,
                           rooms[room_count - 1][1] + rooms[room_count - 1][3] / 2,
                           x + w / 2, y + h / 2);
        rooms[room_count][0] = x; rooms[room_count][1] = y;
        rooms[room_count][2] = w; rooms[room_count][3] = h;
        room_count++;
    }

    player_x = rooms[0][0] + rooms[0][2] / 2;
    player_y = rooms[0][1] + rooms[0][3] / 2;

    monster_count = 0;
    for (int i = 1; i < room_count && monster_count < MAX_MONSTERS; ++i) {
        int n = 1 + rand() % 2;
        while (n-- > 0 && monster_count < MAX_MONSTERS) {
            monster *m = &monsters[monster_count++];
            m->x = rooms[i][0] + rand() % rooms[i][2];
            m->y = rooms[i][1] + rand() % rooms[i][3];
            m->alive = true;
            if (rand() % 3 == 0) {
                m->glyph = 'o'; m->hp = 4; m->colour = nack::green;
            } else if (rand() % 2 == 0) {
                m->glyph = 'r'; m->hp = 2; m->colour = nack::brown;
            } else {
                m->glyph = 'k'; m->hp = 3; m->colour = nack::magenta;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Field of view                                                      */
/* ------------------------------------------------------------------ */

/* Ray casting: crude, but short and good enough to show the idea. */
static void update_fov()
{
    for (auto &row : map)
        for (auto &t : row)
            t.visible = false;

    map[player_y][player_x].visible = true;
    map[player_y][player_x].seen = true;

    for (int angle = 0; angle < 720; ++angle) {
        double radians = angle * 3.14159265358979 / 360.0;
        double step_x, step_y;

        /* cos and sin without pulling in math.h for two calls. */
        {
            double t = radians, s = t, term = t;
            for (int i = 1; i < 8; ++i) {
                term *= -t * t / ((2 * i) * (2 * i + 1));
                s += term;
            }
            step_y = s;
            t = radians; term = 1.0; s = 1.0;
            for (int i = 1; i < 8; ++i) {
                term *= -t * t / ((2 * i - 1) * (2 * i));
                s += term;
            }
            step_x = s;
        }

        double dx = player_x + 0.5;
        double dy = player_y + 0.5;
        for (int i = 0; i < FOV_RADIUS; ++i) {
            dx += step_x;
            dy += step_y;
            int cx = (int)dx;
            int cy = (int)dy;
            if (cx < 0 || cy < 0 || cx >= MAP_W || cy >= MAP_H)
                break;
            map[cy][cx].visible = true;
            map[cy][cx].seen = true;
            if (map[cy][cx].wall)
                break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Turns                                                              */
/* ------------------------------------------------------------------ */

static monster *monster_at(int x, int y)
{
    for (int i = 0; i < monster_count; ++i)
        if (monsters[i].alive && monsters[i].x == x && monsters[i].y == y)
            return &monsters[i];
    return nullptr;
}

static void monsters_act()
{
    for (int i = 0; i < monster_count; ++i) {
        monster *m = &monsters[i];

        if (!m->alive || !map[m->y][m->x].visible)
            continue;

        int dx = (player_x > m->x) - (player_x < m->x);
        int dy = (player_y > m->y) - (player_y < m->y);

        if (m->x + dx == player_x && m->y + dy == player_y) {
            player_hp--;
            logf_message(nack::red, "the {} bites you", m->glyph);
            continue;
        }
        if (!map[m->y + dy][m->x + dx].wall && !monster_at(m->x + dx, m->y + dy)) {
            m->x += dx;
            m->y += dy;
        }
    }
}

static void try_move(int dx, int dy)
{
    int nx = player_x + dx, ny = player_y + dy;

    if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H)
        return;

    if (monster *target = monster_at(nx, ny)) {
        target->hp--;
        if (target->hp <= 0) {
            target->alive = false;
            player_gold += 5 + rand() % 10;
            logf_message(nack::yellow, "you kill the {}", target->glyph);
        } else {
            logf_message(nack::white, "you hit the {}", target->glyph);
        }
    } else if (map[ny][nx].wall) {
        return;                      /* a bump into a wall costs no turn */
    } else {
        player_x = nx;
        player_y = ny;
    }

    turn++;
    update_fov();
    monsters_act();
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

static void draw_map(nack::console_view view, int mouse_x, int mouse_y)
{
    for (int y = 0; y < VIEW_H && y < MAP_H; ++y) {
        for (int x = 0; x < VIEW_W && x < MAP_W; ++x) {
            const tile &t = map[y][x];
            nack::color fg, bg = nack::black;
            std::uint32_t glyph;

            if (!t.seen) {
                view.put(x, y, ' ', nack::black, nack::black);
                continue;
            }

            /* Remembered but unlit terrain is dimmed rather than hidden,
             * which is the convention players expect. */
            if (t.wall) {
                glyph = 0x2588;                       /* full block */
                fg = t.visible ? nack::rgb(120, 110, 95)
                               : nack::rgb(45, 42, 38);
            } else {
                glyph = 0x00B7;                       /* middle dot */
                fg = t.visible ? nack::rgb(90, 90, 80)
                               : nack::rgb(35, 35, 32);
            }

            if (x == mouse_x && y == mouse_y)
                bg = nack::rgb(30, 40, 60);

            view.put(x, y, glyph, fg, bg);
        }
    }

    for (int i = 0; i < monster_count; ++i) {
        const monster &m = monsters[i];
        if (m.alive && map[m.y][m.x].visible && m.x < VIEW_W && m.y < VIEW_H)
            view.put(m.x, m.y, (std::uint32_t)m.glyph, m.colour, nack::black);
    }

    view.put(player_x, player_y, '@', nack::white, nack::black);
}

static void draw_sidebar(nack::console_view panel)
{
    const nack::color panel_bg = nack::rgb(16, 16, 20);

    panel.clear(nack::white, panel_bg);
    panel.draw_box(0, 0, SIDEBAR_W, VIEW_H, nack::rgb(70, 70, 80), panel_bg,
                   "status");

    panel.print(2, 2, nack::white, panel_bg, "turn {}", turn);
    panel.print(2, 4, nack::yellow, panel_bg, "gold {}", player_gold);

    panel.print(2, 6, nack::white, panel_bg, "hp");
    int bar = player_hp > 0 ? player_hp : 0;
    for (int i = 0; i < 16; ++i) {
        nack::color colour = i < bar ? nack::red : nack::rgb(50, 25, 25);
        panel.put(5 + i, 6, 0x2588, colour, panel_bg);
    }

    panel.print(2, 9,  nack::grey, panel_bg, "move");
    panel.print(2, 10, nack::dark_grey, panel_bg, "hjkl yubn");
    panel.print(2, 11, nack::dark_grey, panel_bg, "arrows");
    panel.print(2, 12, nack::dark_grey, panel_bg, "keypad");
    panel.print(2, 14, nack::grey, panel_bg, "r  new level");
    panel.print(2, 15, nack::grey, panel_bg, "f  fullscreen");
    panel.print(2, 16, nack::grey, panel_bg, "esc quit");

    int alive = 0;
    for (int i = 0; i < monster_count; ++i)
        if (monsters[i].alive) alive++;
    panel.print(2, 18, nack::green, panel_bg, "{} left", alive);
}

/* ------------------------------------------------------------------ */

int main()
{
    nack::config config = nack::default_config();
    config.title = "libnack - roguelike";
    config.columns = CONSOLE_W;
    config.rows = CONSOLE_H;
    config.window_scale = 2;
    config.letterbox = nack::rgb(12, 12, 14);

    auto app = nack::app::try_create(config);
    if (!app) {
        std::fprintf(stderr, "nack::app failed: %.*s\n",
                     (int)nack::last_error().size(), nack::last_error().data());
        return 1;
    }

    auto sidebar = nack::console::try_create(SIDEBAR_W, VIEW_H);
    if (!sidebar) {
        std::fprintf(stderr, "%.*s\n", (int)nack::last_error().size(),
                     nack::last_error().data());
        return 1;
    }

    bool dirty = true;
    int mouse_x = -1, mouse_y = -1;

    srand(1234);
    generate_map();
    update_fov();
    logf_message(nack::cyan, "you descend into the dark");

    auto handle_event = [&](const nack::event &ev) {
        if (std::holds_alternative<nack::quit_event>(ev)) {
            app->set_should_close(true);
        } else if (auto *key = std::get_if<nack::key_event>(&ev)) {
            switch (key->key) {
            /* vi keys, arrows and the keypad all move. */
            case nack::key::h: case nack::key::left:  case nack::key::kp_4:
                try_move(-1, 0); break;
            case nack::key::l: case nack::key::right: case nack::key::kp_6:
                try_move(1, 0); break;
            case nack::key::k: case nack::key::up:    case nack::key::kp_8:
                try_move(0, -1); break;
            case nack::key::j: case nack::key::down:  case nack::key::kp_2:
                try_move(0, 1); break;
            case nack::key::y: case nack::key::kp_7: try_move(-1, -1); break;
            case nack::key::u: case nack::key::kp_9: try_move(1, -1); break;
            case nack::key::b: case nack::key::kp_1: try_move(-1, 1); break;
            case nack::key::n: case nack::key::kp_3: try_move(1, 1); break;
            case nack::key::period: case nack::key::kp_5:
                turn++; monsters_act(); break;

            case nack::key::r:
                generate_map();
                update_fov();
                logf_message(nack::cyan, "a new level unfolds");
                break;
            case nack::key::f:
                app->set_fullscreen(!app->fullscreen());
                break;
            case nack::key::escape:
                app->set_should_close(true);
                break;
            default:
                break;
            }
            dirty = true;
        } else if (auto *move = std::get_if<nack::mouse_move_event>(&ev)) {
            /* Already in cells: no pixel arithmetic in game code. */
            mouse_x = move->x;
            mouse_y = move->y;
            dirty = true;
        } else if (auto *button = std::get_if<nack::mouse_button_event>(&ev)) {
            if (button->down && button->x < VIEW_W && button->y < VIEW_H)
                logf_message(nack::grey, "you look at {},{}", button->x,
                             button->y);
            dirty = true;
        } else if (std::holds_alternative<nack::resize_event>(ev) ||
                   std::holds_alternative<nack::focus_event>(ev)) {
            dirty = true;
        }
    };

    while (!app->should_close()) {
        /*
         * Draw before waiting, or the first frame never appears: the wait
         * below blocks until the player does something, and on a fresh window
         * that could be a long time.
         */
        if (dirty) {
            dirty = false;

            app->console().clear(nack::white, nack::black);
            draw_map(app->console(), mouse_x, mouse_y);

            draw_sidebar(*sidebar);
            sidebar->blit_to(app->console(), VIEW_W, 0, 0, 0, SIDEBAR_W,
                             VIEW_H, 1.0f, 1.0f);

            app->console().draw_box(0, VIEW_H, CONSOLE_W, LOG_H,
                                    nack::rgb(70, 70, 80), nack::black, "log");
            for (int i = 0; i < log_count; ++i)
                app->console().print(2, VIEW_H + 1 + i, log_colours[i],
                                     nack::black, log_lines[i]);

            app->present();
        }

        /*
         * Turn based, so block rather than spin: this loop uses no CPU at all
         * while the player is thinking. A real-time game would call
         * app->poll() in a loop instead and present every frame.
         */
        if (auto first = app->wait()) {
            handle_event(*first);
            while (auto next = app->poll())
                handle_event(*next);
        }

        if (player_hp <= 0 && !app->should_close()) {
            logf_message(nack::red, "you die.");
            player_hp = 20;
            generate_map();
            update_fov();
        }
    }

    return 0;
}
