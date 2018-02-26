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


struct Options {
        Options() : host{"localhost:6379"}, show_version{} { }

        std::string host;
        bool show_version;
};


static Options parse_commandline(int argc, char** argv)
{
        Options options;
        bool show_help{};

        auto cli = (
                clipp::option("-h", "--host").set(options.host)
                        .doc("Set the host (and port, optional) to connect to [default: localhost:6379]"),
                clipp::option("--help").set(show_help).doc("Show help and exit."),
                clipp::option("--version").set(options.show_version).doc("Show version and exit.")
        );

        if (!clipp::parse(argc, argv, cli) || show_help) {
                std::cout << clipp::make_man_page(cli, argv[0]) << std::endl;
                std::exit(0);
        }

        if (options.show_version) {
                std::cout
                        << argv[0] << '\n'
                        << "Using resply version " << resply::version() << std::endl;
                std::exit(0);
        }

        return options;
}


/* case-sensitive string comparison */
static bool strcmp_icase(std::string str1, std::string str2)
{
        std::transform(str1.begin(), str1.end(), str1.begin(), ::tolower);
        std::transform(str2.begin(), str2.end(), str2.begin(), ::tolower);

        return str1 == str2;
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

                std::stringstream stream;
                stream << client.command(command);

                std::cout << stream.str() << std::endl;

                if (strcmp_icase(command.front(), "subscribe") || strcmp_icase(command.front(), "psubscribe")) {
                        client.listen_for_messages();
                }
        }

        client.close();

        std::cout << std::endl;
        return 0;
}
