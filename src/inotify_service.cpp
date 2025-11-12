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
#include <mutex>
#include <memory>
#include <fstream>
#include <iostream>
#include <functional>

#include <systemd/sd-daemon.h>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/systemd_sink.h"

#include "inotify_path.h"
#include "inotify_tools.h"

using namespace boost;
using InotifyPathPtr = std::unique_ptr<Inotify::Path>;
using ConfFileModifyEventCb = std::function<void(const std::filesystem::path &)>;
using ConfDirModifyEventCb = std::function<void(const std::filesystem::path &, uint32_t, uint64_t)>;
using JobContinueEventCb = std::function<void(const std::filesystem::path &, uint32_t, const json::object &, uint64_t)>;

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

    const uint32_t EVENTS_BASE = IN_CLOSE_WRITE|IN_DELETE_SELF;

    uint32_t jobToEvents(const json::object & job) {
        uint32_t events = EVENTS_BASE;

        if(job.contains("inotify") && job.at("inotify").is_array()) {
            events = jsonArrayToEvents(job.at("inotify").as_array());
        }

        return events;
    }
}

class InotifyJob : public Inotify::Path {
    json::object job_;
    JobContinueEventCb continueEventCb_;

  public:
    InotifyJob(boost::asio::io_context & ioc, const std::filesystem::path & path,
        const json::object & json, uint32_t events, JobContinueEventCb && func)
        : Inotify::Path(ioc, path, (events | IN_DELETE_SELF)), job_(json), continueEventCb_(std::move(func)) {
    }

    const json::object & getConf(void) const {
        return job_;
    }

    void inOpenEvent(const std::filesystem::path & path, std::string name) override {
        spdlog::debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEventCb_(name.size() ? path / name : path, IN_OPEN, job_, job_id());
    }

    void inCreateEvent(const std::filesystem::path & path, std::string name) override {
        spdlog::debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEventCb_(name.size() ? path / name : path, IN_CREATE, job_, job_id());
    }

    void inAccessEvent(const std::filesystem::path & path, std::string name) override {
        spdlog::debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEventCb_(name.size() ? path / name : path, IN_ACCESS, job_, job_id());
    }

    void inModifyEvent(const std::filesystem::path & path, std::string name) override {
        spdlog::debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEventCb_(name.size() ? path / name : path, IN_MODIFY, job_, job_id());
    }

    void inAttribEvent(const std::filesystem::path & path, std::string name) override {
        spdlog::debug("{}: path: {}, name: {}", __FUNCTION__, path.native(), name);
        continueEventCb_(name.size() ? path / name : path, IN_ATTRIB, job_, job_id());
    }

    void inMoveEvent(const std::filesystem::path & path, std::string name, bool self) override {
        spdlog::debug("{}: path: {}, name: {}, self: {}", __FUNCTION__, path.native(), name, self);
        continueEventCb_(name.size() ? path / name : path, self ? IN_MOVE_SELF : IN_MOVE, job_, job_id());
    }

    void inCloseEvent(const std::filesystem::path & path, std::string name, bool write) override {
        spdlog::debug("{}: path: {}, name: {}, write: {}", __FUNCTION__, path.native(), name, write);

        if(job_.contains("name")) {
            if(name != json::value_to<std::string>(job_["name"])) {
                return;
            }
        }

        continueEventCb_(name.size() ? path / name : path, write ? IN_CLOSE_WRITE : IN_CLOSE_NOWRITE, job_, job_id());
    }

    void inDeleteEvent(const std::filesystem::path & path, std::string name, bool self) override {
        if(self) {
            spdlog::warn("{}: path: {}, name: {}, self: {}", __FUNCTION__, path.native(), name, self);
            cancelAsync();
        } else {
            spdlog::debug("{}: path: {}, name: {}, self: {}", __FUNCTION__, path.native(), name, self);
        }

        // background
        asio::post(ioc_, std::bind(continueEventCb_, name.size() ? path / name : path, self ? IN_DELETE_SELF : IN_DELETE, job_, job_id()));
    }
};

class InotifyConfFile : public Inotify::Path {
    const std::filesystem::path filename_;
    ConfFileModifyEventCb confFileModifyEventCb_;

  public:
    InotifyConfFile(boost::asio::io_context & ioc, const std::filesystem::path & conf_path, ConfFileModifyEventCb && func)
        : Inotify::Path(ioc, conf_path.parent_path(), IN_CLOSE_WRITE|IN_DELETE|IN_DELETE_SELF),
            filename_(conf_path.filename()), confFileModifyEventCb_(std::move(func)) {
    }

    void inCloseEvent(const std::filesystem::path & path, std::string name, bool write) override {
        assert(write);
        if(name == filename_) {
            confFileModifyEventCb_(path / name);
        }
    }

    void inDeleteEvent(const std::filesystem::path & path, std::string name, bool self) override {
        if(self || name == filename_) {
            spdlog::warn("{}: path: {}, name: {}, self: {}", __FUNCTION__, path.native(), name, self);
            cancelAsync();
        }
    }
};

class InotifyConfDir : public Inotify::Path {
    ConfDirModifyEventCb confDirModifyEventCb_;

  public:
    InotifyConfDir(boost::asio::io_context & ioc, const std::filesystem::path & dir_path, ConfDirModifyEventCb && func)
        : Inotify::Path(ioc, dir_path, IN_CLOSE_WRITE|IN_DELETE|IN_DELETE_SELF),
            confDirModifyEventCb_(std::move(func)) {
    }

    void inCloseEvent(const std::filesystem::path & path, std::string name, bool write) override {
        assert(write);
        confDirModifyEventCb_(path / name, IN_CLOSE_WRITE, job_id());
    }

    void inDeleteEvent(const std::filesystem::path & path, std::string name, bool self) override {
        if(self) {
            spdlog::warn("{}: path: {}, name: {}, self: {}", __FUNCTION__, path.native(), name, self);
            cancelAsync();
        } else {
            assert(name.size());
            confDirModifyEventCb_(path / name, IN_DELETE, job_id());
        }
    }
};

class ServiceWatcher : private boost::noncopyable {
    asio::io_context & ioc_;
    asio::signal_set signals_;
    json::object conf_;

    const std::filesystem::path jobs_dir_;

    mutable std::mutex lock_;
    std::list<InotifyPathPtr> jobs_;

    std::unique_ptr<InotifyConfFile> conf_job_;
    std::unique_ptr<InotifyConfDir> dir_jobs_;

  protected:
    void confFileModifyEvent(const std::filesystem::path & path) {
        readConfig(path);
    }

    void confDirModifyEvent(const std::filesystem::path & path, uint32_t event, uint64_t job_id) {
        if(IN_CLOSE_WRITE == event || IN_DELETE == event) {
            std::scoped_lock guard{ lock_ };
            // job delete
            if(auto it = std::find_if(jobs_.begin(), jobs_.end(),
                [job_id](auto & ptr){ return ptr->job_id() == job_id; }); it != jobs_.end()) {
                spdlog::info("{}: remove job, id: {:016x}, path: {}", __FUNCTION__, (*it)->job_id(), (*it)->path().native());
                jobs_.erase(it);
            }

            if(IN_CLOSE_WRITE == event) {
                // job add
                loadFileJob(path);
            }
        }
    }

    void jobContinueEvent(const std::filesystem::path & path, uint32_t event, const json::object & job_conf, uint64_t job_id) {
        if(! job_conf.contains("command")) {
            return;
        }

        spdlog::debug("{}: event: {}", __FUNCTION__, Inotify::maskToName(event));

        if( Inotify::jobToEvents(job_conf) & event ) {
            auto cmd = json::value_to<std::string>(job_conf.at("command"));
            auto owner = job_conf.contains("owner") ? json::value_to<std::string>(job_conf.at("owner")) : std::string{};
            auto escaped = job_conf.contains("escaped") ? json::value_to<bool>(job_conf.at("escaped")) : false;
            std::list<std::string> args = { std::string(Inotify::maskToName(event)), String::quoted(path.native(), escaped) };

            spdlog::info("{}: run cmd: {}, args: [{}]", __FUNCTION__, cmd, boost::algorithm::join(args, ","));
            asio::post(ioc_, std::bind(&System::runCommand, std::move(cmd), std::move(args), std::move(owner)));
        }

        if(IN_DELETE_SELF == event) {
            std::scoped_lock guard{ lock_ };
            // job self delete
            if(auto it = std::find_if(jobs_.begin(), jobs_.end(),
                [job_id](auto & ptr){ return ptr->job_id() == job_id; }); it != jobs_.end()) {
                spdlog::info("{}: remove job, id: {:016x}, path: {}", __FUNCTION__, (*it)->job_id(), (*it)->path().native());
                jobs_.erase(it);
            }
        }

        if(IN_CREATE == event) {
            bool recurse = job_conf.contains("recursive") ? json::value_to<bool>(job_conf.at("recursive")) : false;
            if(std::filesystem::is_directory(path) && recurse) {
                auto new_conf = job_conf;
                new_conf["path"] = path.native();

                auto jobContinueEventCb = std::bind(&ServiceWatcher::jobContinueEvent, this,
                        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);

                std::scoped_lock guard{ lock_ };
                auto ptr = std::make_unique<InotifyJob>(ioc_, path, std::move(new_conf), Inotify::jobToEvents(new_conf), jobContinueEventCb);
                spdlog::info("{}: add job, id: {:016x}, path: {}", __FUNCTION__, ptr->job_id(), ptr->path().native());
                jobs_.emplace_back(std::move(ptr));
            }
        }
    }

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

        bool debug = conf_.contains("debug") ? json::value_to<bool>(conf_["debug"]) : false;
        spdlog::set_level(debug ? spdlog::level::debug : spdlog::level::info);

        if(conf_.contains("jobs") && conf_["jobs"].is_array()) {
            asio::post(ioc_, std::bind(& ServiceWatcher::loadAllJobs, this));
        } else {
            spdlog::warn("{}: config jobs empty", __FUNCTION__);
        }

        spdlog::info("{}: success", __FUNCTION__);
    }

    void loadAllJobs(void) {
        // clear jobs
        {
            std::scoped_lock guard{ lock_ };
            jobs_.clear();
        }

        loadConfigJobs();
        loadDirJobs();
    }

    void loadFileJob(const std::filesystem::path & file) {
        if(file.extension() != ".job") {
            spdlog::debug("{}: skipped job: {}", __FUNCTION__, file.native());
            return;
        }

        std::ifstream ifs{file};
        system::error_code ec;
        auto json = json::parse(std::string{std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()}, ec);

        if(ec) {
            spdlog::warn("{}: {} error, code: {}, message: {}", __FUNCTION__, "json", ec.value(), ec.message());
            return;
        }

        if(! json.is_object()) {
            spdlog::warn("{}: {} failed, not object", __FUNCTION__, "json");
            return;
        }

        loadJob(json.get_object());
    }

    void loadDirJobs(void) {
        for(const auto & file: System::readDir(jobs_dir_, false, ReadDirFilter::File)) {
            loadFileJob(file);
        }
    }

    void loadConfigJobs(void) {
        for(auto & json : conf_["jobs"].get_array()) {
            if(! json.is_object()) {
                spdlog::warn("{}: job skipped, not object", __FUNCTION__);
            }

            loadJob(json.get_object());
        }
    }

    void loadJob(const json::object & job_conf) {

        if(! job_conf.contains("path") || ! job_conf.at("path").is_string()) {
            spdlog::warn("{}: job skipped, tag not found: {}", __FUNCTION__, "path");
            return;
        }

        auto path = std::filesystem::path{job_conf.at("path").get_string().c_str()};
        const uint32_t events = Inotify::jobToEvents(job_conf);

        auto jobContinueEventCb = std::bind(&ServiceWatcher::jobContinueEvent, this,
                    std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);

        if(std::filesystem::is_regular_file(path)) {

            if(events == Inotify::EVENTS_BASE) {
                auto new_conf = job_conf;
                new_conf["name"] = path.filename().native();
                auto ptr = std::make_unique<InotifyJob>(ioc_, path.parent_path(), new_conf, events, std::move(jobContinueEventCb));
                spdlog::info("{}: add job, id: {:016x}, path: {}, name: {}", __FUNCTION__, ptr->job_id(), path.parent_path().native(), path.filename().native());
                std::scoped_lock guard{ lock_ };
                jobs_.emplace_back(std::move(ptr));
            } else {
                auto ptr = std::make_unique<InotifyJob>(ioc_, path, job_conf, events, std::move(jobContinueEventCb));
                spdlog::info("{}: add job, id: {:016x}, path: {}", __FUNCTION__, ptr->job_id(), path.native());
                std::scoped_lock guard{ lock_ };
                jobs_.emplace_back(std::move(ptr));
            }

        } else if(std::filesystem::is_directory(path)) {
            bool recurse = job_conf.contains("recursive") ? json::value_to<bool>(job_conf.at("recursive")) : false;

            if(recurse) {
                auto dirs = System::readDir(path, recurse, ReadDirFilter::Dir);
                for(const auto & dir : dirs) {
                    auto ptr = std::make_unique<InotifyJob>(ioc_, dir, job_conf, events, std::move(jobContinueEventCb));
                    spdlog::info("{}: add job, id: {:016x}, path: {}", __FUNCTION__, ptr->job_id(), dir);
                    std::scoped_lock guard{ lock_ };
                    jobs_.emplace_back(std::move(ptr));
                }
            } else {
                auto ptr = std::make_unique<InotifyJob>(ioc_, path, job_conf, events, std::move(jobContinueEventCb));
                spdlog::info("{}: add job, id: {:016x}, path: {}", __FUNCTION__, ptr->job_id(), path.native());
                std::scoped_lock guard{ lock_ };
                jobs_.emplace_back(std::move(ptr));
            }
        } else {
            spdlog::warn("{}: job skipped, path not found: {}", __FUNCTION__, path.native());
        }
    }

  public:
    ServiceWatcher(boost::asio::io_context & ioc, const std::filesystem::path & conf_path, const std::filesystem::path & jobs_dir)
        : ioc_(ioc), signals_(ioc), jobs_dir_(jobs_dir) {
        spdlog::info("found config: {}", conf_path.native());
        readConfig(conf_path);

        conf_job_ = std::make_unique<InotifyConfFile>(ioc_, conf_path, std::bind(&ServiceWatcher::confFileModifyEvent, this, std::placeholders::_1));

        if(std::filesystem::is_directory(jobs_dir)) {
            dir_jobs_ = std::make_unique<InotifyConfDir>(ioc_, jobs_dir, std::bind(&ServiceWatcher::confDirModifyEvent, this,
                                                            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        }

        signals_.add(SIGINT);
        signals_.add(SIGTERM);
        signals_.add(SIGUSR1);

        signals_.async_wait([this](const system::error_code & ec, int signal) {
            if(signal == SIGINT || signal == SIGTERM) {
                this->ioc_.stop();
            } else {
                this->status();
            }
        });
    }

    void status(void) const {
        std::scoped_lock guard{ lock_ };
        spdlog::info("{}: jobs count: {}", __FUNCTION__, jobs_.size());

        for(const auto & job: jobs_) {
            if(auto ptr = dynamic_cast<InotifyJob*>(job.get())) {
                auto & conf = ptr->getConf();
                auto path = json::value_to<std::string>(conf.at("path"));
                auto cmd = json::value_to<std::string>(conf.at("command"));
                spdlog::info("{}: job id: {:016x}, path: {}, cmd: {}", __FUNCTION__, ptr->job_id(), path, cmd);
            }
        }
    }
};

int main(int argc, char** argv) {

    const char* conf_path = "/etc/inotify_watcher/config.json";
    const char* jobs_dir = "/etc/inotify_watcher/jobs.d";

    if(auto env = getenv("INOTIFY_SERVICE_CONF")) {
        conf_path = env;
    }

    if(auto env = getenv("INOTIFY_JOBS_DIR")) {
        jobs_dir = env;
    }

    if(1 < argc) {
        if(0 == strcmp(argv[1], "--help")) {
            std::cout << "usage: " << argv[0] << " --config <json config>" << std::endl;
            return EXIT_SUCCESS;
        }

        if(0 == strcmp(argv[1], "--config")) {
            conf_path = argv[2];
        }
    }

    if(! std::filesystem::is_regular_file(conf_path)) {
        std::cerr << "config not found: " << conf_path << std::endl;
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

    try {
        auto app = std::make_unique<ServiceWatcher>(ctx, conf_path, jobs_dir);
        sd_notify(0, "READY=1");
        ctx.run();
        spdlog::info("service stopped");
    } catch(const std::exception & err) {
        std::cerr << "exception: " << err.what() << std::endl;
    }

    sd_notify(0, "STOPPING=1");
    return 0;
}
