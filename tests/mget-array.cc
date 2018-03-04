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

        client.command("set", "a", "1");
        client.command("set", "b", "2");
        client.command("del", "c");

        auto result{client.command("mget", "a", "b", "c")};

        return result.type == resply::Result::Type::Array &&
               result.array[0].type == resply::Result::Type::String && result.array[0].string == "1" &&
               result.array[1].type == resply::Result::Type::String && result.array[1].string == "2" &&
               result.array[2].type == resply::Result::Type::Nil;
}
