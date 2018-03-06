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
#include <algorithm>
#include <arpa/inet.h>

#include "asio.hpp"
#include "clipp.h"
#include "spdlog/spdlog.h"
#include "nlohmann/json.hpp"
#include "resply.h"
#include "rslp.pb.h"
#include "grpc++/grpc++.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "rslp.grpc.pb.h"
#pragma GCC diagnostic pop

using json = nlohmann::json;
using namespace google;


struct Options {
        Options() :
                config_path{".proxy-conf.json"}, daemonize{}, log_path{"proxy.log"},
                protobuf_port{6543}, grpc_port{"6544"}, remote_host{"localhost:6379"}, verbose{} { }

        std::string config_path;
        bool daemonize;
        std::string log_path;
        unsigned short protobuf_port;
        std::string grpc_port;
        std::string remote_host;
        bool verbose;
};


namespace {

const std::string GLOBAL_LOGGER_NAME{"proxy"};

void resply_result_to_rslp_data(rslp::Command_Data* data, const resply::Result& result);


Options parse_commandline(int argc, char** argv)
{
        Options options;
        bool show_help{}, show_version{};

        auto cli = (
                clipp::option("-c", "--conf-path").set(options.config_path)
                        .doc("Path to the configuration file [default: $CWD/.proxy-conf.json]"),
                clipp::option("-d", "--daemonize").set(options.daemonize).doc("Fork to background."),
                clipp::option("-l", "--log-path").set(options.log_path)
                        .doc("Path to the log file [default: $CWD/proxy.log] (Only applies when not daemonized.)"),
                clipp::option("--protobuf-port").set(options.protobuf_port)
                        .doc("Port the protobuf server should listen on [default: 6543]"),
                clipp::option("--grpc-port").set(options.grpc_port)
                        .doc("Port the gRPC server should listen on [default: 6544]"),
                clipp::option("-r", "--remote-host").set(options.remote_host)
                        .doc("Host (redis-server) to connect to [default: localhost:6379]"),
                clipp::option("-v", "--verbose").set(options.verbose).doc("Enable verbose logging."),
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

void cleanup()
{
        spdlog::get(GLOBAL_LOGGER_NAME)->info("Shutting down.");

        spdlog::drop_all();
        google::protobuf::ShutdownProtobufLibrary();
}


void install_signal_handler()
{
        std::thread{[&]() {
                ::sigset_t sigset;
                sigemptyset(&sigset);
                sigaddset(&sigset, SIGTERM);
                sigaddset(&sigset, SIGINT);
                sigprocmask(SIG_BLOCK, &sigset, nullptr);

                int sig;
                sigwait(&sigset, &sig);

                cleanup();
                std::exit(0);
        }}.detach();
}

void daemonize_process()
{
        auto logger{spdlog::get(GLOBAL_LOGGER_NAME)};

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

void resply_result_to_rslp(rslp::Command& command, const resply::Result& result)
{
        using Type = resply::Result::Type;

        switch (result.type) {
                case Type::ProtocolError:
                case Type::IOError:
                        command.add_data()->set_err(result.string);
                        break;

                case Type::String:
                        command.add_data()->set_str(result.string);
                        break;

                case Type::Integer:
                        command.add_data()->set_int_(result.integer);
                        break;

                case Type::Array:
                        for (const auto& element: result.array) {
                                resply_result_to_rslp_data(command.add_data(), element);
                        }

                        break;

                case Type::Nil:
                        break;
        }
}

void resply_result_to_rslp_data(rslp::Command_Data* data, const resply::Result& result)
{
        using Type = resply::Result::Type;

        switch (result.type) {
        case Type::String:
                data->set_str(result.string);
                break;

        case Type::Integer:
                data->set_int_(result.integer);
                break;

        case Type::Array:
                for (const auto& element: result.array) {
                        resply_result_to_rslp(*data->mutable_array(), element);
                }
                break;

        default:
                /* Cannot happen */
                break;
        }
}

}


class ProtobufAdapter {
public:
        ProtobufAdapter(const std::string& redis_host, asio::io_context& io_context,
                        std::shared_ptr<spdlog::sinks::sink> logsink) :
                client_{redis_host}, socket_{io_context},
                logger_{std::make_shared<spdlog::logger>(LOGGER_NAME, logsink)}
        { }

        ~ProtobufAdapter()
        {
                logger_->info("[{}] Connection closed.", remote_address_);
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

                client_.connect();

                for (;;) {
                        std::string request{receive_data()};
                        if (!request.length()) {
                                break;
                        }

                        rslp::Command command;
                        command.ParseFromString(request);

                        logger_->debug("[{}] Received message '{}'", remote_address_, command.ShortDebugString());

                        std::vector<std::string> resply_command;
                        for (const auto& arg: command.data()) {
                                resply_command.push_back(arg.str());
                        }

                        resply::Result result{client_.command(resply_command)};

                        rslp::Command response;
                        resply_result_to_rslp(response, result);

                        send_data(response);

                        if (result.type == resply::Result::Type::Array &&
                            result.array[0].type == resply::Result::Type::String &&
                            (result.array[0].string == "subscribe" || result.array[0].string == "psubscribe")) {
                                listen_for_messages();
                        }
                }
        }

private:
        void listen_for_messages()
        {
                client_.listen_for_messages([this](const auto& channel, const auto& message) {
                        rslp::Command response;
                        response.add_data()->set_str("message");
                        response.add_data()->set_str(channel);
                        response.add_data()->set_str(message);

                        send_data(response);
                });
        }

        std::string receive_data()
        {
                asio::error_code error_code;

                uint32_t size;
                {
                        auto buffer{asio::buffer(&size, 4)};
                        asio::read(socket_, buffer, asio::transfer_exactly(4), error_code);
                        if (error_code) { return {}; }

                        size = ntohl(size);
                }

                std::string data(size, '\0');
                {
                        auto buffer{asio::buffer(&data[0], size)};
                        asio::read(socket_, buffer, asio::transfer_exactly(size), error_code);
                        if (error_code) { return {}; }
                }

                return data;
        }

        void send_data(const rslp::Command& command)
        {
                std::string output;
                command.SerializeToString(&output);

                uint32_t size{htonl(static_cast<uint32_t>(output.size()))};
                asio::write(socket_, asio::buffer(&size, 4));
                asio::write(socket_, asio::buffer(output.data(), output.size()));
        }


        resply::Client client_;
        asio::ip::tcp::socket socket_;
        std::shared_ptr<spdlog::logger> logger_;
        std::string remote_address_;

        const std::string LOGGER_NAME{"ProtobufAdapter"};
};

class ProtobufServer {
public:
        void start(const Options& options, std::shared_ptr<spdlog::sinks::sink> logsink)
        {
                auto logger{std::make_shared<spdlog::logger>(LOGGER_NAME, logsink)};

                asio::io_context io_context;
                asio::ip::tcp::acceptor acceptor{io_context};

                try {
                        acceptor = {io_context, {asio::ip::tcp::v4(), options.protobuf_port}};
                } catch (const asio::system_error& ex) {
                        logger->error("Could not start protobuf server on 0.0.0.0:{}, exiting! ({})",
                                        options.protobuf_port, ex.what());
                        return;
                }

                logger->info("Started protobuf server on 0.0.0.0:{}", options.protobuf_port);

                for (;;) {
                        auto server{std::make_shared<ProtobufAdapter>(
                                        options.remote_host, io_context, logsink
                        )};
                        acceptor.accept(server->socket());

                        std::thread{&ProtobufAdapter::start, server}.detach();
                }
        }

private:
        const std::string LOGGER_NAME{"ProtobufServer"};
};


class GrpcAdapter final : public rslp::ProtoAdapter::Service {
public:
        GrpcAdapter(const std::string& redis_host, std::shared_ptr<spdlog::sinks::sink> logsink) :
                client_{redis_host}, logger_{std::make_shared<spdlog::logger>(LOGGER_NAME, logsink)}
        { }

        void initialize()
        {
                client_.connect();
        }

        grpc::Status execute(grpc::ServerContext* context, const rslp::Command* request,
                             rslp::Command* response) override
        {
                std::vector<std::string> command;
                for (const auto& arg: request->data()) {
                        command.push_back(arg.str());
                }

                if (command.size()) {
                        std::string name{command.front()};
                        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

                        if (name == "subscribe" || name == "psubscribe") {
                                logger_->warn("[{}] Received subscription command in execute() rpc, ignoring!",
                                              context->peer());
                                return grpc::Status{
                                        grpc::StatusCode::INVALID_ARGUMENT,
                                        "SUBSCRIBE/PSUBSCRIBE can only be used with rpc subscribe()!"
                                };
                        }
                }

                logger_->debug("[{}] execute(): {}", context->peer(), request->ShortDebugString());

                resply::Result result{client_.command(command)};
                resply_result_to_rslp(*response, result);

                return grpc::Status::OK;
        }

        grpc::Status subscribe(grpc::ServerContext* context, const rslp::Command* request,
                               grpc::ServerWriter<rslp::Command>* writer) override
        {
                std::vector<std::string> command;
                for (const auto& arg: request->data()) {
                        command.push_back(arg.str());
                }

                if (command.size()) {
                        std::string name{command.front()};
                        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

                        if (name != "subscribe" && name != "psubscribe") {
                                logger_->warn("Received non-subscription command in subscribe() rpc, ignoring!");
                                return grpc::Status{
                                        grpc::StatusCode::INVALID_ARGUMENT,
                                        "subscribe() rpc can only be used with SUBSCRIBE/PSUBSCRIBE!"
                                };
                        }
                }

                logger_->debug("[{}] subscribe(): {}", context->peer(), request->ShortDebugString());

                resply::Result result{client_.command(command)};

                client_.listen_for_messages([writer](const auto& channel, const auto& message) {
                        rslp::Command response;
                        response.add_data()->set_str("message");
                        response.add_data()->set_str(channel);
                        response.add_data()->set_str(message);

                        writer->Write(response);
                });

                return grpc::Status::OK;
        }

private:
        resply::Client client_;
        std::shared_ptr<spdlog::logger> logger_;

        const std::string LOGGER_NAME{"GrpcAdapter"};
};

class GrpcServer {
public:
        void start(const Options& options, std::shared_ptr<spdlog::sinks::sink> logsink)
        {
                grpc::ServerBuilder builder;
                builder.AddListeningPort("0.0.0.0:" + options.grpc_port, grpc::InsecureServerCredentials());

                GrpcAdapter adapter{options.remote_host, logsink};
                adapter.initialize();
                builder.RegisterService(&adapter);

                auto server{builder.BuildAndStart()};
                server->Wait();
        }
};


class Proxy {
public:
        Proxy(const Options& options) : options_{options} { }

        void run()
        {
                std::shared_ptr<spdlog::sinks::sink> logsink{
                        std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>()
                };
                logger_.reset(new spdlog::logger(LOGGER_NAME, logsink));

                setup_logger();
                read_config_file();

                if (options_.daemonize) {
                        logger_->info("Daemonizing server, logfile: {}", options_.log_path);
                        daemonize_process();
                        // The parent process has exited already.

                        // Drop the console logger and create a rotating file logger.
                        spdlog::drop(LOGGER_NAME);
                        logsink.reset(new spdlog::sinks::rotating_file_sink_mt(
                                options_.log_path, 1048576 * 10, 10)
                        );
                        logger_.reset(new spdlog::logger(LOGGER_NAME, logsink));

                        setup_logger();
                }

                std::thread protobuf_server{[this, logsink]() {
                        ProtobufServer server;
                        server.start(options_, logsink);
                }};

                std::thread grpc_server{[this, logsink]() {
                        GrpcServer server;
                        server.start(options_, logsink);
                }};

                protobuf_server.join();
                grpc_server.join();
        }

private:
        void read_config_file()
        {
                if (!options_.config_path.length()) {
                        return;
                }

                std::ifstream file{options_.config_path};

                if (!file) {
                        logger_->warn("Configuration file not found! Using compiled-in defaults ..");
                } else {
                        file >> config_;
                        file.close();
                }
        }

        void setup_logger()
        {
                logger_->flush_on(spdlog::level::info);

                if (options_.verbose) {
                        logger_->info("Setting logging level to verbose.");
                        logger_->set_level(spdlog::level::debug);
                }
        }


        const Options& options_;
        std::shared_ptr<spdlog::logger> logger_;
        json config_;

        const std::string LOGGER_NAME{GLOBAL_LOGGER_NAME};
};


int main(int argc, char* argv[])
{
        GOOGLE_PROTOBUF_VERIFY_VERSION;

        auto options{parse_commandline(argc, argv)};

        install_signal_handler();
        Proxy{options}.run();

        return 0;
}
