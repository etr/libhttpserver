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

//TODO: personalized messages

#ifndef _LITTLETEST_HPP_
#define _LITTLETEST_HPP_

#include <string>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <sys/time.h>
#include <vector>

#define WARN 0
#define CHECK 1
#define ASSERT 2

#define LT_BEGIN_TEST_ENV() int main() {

#define LT_END_TEST_ENV() }

#define LT_BEGIN_AUTO_TEST_ENV() LT_BEGIN_TEST_ENV()

#define LT_END_AUTO_TEST_ENV() \
        return !littletest::auto_test_result; \
    }

#define AUTORUN_TESTS() \
    std::vector<littletest::test_base*>::iterator autorun_it; \
    for(autorun_it = littletest::auto_test_vector.begin(); autorun_it != littletest::auto_test_vector.end(); ++autorun_it) \
        littletest::auto_test_runner((*autorun_it)); \
    littletest::auto_test_runner();

#define LT_TEST(name) name ## _obj

#define LT_CREATE_RUNNER(suite_name, runner_name) \
    std::cout << "** Initializing Runner \"" << #runner_name << "\" **" << std::endl; \
    littletest::test_runner runner_name 

#define LT_RUNNER(runner_name) runner_name

#define LT_BEGIN_SUITE(name) \
    struct name : public littletest::suite<name> \
    {

#define LT_END_SUITE(name) \
    };

#define LT_CHECKPOINT() tr->set_checkpoint(__FILE__, __LINE__)

#define LT_BEGIN_TEST(suite_name, test_name) \
    struct test_name : public suite_name, littletest::test<test_name> \
    { \
            test_name() \
            { \
                name = #test_name; \
                littletest::auto_test_vector.push_back(this); \
            } \
            void operator()(littletest::test_runner* tr) \
            {

#define LT_END_TEST(test_name) \
            } \
    }; \
    test_name test_name ## _obj; \

#define LT_BEGIN_AUTO_TEST(suite_name, test_name) LT_BEGIN_TEST(suite_name, test_name)

#define LT_END_AUTO_TEST(test_name) \
    LT_END_TEST(test_name) \

#define LT_SWITCH_MODE(mode) \
        switch(mode) \
        { \
            case(WARN): \
                throw littletest::warn_unattended(ss.str()); \
            case(CHECK): \
                throw littletest::check_unattended(ss.str()); \
            case(ASSERT): \
                throw littletest::assert_unattended(ss.str()); \
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
    catch(littletest::check_unattended& cu) \
    { \
        std::cout << "[CHECK FAILURE] " << cu.what() << std::endl; \
        tr->add_failure(); \
    } \
    catch(littletest::assert_unattended& au) \
    { \
        std::cout << "[ASSERT FAILURE] " << au.what() << std::endl; \
        tr->add_failure(); \
        throw au; \
    } \
    catch(littletest::warn_unattended& wu) \
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
    throw littletest::assert_unattended("");

namespace littletest
{

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

double calculate_duration(timeval* before, timeval* after)
{
    return ((after->tv_sec * 1000 + (after->tv_usec / 1000.0)) -
           (before->tv_sec * 1000 + (before->tv_usec / 1000.0)));
}

class test_base;

std::vector<test_base*> auto_test_vector;
bool auto_test_result = true;

struct test_runner
{
    public:
        test_runner() :
            test_counter(1),
            success_counter(0),
            failures_counter(0),
            last_checkpoint_file(""),
            last_checkpoint_line(-1),
            good_time_total(0.0),
            total_set_up_time(0.0),
            total_tier_down_time(0.0),
            total_time(0.0)
        {
        }

        template <class test_impl>
        test_runner& run(test_impl* t)
        {
            std::cout << "Running test (" << 
                test_counter << "): " << 
                t->name << std::endl;

            t->run_test(this);

            test_counter++;
            return *this;
        }

        template <class test_impl>
        test_runner& operator()(test_impl* t)
        {
            return run(t);
        }

        test_runner& operator()()
        {
            std::cout << "** Runner terminated! **" << std::endl;
            std::cout << test_counter << " tests executed" << std::endl;
            std::cout << (failures_counter + success_counter) << " checks" << std::endl;
            std::cout << "-> " << success_counter << " successes" << std::endl;
            std::cout << "-> " << failures_counter << " failures" << std::endl;
            std::cout << "Total run time: " << total_time << std::endl;
            std::cout << "Total time spent in tests: " << good_time_total << " ms" << std::endl;
            std::cout << "Average set up time: " << (total_set_up_time / test_counter) << " ms" << std::endl;
            std::cout << "Average tier down time: " << (total_tier_down_time / test_counter) << " ms" << std::endl;
        }

        void add_failure()
        {
            failures_counter++;
            auto_test_result = false;
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

        void add_good_time(double t)
        {
            good_time_total += t;
        }

        void add_set_up_time(double t)
        {
            total_set_up_time += t;
        }

        void add_tier_down_time(double t)
        {
            total_tier_down_time += t;
        }

        void add_total_time(double t)
        {
            total_time += t;
        }

        operator int()
        {
            return failures_counter;
        }

        int get_test_number()
        {
            return test_counter;
        }

        int get_successes()
        {
            return success_counter;
        }

        int get_failures()
        {
            return failures_counter;
        }

        double get_test_time()
        {
            return good_time_total;
        }

        double get_total_time()
        {
            return total_time;
        }

        std::string last_checkpoint_file;
        int last_checkpoint_line;

    private:
        int test_counter;
        int success_counter;
        int failures_counter;
        double good_time_total;
        double total_set_up_time;
        double total_tier_down_time;
        double total_time;
};

class test_base
{
    public:
        const char* name;
        virtual void run_test(test_runner* tr) { }
        virtual void operator()() { }
};

test_runner auto_test_runner;

template <class test_impl>
class test : public test_base
{
        virtual void run_test(test_runner* tr)
        {
            double set_up_duration = 0.0, tier_down_duration = 0.0, test_duration = 0.0;
            timeval before, after;
            try
            {
                gettimeofday(&before, NULL);
                static_cast<test_impl* >(this)->suite_set_up();
                gettimeofday(&after, NULL);
                set_up_duration = calculate_duration(&before, &after);
                tr->add_set_up_time(set_up_duration);
            }
            catch(std::exception& e)
            {
                std::cout << "Exception during " << static_cast<test_impl* >(this)->name  << " set up" << std::endl;
                std::cout << e.what() << std::endl;
            }
            catch(...)
            {
                std::cout << "Exception during " << static_cast<test_impl* >(this)->name  << " set up" << std::endl;
            }
            try
            {
                gettimeofday(&before, NULL);
                (*static_cast<test_impl*>(this))(tr);
            }
            catch(assert_unattended& au)
            {
                ;
            }
            catch(std::exception& e)
            {
                std::cout << "Exception during " << static_cast<test_impl* >(this)->name  << " run" << std::endl;
                std::cout << e.what() << std::endl;
                if(tr->last_checkpoint_line != -1)
                    std::cout << "Last checkpoint in " << tr->last_checkpoint_file << ":" << tr->last_checkpoint_line << std::endl;
            }
            catch(...)
            {
                std::cout << "Exception during " << static_cast<test_impl* >(this)->name  << " run" << std::endl;
                if(tr->last_checkpoint_line != -1)
                    std::cout << "Last checkpoint in " << tr->last_checkpoint_file << ":" << tr->last_checkpoint_line << std::endl;
            }
            gettimeofday(&after, NULL);

            test_duration = calculate_duration(&before, &after);

            tr->add_good_time(test_duration);

            std::cout << "- Time spent during \"" << static_cast<test_impl* >(this)->name << "\": " << test_duration << std::endl;

            try
            {
                gettimeofday(&before, NULL);
                static_cast<test_impl* >(this)->suite_tier_down();
                gettimeofday(&after, NULL);
                tier_down_duration = calculate_duration(&before, &after);
                tr->add_tier_down_time(tier_down_duration);
            }
            catch(std::exception& e)
            {
                std::cout << "Exception during " << static_cast<test_impl* >(this)->name  << " tier down" << std::endl;
                std::cout << e.what() << std::endl;
            }
            catch(...)
            {
                std::cout << "Exception during " << static_cast<test_impl* >(this)->name  << " tier down" << std::endl;
            }
            double total = set_up_duration + test_duration + tier_down_duration;
            tr->add_total_time(total);
        }
    protected:
        test() { }
        test(const test<test_impl>& t) { }

        friend class test_runner;
};

};

#endif //_LITTLETEST_HPP_
