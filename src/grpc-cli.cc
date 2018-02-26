//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include <iostream>

#include "clipp.h"


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


int main(int argc, char* argv[])
{
        auto options{parse_commandline(argc, argv)};

        return 0;
}
