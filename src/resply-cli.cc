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

#include "cli-common.h"
#include "resply.h"


/* case-sensitive string comparison */
static bool strcmp_icase(std::string str1, std::string str2)
{
        std::transform(str1.begin(), str1.end(), str1.begin(), ::tolower);
        std::transform(str2.begin(), str2.end(), str2.begin(), ::tolower);

        return str1 == str2;
}


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
