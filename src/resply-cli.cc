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

#include "cli-common.h"
#include "resply.h"


int main(int argc, char* argv[])
{
        auto options{common::parse_commandline(argc, argv)};

        if (options.show_version) {
                std::cout
                        << argv[0] << '\n'
                        << "Using resply version " << resply::version() << std::endl;
                return 0;
        }

        resply::Client client{options.host, options.port};
        client.connect();

        while (std::cin) {
                std::cout << options.host << ':' << options.port << "> ";

                std::string line;
                std::getline(std::cin, line);
                std::stringstream linestream{line};

                std::vector<std::string> command;
                while (linestream >> line) {
                        command.push_back(line);
                }

                std::stringstream sstream;
                sstream << client.command(command);

                std::cout << sstream.str() << std::endl;
        }

        client.close();

        std::cout << std::endl;
        return 0;
}
