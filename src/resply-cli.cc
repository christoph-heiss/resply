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
#include <cctype>
#include <algorithm>

#include "clipp.h"
#include "resply.h"


namespace {

struct Options {
        Options() : host{"localhost:6379"} { }

        std::string host;
};

Options parse_commandline(int argc, char** argv)
{
        Options options;
        bool show_help{}, show_version{};

        auto cli = (
                clipp::option("-h", "--host") & clipp::value("host", options.host)
                        .doc("Set the host (and port, optional) to connect to [default: localhost:6379]"),
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

}


int main(int argc, char* argv[])
{
        auto options{parse_commandline(argc, argv)};

        resply::Client client{options.host};
        client.connect();

        while (std::cin) {
                std::cout << client.host() << ':' << client.port() << "> ";

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

                resply::Result result{client.command(command)};
                std::cout << result << std::endl;

                if (result.type == resply::Result::Type::Array && result.array.size() > 0 &&
                    result.array[0].type == resply::Result::Type::String &&
                    (result.array[0].string == "subscribe" || result.array[0].string == "psubscribe")) {
                        client.listen_for_messages();
                }
        }

        client.close();

        std::cout << std::endl;
        return 0;
}
