/* The smallest useful libnack program. */
#include <nack/nack.hpp>

#include <cstdio>

int main()
{
    nack::config config = nack::default_config();
    config.title = "libnack - hello";
    config.columns = 60;
    config.rows = 20;

    auto app = nack::app::try_create(config);
    if (!app) {
        std::fprintf(stderr, "nack::app failed: %.*s\n",
                     (int)nack::last_error().size(), nack::last_error().data());
        return 1;
    }

    int frame = 0;
    while (!app->should_close()) {
        while (auto event = app->poll()) {
            if (std::holds_alternative<nack::quit_event>(*event))
                app->set_should_close(true);
            if (auto *key = std::get_if<nack::key_event>(&*event))
                if (key->key == nack::key::escape)
                    app->set_should_close(true);
        }

        nack::clear();
        nack::draw_box(0, 0, 60, 20, nack::grey, nack::black, "libnack");
        nack::print(3, 3, nack::white, nack::black, "Hello from the console.");
        nack::print(3, 5, nack::yellow, nack::black, "@");
        nack::print(5, 5, nack::grey, nack::black, "<- that is you, probably");
        nack::print(3, 8, nack::cyan, nack::black, "frame {}", frame++);
        nack::print(3, 9, nack::cyan, nack::black, "{:.1f} ms",
                    app->delta_time() * 1000.0);
        nack::print(3, 17, nack::dark_grey, nack::black, "escape to quit");

        app->present();
    }

    return 0;
}
