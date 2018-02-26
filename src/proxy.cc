//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include <iostream>
#include <string>
#include <fstream>
#include <unistd.h>
#include <cstring>
#include <cstdlib>

#include "clipp.h"
#include "spdlog/spdlog.h"
#include "nlohmann/json.hpp"
#include "resply.h"
#include "resp.pb.h"

using json = nlohmann::json;


struct Options {
        Options() :
                config_path{".proxy-conf.json"}, daemonize{}, log_path{"proxy.log"},
                port{"6543"}, remote_host{"localhost:6379"} { }

        std::string config_path;
        bool daemonize;
        std::string log_path;
        std::string port;
        std::string remote_host;
};


static Options parse_commandline(int argc, char** argv)
{
        Options options;
        bool show_help{}, show_version{};

        auto cli = (
                clipp::option("-c", "--conf-path").set(options.config_path)
                        .doc("Path to the configuration file [default: $CWD/.proxy-conf.json]"),
                clipp::option("-d", "--daemonize").set(options.daemonize).doc("Fork to background."),
                clipp::option("-l", "--log-path").set(options.log_path)
                        .doc("Path to the log file [default: $CWD/proxy.log] (Only applies when not daemonized.)"),
                clipp::option("-p", "--port").set(options.port)
                        .doc("Port to listen on [default: 6543]"),
                clipp::option("-r", "--remote-host").set(options.remote_host)
                        .doc("Host (redis-server) to connect to [default: localhost:6379]"),
                clipp::option("--help").set(show_help).doc("Show help and exit."),
                clipp::option("--version").set(show_version).doc("Show version and exit.")
        );

        if (!clipp::parse(argc, argv, cli) || show_help) {
                std::cout << clipp::make_man_page(cli, argv[0]) << std::endl;
                std::exit(0);
        }

        if (show_version) {
                std::cout
                        << argv[0] << '\n'
                        << "Using resply version " << resply::version() << std::endl;
                std::exit(0);
        }

        return options;
}


static void clean_up()
{
        google::protobuf::ShutdownProtobufLibrary();
}


static json read_config_file(std::shared_ptr<spdlog::logger> logger, const std::string& path)
{
        if (!path.length()) {
                return {};
        }

        std::ifstream config_file{path};
        json config;

        if (!config_file) {
                logger->warn("Configuration file not found! Using compiled-in defaults ..");
        } else {
                config_file >> config;
                config_file.close();
        }

        return config;
}


static void daemonize_process(std::shared_ptr<spdlog::logger> logger)
{
         pid_t pid{::fork()};

        if (pid < 0) {
                logger->error("Could not fork process, reason: {}", ::strerror(errno));
                std::exit(1);
        } else if (pid > 0) {
                // Quit the parent
                std::exit(0);
        }

        // Now only the child is still running
        // Change the file mode mask
        ::umask(07);

        // Create new process session so the daemon becomes independent of the parent
        if (::setsid() < 0) {
                logger->error("Could not create new process session, reason: {}", ::strerror(errno));
                std::exit(2);
        }

        // Change iostreams
        ::freopen("/dev/null", "r", ::stdin);
        ::freopen("/dev/null", "w", ::stdout);
        ::freopen("/dev/null", "w", ::stderr);
}


int main(int argc, char* argv[])
{
        std::atexit(clean_up);

        GOOGLE_PROTOBUF_VERIFY_VERSION;
        auto options{parse_commandline(argc, argv)};
        auto logger{spdlog::stdout_color_mt("proxy-log")};
        logger->flush_on(spdlog::level::warn);

        json config{read_config_file(logger, options.config_path)};

        if (options.daemonize) {
                logger->info("Daemonizing server, logfile: {}", options.log_path);
                daemonize_process(logger);
                // The parent process has exited already.

                logger = spdlog::rotating_logger_mt("proxy-rot-log", options.log_path,
                                                    1048576 * 10, 10);
                logger->flush_on(spdlog::level::warn);
        }

        for (;;) {
        }

        return 0;
}
