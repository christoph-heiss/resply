//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include <iostream>
#include <string>
#include <arpa/inet.h>

#include "asio.hpp"
#include "clipp.h"
#include "rslp.pb.h"


using namespace google;

struct Options {
        Options() : host{"localhost"}, port{"6543"} { }

        std::string host;
        std::string port;
};


static Options parse_commandline(int argc, char** argv)
{
        Options options;
        bool show_help{};

        auto cli = (
                clipp::option("-h", "--host").set(options.host)
                                             .doc("Set the host to connect to [default: localhost]"),
                clipp::option("-p", "--port").set(options.port)
                                             .doc("Set the port to connect to [default: 6543]"),
                clipp::option("--help").set(show_help).doc("Show help and exit.")
        );

        if (!clipp::parse(argc, argv, cli) || show_help) {
                std::cout << clipp::make_man_page(cli, argv[0]) << std::endl;
                std::exit(0);
        }

        return options;
}


static bool connect_to_server(const Options& options, asio::ip::tcp::socket& socket)
{
        asio::io_context io_context;
        asio::error_code error_code;
        asio::ip::tcp::resolver resolver{io_context};

        auto results = resolver.resolve(options.host, options.port, error_code);
        if (error_code) {
                std::cerr << "Error while connecting: " << error_code.message() << std::endl;
                return false;
        }

        asio::connect(socket, results, error_code);
        if (error_code) {
                std::cerr << "Error while connecting: " << error_code.message() << std::endl;
                return false;
        }

        return true;
}


static std::string receive_data(asio::ip::tcp::socket& socket)
{
        asio::error_code error_code;

        size_t size;
        {
                auto buffer{asio::buffer(&size, 4)};
                asio::read(socket, buffer, asio::transfer_exactly(4), error_code);
                if (error_code) { return {}; }

                size = ntohl(size);
        }

        std::string data(size, '\0');
        {
                auto buffer{asio::buffer(&data[0], size)};
                asio::read(socket, buffer, asio::transfer_exactly(size), error_code);
                if (error_code) { return {}; }
        }

        return data;
}


static void send_data(asio::ip::tcp::socket& socket, rslp::Command& command)
{
        std::string output;
        command.SerializeToString(&output);

        uint32_t size{htonl(static_cast<uint32_t>(output.size()))};
        asio::write(socket, asio::buffer(&size, 4));
        asio::write(socket, asio::buffer(output.data(), output.size()));
}


int main(int argc, char* argv[])
{
        GOOGLE_PROTOBUF_VERIFY_VERSION;
        auto options{parse_commandline(argc, argv)};

        asio::io_context io_context;
        asio::ip::tcp::socket socket{io_context};
        if (!connect_to_server(options, socket)) {
                return 1;
        }

        rslp::Command command;
        command.set_type(rslp::Command::Array);

        auto* data{command.add_data()};
        data->set_str("mget");

        data = command.add_data();
        data->set_str("a");

        data = command.add_data();
        data->set_str("b");

        send_data(socket, command);
        command.ParseFromString(receive_data(socket));

        std::cout << command.DebugString() << std::endl;

        google::protobuf::ShutdownProtobufLibrary();
        return 0;
}
