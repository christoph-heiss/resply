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
#include <csignal>
#include <thread>
#include <memory>
#include <cctype>

#include "asio.hpp"
#include "clipp.h"
#include "spdlog/spdlog.h"
#include "nlohmann/json.hpp"
#include "resply.h"
#include "rslp.pb.h"

using json = nlohmann::json;
using namespace google;

const std::string LOGGER_NAME{"proxy-log"};


struct Options {
        Options() :
                config_path{".proxy-conf.json"}, daemonize{}, log_path{"proxy.log"},
                port{6543}, remote_host{"localhost:6379"} { }

        std::string config_path;
        bool daemonize;
        std::string log_path;
        unsigned short port;
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
                std::cout << clipp::make_man_page(cli, argv[0])
                                .append_section("NOTES", "\tCommand line parameter overwrite values in the configuration file.")
                          << std::endl;
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
        spdlog::get(LOGGER_NAME)->info("Shutting down.");

        spdlog::drop_all();
        google::protobuf::ShutdownProtobufLibrary();
}


static json read_config_file(const std::string& path)
{
        auto logger{spdlog::get(LOGGER_NAME)};

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


static void daemonize_process()
{
        auto logger{spdlog::get(LOGGER_NAME)};

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

        // Change working directory
        //if (::chdir("/") < 0) {
                //logger->warn("Could not change working directory, current directory might be locked! Reason: {}", ::strerror(errno));
        //}

        // Change iostreams
        ::freopen("/dev/null", "r", ::stdin);
        ::freopen("/dev/null", "w", ::stdout);
        ::freopen("/dev/null", "w", ::stderr);
}


class ProtobufRedisAdapter {
public:
        ProtobufRedisAdapter(const std::string& redis_host, asio::io_service& io_service) :
                client_{redis_host}, socket_{io_service}, logger_{spdlog::get(LOGGER_NAME)}
        { }

        ~ProtobufRedisAdapter()
        {
                logger_->info("Connection to {} closed.", remote_address_);
        }

        asio::ip::tcp::socket& socket()
        {
                return socket_;
        }

        void start()
        {
                {
                        auto endpoint{socket_.remote_endpoint()};
                        remote_address_ = endpoint.address().to_string() + ':' + std::to_string(endpoint.port());
                }

                logger_->info("New connection from {}.", remote_address_);

                for (;;) {
                        asio::error_code error_code;

                        uint32_t size;
                        auto size_buffer{asio::buffer(&size, 4)};
                        asio::read(socket_, size_buffer, asio::transfer_exactly(4), error_code);
                        if (error_code)
                                break;

                        std::string input(size, '\0');
                        auto input_buffer{asio::buffer(&input[0], size)};
                        asio::read(socket_, input_buffer, asio::transfer_exactly(size), error_code);
                        if (error_code)
                                break;

                        rslp::Command command;
                        command.ParseFromString(input);

                        {
                                std::string str{command.DebugString()};
                                str.erase(std::remove_if(str.begin(), str.end(), ::isspace), str.end());

                                logger_->trace("Received {}", str);
                        }
                }

        }

private:
        resply::Client client_;
        asio::ip::tcp::socket socket_;
        std::shared_ptr<spdlog::logger> logger_;
        std::string remote_address_;
};


static void install_signal_handler()
{
        std::thread{[&]() {
                ::sigset_t sigset;
                sigemptyset(&sigset);
                sigaddset(&sigset, SIGTERM);
                sigaddset(&sigset, SIGINT);
                sigprocmask(SIG_BLOCK, &sigset, nullptr);

                int sig;
                sigwait(&sigset, &sig);

                clean_up();
                std::exit(0);
        }}.detach();
}


[[noreturn]]
static void listen_for_connections(const Options& options)
{
        asio::io_service io_service;

        asio::ip::tcp::endpoint endpoint{asio::ip::tcp::v4(), options.port};
        asio::ip::tcp::acceptor acceptor{io_service, endpoint};

        spdlog::get(LOGGER_NAME)->info("Started listening on 0.0.0.0:{}", options.port);

        for (;;) {
                auto server{std::make_shared<ProtobufRedisAdapter>(options.remote_host, io_service)};
                acceptor.accept(server->socket());

                std::thread{&ProtobufRedisAdapter::start, server}.detach();
        }
}


int main(int argc, char* argv[])
{
        GOOGLE_PROTOBUF_VERIFY_VERSION;

        auto options{parse_commandline(argc, argv)};
        auto logger{spdlog::stdout_color_mt(LOGGER_NAME)};
        logger->flush_on(spdlog::level::warn);

        json config{read_config_file(options.config_path)};

        if (options.daemonize) {
                logger->info("Daemonizing server, logfile: {}", options.log_path);
                daemonize_process();
                // The parent process has exited already.

                // Drop the console logger and create a rotating file logger.
                spdlog::drop(LOGGER_NAME);
                logger = spdlog::rotating_logger_mt(LOGGER_NAME, options.log_path,
                                                    1048576 * 10, 10);
                logger->flush_on(spdlog::level::warn);
        }

        install_signal_handler();
        listen_for_connections(options);
}
