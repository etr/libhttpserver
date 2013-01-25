/*
     This file is part of libhttpserver
     Copyright (C) 2011 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 
     USA
*/

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef _BINDERS_HPP_
#define _BINDERS_HPP_

namespace httpserver {
namespace details
{
    namespace binders
    {
        class generic_class;
        const int MEMFUNC_SIZE = sizeof(void (generic_class::*)());

        template<int N>
        struct converter
        {
            template<typename X,
                typename func_type,
                typename generic_mem_func_type>
            inline static generic_class* convert(
                    X* pmem,
                    func_type func,
                    generic_mem_func_type &bound
            )
            {
                typedef char ERROR_member_pointer_not_supported[N-100];
                return 0;
            }
        };

        template<>
        struct converter<MEMFUNC_SIZE>
        {
            template<typename X,
                typename func_type, 
                typename generic_mem_func_type>
            inline static generic_class* convert(
                    X* pmem,
                    func_type func,
                    generic_mem_func_type &bound
            )
            {
                bound = reinterpret_cast<generic_mem_func_type>(func);
                return reinterpret_cast<generic_class*>(pmem);
            }
        };

        template<typename generic_mem,
            typename static_function,
            typename void_static_function>
        class binder
        {
            private:
                typedef void (generic_class::*generic_mem_fun)();
                generic_class *pmem;
                generic_mem_fun _pfunc;

                typedef void (*generic_mem_ptr)();
                generic_mem_ptr _spfunc;

            public:
                template<typename X, typename Y>
                inline void bind(X* pmem, Y fun)
                {
                    this->pmem =
                        converter<sizeof(fun)>::convert(pmem, fun, _pfunc);
                    _spfunc = 0;
                }
                template <class DC, class parent_invoker>
                inline void bind_static(DC *pp,
                        parent_invoker invoker,
                        static_function fun
                )
                {
                    if (fun==0)
                        _pfunc=0;
                    else
                        bind(pp, invoker);
                    _spfunc = reinterpret_cast<generic_mem_ptr>(fun);
                }
                inline generic_class* exec() const
                {
                    return pmem;
                }
                inline generic_mem get_mem_ptr() const
                {
                    return reinterpret_cast<generic_mem>(_pfunc);
                }
                inline void_static_function get_static_func() const
                {
                    return reinterpret_cast<void_static_function>(_spfunc);
                }
        };

        template<typename RET_TYPE=void>
        class functor_zero
        {
            private:
                typedef RET_TYPE (*static_function)();
                typedef RET_TYPE (*void_static_function)();
                typedef RET_TYPE (generic_class::*generic_mem)();
                typedef binder<generic_mem,
                        static_function, void_static_function> binder_type;
                binder_type _binder;

                RET_TYPE exec_static() const
                {
                    return (*(_binder.get_static_func()))();
                }
            public:
                typedef functor_zero type;
                functor_zero() { }

                template <typename X, typename Y>
                functor_zero(Y* pmem, RET_TYPE(X::*func)() )
                {
                    _binder.bind(reinterpret_cast<X*>(pmem), func);
                }
                functor_zero(RET_TYPE(*func)() )
                {
                    bind(func);
                }
                template < class X, class Y >
                inline void bind(Y* pmem, RET_TYPE(X::*func)()) 
                {
                    _binder.bind(reinterpret_cast<X*>(pmem), func);
                }
                inline void bind(RET_TYPE(*func)())
                {
                    _binder.bind_static(this, &functor_zero::exec_static, func);
                }
                RET_TYPE operator() () const
                {
                    return (_binder.exec()->*(_binder.get_mem_ptr()))();
                }
        };

        template<typename PAR1, typename RET_TYPE=void>
        class functor_one
        {
            private:
                typedef RET_TYPE (*static_function)(PAR1 p1);
                typedef RET_TYPE (*void_static_function)(PAR1 p1);
                typedef RET_TYPE (generic_class::*generic_mem)(PAR1 p1);
                typedef binder<generic_mem,
                        static_function, void_static_function> binder_type;
                binder_type _binder;

                RET_TYPE exec_static(PAR1 p1) const
                {
                    return (*(_binder.get_static_func()))(p1);
                }
            public:
                typedef functor_one type;
                functor_one() { }

                template <typename X, typename Y>
                functor_one(Y* pmem, RET_TYPE(X::*func)(PAR1 p1) )
                {
                    _binder.bind(reinterpret_cast<X*>(pmem), func);
                }
                functor_one(RET_TYPE(*func)(PAR1 p1) )
                {
                    bind(func);
                }
                template < class X, class Y >
                inline void bind(Y* pmem, RET_TYPE(X::*func)(PAR1 p1)) 
                {
                    _binder.bind(reinterpret_cast<X*>(pmem), func);
                }
                inline void bind(RET_TYPE(*func)(PAR1 P1))
                {
                    _binder.bind_static(this, &functor_one::exec_static, func);
                }
                RET_TYPE operator() (PAR1 p1) const
                {
                    return (_binder.exec()->*(_binder.get_mem_ptr()))(p1);
                }
        };

        template<typename PAR1, typename PAR2, typename RET_TYPE=void>
        class functor_two
        {
            private:
                typedef RET_TYPE (*static_function)(PAR1 p1, PAR2 p2);
                typedef RET_TYPE (*void_static_function)(PAR1 p1, PAR2 p2);

                typedef RET_TYPE 
                    (generic_class::*generic_mem)(PAR1 p1, PAR2 p2);

                typedef binder<
                    generic_mem, static_function, void_static_function
                > binder_type;

                binder_type _binder;

                RET_TYPE exec_static(PAR1 p1, PAR2 p2) const
                {
                    return (*(_binder.get_static_func()))(p1, p2);
                }
            public:
                typedef functor_two type;
                functor_two() { }

                template <typename X, typename Y>
                functor_two(Y* pmem, RET_TYPE(X::*func)(PAR1 p1, PAR2 p2) )
                {
                    _binder.bind(reinterpret_cast<X*>(pmem), func);
                }
                functor_two(RET_TYPE(*func)(PAR1 p1, PAR2 p2) )
                {
                    bind(func);
                }
                template < class X, class Y >
                inline void bind(Y* pmem, RET_TYPE(X::*func)(PAR1 p1, PAR2 p2))
                {
                    _binder.bind(reinterpret_cast<X*>(pmem), func);
                }
                inline void bind(RET_TYPE(*func)(PAR1 P1, PAR2 p2))
                {
                    _binder.bind_static(this, &functor_two::exec_static, func);
                }
                RET_TYPE operator() (PAR1 p1, PAR2 p2) const
                {
                    return (_binder.exec()->*(_binder.get_mem_ptr()))(p1, p2);
                }
        };

        template<typename PAR1,
            typename PAR2, typename PAR3, typename RET_TYPE=void>
        class functor_three
        {
            private:
                typedef RET_TYPE (*static_function)(PAR1 p1, PAR2 p2, PAR3 p3);

                typedef RET_TYPE 
                    (*void_static_function)(PAR1 p1, PAR2 p2, PAR3 p3);

                typedef RET_TYPE
                    (generic_class::*generic_mem)(PAR1 p1, PAR2 p2, PAR3 p3);

                typedef binder<
                    generic_mem, static_function, void_static_function
                > binder_type;

                binder_type _binder;

                RET_TYPE exec_static(PAR1 p1, PAR2 p2, PAR3 p3) const
                {
                    return (*(_binder.get_static_func()))(p1, p2, p3);
                }
            public:
                typedef functor_three type;
                functor_three() { }

                template <typename X, typename Y>
                functor_three(Y* pmem, RET_TYPE(X::*func)(
                            PAR1 p1, PAR2 p2, PAR3 p3) 
                )
                {
                    _binder.bind(reinterpret_cast<X*>(pmem), func);
                }
                functor_three(RET_TYPE(*func)(PAR1 p1, PAR2 p2, PAR3 p3) )
                {
                    bind(func);
                }
                template < class X, class Y >
                inline void bind(Y* pmem,
                        RET_TYPE(X::*func)(PAR1 p1, PAR2 p2, PAR3 p3)
                )
                {
                    _binder.bind(reinterpret_cast<X*>(pmem), func);
                }
                inline void bind(RET_TYPE(*func)(PAR1 P1, PAR2 p2, PAR3 p3))
                {
                    _binder.bind_static(this, &functor_three::exec_static,func);
                }
                RET_TYPE operator() (PAR1 p1, PAR2 p2, PAR3 p3) const
                {
                    return (_binder.exec()->*(_binder.get_mem_ptr()))(p1,p2,p3);
                }
        };

        template<typename PAR1,
            typename PAR2, typename PAR3, typename PAR4, typename RET_TYPE=void>
        class functor_four
        {
            private:
                typedef RET_TYPE
                    (*static_function)(PAR1 p1, PAR2 p2, PAR3 p3, PAR4 p4);

                typedef RET_TYPE
                    (*void_static_function)(PAR1 p1, PAR2 p2, PAR3 p3, PAR4 p4);

                typedef RET_TYPE (generic_class::*generic_mem)
                    (PAR1 p1, PAR2 p2, PAR3 p3, PAR4 p4);

                typedef binder<
                    generic_mem, static_function, void_static_function
                > binder_type;

                binder_type _binder;

                RET_TYPE exec_static(PAR1 p1, PAR2 p2, PAR3 p3, PAR4 p4) const
                {
                    return (*(_binder.get_static_func()))(p1, p2, p3, p4);
                }
            public:
                typedef functor_four type;
                functor_four() { }

                template <typename X, typename Y>
                functor_four(Y* pmem,
                        RET_TYPE(X::*func)(PAR1 p1, PAR2 p2, PAR3 p3, PAR4 p4)
                )
                {
                    _binder.bind(reinterpret_cast<X*>(pmem), func);
                }
                functor_four(RET_TYPE(*func)(PAR1 p1,PAR2 p2,PAR3 p3,PAR4 p4))
                {
                    bind(func);
                }
                template < class X, class Y >
                inline void bind(Y* pmem,
                        RET_TYPE(X::*func)(PAR1 p1, PAR2 p2, PAR3 p3, PAR4 p4)
                )
                {
                    _binder.bind(reinterpret_cast<X*>(pmem), func);
                }
                inline void bind(
                        RET_TYPE(*func)(PAR1 P1, PAR2 p2, PAR3 p3, PAR4 p4)
                )
                {
                    _binder.bind_static(this, &functor_four::exec_static, func);
                }
                RET_TYPE operator() (PAR1 p1, PAR2 p2, PAR3 p3, PAR4 p4) const
                {
                    return 
                        (_binder.exec()->*(_binder.get_mem_ptr()))(p1,p2,p3,p4);
                }
        };
    }
}}

#endif
