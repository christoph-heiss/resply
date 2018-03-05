//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include <sstream>
#include <cctype>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <unordered_map>
#include <cctype>
#include <fstream>
#include <array>
#include <chrono>
#include <thread>

#include <asio.hpp>

#include "resply.h"
#include "resp-parser.h"


namespace {

bool check_asio_error(asio::error_code& error_code)
{
        if (error_code) {
                std::cerr << error_code.message() << std::endl;
        }

        return !!error_code;
}

long get_system_clock_ms()
{
        namespace chrono = std::chrono;

        auto now{chrono::system_clock::now()};
        auto millisec{chrono::time_point_cast<chrono::milliseconds>(now)};

        return millisec.time_since_epoch().count();
}

}


namespace resply {

const std::string& version()
{
        const static std::string version{RESPLY_VERSION};

        return version;
}


std::ostream& operator<<(std::ostream& ostream, const Result& result)
{
        switch (result.type) {
        case Result::Type::ProtocolError:
        case Result::Type::IOError:
                ostream << "(error) ";
                [[fallthrough]];

        case Result::Type::String:
                ostream << '"' << result.string << '"';
                break;

        case Result::Type::Integer:
                ostream << result.integer;
                break;

        case Result::Type::Nil:
                ostream << "(nil)";
                break;

        case Result::Type::Array:
                for (size_t i{}; i < result.array.size(); i++) {
                        ostream << i+1 << ") " << result.array[i];

                        if (i < result.array.size()-1) {
                                ostream << '\n';
                        }
                }

                break;
        }

        return ostream;
}


class ClientImpl {
public:
        friend class Client;

        ClientImpl(const std::string& host, const std::string& port, size_t timeout)
                : host_{host}, port_{port}, timeout_{timeout}, socket_{io_context_}
        {
                (void)timeout_;
        }


        ~ClientImpl()
        {
                close();
        }


        void connect()
        {
                asio::error_code error_code;

                asio::ip::tcp::resolver resolver{io_context_};
                auto results{resolver.resolve(host_, port_, error_code)};
                check_asio_error(error_code);

                asio::connect(socket_, results, error_code);
                check_asio_error(error_code);
        }


        void close()
        {
                socket_.close();
        }


        Result send(const std::string& command)
        {
                asio::error_code error_code;

                asio::write(socket_, asio::buffer(command), error_code);
                check_asio_error(error_code);

                return in_subscribed_mode() ? Result{} : receive_response();
        }

        std::vector<Result> send_batch(const std::vector<std::string>& commands)
        {
                asio::error_code error_code;
                const std::string raw_commands{
                        std::accumulate(commands.cbegin(), commands.cend(), std::string{})
                };

                asio::write(socket_, asio::buffer(raw_commands), error_code);
                check_asio_error(error_code);

                return receive_responses(commands.size());
        }

        void listen_for_messages(ChannelCallback other)
        {
                for (;;) {
                        Result result{receive_response()};

                        if (result.type == Result::Type::Array && result.array.size() == 3 &&
                            result.array.front().type == Result::Type::String && result.array.front().string == "message") {
                                std::string& channel = result.array[1].string;
                                std::string& message = result.array[2].string;

                                if (channel_callbacks_.count(channel)) {
                                        channel_callbacks_[channel](channel, message);
                                } else {
                                        other(channel, message);
                                }
                        }
                }
        }

        std::unordered_map<std::string, ChannelCallback>& channel_callbacks()
        {
                return channel_callbacks_;
        }

        const std::string& host() const
        {
                return host_;
        }

        const std::string& port() const
        {
                return port_;
        }

        bool is_connected() const
        {
                return socket_.is_open();
        }

        bool in_subscribed_mode() const
        {
                return channel_callbacks_.size();
        }

private:
        Result receive_response()
        {
                return receive_responses(1).front();
        }

        std::vector<Result> receive_responses(size_t num)
        {
                asio::error_code error_code;
                asio::streambuf buffer;
                std::vector<Result> results;

                for (size_t i{}; i < num; i++) {
                        RespParser parser;
                        bool cont{false};

                        do {
                                if (!buffer.size()) {
                                        asio::read_until(socket_, buffer, '\n');
                                        check_asio_error(error_code);
                                }
                                std::istream stream{&buffer};
                                cont = parser.parse(stream);
                        } while (cont);

                        results.push_back(parser.result());
                }

                return results;
        }

        const std::string host_;
        const std::string port_;
        const size_t timeout_;

        asio::io_context io_context_;
        asio::ip::tcp::socket socket_;

        std::unordered_map<std::string, ChannelCallback> channel_callbacks_;

};


Client::Client() : Client{"localhost", "6379"} { }


Client::Client(const std::string& address, size_t timeout)
{
        std::string host, port;
        std::stringstream sstream{address};

        std::getline(sstream, host, ':');
        std::getline(sstream, port);

        impl_ = std::make_unique<ClientImpl>(host, port, timeout);
}


Client::Client(const std::string& host, const std::string& port, size_t timeout)
        : impl_{std::make_unique<ClientImpl>(host, port, timeout)}
{
}


Client::~Client() { }


void Client::connect() { impl_->connect(); }
void Client::close() { impl_->close(); }
const std::string& Client::host() const { return impl_->host(); }
const std::string& Client::port() const { return impl_->port(); }
bool Client::is_connected() const { return impl_->is_connected(); }

bool Client::in_subscribed_mode() const
{
        return impl_->in_subscribed_mode();
}

Client& Client::subscribe(const std::string& channel, ChannelCallback callback)
{
        impl_->channel_callbacks()[channel] = callback;
        command("subscribe", channel);

        return *this;
}

Client& Client::psubscribe(const std::string& pattern, ChannelCallback callback)
{
        impl_->channel_callbacks()[pattern] = callback;
        command("psubscribe", pattern);

        return *this;
}

Result Client::finish_command(const std::string& command)
{
        return command.empty() ? Result{} : impl_->send(command);
}

void Client::listen_for_messages(ChannelCallback other)
{
        impl_->listen_for_messages(other);
}

std::vector<Result> Client::Pipeline::send()
{
        if (commands_.empty()) {
                return {};
        }

        auto results = client_.impl_->send_batch(commands_);

        commands_.clear();
        return results;
}

Client::Pipeline& Client::Pipeline::finish_command(const std::string& command)
{
        std::string lower;
        std::transform(command.begin(), command.end(), lower.begin(), ::tolower);

        if (!command.empty() && lower.find("subscribe") == std::string::npos) {
                commands_.emplace_back(command + "\r\n");
        }

        return *this;
}


Redlock::Redlock(std::string resource_name, const std::vector<std::string>& hosts) :
        resource_name_{resource_name}, lock_value_{generate_lock_value()},
        retry_count_{3}, retry_delay_max_{250}, random_number_gen_{std::random_device()()}
{
        for (const std::string& host: hosts) {
                clients_.push_back(std::make_shared<Client>(host));
        }
}

Redlock::Redlock(std::string resource_name, std::vector<std::shared_ptr<Client>> clients) :
        clients_{std::move(clients)}, resource_name_{resource_name}, lock_value_{generate_lock_value()},
        retry_count_{3}, retry_delay_max_{250}, random_number_gen_{std::random_device()()}
{

}

Redlock::Redlock(std::string resource_name, const std::initializer_list<std::string> hosts) :
        resource_name_{resource_name}, lock_value_{generate_lock_value()},
        retry_count_{3}, retry_delay_max_{250}, random_number_gen_{std::random_device()()}
{
        for (const std::string& host: hosts) {
                clients_.push_back(std::make_shared<Client>(host));
        }
}

Redlock::Redlock(std::string resource_name, std::initializer_list<std::shared_ptr<Client>> clients) :
        clients_{std::move(clients)}, resource_name_{resource_name}, lock_value_{generate_lock_value()},
        retry_count_{3}, retry_delay_max_{250}, random_number_gen_{std::random_device()()}
{

}

Redlock::~Redlock()
{
        unlock();
}

void Redlock::initialize()
{
        for (auto& client: clients_) {
                if (!client->is_connected()) {
                        client->connect();
                }
        }
}

size_t Redlock::lock(size_t ttl)
{
        for (size_t retries{}; retries < retry_count_; retries++) {
                size_t locked{};
                long start_time{get_system_clock_ms()};

                for (auto& client: clients_) {
                        locked += lock_instance(client, ttl);
                }

                size_t drift{ttl / CLOCK_DRIFT_DIV};
                size_t valid_time{ttl - (get_system_clock_ms() - start_time) - drift};

                // We need to have at least N/2 + 1 instances locked
                if (locked >= (clients_.size() / 2 + 1) && valid_time > 0) {
                        return valid_time;
                } else {
                        unlock();
                }

                // Retry after random delay
                std::this_thread::sleep_for(get_random_delay());
        }

        return 0;
}

void Redlock::unlock()
{
        for (auto& client: clients_) {
                unlock_instance(client);
        }
}

bool Redlock::lock_instance(std::shared_ptr<Client> client, size_t ttl)
{
        auto result{client->command("set", resource_name_, lock_value_, "NX", "PX", ttl)};

        return result.type == Result::Type::String && result.string == "OK";
}

void Redlock::unlock_instance(std::shared_ptr<Client> client)
{
        client->command("eval", UNLOCK_SCRIPT_, 1, resource_name_, lock_value_);
}

std::chrono::milliseconds Redlock::get_random_delay()
{
        std::uniform_int_distribution<> dist(1, retry_delay_max_);

        return std::chrono::milliseconds{dist(random_number_gen_)};
}

std::string Redlock::generate_lock_value()
{
        static const std::string BASE36_LUT{"0123456789abcdefghijklmnopqrstuvwxyz"};

        std::ifstream file{"/dev/urandom", std::ios_base::binary};
        std::array<char, 20> buffer;
        file.read(buffer.data(), 20);

        std::string uid;
        for (unsigned char byte: buffer) {
                while (byte) {
                        uid += BASE36_LUT[byte % 36];
                        byte /= 36;
                }
        }

        return uid;
}

std::string Redlock::UNLOCK_SCRIPT_ = R"(
if redis.call('get', KEYS[1]) == ARGV[1] then
        return redis.call('del', KEYS[1])
else
        return 0
end
)";

}

