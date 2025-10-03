/***************************************************************************
 *   Copyright © 2025 by Andrey Afletdinov <public.irkutsk@gmail.com>      *
 *                                                                         *
 *   Part of the InotifyWatcher                                            *
 *   https://github.com/AndreyBarmaley/inotify-watcher                     *
 *                                                                         *
 *   MIT License                                                           *
 *                                                                         *
 ***************************************************************************/

#include <boost/json.hpp>
#include <boost/algorithm/string/join.hpp>

#include <list>
#include <memory>
#include <fstream>
#include <iostream>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/systemd_sink.h"

#include "inotify_path.h"
#include "inotify_tools.h"

using namespace boost;
using InotifyPathPtr = std::unique_ptr<Inotify::Path>;

namespace Inotify {
    uint32_t jsonArrayToEvents(const json::array & ar) {
        std::vector<std::string> names;
        json::parse_into(names, json::serialize(ar));
        uint32_t events = 0;
        for(auto & name: names) {
            events |= Inotify::nameToMask(name);
        }
        return events;
    }
}

class InotifyJob : public Inotify::Path {
    asio::io_context & ioc_;
    json::object job_;

  protected:
    void continueEvent(const char* func, uint32_t event, const std::filesystem::path & path) {
        if(! job_.contains("command")) {
            return;
        }

        auto cmd = json::value_to<std::string>(job_["command"]);
        auto owner = job_.contains("owner") ? json::value_to<std::string>(job_["owner"]) : std::string{};
        std::list<std::string> args = { std::string(Inotify::maskToName(event)), path.native() };

        spdlog::debug("{}: run cmd: {}, args: [{}]", func, cmd, boost::algorithm::join(args, ","));
        asio::post(ioc_, std::bind(&System::runCommand, std::move(cmd), std::move(args), std::move(owner)));
    }

  public:
    InotifyJob(boost::asio::io_context & ioc, const std::filesystem::path & path, const json::object & json, uint32_t events)
        : Inotify::Path(ioc, path, events), ioc_(ioc), job_(json) {
    }

    void inOpenEvent(const std::filesystem::path & path, std::string name) override {
        spdlog::debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEvent(__FUNCTION__, IN_OPEN, name.size() ? path / name : path);
    }

    void inCreateEvent(const std::filesystem::path & path, std::string name) override {
        spdlog::debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEvent(__FUNCTION__, IN_CREATE, name.size() ? path / name : path);
    }

    void inAccessEvent(const std::filesystem::path & path, std::string name) override {
        spdlog::debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEvent(__FUNCTION__, IN_ACCESS, name.size() ? path / name : path);
    }

    void inModifyEvent(const std::filesystem::path & path, std::string name) override {
        spdlog::debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEvent(__FUNCTION__, IN_MODIFY, name.size() ? path / name : path);
    }

    void inAttribEvent(const std::filesystem::path & path, std::string name) override {
        spdlog::debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEvent(__FUNCTION__, IN_ATTRIB, name.size() ? path / name : path);
    }

    void inMoveEvent(const std::filesystem::path & path, std::string name, bool self) override {
        spdlog::debug("{}: path: {}, name: {}, self: {}", __FUNCTION__, path.native(), name, self);
        continueEvent(__FUNCTION__, self ? IN_MOVE_SELF : IN_MOVE, name.size() ? path / name : path);
    }

    void inCloseEvent(const std::filesystem::path & path, std::string name, bool write) override {
        spdlog::debug("{}: path: {}, name: {}, write: {}", __FUNCTION__, path.native(), name, write);

        if(job_.contains("name")) {
            if(name != json::value_to<std::string>(job_["name"])) {
                return;
            }
        }

        continueEvent(__FUNCTION__, write ? IN_CLOSE_WRITE : IN_CLOSE_NOWRITE, name.size() ? path / name : path);
    }

    void inDeleteEvent(const std::filesystem::path & path, std::string name, bool self) override {
        if(self) {
            spdlog::warn("{}: path: {}, name: {}, self: {}", __FUNCTION__, path.native(), name, self);
            cancelAsync();
        } else {
            spdlog::debug("{}: path: {}, name: {}, self: {}", __FUNCTION__, path.native(), name, self);
            continueEvent(__FUNCTION__, IN_DELETE, name.size() ? path / name : path);
        }
    }
};

class ServiceConfig : public Inotify::Path {
    asio::io_context & ioc_;
    json::object conf_;

    const std::filesystem::path filename_;
    std::list<InotifyPathPtr> jobs_;

  protected:
    void readConfig(const std::filesystem::path & path) {
        std::ifstream ifs{path};
        system::error_code ec;
        auto json = json::parse(std::string{std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()}, ec);

        if(ec) {
            spdlog::error("{}: {} error, code: {}, message: {}", __FUNCTION__, "json", ec.value(), ec.message());
            return;
        }

        if(! json.is_object()) {
            spdlog::error("{}: {} failed, not object", __FUNCTION__, "json");
            return;
        }

        conf_ = json.as_object();

        if(! conf_.contains("jobs") || ! conf_["jobs"].is_array()) {
            spdlog::error("{}: json skipped, tag not found: {}", __FUNCTION__, "jobs");
            return;
        }

        bool debug = conf_.contains("debug") ? json::value_to<bool>(conf_["debug"]) : false;
        spdlog::set_level(debug ? spdlog::level::debug : spdlog::level::info);

        asio::post(ioc_, std::bind(& ServiceConfig::parseJobs, this));
        spdlog::info("{}: success", __FUNCTION__);
    }

    void parseJobs(void) {
        jobs_.clear();

        for(auto & json : conf_["jobs"].get_array()) {
            if(! json.is_object()) {
                spdlog::warn("{}: job skipped, not object", __FUNCTION__);
            }

            auto job = json.get_object();

            if(! job.contains("path") || ! job["path"].is_string()) {
                spdlog::warn("{}: job skipped, tag not found: {}", __FUNCTION__, "path");
                continue;
            }

            auto path = std::filesystem::path{job["path"].get_string().c_str()};
            const uint32_t events_base = IN_CLOSE_WRITE|IN_DELETE_SELF;

            uint32_t events = events_base;
            if(job.contains("inotify") && job["inotify"].is_array()) {
                events = Inotify::jsonArrayToEvents(job["inotify"].as_array());
            }

            if(std::filesystem::is_regular_file(path)) {

                if(events == events_base) {
                    job["name"] = path.filename().native();
                    jobs_.emplace_back(std::make_unique<InotifyJob>(ioc_, path.parent_path(), job, events));
                    spdlog::info("{}: add job, path: {}, name: {}", "AddNotify", path.parent_path().native(), path.filename().native());
                } else {
                    jobs_.emplace_back(std::make_unique<InotifyJob>(ioc_, path, job, events));
                    spdlog::info("{}: add job, path: {}", "AddNotify", path.native());
                }

            } else if(std::filesystem::is_directory(path)) {
                bool recurse = job.contains("recursive") ? json::value_to<bool>(job["recursive"]) : false;
                
                if(recurse) {
                    auto dirs = System::readDir(path, recurse, ReadDirFilter::Dir);
                    for(const auto & dir : dirs) {
                        jobs_.emplace_back(std::make_unique<InotifyJob>(ioc_, dir, job, events));
                        spdlog::info("{}: add job, path: {}", "AddNotify", dir);
                    }
                } else {
                    jobs_.emplace_back(std::make_unique<InotifyJob>(ioc_, path, job, events));
                    spdlog::info("{}: add job, path: {}", "AddNotify", path.native());
                }
            } else {
                spdlog::warn("{}: job skipped, path not found: {}", __FUNCTION__, path.native());
            }
        }
    }

  public:
    ServiceConfig(boost::asio::io_context & ioc, const std::filesystem::path & path)
        : Inotify::Path(ioc, path.parent_path(), IN_CLOSE_WRITE), ioc_(ioc), filename_(path.filename()) {
        spdlog::info("found config: {}", path.native());
        readConfig(path);
    }

    void inCloseEvent(const std::filesystem::path & path, std::string name, bool write) override {
        // close_write event
        if(write && name == filename_.native()) {
            readConfig(path / name);
        }
    }

    void inDeleteEvent(const std::filesystem::path & path, std::string name, bool self) override {
        if(self) {
            spdlog::warn("{}: delete self event, path: {}", __FUNCTION__, path.c_str());
            cancelAsync();
        }
    }
};

int main(int argc, char** argv) {

    const char* path = "/etc/inotify_watcher/config.json";

    if(auto env = getenv("INOTIFY_SERVICE_CONF")) {
        path = env;
    }

    if(1 < argc) {
        if(0 == strcmp(argv[1], "--help")) {
            std::cout << "usage: " << argv[0] << " --config <json config>" << std::endl;
            return EXIT_SUCCESS;
        }

        if(0 == strcmp(argv[1], "--config")) {
            path = argv[2];
        }
    }

    if(! std::filesystem::is_regular_file(path)) {
        std::cerr << "config not found: " << path << std::endl;
        std::cout << "usage: " << argv[0] << " --config <json config>" << std::endl;
        return EXIT_FAILURE;
    }

    auto sink = std::make_shared<spdlog::sinks::systemd_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("inotify_watcher", sink);

    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);

    spdlog::set_pattern("%v");
    spdlog::set_level(spdlog::level::info);

    asio::io_context ctx{4};
    asio::signal_set signals(ctx, SIGINT, SIGTERM);

    signals.async_wait([&](const system::error_code & ec, int signal) {
        ctx.stop();
    });

    try {
        auto app = std::make_unique<ServiceConfig>(ctx, path);
        ctx.run();
    } catch(const std::exception & err) {
        std::cerr << "exception: " << err.what() << std::endl;
    }

    return 0;
}
