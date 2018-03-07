//
// Copyright 2018 Christoph Heiss <me@christoph-heiss.me>
// Distributed under the Boost Software License, Version 1.0.
//
// See accompanying file LICENSE in the project root directory
// or copy at http://www.boost.org/LICENSE_1_0.txt
//

#pragma once


/*! \brief Very simple re-implementation of std::{experimental::}optional.
 *  \param T Type of the value the optional should hold.
 *
 *  Until std::{experimental::}optional is standardized, this can be used as a
 *  very simple and dumb replacement.
 */
template <typename T>
class Optional {
public:
        /*! \brief Type of the value the optional holds. */
        typedef T value_type;

        /*! \brief Constructs a new, empty optional.
         *
         *  The default value will be default-constructed.
         */
        Optional() : has_value_{}, default_value_{} { }

        /*! \brief Constructos a new optional with an default value.
         *  \param default_value The default value the optional will hold.
         */
        Optional(T default_value) : has_value_{}, default_value_{default_value} { }

        /*! \brief Indicates if the optional holds a value.
         *  \return If the optional holds a value.
         */
        bool has_value() const
        {
                return has_value_;
        }

        /*! \brief Gets the value.
         *  \return The value the optional holds.
         *
         *  If the optional has no value, a default-constructed object is returned.
         */
        const T& value() const&
        {
                return value_;
        }

        /*! \brief Returns the value if the optional has one, otherwise the default value.
         *  \return The value of the optional if it has one, otherwise the default value.
         */
        const T& value_or_default() const&
        {
                return has_value() ? value() : default_value();
        }

        /*! \brief Sets the value of the optional.
         *  \param val The new value of the optional.
         *  \return The new value of the optional.
         */
        const T& set_value(T&& val)
        {
                has_value_ = true;
                value_ = std::move(val);
                return value();
        }

        /*! \brief Returns the default value of the optional.
         *  \return The default value of the optional.
         */
        const T& default_value() const&
        {
                return default_value_;
        }

private:
        /*! \brief Indicates if this optional has a value set. */
        bool has_value_;

        /*! \brief The value that may or may not be present.
         *
         *  Its present is indicated by #has_value_
         */
        T value_;

        /*! \brief The default value of this optional. */
        const T default_value_;
};
