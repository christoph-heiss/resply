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
#include <functional>
#include <type_traits>
#include <arpa/inet.h>

#include "asio.hpp"
#include "clipp.h"
#include "spdlog/spdlog.h"
#include "nlohmann/json.hpp"
#include "resply.h"
#include "optional.h"
#include "rslp.pb.h"
#include "grpc++/grpc++.h"
#include "grpc/support/log.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "rslp.grpc.pb.h"
#pragma GCC diagnostic pop

using json = nlohmann::json;
using namespace google;


namespace {

const std::string GLOBAL_LOGGER_NAME{"Proxy"};

void resply_result_to_rslp_data(rslp::Command_Data* data, const resply::Result& result);

struct Options {
        bool daemonize;
        std::string log_path;
        unsigned short protobuf_port;
        unsigned short grpc_port;
        std::string redis_host;
        bool verbose;
};

Options parse_commandline(int argc, char** argv)
{
        bool show_help{}, show_version{};

        Optional<bool> daemonize{}, verbose{};
        Optional<unsigned short> protobuf_port{6543}, grpc_port{6544};
        Optional<std::string> config_path{".proxy-conf.json"};
        Optional<std::string> log_path{"proxy.log"};
        Optional<std::string> redis_host{"localhost:6379"};

        auto cli = (
                clipp::option("-c", "--conf-path") & clipp::value("path")
                        .call([&](auto p) { config_path.set_value(p); })
                        .doc("Path to the configuration file [default: $CWD/.proxy-conf.json]"),

                clipp::option("-d", "--daemonize")
                        .call([&](auto d) { daemonize.set_value(d); })
                        .doc("Fork to background."),

                clipp::option("-l", "--log-path") & clipp::value("path")
                        .call([&](auto p) { log_path.set_value(p); })
                        .doc("Path to the log file [default: $CWD/proxy.log] (Only applies when daemonized.)"),

                clipp::option("--protobuf-port") & clipp::integer("port")
                        .call([&](auto p) { protobuf_port.set_value(static_cast<unsigned short>(std::stoi(p))); })
                        .doc("Port the protobuf server should listen on [default: 6543]"),

                clipp::option("--grpc-port") & clipp::integer("port")
                        .call([&](auto p) { grpc_port.set_value(static_cast<unsigned short>(std::stoi(p))); })
                        .doc("Port the gRPC server should listen on [default: 6544]"),

                clipp::option("-r", "--redis-host") & clipp::value("host")
                        .call([&](auto h) { redis_host.set_value(h); })
                        .doc("Host (redis server) to connect to [default: localhost:6379]"),

                clipp::option("-v", "--verbose")
                        .call([&](auto v) { verbose.set_value(v); })
                        .doc("Enable verbose logging."),

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

        json config;
        if (config_path.value_or_default().length()) {
                std::ifstream file{config_path.value_or_default()};
                if (file) {
                        file >> config;
                        file.close();
                }
        }

        auto choose_opt = [&](const std::string& name, const auto& opt)
            -> typename std::remove_reference<decltype(opt)>::type::value_type {
                if (opt.has_value()) {
                        return opt.value();
                } else if (config.count(name)) {
                        return config[name];
                }

                return opt.default_value();
        };

        Options options;
        options.daemonize = choose_opt("daemonize", daemonize);
        options.log_path = choose_opt("log-path", log_path);
        options.protobuf_port = choose_opt("protobuf-port", protobuf_port);
        options.grpc_port = choose_opt("grpc-port", grpc_port);
        options.redis_host = choose_opt("redis-host", redis_host);
        options.verbose = choose_opt("verbose", verbose);

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

std::shared_ptr<spdlog::logger> create_logger(const std::string& name)
{
        auto global_logger{spdlog::get(GLOBAL_LOGGER_NAME)};
        auto global_sink{global_logger->sinks().front()};

        auto logger{std::make_shared<spdlog::logger>(name, global_sink)};
        logger->set_level(global_logger->level());

        return logger;
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
        ProtobufAdapter(const std::string& redis_host, asio::io_context& io_context) :
                client_{redis_host}, socket_{io_context}, logger_{create_logger(LOGGER_NAME)}
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


        const std::string LOGGER_NAME{"ProtobufAdapter"};

        resply::Client client_;
        asio::ip::tcp::socket socket_;
        std::string remote_address_;
        std::shared_ptr<spdlog::logger> logger_;
};

class ProtobufServer {
public:
        void start(const Options& options)
        {
                auto logger{create_logger(LOGGER_NAME)};

                asio::io_context io_context;
                asio::ip::tcp::acceptor acceptor{io_context};

                try {
                        acceptor = {io_context, {asio::ip::tcp::v4(), options.protobuf_port}};
                } catch (const asio::system_error& ex) {
                        logger->error("Could not start protobuf server on 0.0.0.0:{}, exiting! ({})",
                                        options.protobuf_port, ex.what());
                        return;
                }

                logger->info("Started listening on 0.0.0.0:{}", options.protobuf_port);

                for (;;) {
                        auto server{std::make_shared<ProtobufAdapter>(
                                options.redis_host, io_context
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
        explicit GrpcAdapter(const std::string& redis_host) :
                client_{redis_host}, logger_{create_logger(LOGGER_NAME)}
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

                rslp::Command response;
                resply_result_to_rslp(response, result);
                writer->Write(response);

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
        const std::string LOGGER_NAME{"GrpcAdapter"};

        resply::Client client_;
        std::shared_ptr<spdlog::logger> logger_;
};

class GrpcServer {
public:
        void start(const Options& options)
        {
                auto logger{create_logger(LOGGER_NAME)};
                setup_grpc_logging();

                grpc::ServerBuilder builder;
                builder.AddListeningPort(
                        "0.0.0.0:" + std::to_string(options.grpc_port),
                        grpc::InsecureServerCredentials()
                );

                GrpcAdapter adapter{options.redis_host};
                adapter.initialize();
                builder.RegisterService(&adapter);

                auto server{builder.BuildAndStart()};
                logger->info("Started listening on 0.0.0.0:{}", options.grpc_port);

                server->Wait();
        }

private:
        void setup_grpc_logging()
        {
                gpr_set_log_function([](gpr_log_func_args *args) {
                        // We cannot use LOGGER_NAME here as we cannot capture
                        // any variables.
                        auto logger{spdlog::get("GrpcServer")};

                        switch (args->severity) {
                        case GPR_LOG_SEVERITY_DEBUG:
                                logger->debug("{}", args->message);
                                break;

                        case GPR_LOG_SEVERITY_INFO:
                                logger->info("{}", args->message);
                                break;

                        case GPR_LOG_SEVERITY_ERROR:
                                logger->error("{}", args->message);
                                break;
                        }
                });
        }

        const std::string LOGGER_NAME{"GrpcServer"};
};


class Proxy {
public:
        explicit Proxy(const Options& options) :
                options_{options}, logger_{spdlog::stdout_color_mt(LOGGER_NAME)} { }

        void run()
        {
                if (options_.daemonize) {
                        daemonize();
                }

                if (options_.verbose) {
                        logger_->info("Setting logging level to verbose.");
                        logger_->set_level(spdlog::level::debug);
                }

                ProtobufServer protobuf_server;
                std::thread{&ProtobufServer::start, &protobuf_server, std::cref(options_)}.detach();

                GrpcServer grpc_server;
                std::thread{&GrpcServer::start, &grpc_server, std::cref(options_)}.detach();

                for (;;);
        }

private:
        void daemonize()
        {
                logger_->info("Daemonizing server, logfile: {}", options_.log_path);
                daemonize_process();
                // The parent process has exited already.

                // Drop the console logger and create a rotating file logger.
                spdlog::drop(LOGGER_NAME);

                logger_ = spdlog::rotating_logger_mt(
                        LOGGER_NAME, options_.log_path, 10485760 /* 10MB */, 10 /* files */
                );
        }


        const std::string LOGGER_NAME{GLOBAL_LOGGER_NAME};

        const Options& options_;
        std::shared_ptr<spdlog::logger> logger_;
};


int main(int argc, char* argv[])
{
        GOOGLE_PROTOBUF_VERIFY_VERSION;

        auto options{parse_commandline(argc, argv)};

        install_signal_handler();
        Proxy{options}.run();

        return 0;
}
