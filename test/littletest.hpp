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

//TODO: time extimations
//TODO: personalized messages
//TODO: statistics in runner closure

#ifndef _LITTLETEST_HPP_
#define _LITTLETEST_HPP_

#include <string>
#include <iostream>
#include <sstream>
#include <algorithm>

#define WARN 0
#define CHECK 1
#define ASSERT 2

#define LT_BEGIN_TEST_ENV() int main() {
#define LT_END_TEST_ENV() return 0; }
#define LT_TEST(name) name ## _obj
#define LT_CREATE_RUNNER(suite_name, runner_name) \
    std::cout << "** Initializing Runner \"" << #runner_name << "\" for suite \"" << #suite_name << "\" **" << std::endl; \
    test_runner<suite_name> runner_name 
#define LT_RUNNER(runner_name) runner_name
#define LT_SUITE(name) struct name : public suite<name>
#define LT_CHECKPOINT() tr->set_checkpoint(__FILE__, __LINE__)
#define LT_BEGIN_TEST(suite_name, test_name) \
    struct test_name : public suite_name, test<suite_name, test_name> \
    { \
        public: \
            static const char* name; \
            void operator()(test_runner<suite_name>* tr) \
            {

#define LT_END_TEST(test_name) \
            } \
    }; \
    const char* test_name::name = #test_name; \
    test_name test_name ## _obj;

#define LT_SWITCH_MODE(mode) \
        switch(mode) \
        { \
            case(WARN): \
                throw warn_unattended(ss.str()); \
            case(CHECK): \
                throw check_unattended(ss.str()); \
            case(ASSERT): \
                throw assert_unattended(ss.str()); \
        }

#define LT_SIMPLE_OP(name, val, file, line, mode) \
    if(!(val)) \
    { \
        std::stringstream ss; \
        ss << "(" << file << ":" << line << ") - error in " << "\"" << name << "\""; \
        LT_SWITCH_MODE(mode) \
    }

#define LT_THROW_OP(name, operation, file, line, mode) \
    bool thrown = false; \
    std::stringstream ss; \
    try \
    { \
        operation ;\
        ss << "(" << file << ":" << line << ") - error in " << "\"" << name << "\": no exceptions thown by " << #operation; \
        thrown = true; \
    } \
    catch(...) { } \
    if(thrown) \
        LT_SWITCH_MODE(mode)

#define LT_NOTHROW_OP(name, operation, file, line, mode) \
    try \
    { \
        operation ;\
    } \
    catch(...) \
    { \
        std::stringstream ss; \
        ss << "(" << file << ":" << line << ") - error in " << "\"" << name << "\": exceptions thown by " << #operation; \
        LT_SWITCH_MODE(mode) \
    }

#define LT_COLLEQ_OP(name, first_begin, first_end, second_begin, file, line, mode) \
    if(! std::equal(first_begin, first_end, second_begin)) \
    { \
        std::stringstream ss; \
        ss << "(" << file << ":" << line << ") - error in " << "\"" << name << "\": collections are different"; \
        LT_SWITCH_MODE(mode) \
    }

#define LT_COLLNEQ_OP(name, first_begin, first_end, second_begin, file, line, mode) \
    if(std::equal(first_begin, first_end, second_begin)) \
    { \
        std::stringstream ss; \
        ss << "(" << file << ":" << line << ") - error in " << "\"" << name << "\": collections are equal"; \
        LT_SWITCH_MODE(mode) \
    }

#define LT_OP(name, a, b, file, line, op, mode) \
    if(a op b) \
    { \
        std::stringstream ss; \
        ss << "(" << file << ":" << line << ") - error in " << "\"" << name << "\": " << a << #op << b; \
        LT_SWITCH_MODE(mode) \
    }

#define LT_CATCH_ERRORS \
    catch(check_unattended& cu) \
    { \
        std::cout << "[CHECK FAILURE] " << cu.what() << std::endl; \
        tr->add_failure(); \
    } \
    catch(assert_unattended& au) \
    { \
        std::cout << "[ASSERT FAILURE] " << au.what() << std::endl; \
        tr->add_failure(); \
        throw au; \
    } \
    catch(warn_unattended& wu) \
    { \
        std::cout << "[WARN] " << wu.what() << std::endl; \
    }

#define LT_ADD_SUCCESS(mode) \
    if(mode) \
        tr->add_success();

#define LT_EV(a, b, op, mode) \
    try \
    { \
        LT_OP(name, a, b, __FILE__, __LINE__, op, mode); \
        LT_ADD_SUCCESS(mode) \
    } \
    LT_CATCH_ERRORS

#define LT_SIMPLE_EV(val, mode) \
    try \
    { \
        LT_SIMPLE_OP(name, val, __FILE__, __LINE__, mode); \
        LT_ADD_SUCCESS(mode) \
    } \
    LT_CATCH_ERRORS

#define LT_THROW_EV(operation, mode) \
    try \
    { \
        LT_THROW_OP(name, operation, __FILE__, __LINE__, mode); \
        LT_ADD_SUCCESS(mode) \
    } \
    LT_CATCH_ERRORS

#define LT_NOTHROW_EV(operation, mode) \
    try \
    { \
        LT_NOTHROW_OP(name, operation, __FILE__, __LINE__, mode); \
        LT_ADD_SUCCESS(mode) \
    } \
    LT_CATCH_ERRORS

#define LT_COLLEQ_EV(first_begin, first_end, second_begin, mode) \
    try \
    { \
        LT_COLLEQ_OP(name, first_begin, first_end, second_begin, __FILE__, __LINE__, mode); \
        LT_ADD_SUCCESS(mode) \
    } \
    LT_CATCH_ERRORS

#define LT_COLLNEQ_EV(first_begin, first_end, second_begin, mode) \
    try \
    { \
        LT_COLLNEQ_OP(name, first_begin, first_end, second_begin, __FILE__, __LINE__, mode); \
        LT_ADD_SUCCESS(mode) \
    } \
    LT_CATCH_ERRORS

#define LT_WARN(val) LT_SIMPLE_EV(val, WARN)
#define LT_WARN_EQ(a, b) LT_EV(a, b, !=, WARN)
#define LT_WARN_NEQ(a, b) LT_EV(a, b, ==, WARN)
#define LT_WARN_GT(a, b) LT_EV(a, b, <=, WARN)
#define LT_WARN_GTE(a, b) LT_EV(a, b, <, WARN)
#define LT_WARN_LT(a, b) LT_EV(a, b, >=, WARN)
#define LT_WARN_LTE(a, b) LT_EV(a, b, >, WARN)
#define LT_WARN_THROW(operation) LT_THROW_EV(operation, WARN)
#define LT_WARN_NOTHROW(operation) LT_NOTHROW_EV(operation, WARN)
#define LT_WARN_COLLECTIONS_EQ(first_begin, first_end, second_begin) LT_COLLEQ_EV(first_begin, first_end, second_begin, WARN)
#define LT_WARN_COLLECTIONS_NEQ(first_begin, first_end, second_begin) LT_COLLNEQ_EV(first_begin, first_end, second_begin, WARN)

#define LT_CHECK(val) LT_SIMPLE_EV(val, CHECK)
#define LT_CHECK_EQ(a, b) LT_EV(a, b, !=, CHECK)
#define LT_CHECK_NEQ(a, b) LT_EV(a, b, ==, CHECK)
#define LT_CHECK_GT(a, b) LT_EV(a, b, <=, CHECK)
#define LT_CHECK_GTE(a, b) LT_EV(a, b, <, CHECK)
#define LT_CHECK_LT(a, b) LT_EV(a, b, >=, CHECK)
#define LT_CHECK_LTE(a, b) LT_EV(a, b, >, CHECK)
#define LT_CHECK_THROW(operation) LT_THROW_EV(operation, CHECK)
#define LT_CHECK_NOTHROW(operation) LT_NOTHROW_EV(operation, CHECK)
#define LT_CHECK_COLLECTIONS_EQ(first_begin, first_end, second_begin) LT_COLLEQ_EV(first_begin, first_end, second_begin, CHECK)
#define LT_CHECK_COLLECTIONS_NEQ(first_begin, first_end, second_begin) LT_COLLNEQ_EV(first_begin, first_end, second_begin, CHECK)

#define LT_ASSERT(val) LT_SIMPLE_EV(val, ASSERT)
#define LT_ASSERT_EQ(a, b) LT_EV(a, b, !=, ASSERT)
#define LT_ASSERT_NEQ(a, b) LT_EV(a, b, ==, ASSERT)
#define LT_ASSERT_GT(a, b) LT_EV(a, b, <=, ASSERT)
#define LT_ASSERT_GTE(a, b) LT_EV(a, b, <, ASSERT)
#define LT_ASSERT_LT(a, b) LT_EV(a, b, >=, ASSERT)
#define LT_ASSERT_LTE(a, b) LT_EV(a, b, >, ASSERT)
#define LT_ASSERT_THROW(operation) LT_THROW_EV(operation, ASSERT)
#define LT_ASSERT_NOTHROW(operation) LT_NOTHROW_EV(operation, ASSERT)
#define LT_ASSERT_COLLECTIONS_EQ(first_begin, first_end, second_begin) LT_COLLEQ_EV(first_begin, first_end, second_begin, ASSERT)
#define LT_ASSERT_COLLECTIONS_NEQ(first_begin, first_end, second_begin) LT_COLLNEQ_EV(first_begin, first_end, second_begin, ASSERT)

#define LT_FAIL(message) \
    std::cout << "[ASSERT FAILURE] (" << __FILE__ << ":" << __LINE__ << ") - error in " << "\"" << name << "\": " << message << std::endl; \
    tr->add_failure(); \
    throw assert_unattended("");

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

struct warn_unattended : public std::exception
{
    warn_unattended(const std::string& message):
        message(message)
    {
    }
    ~warn_unattended() throw() { }
    virtual const char* what() const throw()
    {
        return message.c_str();
    } 
    
    private:
        std::string message;
};

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
                if(tr->last_checkpoint_line != -1)
                    std::cout << "Last checkpoint in " << tr->last_checkpoint_file << ":" << tr->last_checkpoint_line << std::endl;
            }
            catch(...)
            {
                std::cout << "Exception during " << test_impl::name  << " run" << std::endl;
                if(tr->last_checkpoint_line != -1)
                    std::cout << "Last checkpoint in " << tr->last_checkpoint_file << ":" << tr->last_checkpoint_line << std::endl;
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
            failures_counter(0),
            last_checkpoint_file(""),
            last_checkpoint_line(-1)
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

        void set_checkpoint(const char* file, int line)
        {
            last_checkpoint_file = file;
            last_checkpoint_line = line;
        }

        std::string last_checkpoint_file;
        int last_checkpoint_line;

    private:
        int test_counter;
        int success_counter;
        int failures_counter;
};

#endif //_LITTLETEST_HPP_
