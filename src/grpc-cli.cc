//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <functional>

#include "clipp.h"
#include "grpc++/grpc++.h"
#include "rslp.pb.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "rslp.grpc.pb.h"
#pragma GCC diagnostic pop


namespace {

struct Options {
        Options() : host{"localhost:6544"} { }

        std::string host;
};

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

Options parse_commandline(int argc, char** argv)
{
        Options options;
        bool show_help{};

        auto cli = (
                clipp::option("-h", "--host") & clipp::value("host", options.host)
                        .doc("Sets the host and port to connect to [default: localhost:6544]"),
                clipp::option("--help").set(show_help).doc("Show help and exit.")
        );

        if (!clipp::parse(argc, argv, cli) || show_help) {
                std::cout << clipp::make_man_page(cli, argv[0]) << std::endl;
                std::exit(0);
        }

        return options;
}

class GrpcResplyClient {
public:
        explicit GrpcResplyClient(std::shared_ptr<grpc::Channel> channel) :
                stub_{rslp::ProtoAdapter::NewStub(channel)}
        { }

        rslp::Command command(const std::vector<std::string>& arguments)
        {
                rslp::Command command;

                for (const std::string& arg: arguments) {
                        command.add_data()->set_str(arg);
                }

                grpc::ClientContext context;
                rslp::Command response;

                if (stub_->execute(&context, command, &response).ok()) {
                        return response;
                } else {
                        return {};
                }
        }

        void subscribe(const std::vector<std::string>& arguments,
                       std::function<void(const std::string& channel, const std::string& message)> callback)
        {
                rslp::Command command;

                for (const std::string& arg: arguments) {
                        command.add_data()->set_str(arg);
                }

                grpc::ClientContext context;
                rslp::Command response;

                auto reader{stub_->subscribe(&context, command)};
                reader->Read(&response);

                while (reader->Read(&response)) {
                        callback(response.data(1).str(), response.data(2).str());
                }

                reader->Finish();
        }

private:
        std::unique_ptr<rslp::ProtoAdapter::Stub> stub_;
};

}


int main(int argc, char* argv[])
{
        auto options{parse_commandline(argc, argv)};

        auto channel{grpc::CreateChannel(
                options.host, grpc::InsecureChannelCredentials()
        )};
        GrpcResplyClient client{channel};

        while (std::cin) {
                std::cout << options.host << "> ";

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

                std::string name{command.front()};
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);

                if (name == "subscribe" || name == "psubscribe") {
                        client.subscribe(command, [](const auto& channel, const auto& message) {
                                std::cout << channel << ": " << message << std::endl;
                        });
                } else {
                        rslp::Command result{client.command(command)};
                        std::cout << result << std::endl;
                }
        }

        std::cout << std::endl;

        google::protobuf::ShutdownProtobufLibrary();
        return 0;
}
