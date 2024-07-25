#include "cfg.hpp"

#include <chrono>
#include <saucer/webview.hpp>

using namespace boost::ut;
using namespace boost::ut::literals;

void tests(saucer::webview &webview, bool thread)
{
    std::string last_url{};
    webview.on<saucer::web_event::url_changed>([&](const auto &url) { last_url = url; });

    bool dom_ready{false};
    webview.once<saucer::web_event::dom_ready>([&]() { dom_ready = true; });

    bool load_started{false};
    webview.once<saucer::web_event::load_started>([&]() { load_started = true; });

    bool load_finished{false};
    webview.once<saucer::web_event::load_finished>([&]() { load_finished = true; });

    "background"_test = [&]()
    {
        webview.set_background({255, 0, 0, 255});

        auto [r, g, b, a] = webview.background();
        expect(r == 255 && g == 0 && b == 0 && a == 255);
    };

    "url"_test = [&]()
    {
        webview.set_url("https://github.com/saucer/saucer");
        std::this_thread::sleep_for(std::chrono::seconds(5));

        expect(!thread || load_started);
        expect(!thread || load_finished);
        expect(!thread || dom_ready);

        expect(webview.url() == last_url) << webview.url() << ":" << last_url;
        expect(webview.url().find("github.com/saucer/saucer") != std::string::npos) << webview.url();
    };

    "page_title"_test = [&]()
    {
        webview.set_url("https://saucer.github.io/");
        std::this_thread::sleep_for(std::chrono::seconds(2));

        expect(!thread || webview.page_title() == "Saucer | Saucer");
    };

    "dev_tools"_test = [&]()
    {
        expect(not webview.dev_tools());
        webview.set_dev_tools(true);

        expect(webview.dev_tools());
        webview.set_dev_tools(false);

        expect(not webview.dev_tools());
    };

    "context_menu"_test = [&]()
    {
        webview.set_context_menu(true);
        expect(webview.context_menu());

        webview.set_context_menu(false);
        expect(not webview.context_menu());
    };

    "embed"_test = [&]()
    {
        std::string page = R"html(
        <!DOCTYPE html>
        <html>
            <head>
                <script>
                    location.href = "https://github.com/Curve";
                </script>
            </head>
        </html>
        )html";

        webview.embed({{"index.html", saucer::embedded_file{
                                          .content = saucer::make_stash(page),
                                          .mime    = "text/html",
                                      }}});

        webview.serve("index.html");
        std::this_thread::sleep_for(std::chrono::seconds(1));

        expect(!thread || webview.url().find("github.com/Curve") != std::string::npos);

        webview.clear_embedded("index.html");

        webview.serve("index.html");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        expect(!thread || webview.url().find("github.com/Curve") == std::string::npos);
    };

    "execute"_test = [&]()
    {
        webview.set_url("https://saucer.github.io/");
        std::this_thread::sleep_for(std::chrono::seconds(2));

        webview.execute("document.title = 'Execute Test'");
        std::this_thread::sleep_for(std::chrono::seconds(2));

        expect(!thread || webview.page_title() == "Execute Test");
    };

    "inject"_test = [&]()
    {
        webview.inject("location.href = 'https://isocpp.org'", saucer::load_time::creation);

        webview.set_url("https://cppreference.com/");
        std::this_thread::sleep_for(std::chrono::seconds(5));

        expect(!thread || webview.url().find("isocpp.org") != std::string::npos);
        webview.clear_scripts();

        webview.inject("document.title = 'Hi!'", saucer::load_time::ready);

        webview.set_url("https://cppreference.com/");
        std::this_thread::sleep_for(std::chrono::seconds(2));

        expect(!thread || webview.page_title() == "Hi!");
        expect(!thread || webview.url().find("cppreference.com") != std::string::npos);

        webview.clear_scripts();
    };

    "scheme"_test = [&]()
    {
        webview.handle_scheme("test",
                              [](const saucer::request &req) -> saucer::scheme_handler::result_type
                              {
                                  expect(req.url() == "test:/index.html");
                                  expect(req.method() == "GET");

                                  std::string html = R"html(
                                  <!DOCTYPE html>
                                  <html>
                                      <head>
                                        <title>Custom Scheme</title>
                                      </head>
                                  </html>
                                  )html";

                                  return saucer::response{
                                      .data = saucer::make_stash(html),
                                      .mime = "text/html",
                                  };
                              });

        webview.serve("index.html", "test");
        std::this_thread::sleep_for(std::chrono::seconds(1));

        expect(!thread || webview.page_title() == "Custom Scheme");
        webview.remove_scheme("test");
    };

    if (!thread)
    {
        webview.clear(saucer::web_event::dom_ready);
        webview.clear(saucer::web_event::load_started);
        webview.clear(saucer::web_event::load_finished);
    }

    webview.clear(saucer::web_event::url_changed);
}

suite<"webview"> webview_suite = []
{
    saucer::webview::register_scheme("test");
    saucer::webview webview{{.hardware_acceleration = false}};

    "from-main"_test = [&]()
    {
        tests(webview, false);
    };

    std::jthread thread{[&]()
                        {
                            std::this_thread::sleep_for(std::chrono::seconds(2));

                            "from-thread"_test = [&]()
                            {
                                tests(webview, true);
                            };

                            webview.close();
                        }};

    webview.show();
    webview.run();
};
