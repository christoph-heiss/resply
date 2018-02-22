//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#pragma once

#include <cstddef>
#include <istream>
#include "resply.h"


/*! \brief A streaming parser for RESP.
 *
 *  This parser is written after the specs at <https://redis.io/topics/protocol>.
 */
class RespParser {
public:
        RespParser()
                : state_{State::NeedType}, remaining_bytes_{READ_UNTIL_EOL},
                  remaining_elements_{0}
        { }

        /*! \brief Does the actual parsing of the data.
         *  \param stream Content to parse.
         *  \return Status if still more data is needed.
         */
        bool parse(std::istream& stream);

        /*! \brief Returns the (current) result.
         *  \return The current result.
         *
         *  Should only be called after the parsing is complete, i.e. #parse returned false.
         */
        const resply::Result& result() const { return result_; }

private:
        /*! \brief Represents the current internal parser state. */
        enum class State {
                NeedType, /*!< The parser needs a datatype for the result. */
                NeedSize, /*!< The parser needs a size for the result. */
                NeedData, /*!< The parser needs some (more) data. */
                Finished  /*!< Parsing is finished (duh.) */
        };

        /*! \brief The response was either a Simple String or Error - which means reading until CRLF */
        const long READ_UNTIL_EOL = -1;

        /*! \brief Sets the parser state according to the type for further parsing. */
        void parse_type(char type);
        void parse_type(char type, resply::Result& result);

        /*! \brief Sets #state_ and #remaining_ accordingly for further parsing. */
        void parse_size(const std::string& buffer);

        /*! \brief Consume data and puts them into the result. */
        void parse_data(const std::string& buffer);

        resply::Result result_;
        State state_;
        long remaining_bytes_;
        long remaining_elements_;
};


