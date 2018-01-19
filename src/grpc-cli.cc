//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include "cli-common.h"


int main(int argc, char* argv[])
{
        auto options{common::parse_commandline(argc, argv)};

        return 0;
}
