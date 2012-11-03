/*
     This file is part of liblittletest
     Copyright (C) 2012 Sebastiano Merlino

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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#ifndef _LITTLETEST_HPP_
#define _LITTLETEST_HPP_

#include <string>
#include <iostream>
#include <sstream>

#define BEGIN_TEST_ENV() int main() {
#define END_TEST_ENV() return 0; }
#define TEST(name) name ## _obj
#define CREATE_RUNNER(suite_name, runner_name) \
    std::cout << "** Initializing Runner \"" << #runner_name << "\" for suite \"" << #suite_name << "\" **" << std::endl; \
    test_runner<suite_name> runner_name 
#define RUNNER(runner_name) runner_name
#define SUITE(name) struct name : public suite<name>
#define BEGIN_TEST(suite_name, test_name) \
    struct test_name : public suite_name, test<suite_name, test_name> \
    { \
        public: \
            static const char* name; \
            void operator()(test_runner<suite_name>* tr) \
            {

#define END_TEST(test_name) \
            } \
    }; \
    const char* test_name::name = #test_name; \
    test_name test_name ## _obj;

#define CHECK_EQ(a, b) \
    try \
    { \
        check_eq(name, a, b, __FILE__, __LINE__); \
        tr->add_success(); \
    } \
    catch(check_unattended& cu) \
    { \
        std::cout << cu.what() << std::endl; \
        tr->add_failure(); \
    }

//#define CHECK_NEQ((a), (b)) check_eq(a, b);
//#define CHECK_GT((a), (b)) check_eq(a, b);
//#define CHECK_GTE((a), (b)) check_eq(a, b);
//#define CHECK_LT((a), (b)) check_eq(a, b);
//#define CHECK_LTE((a), (b)) check_eq(a, b);

struct check_unattended : public std::exception
{
    check_unattended(const std::string& message):
        message(message)
    {
    }
    ~check_unattended() throw() { }

    virtual const char* what() const throw()
    {
        return message.c_str();
    }
    
    private:
        std::string message;
};

struct assert_unattended : public std::exception
{
    assert_unattended(const std::string& message):
        message(message)
    {
    }
    ~assert_unattended() throw() { }
    virtual const char* what() const throw()
    {
        return message.c_str();
    } 
    
    private:
        std::string message;
};

template <typename T1, typename T2>
void check_eq(std::string name, T1 a, T2 b, const char* file, short line)
    throw(check_unattended)
{
    if(a != b)
    {
        std::stringstream ss;
        ss << "(" << file << ":" << line << ") - error in " << "\"" << name << "\": " << a << " != " << b;
        throw check_unattended(ss.str());
    }
}

/*
template <typename T1, typename T2>
void check_neq(T1 a, T2 b)
    throw(check_unattended)
{
    if(a == b)
    {
        std::stringstream ss;
        ss << a << "==" << b;
        throw check_unattended(ss.str());
    }
}

template <typename T1, typename T2>
void check_gt(T1 a, T2 b)
    throw(check_unattended)
{
    if(a <= b)
    {
        std::stringstream ss;
        ss << a << "<=" << b;
        throw check_unattended(ss.str());
    }
}

template <typename T1, typename T2>
void check_gte(T1 a, T2 b)
    throw(check_unattended)
{
    if(a < b)
    {
        std::stringstream ss;
        ss << a << "<" << b;
        throw check_unattended(ss.str());
    }
}

template <typename T1, typename T2>
void check_lt(T1 a, T2 b)
    throw(check_unattended)
{
    if(a >= b)
    {
        std::stringstream ss;
        ss << a << ">=" << b;
        throw check_unattended(ss.str());
    }
}

template <typename T1, typename T2>
void check_lte(T1 a, T2 b)
    throw(check_unattended)
{
    if(a > b)
    {
        std::stringstream ss;
        ss << a << ">" << b;
        throw check_unattended(ss.str());
    }
}
*/

template <class suite_impl>
class suite
{
    public:
        void suite_set_up()
        {
            static_cast<suite_impl*>(this)->set_up();
        }

        void suite_tier_down()
        {
            static_cast<suite_impl*>(this)->tier_down();
        }

        suite() { }
        suite(const suite<suite_impl>& s) { }
};

template <class suite_impl>
struct test_runner;

template <class suite_impl, class test_impl>
class test
{
    private:
        bool run_test(test_runner<suite_impl>* tr)
        {
            static_cast<test_impl* >(this)->suite_set_up();
            bool result = false;
            try
            {
                (*static_cast<test_impl*>(this))(tr);
                result = true;
            }
            catch(assert_unattended& au)
            {
            }
            catch(std::exception& e)
            {
                std::cout << "Exception during " << test_impl::name  << " run" << std::endl;
                std::cout << e.what() << std::endl;
            }
            catch(...)
            {
                std::cout << "Exception during " << test_impl::name  << " run" << std::endl;
            }
            static_cast<test_impl* >(this)->suite_tier_down();
            return result;
        }
    protected:
        test() { }
        test(const test<suite_impl, test_impl>& t) { }

        friend class test_runner<suite_impl>;
};
 
template <class suite_impl>
struct test_runner
{
    public:
        test_runner() :
            test_counter(1),
            success_counter(0),
            failures_counter(0)
        {
        }

        template <class test_impl>
        test_runner& run(test<suite_impl, test_impl>& t)
        {
            std::cout << "Running test (" << 
                test_counter << "): " << 
                static_cast<test_impl*>(&t)->name << std::endl;

            t.run_test(this);

            test_counter++;
            return *this;
        }

        template <class test_impl>
        test_runner& operator()(test<suite_impl, test_impl>& t)
        {
            return run(t);
        }

        test_runner& operator()()
        {
            std::cout << "** Runner terminated! **" << std::endl;
        }

        void add_failure()
        {
            failures_counter++;
        }

        void add_success()
        {
            success_counter++;
        }


    private:
        int test_counter;
        int success_counter;
        int failures_counter;
};

#endif //_LITTLETEST_HPP_
