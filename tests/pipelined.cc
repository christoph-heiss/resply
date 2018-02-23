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

        client.command("set", "a", "0");
        auto result = client.pipelined()
                .command("incr", "a")
                .command("incr", "a")
                .command("incr", "a")
                .send();

        return !(result.size() == 3 &&
               result[0].integer == 1 &&
               result[1].integer == 2 &&
               result[2].integer == 3);
}
