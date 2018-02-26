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
#include <functional>


namespace resply {
        /*! \brief Function signature for channel callbacks. */
        typedef std::function<void(const std::string& channel, const std::string& message)> ChannelCallback;


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

                /*! \brief Constructs a new (empty) nil-result. */
                Result() : type{Type::Nil} { }

                /*! \brief Constructs  a new integer-result.
                 *  \param integer The value of the result.
                 */
                Result(long long integer) : type{Type::Integer}, integer{integer} { }

                /*! \brief Holds the type of the response. */
                Type type;

                /*! \brief Use when #type is Type::String, Type::ProtocolError or Type::IOError */
                std::string string;

                /*! \brief Use when #type is Type::Integer */
                long long integer;

                /*! \brief Use when #type is Type::Array */
                std::vector<Result> array;

                /*! \brief This outputs the stringify'd version of the response into the supplied stream.
                 *  \return Reference to the stream.
                 *
                 *  It acts according to the #type field.
                 *  If #type is either Type::ProtocolError or Type::IOError, "(error) " is
                 *  prepended to the error message. If #type is Type::Nil, the output is "(nil)".
                 */
                friend std::ostream& operator<<(std::ostream& ostream, const Result& result);
        };


        /*! \brief Implements a template-based RESP command serializer.
         *  \param R Return type for #command.
         */
        template <typename R>
        class RespCommandSerializer {
        public:
                /*! \brief Serializes a command and its parameters.
                 *  \param str List of command name and its parameters.
                 *  \return \p R
                 *
                 *  The command and parameters are automatically converted to RESP
                 *  as specificed at <https://redis.io/topics/protocol>.
                 */
                R command(const std::vector<std::string>& str)
                {
                        std::stringstream builder;

                        if (str.empty()) {
                                return command(builder);
                        }

                        builder << '*' << str.size() << "\r\n";

                        for (const std::string& part: str) {
                                builder << '$' << part.length() << "\r\n" << part << "\r\n";
                        }

                        return command(builder);
                }

                /*! \brief Serializes a command and its parameters.
                 *  \param str The name of the command.
                 *  \param args A series of command arguments.
                 *  \return \p R
                 *
                 *  The command and parameters are automatically converted to RESP
                 *  as specificed at <https://redis.io/topics/protocol>.
                 */
                template <typename... ArgTypes>
                R command(const std::string& str, ArgTypes... args)
                {
                        std::stringstream builder;

                        if (str.empty()) {
                                return command(builder);
                        }

                        builder << '*' << sizeof...(ArgTypes) + 1 << "\r\n";
                        return command(builder, str, args...);
                }

        private:
                /*! \brief String-specialization variant of #command. */
                template <typename... ArgTypes>
                R command(std::stringstream& builder, const std::string& str, ArgTypes... args)
                {
                        builder << '$' << str.length() << "\r\n" << str << "\r\n";
                        return command(builder, args...);
                }

                /*! \brief Number-specialization variant of #command. */
                template <typename T,
                          typename = typename std::enable_if<std::is_integral<T>::value, T>::type,
                          typename... ArgTypes>
                R command(std::stringstream& builder, T num, ArgTypes... args)
                {
                        return command(builder, std::to_string(num), args...);
                }

                /*! \brief Specialization of #command for ending recursion. */
                R command(const std::stringstream& builder)
                {
                        return finish_command(builder.str());
                }

        protected:
                /*! \brief Finishes a commmand. Semantics depend on derived class.
                 *  \param command The serialized command.
                 *  \return \p R
                 *
                 *  This method must be implemented by the derived class.
                 */
                virtual R finish_command(const std::string& command) = 0;
        };

        class ClientImpl;

        /*! \brief Redis client interface
         *
         *  This class implements the RESP to communicate with a redis server.
         */
        class Client : public RespCommandSerializer<Result> {
        public:
                /*! \brief A pipelined redis client. */
                class Pipeline : public RespCommandSerializer<Pipeline&> {
                public:
                        /*! \brief Constructs a new pipelined client.
                         *  \param client A connected redis client.
                         */
                        Pipeline(Client& client) : client_{client} { }

                        /*! \brief Sends the batch of commands to the server.
                         *  \return The results of the commands.
                         */
                        std::vector<Result> send();

                private:
                        /*! \brief Adds the command to the batch.
                         *  \param command Command to add.
                         *  \return The pipelined client.
                         */
                        Pipeline& finish_command(const std::string& command) override;

                        /*! \brief Redis client connection this pipeline will use. */
                        Client& client_;

                        /*! \brief The batch of commands to send. */
                        std::vector<std::string> commands_;
                };

                /*! \brief Represents a pipelined redis client. */
                friend class Pipeline;

                /*! \brief Constructs a new redis client which connects to localhost:6379. */
                Client();

                /*! \brief Constructs a new redis client.
                 *  \param address Redis server address in the format "<host>[:<port>]".
                 *                 The ":port" component may be omitted, in which case it defaults to "6379".
                 *  \param timeout Timeout in milliseconds when connecting to server. Default are 500ms.
                 */
                explicit Client(const std::string& address, size_t timeout=500);

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

                /*! \brief Retrieves the address of the server this client is connected to.
                 *  \return The server address of the redis server.
                 */
                const std::string& host() const;

                /*! \brief Retrieves the port of the server this client is connected to.
                 *  \return The server port of the redis server.
                 */
                const std::string& port() const;

                /*! \brief Creates a new pipelined client using this client.
                 *  \return A pipelined client.
                 */
                Pipeline pipelined() {
                        return Pipeline(*this);
                }

                /*! \brief Indicates if the client is currently subscribed to any channels.
                 *  \return If the client is in subscription-mode.
                 *
                 *  If this returns true, the server will reject any command other than
                 *    UNSUBSCRIBE, PUNSUBSCRIBE, PING and QUIT.
                 *  For SUBSCRIBE and PSUBSCRIBE functionality use #subscribe and #psubscribe,
                 *    respectivly.
                 *  Only after unsubscribing to all channels, the client will return
                 *    to normal mode.
                 *
                 *  For more information regarding the pub/sub mechanism, look here:
                 *    <https://redis.io/topics/pubsub>
                 */
                bool in_subscribed_mode() const;

                /*! \brief Subscribes to a channel.
                 *  \param channel Name of the channel to subscribe to.
                 *  \param callback Callback for messages on this channel.
                 *  \return The client.
                 */
                Client& subscribe(const std::string& channel, ChannelCallback callback);

                /*! \brief Subscribes to multiple channels based on a pattern.
                 *  \param pattern Pattern of the channels to subscribe to.
                 *  \param callback Callback for messages on the channels.
                 *  \return The client.
                 */
                Client& psubscribe(const std::string& pattern, ChannelCallback callback);

                /*! \brief Puts the client into subscribed mode.
                 *
                 *  This method will not return until the client has been unsubscribed
                 *  from all channels.
                 */
                void listen_for_messages();

        private:
                /*! \brief Sends the command to the server.
                 *  \param command The command to send.
                 *  \return The result of the command.
                 */
                Result finish_command(const std::string& command) override;

                /*! \brief Internal client implementation. */
                std::unique_ptr<ClientImpl> impl_;
        };
}
