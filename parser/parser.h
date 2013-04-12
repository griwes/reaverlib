/**
 * Reaver Library Licence
 *
 * Copyright (C) 2012-2013 Reaver Project Team:
 * 1. Michał "Griwes" Dominiak
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation is required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Michał "Griwes" Dominiak
 *
 **/

/**
 * Watch out, crazy template code ahead. All hope abandon ye who enter here.
 *
 * Lexer is full of run-time, er, somethings, this one is full of compile time
 * ones. And run-time too. I *did* write `all hope abandon`, didn't I?
 */

#pragma once

#include <type_traits>

#include <boost/optional.hpp>
#include <boost/variant.hpp>

#include <reaver/parser/lexer.h>
#include <reaver/tmp.h>

namespace reaver
{
    namespace parser
    {
        class parser
        {
        };

        namespace _detail
        {
            // TODO: deep removal of optionals when constructing variants

            template<typename Ret, typename... Args>
            struct _constructor
            {
                static Ret construct(const Args &... args)
                {
                    return Ret{ args... };
                }
            };

            template<typename Ret, typename... Args1, typename Opt, typename... Args2>
            struct _constructor<Ret, Args1..., boost::optional<Opt>, Args2...>
            {
                static Ret construct(const Args1 &... args1, const boost::optional<Opt> opt, const Args2 &... args2)
                {
                    return _constructor<Ret, Args1..., Opt, Args2...>::construct(args1..., *opt, args2...);
                }
            };

            template<typename Ret, typename... Args1, typename... TupleTypes, typename... Args2>
            struct _constructor<Ret, Args1..., std::tuple<TupleTypes...>, Args2...>
            {
                template<int... I>
                static Ret unpack(const Args1... args1, const std::tuple<TupleTypes...> & t, const Args2 &... args2)
                {
                    return _constructor<Ret, Args1..., TupleTypes..., Args2...>::construct(args1..., std::get<I>(t)..., args2...);
                }

                static Ret construct(const Args1... args1, const std::tuple<TupleTypes...> & t, const Args2 &... args2)
                {
                    return unpack<generator<sizeof...(TupleTypes)>::value>(args1..., t, args2...);
                }
            };

            template<typename Ret, typename... Args>
            struct _constructor<boost::optional<Ret>, Args...>
            {
                static boost::optional<Ret> construct(const Args &... args)
                {
                    return { Ret{ args... } };
                }
            };

            template<typename Ret, typename Arg>
            struct _constructor<Ret, boost::optional<Arg>>
            {
                static Ret construct(const boost::optional<Arg> & arg)
                {
                    return _constructor<Ret, Arg>::construct(*arg);
                }
            };

            template<typename Ret, typename Arg>
            struct _constructor<boost::optional<Ret>, boost::optional<Arg>>
            {
                static boost::optional<Ret> construct(const boost::optional<Arg> & arg)
                {
                    return { Ret{ *arg } };
                }
            };

            template<typename Ret, typename T>
            struct _constructor<Ret, typename std::enable_if<is_vector<Ret>::value && is_vector<T>::value, T>::type>
            {
                // construct vector of objects from a vector of values
                static Ret construct(const T & vec)
                {
                    Ret ret(vec.size());

                    for (auto it = vec.begin(); it != vec.end(); ++it)
                    {
                        ret.emplace_back(_constructor<typename T::value_type>::construct(*it));
                    }

                    return ret;
                }
            };

            class _skip_wrapper : public parser
            {
            public:
                virtual ~_skip_wrapper() {}

                virtual bool match(std::vector<lexer::token>::const_iterator &, std::vector<lexer::token>::const_iterator) const = 0;
            };

            template<typename T>
            class _skip_wrapper_impl : public _skip_wrapper
            {
            public:
                _skip_wrapper_impl(const T & skip) : _skip_ref{ skip }
                {
                }

                virtual bool match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end) const
                {
                    return _skip_ref.match(begin, end);
                }

            private:
                const T & _skip_ref;
            };

            template<typename Ret>
            class _converter
            {
            public:
                virtual ~_converter() {}

                virtual Ret get(std::vector<lexer::token>::const_iterator &, std::vector<lexer::token>::const_iterator) const = 0;

                template<typename T>
                void set_skip(const T & skip)
                {
                    _skip.reset(new _skip_wrapper_impl<T>{ skip });
                }

            protected:
                std::shared_ptr<_skip_wrapper> _skip;
            };

            template<typename Ret, typename Parser, typename = typename std::enable_if<!std::is_same<typename Parser::value_type,
                void>::value>::type>
            class _converter_impl : public _converter<Ret>
            {
            public:
                _converter_impl(const Parser & p) : _parser{ p }
                {
                }

                virtual Ret get(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end) const
                {
                    while (this->_skip->match(begin, end)) {}

                    return _constructor<Ret, typename Parser::value_type>::construct(_parser.match(begin, end, *this->_skip));
                }

            private:
                Parser _parser;
            };

            class _def_skip : public parser
            {
            public:
                using value_type = bool;

                bool match(std::vector<lexer::token>::const_iterator &, std::vector<lexer::token>::const_iterator) const
                {
                    return false;
                }
            };
        }

        template<typename T>
        class rule : public parser
        {
        public:
            using value_type = boost::optional<T>;

            rule() : _type{ -1 }, _converter{}
            {
            }

            rule(const rule & rhs) : _type{ rhs._type }, _converter{ rhs._converter }
            {
            }

            template<typename U, typename = typename std::enable_if<std::is_base_of<parser, U>::value &&
                std::is_constructible<T, typename U::value_type::value_type>::value>::type>
            rule(const U & p) : _type{ -1 }, _converter{ new _detail::_converter_impl<T, U>{ p } }
            {
            }

            // directly use lexer token description as a parser
            // in this case, the type check is done at runtime - therefore, produces less helpful error messages
            rule(const lexer::token_description & desc) : _type(desc.type())
            {
            }

            // directly use lexer token *definition* as a parser
            // in *this* case, the type check is done at compile time - error messages are compile time
            rule(const lexer::token_definition<T> & def) : _type(def.type())
            {
            }

            template<typename U>
            rule(const lexer::token_definition<U> &)
            {
                static_assert(std::is_same<T, U>::value, "You cannot create a rule directly from token definition with another match type.");
            }

            template<typename U, typename = typename std::enable_if<std::is_base_of<parser, U>::value>::type>
            rule & operator=(const U & parser)
            {
                _type = -1;
                _converter.reset(new _detail::_converter_impl<T, U>{ parser });
                return *this;
            }

            // directly use lexer token description as a parser
            // in this case, the type check is done at runtime - therefore, produces less helpful error messages
            rule & operator=(const lexer::token_description & desc)
            {
                _type = desc.type();
                _converter.reset();
                return * this;
            }

            // directly use lexer token *definition* as a parser
            // in *this* case, the type check is done at compile time - error messages are compile time
            rule & operator=(const lexer::token_definition<T> & def)
            {
                _type = def.type();
                _converter.reset();
                return *this;
            }

            template<typename U>
            rule & operator=(const lexer::token_definition<U> &)
            {
                static_assert(std::is_same<T, U>::value, "You cannot create a rule directly from token definition with another match type.");
                return *this;
            }

            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end) const
            {
                return match(begin, end, _detail::_def_skip{});
            }

            template<typename Skip, typename = typename std::enable_if<std::is_base_of<parser, Skip>::value>::type>
            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end, const Skip & skip) const
            {
                while (skip.match(begin, end)) {}

                if (begin == end)
                {
                    return {};
                }

                if (_type != -1)
                {
                    if (begin->type() == _type)
                    {
                        return { (begin++)->as<T>() };
                    }

                    else
                    {
                        return {};
                    }
                }

                else if (_converter)
                {
                    _converter->set_skip(skip);
                    return _converter->get(begin, end);
                }

                else
                {
                    throw std::runtime_error{ "Called match on empty rule." };
                }
            }

        private:
            uint64_t _type;
            std::shared_ptr<_detail::_converter<T>> _converter;
        };

        template<typename T>
        rule<T> token(const lexer::token_definition<T> & def)
        {
            return rule<T>{ def };
        }

        template<typename T>
        rule<T> token(const lexer::token_description & desc)
        {
            return rule<T>{ desc };
        }

        template<typename T>
        class not_parser : public parser
        {
        public:
            using value_type = bool;

            not_parser(const T & np) : _negated{ np }
            {
            }

            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end) const
            {
                return match(begin, end, _detail::_def_skip{});
            }

            template<typename Skip, typename = typename std::enable_if<std::is_base_of<parser, Skip>::value>::type>
            value_type match(std::vector<lexer::token>::const_iterator begin, std::vector<lexer::token>::const_iterator end, const Skip & skip) const
            {
                while (skip.match(begin, end)) {}

                return !_negated.match(begin, end, skip);
            }

        private:
            T _negated;
        };

        template<typename T>
        class and_parser : public parser
        {
        public:
            using value_type = bool;

            and_parser(const T & ap) : _and{ ap }
            {
            }

            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end) const
            {
                return match(begin, end, _detail::_def_skip{});
            }

            template<typename Skip, typename = typename std::enable_if<std::is_base_of<parser, Skip>::value>::type>
            value_type match(std::vector<lexer::token>::const_iterator begin, std::vector<lexer::token>::const_iterator end, const Skip & skip) const
            {
                while (skip.match(begin, end)) {}

                return _and.match(begin, end, skip);
            }

        private:
            T _and;
        };

        template<typename T, typename = typename std::enable_if<!std::is_same<typename T::value_type, void>::value>::type>
        class optional_parser : public parser
        {
        public:
            using value_type = boost::optional<typename T::value_type>;

            optional_parser(const T & opt) : _optional{ opt }
            {
            }

            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end) const
            {
                return match(begin, end, _detail::_def_skip{});
            }

            template<typename Skip, typename = typename std::enable_if<std::is_base_of<parser, Skip>::value>::type>
            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end, const Skip & skip) const
            {
                while (skip.match(begin, end)) {}

                return _optional.match(begin, end, skip);
            }

        private:
            T _optional;
        };

        template<typename T, typename = typename std::enable_if<is_optional<typename T::value_type>::value>::type>
        class kleene_parser : public parser
        {
        public:
            using value_type = std::vector<typename T::value_type::value_type>;

            kleene_parser(const T & other) : _kleene{ other }
            {
            }

            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end) const
            {
                return match(begin, end, _detail::_def_skip{});
            }

            template<typename Skip, typename = typename std::enable_if<std::is_base_of<parser, Skip>::value>::type>
            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end, const Skip & skip) const
            {
                while (skip.match(begin, end)) {}

                value_type ret;

                boost::optional<typename value_type::value_type> val;

                while (val = _kleene.match(begin, end, skip))
                {
                    ret.emplace_back(_detail::_constructor<typename value_type::value_type, typename T::value_type>
                        ::construct(val));

                    while (skip.match(begin, end)) {}
                }

                return ret;
            }

        private:
            T _kleene;
        };

        template<typename T, typename = typename std::enable_if<is_optional<typename T::value_type>::value>::type>
        class plus_parser : public parser
        {
        public:
            using value_type = boost::optional<std::vector<typename T::value_type::value_type>>;

            plus_parser(const T & other) : _plus{ other }
            {
            }

            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end) const
            {
                return match(begin, end, _detail::_def_skip{});
            }

            template<typename Skip, typename = typename std::enable_if<std::is_base_of<parser, Skip>::value>::type>
            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end, const Skip & skip) const
            {
                value_type ret{ typename value_type::value_type{} };

                auto b = begin;
                while (skip.match(b, end)) {}
                auto val = _plus.match(b, end, skip);

                if (!val)
                {
                    return {};
                }

                do
                {
                    ret->emplace_back(_detail::_constructor<typename value_type::value_type::value_type, typename T::value_type>
                        ::construct(val));

                    while (skip.match(b, end)) {}
                } while (val = _plus.match(b, end, skip));

                begin = b;

                return ret;
            }

        private:
            T _plus;
        };

        namespace _detail
        {
            template<typename>
            struct _is_variant_parser : public std::false_type
            {
            };

            template<typename T>
            struct _is_variant_parser<typename std::enable_if<is_variant<typename T::value_type::value_type>::value, T>::type>
                : public std::true_type
            {
            };
        }

        template<typename T, typename U, typename = typename std::enable_if<(is_optional<typename T::value_type>::value
            || _detail::_is_variant_parser<T>::value) && (is_optional<typename U::value_type>::value
            || _detail::_is_variant_parser<U>::value)>::type>
        class variant_parser : public parser
        {
        public:
            using value_type = boost::optional<typename make_variant_type<typename T::value_type, typename U::value_type>::type>;

            variant_parser(const T & first, const U & second) : _first{ first }, _second{ second }
            {
            }

            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end) const
            {
                return match(begin, end, _detail::_def_skip{});
            }

            template<typename Skip, typename = typename std::enable_if<std::is_base_of<parser, Skip>::value>::type>
            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end, const Skip & skip) const
            {
                auto b = begin;

                while (skip.match(b, end)) {}

                auto f = _first.match(b, end, skip);

                if (f)
                {
                    begin = b;
                    return { _detail::_constructor<value_type, typename T::value_type>::construct(f) };
                }

                auto s = _second.match(b, end, skip);

                if (s)
                {
                    begin = b;
                    return { _detail::_constructor<value_type, typename U::value_type>::construct(s) };
                }

                return {};
            }

        private:
            T _first;
            U _second;
        };

        template<typename T, typename U, typename = typename std::enable_if<std::is_base_of<parser, T>::value
            && std::is_base_of<parser, U>::value && !(std::is_same<typename T::value_type, void>::value
            && std::is_same<typename U::value_type, void>::value) && (std::is_same<typename T::value_type, void>::value
                || is_vector<typename T::value_type>::value || is_optional<typename T::value_type>::value)
            && (std::is_same<typename U::value_type, void>::value || is_vector<typename U::value_type>::value
                || is_optional<typename U::value_type>::value)>::type>
        class sequence_parser : public parser
        {
        public:
            using value_type = boost::optional<typename std::conditional<
                !std::is_same<typename T::value_type, void>::value
                    && std::is_same<typename T::value_type, typename U::value_type>::value,
                typename std::conditional<
                    is_vector<typename T::value_type>::value,
                    typename T::value_type,
                    std::vector<typename T::value_type>
                >::type,
                typename std::conditional<
                    std::is_same<typename T::value_type, void>::value,
                    std::vector<typename U::value_type>,
                    typename make_tuple_type<typename T::value_type, typename U::value_type>::type
                >::type
            >::type>; // this is not even my final form! ...and probably some bugs on the way

            sequence_parser(const T & first, const U & second) : _first{ first }, _second{ second }
            {
            }

            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end) const
            {
                return match(begin, end, _detail::_def_skip{});
            }

            template<typename Skip, typename = typename std::enable_if<std::is_base_of<parser, Skip>::value>::type>
            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end, const Skip & skip) const
            {
                auto b = begin;

                while (skip.match(b, end)) {}
                auto first_matched = _first.match(b, end, skip);

                if (!_matched(first_matched))
                {
                    return {};
                }

                while (skip.match(b, end)) {}
                auto second_matched = _second.match(b, end, skip);

                if (!_matched(second_matched))
                {
                    return {};
                }

                begin = b;

                return _detail::_constructor<value_type, typename T::value_type, typename U::value_type>::construct(first_matched, second_matched);
            }

        private:
            template<typename V>
            struct _checker
            {
                static bool matched(const V & v)
                {
                    return v;
                }
            };

            template<typename V>
            struct _checker<std::vector<V>>
            {
                static bool matched(const std::vector<V> &)
                {
                    return true;
                }
            };

            template<typename V>
            bool _matched(const V & v) const
            {
                return _checker<V>::matched(v);
            }

            T _first;
            U _second;
        };

        template<typename T, typename U>
        class difference_parser : public parser
        {
        public:
            using value_type = typename T::value_type;

            difference_parser(const T & match, const U & dont) : _match{ match }, _dont{ dont }
            {
            }

            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end) const
            {
                return match(begin, end, _detail::_def_skip{});
            }

            template<typename Skip, typename = typename std::enable_if<std::is_base_of<parser, Skip>::value>::type>
            value_type match(std::vector<lexer::token>::const_iterator & begin, std::vector<lexer::token>::const_iterator end, const Skip & skip) const
            {
                while (skip.match(begin, end)) {}

                auto b = begin;
                auto m = _match.match(b, end, skip);

                while (skip.match(b, end)) {}

                auto b2 = b;
                auto d = _dont.match(b, end, skip);

                if (m && !d)
                {
                    begin = b;
                    return { m };
                }

                return {};
            }

        private:
            T _match;
            U _dont;
        };

        template<typename T, typename U>
        class seqor_parser : public parser
        {
            using value_type = typename std::conditional<
                std::is_same<typename T::value_type, void>::value,
                typename std::conditional<
                    std::is_same<typename U::value_type, void>::value,
                    void,
                    boost::optional<typename U::value_type>
                >::type,
                typename std::conditional<
                    std::is_same<typename U::value_type, void>::value,
                    boost::optional<typename T::value_type>,
                    std::tuple<boost::optional<typename T::value_type>, boost::optional<typename U::value_type>>
                >::type
            >::type;
        };

        template<typename T, typename U>
        class list_parser : public parser
        {
            using value_type = std::vector<typename T::value_type>; // no void, sorry - you can hardly have a list of voids
            // and it hardly makes any sense
        };

        template<typename T, typename U>
        class expect_parser;

        template<typename T, typename = typename std::enable_if<std::is_base_of<parser, T>::value>::type>
        not_parser<T> operator!(const T & parser)
        {
            return { parser };
        }

        template<typename T, typename = typename std::enable_if<std::is_base_of<parser, T>::value>::type>
        and_parser<T> operator&(const T & parser)
        {
            return { parser };
        }

        template<typename T, typename = typename std::enable_if<std::is_base_of<parser, T>::value>::type>
        optional_parser<T> operator-(const T & parser)
        {
            return { parser };
        }

        template<typename T, typename = typename std::enable_if<std::is_base_of<parser, T>::value>::type>
        plus_parser<T> operator+(const T & parser)
        {
            return { parser };
        }

        template<typename T, typename = typename std::enable_if<std::is_base_of<parser, T>::value>::type>
        kleene_parser<T> operator*(const T & parser)
        {
            return { parser };
        }

        template<typename T, typename U, typename = typename std::enable_if<std::is_base_of<parser, T>::value &&
            std::is_base_of<parser, U>::value>::type>
        variant_parser<T, U> operator|(const T & lhs, const U & rhs)
        {
            return { lhs, rhs };
        }

        template<typename T, typename U, typename = typename std::enable_if<std::is_base_of<parser, T>::value &&
            std::is_base_of<parser, U>::value>::type>
        sequence_parser<T, U> operator>>(const T & lhs, const U & rhs)
        {
            return { lhs, rhs };
        }

        template<typename T, typename U, typename = typename std::enable_if<std::is_base_of<parser, T>::value &&
            std::is_base_of<parser, U>::value>::type>
        difference_parser<T, U> operator-(const T & lhs, const U & rhs)
        {
            return { lhs, rhs };
        }

        template<typename T, typename U, typename = typename std::enable_if<std::is_base_of<parser, T>::value &&
            std::is_base_of<parser, U>::value>::type>
        seqor_parser<T, U> operator||(const T & lhs, const U & rhs)
        {
            return { lhs, rhs };
        }

        template<typename T, typename U, typename = typename std::enable_if<std::is_base_of<parser, T>::value &&
            std::is_base_of<parser, U>::value>::type>
        list_parser<T, U> operator%(const T & lhs, const U & rhs)
        {
            return { lhs, rhs };
        }
    }
}
