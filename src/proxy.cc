//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include <iostream>

#include "clipp.h"
#include "spdlog/spdlog.h"
#include "resply.h"
#include "resp.pb.h"


struct Options {
        Options() : daemonize{}, show_version{} { }

        bool daemonize;
        bool show_version;
};


static Options parse_commandline(int argc, char** argv)
{
        Options options;
        bool show_help{};

        auto cli = (
                clipp::option("-d", "--daemonize").set(options.daemonize).doc("Fork to background."),
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


int main(int argc, char* argv[])
{
        GOOGLE_PROTOBUF_VERIFY_VERSION;

        auto options{parse_commandline(argc, argv)};

        if (options.show_version) {
                std::cout
                        << argv[0] << '\n'
                        << "Using resply version " << resply::version() << std::endl;
                return 0;
        }


        google::protobuf::ShutdownProtobufLibrary();

        return 0;
}
