//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include "resply.h"


int main()
{
        resply::Client client;
        client.connect();

        std::stringstream stream;
        stream << client.command("ping");

        return stream.str() != "PONG";
}
