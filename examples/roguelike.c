/*
 * A small roguelike, to show the shape libnack is meant to take.
 *
 * Demonstrates the things a roguelike actually needs: a fixed console scaled
 * to the window, eight-way movement from both the keypad and the vi keys,
 * field of view with remembered terrain, a mouse that reports cells rather
 * than pixels, panels composed offscreen and blitted, and a blocking event
 * loop so a turn-based game uses no CPU while it waits for the player.
 */
#include "nack/nack.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

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
    struct nack_color colour;
};

static struct tile map[MAP_H][MAP_W];
static struct monster monsters[MAX_MONSTERS];
static int monster_count;
static int player_x, player_y, player_hp = 20, player_gold;
static int turn;

static char log_lines[LOG_H - 2][80];
static struct nack_color log_colours[LOG_H - 2];
static int log_count;

/* ------------------------------------------------------------------ */
/* Message log                                                        */
/* ------------------------------------------------------------------ */

static void logf_message(struct nack_color colour, const char *fmt, ...)
{
    va_list args;
    int i;

    if (log_count == LOG_H - 2) {
        for (i = 0; i < log_count - 1; ++i) {
            memcpy(log_lines[i], log_lines[i + 1], sizeof log_lines[0]);
            log_colours[i] = log_colours[i + 1];
        }
        log_count--;
    }

    va_start(args, fmt);
    vsnprintf(log_lines[log_count], sizeof log_lines[0], fmt, args);
    va_end(args);
    log_colours[log_count] = colour;
    log_count++;
}

/* ------------------------------------------------------------------ */
/* Map generation                                                     */
/* ------------------------------------------------------------------ */

static void carve_room(int x, int y, int w, int h)
{
    int ix, iy;
    for (iy = y; iy < y + h; ++iy)
        for (ix = x; ix < x + w; ++ix)
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

static void generate_map(void)
{
    int rooms[MAX_ROOMS][4];
    int room_count = 0, attempt;

    memset(map, 0, sizeof map);
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x)
            map[y][x].wall = true;

    for (attempt = 0; attempt < 200 && room_count < MAX_ROOMS; ++attempt) {
        int w = 5 + rand() % 8;
        int h = 3 + rand() % 5;
        int x = 1 + rand() % (MAP_W - w - 2);
        int y = 1 + rand() % (MAP_H - h - 2);
        int i;
        bool overlaps = false;

        for (i = 0; i < room_count; ++i) {
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
            struct monster *m = &monsters[monster_count++];
            m->x = rooms[i][0] + rand() % rooms[i][2];
            m->y = rooms[i][1] + rand() % rooms[i][3];
            m->alive = true;
            if (rand() % 3 == 0) {
                m->glyph = 'o'; m->hp = 4; m->colour = NACK_GREEN;
            } else if (rand() % 2 == 0) {
                m->glyph = 'r'; m->hp = 2; m->colour = NACK_BROWN;
            } else {
                m->glyph = 'k'; m->hp = 3; m->colour = NACK_MAGENTA;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Field of view                                                      */
/* ------------------------------------------------------------------ */

/* Ray casting: crude, but short and good enough to show the idea. */
static void update_fov(void)
{
    int angle;

    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x)
            map[y][x].visible = false;

    map[player_y][player_x].visible = true;
    map[player_y][player_x].seen = true;

    for (angle = 0; angle < 720; ++angle) {
        double radians = angle * 3.14159265358979 / 360.0;
        double dx = 0.0, dy = 0.0;
        double step_x = 0.0, step_y = 0.0;
        int i;

        /* cos and sin without pulling in math.h for two calls. */
        {
            double t = radians, s = t, term = t;
            for (i = 1; i < 8; ++i) {
                term *= -t * t / ((2 * i) * (2 * i + 1));
                s += term;
            }
            step_y = s;
            t = radians; term = 1.0; s = 1.0;
            for (i = 1; i < 8; ++i) {
                term *= -t * t / ((2 * i - 1) * (2 * i));
                s += term;
            }
            step_x = s;
        }

        dx = player_x + 0.5;
        dy = player_y + 0.5;
        for (i = 0; i < FOV_RADIUS; ++i) {
            int cx, cy;
            dx += step_x;
            dy += step_y;
            cx = (int)dx;
            cy = (int)dy;
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

static struct monster *monster_at(int x, int y)
{
    for (int i = 0; i < monster_count; ++i)
        if (monsters[i].alive && monsters[i].x == x && monsters[i].y == y)
            return &monsters[i];
    return NULL;
}

static void monsters_act(void)
{
    for (int i = 0; i < monster_count; ++i) {
        struct monster *m = &monsters[i];
        int dx, dy;

        if (!m->alive || !map[m->y][m->x].visible)
            continue;

        dx = (player_x > m->x) - (player_x < m->x);
        dy = (player_y > m->y) - (player_y < m->y);

        if (m->x + dx == player_x && m->y + dy == player_y) {
            player_hp--;
            logf_message(NACK_RED, "the %c bites you", m->glyph);
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
    struct monster *target;

    if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H)
        return;

    target = monster_at(nx, ny);
    if (target) {
        target->hp--;
        if (target->hp <= 0) {
            target->alive = false;
            player_gold += 5 + rand() % 10;
            logf_message(NACK_YELLOW, "you kill the %c", target->glyph);
        } else {
            logf_message(NACK_WHITE, "you hit the %c", target->glyph);
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

static void draw_map(struct nack_console *view, int mouse_x, int mouse_y)
{
    for (int y = 0; y < VIEW_H && y < MAP_H; ++y) {
        for (int x = 0; x < VIEW_W && x < MAP_W; ++x) {
            const struct tile *tile = &map[y][x];
            struct nack_color fg, bg = NACK_BLACK;
            uint32_t glyph;

            if (!tile->seen) {
                nack_put(view, x, y, ' ', NACK_BLACK, NACK_BLACK);
                continue;
            }

            /* Remembered but unlit terrain is dimmed rather than hidden,
             * which is the convention players expect. */
            if (tile->wall) {
                glyph = 0x2588;                       /* full block */
                fg = tile->visible ? NACK_RGB(120, 110, 95)
                                   : NACK_RGB(45, 42, 38);
            } else {
                glyph = 0x00B7;                       /* middle dot */
                fg = tile->visible ? NACK_RGB(90, 90, 80)
                                   : NACK_RGB(35, 35, 32);
            }

            if (x == mouse_x && y == mouse_y)
                bg = NACK_RGB(30, 40, 60);

            nack_put(view, x, y, glyph, fg, bg);
        }
    }

    for (int i = 0; i < monster_count; ++i) {
        const struct monster *m = &monsters[i];
        if (m->alive && map[m->y][m->x].visible && m->x < VIEW_W && m->y < VIEW_H)
            nack_put(view, m->x, m->y, (uint32_t)m->glyph, m->colour, NACK_BLACK);
    }

    nack_put(view, player_x, player_y, '@', NACK_WHITE, NACK_BLACK);
}

static void draw_sidebar(struct nack_console *panel)
{
    int bar, i;

    nack_clear_to(panel, NACK_WHITE, NACK_RGB(16, 16, 20));
    nack_draw_box(panel, 0, 0, SIDEBAR_W, VIEW_H, NACK_RGB(70, 70, 80),
                  NACK_RGB(16, 16, 20), "status");

    nack_printf(panel, 2, 2, NACK_WHITE, NACK_RGB(16, 16, 20), "turn %d", turn);
    nack_printf(panel, 2, 4, NACK_YELLOW, NACK_RGB(16, 16, 20),
                "gold %d", player_gold);

    nack_print(panel, 2, 6, NACK_WHITE, NACK_RGB(16, 16, 20), "hp");
    bar = player_hp > 0 ? player_hp : 0;
    for (i = 0; i < 16; ++i) {
        struct nack_color colour = i < bar ? NACK_RED : NACK_RGB(50, 25, 25);
        nack_put(panel, 5 + i, 6, 0x2588, colour, NACK_RGB(16, 16, 20));
    }

    nack_print(panel, 2, 9,  NACK_GREY, NACK_RGB(16, 16, 20), "move");
    nack_print(panel, 2, 10, NACK_DARK_GREY, NACK_RGB(16, 16, 20), "hjkl yubn");
    nack_print(panel, 2, 11, NACK_DARK_GREY, NACK_RGB(16, 16, 20), "arrows");
    nack_print(panel, 2, 12, NACK_DARK_GREY, NACK_RGB(16, 16, 20), "keypad");
    nack_print(panel, 2, 14, NACK_GREY, NACK_RGB(16, 16, 20), "r  new level");
    nack_print(panel, 2, 15, NACK_GREY, NACK_RGB(16, 16, 20), "f  fullscreen");
    nack_print(panel, 2, 16, NACK_GREY, NACK_RGB(16, 16, 20), "esc quit");

    {
        int alive = 0;
        for (i = 0; i < monster_count; ++i)
            if (monsters[i].alive) alive++;
        nack_printf(panel, 2, 18, NACK_GREEN, NACK_RGB(16, 16, 20),
                    "%d left", alive);
    }
}

/* ------------------------------------------------------------------ */

int main(void)
{
    struct nack_config config;
    struct nack_console *sidebar;
    bool dirty = true;
    int mouse_x = -1, mouse_y = -1;

    nack_config_defaults(&config);
    config.title = "libnack - roguelike";
    config.columns = CONSOLE_W;
    config.rows = CONSOLE_H;
    config.window_scale = 2;
    config.letterbox = NACK_RGB(12, 12, 14);

    if (!nack_init(&config)) {
        fprintf(stderr, "nack_init failed: %s\n", nack_get_error());
        return 1;
    }

    sidebar = nack_console_new(SIDEBAR_W, VIEW_H);
    if (!sidebar) {
        fprintf(stderr, "%s\n", nack_get_error());
        nack_shutdown();
        return 1;
    }

    srand(1234);
    generate_map();
    update_fov();
    logf_message(NACK_CYAN, "you descend into the dark");

    while (!nack_should_close()) {
        struct nack_event event;

        /*
         * Draw before waiting, or the first frame never appears: the wait
         * below blocks until the player does something, and on a fresh window
         * that could be a long time.
         */
        if (dirty) {
            dirty = false;

            nack_clear_to(NULL, NACK_WHITE, NACK_BLACK);
            draw_map(NULL, mouse_x, mouse_y);

            draw_sidebar(sidebar);
            nack_blit(sidebar, 0, 0, SIDEBAR_W, VIEW_H, NULL, VIEW_W, 0,
                      1.0f, 1.0f);

            nack_draw_box(NULL, 0, VIEW_H, CONSOLE_W, LOG_H,
                          NACK_RGB(70, 70, 80), NACK_BLACK, "log");
            for (int i = 0; i < log_count; ++i)
                nack_print(NULL, 2, VIEW_H + 1 + i, log_colours[i], NACK_BLACK,
                           log_lines[i]);

            nack_present();
        }

        /*
         * Turn based, so block rather than spin: this loop uses no CPU at all
         * while the player is thinking. A real-time game would call
         * nack_poll_event in a loop instead and present every frame.
         */
        if (nack_wait_event(&event)) {
            do {
                switch (event.type) {
                case NACK_EVENT_QUIT:
                    nack_set_should_close(true);
                    break;

                case NACK_EVENT_KEY_DOWN:
                    switch (event.data.key.key) {
                    /* vi keys, arrows and the keypad all move. */
                    case NACK_KEY_H: case NACK_KEY_LEFT:  case NACK_KEY_KP_4:
                        try_move(-1, 0); break;
                    case NACK_KEY_L: case NACK_KEY_RIGHT: case NACK_KEY_KP_6:
                        try_move(1, 0); break;
                    case NACK_KEY_K: case NACK_KEY_UP:    case NACK_KEY_KP_8:
                        try_move(0, -1); break;
                    case NACK_KEY_J: case NACK_KEY_DOWN:  case NACK_KEY_KP_2:
                        try_move(0, 1); break;
                    case NACK_KEY_Y: case NACK_KEY_KP_7: try_move(-1, -1); break;
                    case NACK_KEY_U: case NACK_KEY_KP_9: try_move(1, -1); break;
                    case NACK_KEY_B: case NACK_KEY_KP_1: try_move(-1, 1); break;
                    case NACK_KEY_N: case NACK_KEY_KP_3: try_move(1, 1); break;
                    case NACK_KEY_PERIOD: case NACK_KEY_KP_5:
                        turn++; monsters_act(); break;

                    case NACK_KEY_R:
                        generate_map();
                        update_fov();
                        logf_message(NACK_CYAN, "a new level unfolds");
                        break;
                    case NACK_KEY_F:
                        nack_set_fullscreen(!nack_is_fullscreen());
                        break;
                    case NACK_KEY_ESCAPE:
                        nack_set_should_close(true);
                        break;
                    default:
                        break;
                    }
                    dirty = true;
                    break;

                case NACK_EVENT_MOUSE_MOVE:
                    /* Already in cells: no pixel arithmetic in game code. */
                    mouse_x = event.data.mouse.x;
                    mouse_y = event.data.mouse.y;
                    dirty = true;
                    break;

                case NACK_EVENT_MOUSE_DOWN:
                    if (event.data.mouse.x < VIEW_W && event.data.mouse.y < VIEW_H)
                        logf_message(NACK_GREY, "you look at %d,%d",
                                     event.data.mouse.x, event.data.mouse.y);
                    dirty = true;
                    break;

                case NACK_EVENT_RESIZE:
                case NACK_EVENT_FOCUS:
                    dirty = true;
                    break;

                default:
                    break;
                }
            } while (nack_poll_event(&event));
        }

        if (player_hp <= 0 && !nack_should_close()) {
            logf_message(NACK_RED, "you die.");
            player_hp = 20;
            generate_map();
            update_fov();
        }

    }

    nack_console_free(sidebar);
    nack_shutdown();
    return 0;
}
