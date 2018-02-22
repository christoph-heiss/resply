//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#include <iostream>
#include <string>
#include <cstdlib>

#include "resp-parser.h"


using resply::Result;

enum RespTypes {
        SIMPLE_STRING = '+',
        ERROR = '-',
        INTEGER = ':',
        BULK_STRING = '$',
        ARRAY = '*'
};


bool RespParser::parse(std::istream& stream)
{
        std::string line;

        while (state_ != State::Finished && !stream.eof()) {
                switch (state_) {
                case State::NeedType:
                        parse_type(stream.get());
                        break;

                case State::NeedSize:
                        std::getline(stream, line);
                        line.pop_back();
                        parse_size(line);
                        break;

                case State::NeedData: {
                        std::getline(stream, line);
                        if (line.back() == '\r') {
                                line.push_back('\n');
                        }

                        parse_data(line);
                        break;
                }

                case State::Finished:
                        break;
                }

                // Update internal stream state.
                // Especially, sets the eofbit appropriatly if we read all data.
                // This is needed because when using .read() does not update the
                // internal stream state, i.e. does not set eofbit even if all data
                // was read.
                stream.peek();
        }

        if (result_.type == Result::Type::Array && !remaining_elements_) {
                state_ = State::Finished;
        }

        return remaining_bytes_ <= 0 || remaining_elements_ ? false : state_ != State::Finished;
}


void RespParser::parse_type(char type)
{
        if (result_.type == Result::Type::Array) {
                Result result;
                parse_type(type, result);
                result_.array.push_back(result);
        } else {
                parse_type(type, result_);
        }
}


void RespParser::parse_type(char type, Result& result)
{
        switch (type) {
        case RespTypes::SIMPLE_STRING:
                result.type = Result::Type::String;
                state_ = State::NeedData;
                break;

        case RespTypes::ERROR:
                result.type = Result::Type::ProtocolError;
                state_ = State::NeedData;
                break;

        case RespTypes::INTEGER:
                result.type = Result::Type::Integer;
                state_ = State::NeedData;
                break;

        case RespTypes::BULK_STRING:
                result.type = Result::Type::String;
                state_ = State::NeedSize;
                break;

        case RespTypes::ARRAY:
                result.type = Result::Type::Array;
                state_ = State::NeedSize;
                break;

        default:
                result.type = Result::Type::ProtocolError;
                result.string = "Parsing error.";
                state_ = State::Finished;
                break;
        }
}


void RespParser::parse_size(const std::string& buffer)
{
        long size{std::stol(buffer)};

        if (size == -1) {
                Result& result = result_.type == Result::Type::Array ? result_.array.back() : result_;

                result.type = Result::Type::Nil;
                state_ = State::Finished;
        } else {
                if (result_.type == Result::Type::Array && !remaining_elements_) {
                        remaining_elements_ = size;
                        state_ = State::NeedType;
                } else {
                        remaining_bytes_ = size;
                        state_ = State::NeedData;
                }
        }
}


void RespParser::parse_data(const std::string& buffer)
{
        Result::Type type{result_.type == Result::Type::Array ? result_.array.back().type : result_.type};

        switch (type) {
        case Result::Type::String:
        case Result::Type::ProtocolError:
        case Result::Type::IOError:
                if (result_.type == Result::Type::Array) {
                        result_.array.back().string += buffer;
                } else {
                        result_.string += buffer;
                }

                if (remaining_bytes_ == READ_UNTIL_EOL) {
                        // Response was a simple string
                        state_ = State::Finished;
                        result_.string.pop_back();
                        result_.string.pop_back();
                } else {
                        remaining_bytes_ -= buffer.length();

                        if (remaining_bytes_ <= 0) {
                                if (result_.type == Result::Type::Array) {
                                        result_.array.back().string.pop_back();
                                        result_.array.back().string.pop_back();
                                        remaining_elements_--;
                                } else {
                                        state_ = State::Finished;
                                        result_.string.pop_back();
                                        result_.string.pop_back();
                                }
                        }
                }

                break;

        case Result::Type::Integer:
                if (result_.type == Result::Type::Array) {
                        result_.array.emplace_back(std::stoll(buffer));
                        remaining_elements_--;
                } else {
                        result_.integer = std::stoll(buffer);
                        state_ = State::Finished;
                }

                break;

        case Result::Type::Array:
        default:
                break;
        }

        if (remaining_bytes_ <= 0 && remaining_elements_ > 0) {
                state_ = State::NeedType;
        }
}
