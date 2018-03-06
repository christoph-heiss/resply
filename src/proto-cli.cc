//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include <iostream>
#include <string>
#include <vector>
#include <ostream>
#include <arpa/inet.h>
#include <functional>

#include "asio.hpp"
#include "clipp.h"
#include "rslp.pb.h"


using namespace google;

struct Options {
        Options() : host{"localhost"}, port{"6543"} { }

        std::string host;
        std::string port;
};


namespace {

Options parse_commandline(int argc, char** argv)
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

std::ostream& operator<<(std::ostream& ostream, const rslp::Command& command)
{
        using Type = rslp::Command_Data::DataCase;

        for (int i{}; i < command.data_size(); i++) {
                auto data{command.data(i)};

                if (command.data_size() > 1) {
                        ostream << i+1 << ") ";
                }

                switch (data.data_case()) {
                case Type::kErr:
                        ostream << "(error) \"" << data.err() << '"';
                        break;

                case Type::kStr:
                        ostream << '"' << data.str() << '"';
                        break;

                case Type::kInt:
                        ostream << data.int_();
                        break;

                case Type::kArray:
                        ostream << data.array();
                        break;

                default:
                        ostream << "(nil)";
                        break;
                }

                if (command.data_size() > 1 && i < command.data_size()-1) {
                        ostream << '\n';
                }
        }

        return ostream;
}

class ProtobufResplyClient {
public:
        ProtobufResplyClient(const std::string& host, const std::string& port) :
                host_{host}, port_{port}, socket_{io_context_}
        { }

        bool connect()
        {
                asio::error_code error_code;
                asio::ip::tcp::resolver resolver{io_context_};

                auto results = resolver.resolve(host_, port_, error_code);
                if (error_code) {
                        std::cerr << "Error while connecting: " << error_code.message() << std::endl;
                        return false;
                }

                asio::connect(socket_, results, error_code);
                if (error_code) {
                        std::cerr << "Error while connecting: " << error_code.message() << std::endl;
                        return false;
                }

                return true;
        }

        void close()
        {
                socket_.close();
        }

        rslp::Command send_command(const std::vector<std::string>& arguments)
        {
                rslp::Command command;

                for (const std::string& arg: arguments) {
                        command.add_data()->set_str(arg);
                }

                send_data(command);

                command.ParseFromString(receive_data());
                return command;
        }

        void listen_for_messages(std::function<void(const std::string&, const std::string&)> callback)
        {
                for (;;) {
                        rslp::Command command;
                        command.ParseFromString(receive_data());

                        callback(command.data(1).str(), command.data(2).str());
                }
        }

private:
        std::string receive_data()
        {
                asio::error_code error_code;

                size_t size;
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

        void send_data(rslp::Command& command)
        {
                std::string output;
                command.SerializeToString(&output);

                uint32_t size{htonl(static_cast<uint32_t>(output.size()))};
                asio::write(socket_, asio::buffer(&size, 4));
                asio::write(socket_, asio::buffer(output.data(), output.size()));
        }

private:
        const std::string& host_;
        const std::string& port_;

        asio::io_context io_context_;
        asio::ip::tcp::socket socket_;
};

}


int main(int argc, char* argv[])
{
        GOOGLE_PROTOBUF_VERIFY_VERSION;
        auto options{parse_commandline(argc, argv)};

        ProtobufResplyClient client{options.host, options.port};
        if (!client.connect()) {
                return 1;
        }

        while (std::cin) {
                std::cout << options.host << ':' << options.port << "> ";

                std::string line;
                std::getline(std::cin, line);

                if (!line.length()) {
                        continue;
                }
                std::stringstream linestream{line};

                std::vector<std::string> command;
                while (linestream >> line) {
                        command.push_back(line);
                }

                rslp::Command result{client.send_command(command)};
                std::cout << result << std::endl;

                if (result.data_size() > 0) {
                        auto data{result.data(0)};
                        if (data.data_case() == rslp::Command_Data::DataCase::kStr &&
                            (data.str() == "subscribe" || data.str() == "psubscribe")) {
                                client.listen_for_messages([](const auto& channel, const auto& message) {
                                        std::cout << channel << ": " << message << std::endl;
                                });
                        }
                }
        }

        client.close();
        std::cout << std::endl;

        google::protobuf::ShutdownProtobufLibrary();
        return 0;
}
