//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/http_proto
//

#ifndef BOOST_HTTP_PROTO_IMPL_BASIC_PARSER_IPP
#define BOOST_HTTP_PROTO_IMPL_BASIC_PARSER_IPP

#include <boost/http_proto/basic_parser.hpp>
#include <boost/http_proto/ctype.hpp>
#include <boost/http_proto/error.hpp>
#include <boost/assert.hpp>
#include <memory>

namespace boost {
namespace http_proto {

//------------------------------------------------

basic_parser::
basic_parser(
    context& ctx) noexcept
    : ctx_(ctx)
    , buffer_(nullptr)
    , capacity_(0)
    , committed_(0)
    , parsed_(0)
    , state_(state::nothing_yet)
    , header_limit_(8192)
    , skip_(0)
    , f_(0)
{
}

basic_parser::
~basic_parser()
{
    if(buffer_)
        delete[] buffer_;
}

void
basic_parser::
reset()
{
}

std::pair<void*, std::size_t>
basic_parser::
prepare()
{
    if(! buffer_)
    {
        buffer_ = new char[4096];
        capacity_ = 4096;
    }
    else if(capacity_ <= committed_)
    {
        auto buffer = new char[
            capacity_ + 4096];
        std::memcpy(buffer,
            buffer_, committed_);
        delete[] buffer_;
        buffer_ = buffer;
        capacity_ = capacity_ + 4096;
    }

    return {
        buffer_ + committed_,
        capacity_ - committed_ };
}

void
basic_parser::
commit(
    std::size_t n)
{
    // VFALCO Not sure about this
    BOOST_ASSERT(n > 0);

    BOOST_ASSERT(n <=
        capacity_ - committed_);
    committed_ += n;
}

void
basic_parser::
commit_eof()
{
}

//------------------------------------------------

void
basic_parser::
parse_header(
    error_code& ec)
{
    ec = {};

    // parse algorithms assume at
    // least one committed char.
    if(committed_ == 0)
    {
        ec = error::need_more;
        return;
    }

    auto first = buffer_ + parsed_;
    char const* const last =
        buffer_ + committed_;

    switch(state_)
    {
    case state::nothing_yet:
        state_ = state::start_line;
        BOOST_FALLTHROUGH;

    case state::start_line:
    {
        // Nothing can come before start-line
        BOOST_ASSERT(parsed_ == 0);
        parse_start_line(first, last, ec);
        if(ec)
            goto finish;
        state_ = state::fields;
        BOOST_FALLTHROUGH;
    }

    case state::fields:
    {
        parse_fields(first, last, ec);
        if(ec)
            goto finish;
        BOOST_ASSERT(! ec);
        state_ = state::body;
        break;
    }

    default:
        break;
    }

finish:
    parsed_ = first - buffer_;
}

void
basic_parser::
parse_body(
    error_code& ec)
{
    (void)ec;
    switch(state_)
    {
    case state::nothing_yet:
        state_ = state::start_line;
        BOOST_FALLTHROUGH;
    }
}

void
basic_parser::
parse_body_part(
    error_code& ec)
{
    (void)ec;
    switch(state_)
    {
    case state::nothing_yet:
        state_ = state::start_line;
        BOOST_FALLTHROUGH;
    }
}

void
basic_parser::
parse_chunk_ext(
    error_code& ec)
{
    (void)ec;
}

void
basic_parser::
parse_chunk_part(
    error_code& ec)
{
    (void)ec;
}

void
basic_parser::
parse_chunk_trailer(
    error_code& ec)
{
    (void)ec;
}

string_view
basic_parser::
body() const
{
    return {nullptr, 0};
}

//------------------------------------------------

void
basic_parser::
parse_version(
    char*& first,
    char const* const last,
    error_code& ec)
{
    auto const need_more =
        [&ec]{ ec = error::need_more; };
    auto it = first;
    if(last - it < 8)
        return need_more();
    if(std::memcmp(it, "HTTP/1.", 7) != 0)
    {
        ec = error::bad_version;
        return;
    }
    it += 7;
    if(*it == '0')
    {
        version_ = 0;
    }
    else if(*it == '1')
    {
        version_ = 1;
    }
    else
    {
        ec = error::bad_version;
        return;
    }
    ++it;
    first = it;
}

//------------------------------------------------

void
basic_parser::
parse_fields(
    char*& first,
    char const* last,
    error_code& ec)
{
    auto const need_more =
        [&ec]{ ec = error::need_more; };
    auto in = first;
    for(;;)
    {
        if(last - in < 2)
            return need_more();
        if(in[0] == '\r')
        {
            if(in[1] != '\n')
            {
                ec = error::bad_line_ending;
                return;
            }
            // end of header
            in = in + 2;
            first = in;
            break;
        }
        in = parse_field(
            in, last, ec);
        if(ec)
            return;
        first = in;
    }
}

char*
basic_parser::
parse_field(
    char* const start,
    char const* end,
    error_code& ec)
{
/*
    header-field   = field-name ":" OWS field-value OWS

    field-name      = token
    field-value     = *( field-content / obs-fold )
    field-content   = field-vchar [ 1*( SP / HTAB ) field-vchar ]

    obs-fold        = CRLF 1*( SP / HTAB )
                    ; obsolete line folding
                    ; see Section 3.2.4

    token           = 1*tchar
    tchar           = "!" / "#" / "$" / "%" / "&" / "'" /
                      "*" / "+" / "-" / "." / "^" / "_" /
                      "`" / "|" / "~" / DIGIT / ALPHA
*/
    BOOST_ASSERT(start != end);
    auto in = start;
    char const* k1; // end of name
    char const* v0  // start of value
        = nullptr;
    char const* v1; // end of value

    ws_set ws;
    tchar_set ts;
    field_vchar_set fvs;

    // make sure we have at least
    // 3 chars, to detect obs-fold
    if(end - in < 3)
    {
        ec = error::need_more;
        return start;
    }
    end -= 3;

    // field-name
    in = ts.skip(in, end);

    // ":"
    if(in == end)
    {
        ec = error::need_more;
        return start;
    }
    if(*in != ':')
    {
        // invalid field char
        ec = error::bad_field;
        return start;
    }
    if(in == start)
    {
        // empty field name
        ec = error::bad_field;
        return start;
    }
    k1 = in;
    ++in;

    // OWS
    in = ws.skip(in, end);

    // *( field-content / obs-fold )
    for(;;)
    {
        if(in == end)
        {
            ec = error::need_more;
            return start;
        }

        // check field-content first, as
        // it is more frequent than CRLF
        if(fvs.contains(*in))
        {
            // field-content
            if(! v0)
                v0 = in;
            ++in;
            // *field-vchar
            in = fvs.skip(in, end);
            if(in == end)
            {
                ec = error::need_more;
                return start;
            }
            v1 = in;
            //  1*( SP / HTAB )
            if(ws.contains(*in))
            {
                in = ws.skip(in, end);
                if(in[0] == '\r')
                {
                    if(in[1] != '\n')
                    {
                        // expected LF
                        ec = error::bad_line_ending;
                        return start;
                    }
                    if(ws.contains(in[2]))
                    {
                        // illegal obs-fold
                        ec = error::bad_value;
                        return start;
                    }
                    // end of line
                    v1 = in;
                    in += 2;
                    break;
                }
            }
        }

        // obs-fold / CRLF
        if(in[0] == '\r')
        {
            if(in[1] != '\n')
            {
                // expected LF
                ec = error::bad_line_ending;
                return start;
            }
            if(! ws.contains(in[2]))
            {
                // end of line
                if(! v0)
                    v0 = in;
                v1 = in;
                in += 2;
                break;
            }
            // replace CRLF with SP SP
            in[0] = ' ';
            in[1] = ' ';
            in[2] = ' ';
            in += 3;
            // *( SP / HTAB )
            in = ws.skip(in, end);
            continue;
        }

        // illegal value
        ec = error::bad_field;
        return start;
    }

    string_view k(start, k1 - start);
    string_view v(v0, v1 - v0);
    auto const f =
        string_to_field(k);
    switch(f)
    {
    case field::connection:
    case field::proxy_connection:
        do_connection(v, ec);
        if(ec)
            return in;
        break;
    case field::content_length:
        do_content_length(v, ec);
        if(ec)
            return in;
        break;
    case field::transfer_encoding:
        do_transfer_encoding(v, ec);
        if(ec)
            return in;
        break;
    case field::upgrade:
        do_upgrade(v, ec);
        if(ec)
            return in;
        break;
    default:
        break;
    }
    return in;
}

//------------------------------------------------

// https://datatracker.ietf.org/doc/html/rfc7230#section-6.1
void
basic_parser::
do_connection(
    string_view v, error_code& ec)
{
    (void)v;
    (void)ec;
}

//------------------------------------------------

void
basic_parser::
do_content_length(
    string_view v, error_code& ec)
{
    (void)v;
    (void)ec;
}

void
basic_parser::
do_transfer_encoding(
    string_view v, error_code& ec)
{
    (void)v;
    (void)ec;
}

void
basic_parser::
do_upgrade(
    string_view v, error_code& ec)
{
    (void)v;
    (void)ec;
}

} // http_proto
} // boost

#endif
