//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include <cstdlib>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <iostream>

#include <asio.hpp>

#include "resply.h"


namespace {

bool is_number(const std::string& str) {
        return !str.empty() && std::all_of(str.begin(), str.end(), ::isdigit);
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
        case Result::Type::ProtError:
        case Result::Type::IOError:
                ostream << "(error) ";
                /* fallthrough */

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
                for (const auto& res: result.array) {
                        ostream << res;
                }

                break;
        };

        return ostream;
}


class ClientImpl {
public:
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

                Result result;
                asio::streambuf buffer;
                size_t remaining{};

                do {
                        asio::read(socket_, buffer, error_code);
                        check_asio_error(error_code);

                        parse_response(result, buffer, remaining);
                } while (remaining);

                return result;
        }

private:
        static bool check_asio_error(asio::error_code& error_code)
        {
                if (error_code) {
                        std::cerr << error_code.message() << std::endl;
                }

                return !!error_code;
        }

        static void parse_response(Result& result, asio::streambuf& streambuf, size_t& remaining)
        {
                std::string buffer{asio::buffers_begin(streambuf.data()),
                                   asio::buffers_end(streambuf.data())};

                if (!remaining) {
                        // First pass
                        parse_type(result, buffer, remaining);
                } else {
                        // All other
                        continue_parse_type(result, buffer, remaining);
                }

                streambuf.consume(streambuf.size());
        }

        static void parse_type(Result& result, const std::string& buffer, size_t& remaining)
        {
                switch (buffer.front()) {
                case '+':
                        result.type = Result::Type::String;
                        // Exclude the final \r\n bytes
                        result.string += buffer.substr(1, buffer.length() - 3);

                        break;


                case '-':
                        result.type = Result::Type::ProtError;
                        // Exclude the final \r\n bytes
                        result.string += buffer.substr(1, buffer.length() - 3);

                        break;

                case ':':
                        result.type = Result::Type::Integer;
                        result.integer = std::stoll(buffer.substr(1));

                        break;

                case '$': {
                        size_t size;
                        long length{std::stol(buffer.substr(1), &size)};

                        if (length == -1) {
                                result.type = Result::Type::Nil;
                                break;
                        }

                        result.type = Result::Type::String;
                        if (static_cast<size_t>(length) < buffer.length()) {
                                result.string += buffer.substr(size + 3, buffer.length() - size - 3);
                        } else {
                                result.string += buffer.substr(size + 3);
                                remaining = length - result.string.length();
                        }

                        break;
                }
                case '*': {
                        size_t size;
                        long length{std::stol(buffer.substr(1), &size)};

                        if (length == -1) {
                                result.type = Result::Type::Nil;
                                break;
                        }

                        result.type = Result::Type::Array;
                        remaining = length;

                        while (remaining) {

                        }

                        break;
                }
                default:
                        // Error
                        break;
                }
        }

        static void continue_parse_type(Result& result, const std::string& buffer, size_t& remaining)
        {
                switch (result.type) {
                case Result::Type::String:
                case Result::Type::ProtError:
                case Result::Type::IOError:
                        if (remaining == buffer.length() - 2) {
                                result.string += buffer.substr(0, remaining);
                                remaining = 0;
                        } else {
                                result.string += buffer;
                                remaining -= buffer.length();
                        }
                default:
                        break;
                }
        }

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

Result Client::command(const std::vector<std::string>& str)
{
        if (str.empty()) {
                return {};
        }

        std::stringstream builder;
        builder << '*' << str.size() << "\r\n";

        for (const std::string& part: str) {
                if (is_number(part)) {
                        builder << ':' << part << "\r\n";
                } else {
                        builder << '$' << part.length() << "\r\n" << part << "\r\n";
                }
        }

        return impl_->command(builder.str());
}

Result Client::command(const std::stringstream& builder)
{
       return impl_->command(builder.str());
}

}

