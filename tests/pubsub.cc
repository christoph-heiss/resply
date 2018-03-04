//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include <future>
#include <string>
#include <utility>
#include <thread>
#include <chrono>
#include "resply.h"

using namespace std::literals;


int main()
{
        resply::Client client1, client2;
        client1.connect();
        client2.connect();

        std::promise<std::pair<std::string, std::string>> result;
        client1.subscribe("a", [&](const auto& channel, const auto& message) {
                result.set_value(std::make_pair(channel, message));
        });

        std::thread{[&]() { client1.listen_for_messages(); }}.detach();

        std::this_thread::sleep_for(1s);
        client2.command("publish", "a", "pubsub-test");

        auto message{result.get_future().get()};

        return message.first == "a" && message.second == "pubsub-test";
}
