//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstddef>
#include <sstream>
#include <type_traits>
#include <iostream>


namespace resply {
        /*! \return The version of the resply library. */
        const std::string& version();


        /*! \brief Holds the response of a redis command. */
        struct Result {
                /*! \brief Indicates the type of the response. */
                enum class Type {
                        String,
                        Integer,
                        Array,
                        ProtocolError,
                        IOError,
                        Nil
                };

                Result() : type{Type::Nil} { }

                /*! \brief Holds the type of the response. */
                Type type;

                /*! \brief Use when #type is Type::String, Type::ProtocolError or Type::IOError */
                std::string string;

                /*! \brief Use when #type is Type::Integer */
                long long integer;

                /*! \brief Use when #type is Type::Array */
                std::vector<Result> array;

                /*! \brief This outputs the stringify'd version of the response into the supplied stream.
                 *
                 *  It acts according to the #type field.
                 *  If #type is either Type::ProtocolError or Type::IOError, "(error) " is
                 *  prepended to the error message. If #type is Type::Nil, the output is "(nil)".
                 */
                friend std::ostream& operator<<(std::ostream& ostream, const Result& result);
        };


        class ClientImpl;

        /*! \brief Redis client interface
         *
         *  This class implements the RESP to communicate with a redis server.
         */
        class Client {
        public:
                /*! \brief Constructs a new redis client which connects to localhost:6379. */
                Client();

                /*! \brief Constructs a new redis client.
                 *  \param address Redis server address in the format "<host>[:<port>]".
                 *                 The ":port" component may be omitted, in which case it defaults to "6379".
                 *  \param timeout Timeout in milliseconds when connecting to server. Default are 500ms.
                 */
                Client(const std::string& address, size_t timeout=500);

                 /*! \brief Constructs a new redis client.
                  *  \param host Redis server address.
                  *  \param port Redis server port.
                  *  \param timeout Timeout in milliseconds when connecting to server. Default are 500ms.
                  */
                Client(const std::string& host, const std::string& port, size_t timeout=500);

                /*! \brief Closes the connection to the redis server. */
                ~Client();

                /*! \brief Establishes a connection to the server. */
                void connect();

                /*! \brief Closes the connection to the server.
                 *
                 *  Optional, is also called in the destructor.
                 */
                void close();

                /*! \brief Send a command to the server.
                 *  \param str List of command name and its parameters.
                 *  \return The result of the command.
                 *
                 *  The command and parameters are automatically converted to RESP
                 *  as specificed at <https://redis.io/topics/protocol>.
                 */
                Result command(const std::vector<std::string>& str);

                /*! \brief Send a command to the server.
                 *  \param str The name of the command.
                 *  \param args A series of command arguments.
                 *  \return The result of the command.
                 *
                 *  The command and parameters are automatically converted to RESP
                 *  as specificed at <https://redis.io/topics/protocol>.
                 */
                template <typename... ArgTypes>
                Result command(const std::string& str, ArgTypes... args)
                {
                        if (str.empty()) {
                                return {};
                        }

                        std::stringstream builder;

                        builder << '*' << sizeof...(ArgTypes) + 1 << "\r\n";
                        return command(builder, str, args...);
                }

        private:
                /*! \brief String-specialization variant of #command. */
                template <typename... ArgTypes>
                Result command(std::stringstream& builder, const std::string& str, ArgTypes... args)
                {
                        builder << '$' << str.length() << "\r\n" << str << "\r\n";
                        return command(builder, args...);
                }

                /*! \brief Number-specialization variant of #command. */
                template <typename T,
                          typename = typename std::enable_if<std::is_integral<T>::value, T>::type,
                          typename... ArgTypes>
                Result command(std::stringstream& builder, T num, ArgTypes... args)
                {
                        builder << ':' << num << "\r\n";
                        return command(builder, args...);
                }

                /*! \brief Specializtion of #command for ending recursion. */
                Result command(const std::stringstream& builder);

                std::unique_ptr<ClientImpl> impl_;
        };
}
