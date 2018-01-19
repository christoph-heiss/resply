//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include <iostream>
#include <utility>
#include <clipp.h>

#include "resply.h"


namespace common {

struct Options {
        Options() : host{"localhost"}, port{"6379"}, show_version{} { }

        std::string host;
        std::string port;
        bool show_version;
};


Options parse_commandline(int argc, char** argv)
{
        Options options;
        bool show_help{};

        auto cli = (
                clipp::option("-h", "--host").set(options.host)
                                             .doc("Set the host to connect to [default: localhost]"),

                clipp::option("-p", "--port").set(options.port)
                                             .doc("Set the port to connect to [default: 6379]"),

                clipp::option("--help").set(show_help).doc("Show help and exit."),
                clipp::option("--version").set(options.show_version).doc("Show version and exit.")
        );

        if (!clipp::parse(argc, argv, cli) || show_help) {
                std::cout << clipp::make_man_page(cli, argv[0]) << std::endl;
                std::exit(0);
        }

        return options;
}

}
