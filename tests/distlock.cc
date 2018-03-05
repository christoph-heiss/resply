//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include <iostream>
#include "resply.h"


int main()
{
        resply::Redlock rlock1{"resply-test", {
                "localhost:6379", "localhost:6380", "localhost:6381",
                "localhost:6382", "localhost:6383"
        }};
        rlock1.initialize();

        resply::Redlock rlock2{"resply-test", {
                "localhost:6379", "localhost:6380", "localhost:6381",
                "localhost:6382", "localhost:6383"
        }};
        rlock2.initialize();

        std::cout << "Locking lock 1 (should succeed) ... ";
        size_t status1{rlock1.lock(750)};
        std::cout << (status1 ? "success" : "failed") << std::endl;

        std::cout << "Locking lock 2 (should fail) ... ";
        size_t status2{rlock2.lock(500)};
        std::cout << (status2 ? "success" : "failed") << std::endl;

        return status1 && !status2;
}
