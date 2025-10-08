/***************************************************************************
 *   Copyright © 2025 by Andrey Afletdinov <public.irkutsk@gmail.com>      *
 *                                                                         *
 *   Part of the InotifyWatcher                                            *
 *   https://github.com/AndreyBarmaley/inotify-watcher                     *
 *                                                                         *
 *   MIT License                                                           *
 *                                                                         *
 ***************************************************************************/

#include <boost/algorithm/string/join.hpp>

#include <list>
#include <memory>
#include <fstream>
#include <iostream>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/syslog_sink.h"

#include "swe_json.h"
#include "inotify_path.h"
#include "inotify_tools.h"

using namespace boost;
using namespace SWE;

using InotifyPathPtr = std::unique_ptr<Inotify::Path>;

namespace Inotify {
    uint32_t jsonArrayToEvents(const JsonArray* ar) {
        uint32_t events = 0;
        for(auto & name: ar->toStdVector<std::string>()) {
            events |= Inotify::nameToMask(name);
        }
        return events;
    }
}

class InotifyJob : public Inotify::Path {
    asio::io_context & ioc_;
    JsonObject job_;

  protected:
    void continueEvent(const char* func, uint32_t event, const std::filesystem::path & path) {
        if(job_.hasKey("command")) {
    	    auto cmd = job_.getString("command");
    	    auto owner = job_.getString("owner", "");
    	    std::list<std::string> args = { std::string(Inotify::maskToName(event)), String::escaped(path.native(), true) };

    	    log->debug("{}: run cmd: {}, args: [{}]", func, cmd, boost::algorithm::join(args, ","));
    	    asio::post(ioc_, std::bind(&System::runCommand, std::move(cmd), std::move(args), std::move(owner)));
	}
    }

  public:
    InotifyJob(boost::asio::io_context & ioc, const std::filesystem::path & path, JsonObject && json, uint32_t events)
        : Inotify::Path(ioc, path, events), ioc_(ioc), job_(std::move(json)) {
    }

    void inOpenEvent(const std::filesystem::path & path, std::string name) override {
        log->debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEvent(__FUNCTION__, IN_OPEN, name.size() ? path / name : path);
    }

    void inCreateEvent(const std::filesystem::path & path, std::string name) override {
        log->debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEvent(__FUNCTION__, IN_CREATE, name.size() ? path / name : path);
    }

    void inAccessEvent(const std::filesystem::path & path, std::string name) override {
        log->debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEvent(__FUNCTION__, IN_ACCESS, name.size() ? path / name : path);
    }

    void inModifyEvent(const std::filesystem::path & path, std::string name) override {
        log->debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEvent(__FUNCTION__, IN_MODIFY, name.size() ? path / name : path);
    }

    void inAttribEvent(const std::filesystem::path & path, std::string name) override {
        log->debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEvent(__FUNCTION__, IN_ATTRIB, name.size() ? path / name : path);
    }

    void inMoveEvent(const std::filesystem::path & path, std::string name, bool self) override {
        log->debug("{}: path: {}, name: {}, self: {}", __FUNCTION__, path.native(), name, self);
        continueEvent(__FUNCTION__, self ? IN_MOVE_SELF : IN_MOVE, name.size() ? path / name : path);
    }

    void inCloseEvent(const std::filesystem::path & path, std::string name, bool write) override {
        log->debug("{}: path: {}, name: {}, write: {}", __FUNCTION__, path.native(), name, write);

        if(job_.hasKey("name")) {
            if(name != job_.getString("name")) {
                return;
            }
        }

        continueEvent(__FUNCTION__, write ? IN_CLOSE_WRITE : IN_CLOSE_NOWRITE, name.size() ? path / name : path);
    }

    void inDeleteEvent(const std::filesystem::path & path, std::string name, bool self) override {
        if(self) {
            log->warn("{}: path: {}, name: {}, self: {}", __FUNCTION__, path.native(), name, self);
            cancelAsync();
        } else {
            log->debug("{}: path: {}, name: {}, self: {}", __FUNCTION__, path.native(), name, self);
            continueEvent(__FUNCTION__, IN_DELETE, name.size() ? path / name : path);
        }
    }
};

class ServiceConfig : public Inotify::Path {
    asio::io_context & ioc_;
    JsonObject conf_;

    const std::filesystem::path filename_;
    std::list<InotifyPathPtr> jobs_;

  protected:
    void readConfig(const std::filesystem::path & path) {

	auto jc = JsonContentFile(path);

        if(! jc.isObject()) {
            log->error("{}: {} failed, not object", __FUNCTION__, "json");
            return;
        }

        conf_ = jc.toObject();

        if(! conf_.hasKey("jobs") || ! conf_.isArray("jobs")) {
            log->error("{}: json skipped, tag not found: {}", __FUNCTION__, "jobs");
            return;
        }

        bool debug = conf_.getBoolean("debug", false);
        log->set_level(debug ? spdlog::level::debug : spdlog::level::info);

        asio::post(ioc_, std::bind(& ServiceConfig::parseJobs, this));
        log->info("{}: success", __FUNCTION__);
    }

    void parseJobs(void) {
        jobs_.clear();

	const JsonArray* ja = conf_.getArray("jobs");

        for(int it = 0; it < ja->size(); ++it) {
	    auto jo = ja->getObject(it);

            if(! jo) {
                log->warn("{}: job skipped, not object", __FUNCTION__);
            }

	    auto job = *jo;
            if(! job.isString("path")) {
                log->warn("{}: job skipped, tag not found: {}", __FUNCTION__, "path");
                continue;
            }

            auto path = std::filesystem::path{job.getString("path")};
            const uint32_t events_base = IN_CLOSE_WRITE|IN_DELETE_SELF;

            uint32_t events = events_base;
            if(job.isArray("inotify")) {
                events = Inotify::jsonArrayToEvents(job.getArray("inotify"));
            }

            if(std::filesystem::is_regular_file(path)) {

                if(events == events_base) {
                    job.addString("name", path.filename().native());
                    jobs_.emplace_back(std::make_unique<InotifyJob>(ioc_, path.parent_path(), std::move(job), events));
                    log->info("{}: add job, path: {}, name: {}", "AddNotify", path.parent_path().native(), path.filename().native());
                } else {
                    jobs_.emplace_back(std::make_unique<InotifyJob>(ioc_, path, std::move(job), events));
                    log->info("{}: add job, path: {}", "AddNotify", path.native());
                }

            } else if(std::filesystem::is_directory(path)) {
                bool recurse = job.getBoolean("recursive", false);
                
                if(recurse) {
                    auto dirs = System::readDir(path, recurse, ReadDirFilter::Dir);
                    for(const auto & dir : dirs) {
                        jobs_.emplace_back(std::make_unique<InotifyJob>(ioc_, dir, std::move(job), events));
                        log->info("{}: add job, path: {}", "AddNotify", dir);
                    }
                } else {
                    jobs_.emplace_back(std::make_unique<InotifyJob>(ioc_, path, std::move(job), events));
                    log->info("{}: add job, path: {}", "AddNotify", path.native());
                }
            } else {
                log->warn("{}: job skipped, path not found: {}", __FUNCTION__, path.native());
            }
        }
    }

  public:
    ServiceConfig(boost::asio::io_context & ioc, const std::filesystem::path & path)
        : Inotify::Path(ioc, path.parent_path(), IN_CLOSE_WRITE), ioc_(ioc), filename_(path.filename()) {
        log->info("found config: {}", path.native());
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
            log->warn("{}: delete self event, path: {}", __FUNCTION__, path.c_str());
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

    auto sink = std::make_shared<spdlog::sinks::syslog_sink>("inotify_watcher");
    auto logger = std::make_shared<spdlog::logger>("inotify_watcher", sink);

    spdlog::register_logger(logger);

    logger->set_pattern("%v");
    logger->set_level(spdlog::level::info);

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
