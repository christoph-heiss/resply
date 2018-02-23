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
                ostream << result.string;
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

                        if (i != result.array.size()-1) {
                                ostream << '\n';
                        }
                }

                break;
        };

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
                auto results = resolver.resolve(host_, port_, error_code);
                check_asio_error(error_code);

                asio::connect(socket_, results, error_code);
                check_asio_error(error_code);
        }


        void close()
        {
                socket_.close();
        }


        Result command(const std::string& command)
        {
                asio::error_code error_code;

                asio::write(socket_, asio::buffer(command), error_code);
                check_asio_error(error_code);

                RespParser parser;
                asio::streambuf buffer;
                bool cont{false};

                do {
                        buffer.consume(buffer.size());
                        asio::read_until(socket_, buffer, '\n');
                        check_asio_error(error_code);
                        std::istream stream{&buffer};
                        cont = parser.parse(stream);
                } while (cont);

                return parser.result();
        }

private:
        const std::string host_;
        const std::string port_;
        const size_t timeout_;

        asio::io_context io_context_;
        asio::ip::tcp::socket socket_;
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

Result Client::finish_command(const std::string& command)
{
       return command.empty() ? Result{} : impl_->command(command);
}

std::vector<Result> Client::Pipeline::send()
{
        asio::error_code error_code;

        asio::write(client_.impl_->socket_, asio::buffer(commands_), error_code);
        check_asio_error(error_code);

        std::vector<Result> results;
        asio::streambuf buffer;

        for (size_t i{}; i < num_commands_; i++) {
                RespParser parser;
                bool cont{false};

                do {
                        if (!buffer.size()) {
                                asio::read_until(client_.impl_->socket_, buffer, '\n');
                                check_asio_error(error_code);
                        }
                        std::istream stream{&buffer};
                        cont = parser.parse(stream);
                } while (cont);

                results.push_back(std::move(parser.result()));
        }

        commands_.clear();
        return results;
}

Client::Pipeline& Client::Pipeline::finish_command(const std::string& command)
{
        if (!command.empty()) {
                commands_ += command + "\r\n";
                num_commands_++;
        }

        return *this;
}

}

