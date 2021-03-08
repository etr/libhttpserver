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

#include <sys/time.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define LT_VERSION 1.0

#define WARN 0
#define CHECK 1
#define ASSERT 2

#define LT_BEGIN_TEST_ENV() int main() {

#define LT_END_TEST_ENV() }

#define LT_BEGIN_AUTO_TEST_ENV() LT_BEGIN_TEST_ENV()

#define LT_END_AUTO_TEST_ENV() \
        return (__lt_result__); \
    }

#define AUTORUN_TESTS() \
    std::vector<littletest::test_base*>::iterator __lt_autorun_it__; \
    for(__lt_autorun_it__ = littletest::auto_test_vector.begin(); __lt_autorun_it__ != littletest::auto_test_vector.end(); ++__lt_autorun_it__) \
        littletest::auto_test_runner((*__lt_autorun_it__)); \
    int __lt_result__ = littletest::auto_test_runner();

#define LT_CREATE_RUNNER(__lt_suite_name__, __lt_runner_name__) \
    std::cout << "** Initializing Runner \"" << #__lt_runner_name__ << "\" **" << std::endl; \
    littletest::test_runner __lt_runner_name__

#define LT_RUNNER(__lt_runner_name__) __lt_runner_name__

#define LT_BEGIN_SUITE(__lt_name__) \
    struct __lt_name__ : public littletest::suite<__lt_name__> \
    {

#define LT_END_SUITE(__lt_name__) \
    };

#define LT_CHECKPOINT() __lt_tr__->set_checkpoint(__FILE__, __LINE__)

#define LT_BEGIN_TEST(__lt_suite_name__, __lt_test_name__) \
    struct __lt_test_name__ ## _class: public __lt_suite_name__, littletest::test<__lt_test_name__ ## _class> \
    { \
            __lt_test_name__ ## _class() \
            { \
                __lt_name__ = #__lt_test_name__; \
                littletest::auto_test_vector.push_back(this); \
            } \
            void operator()(littletest::test_runner* __lt_tr__) \
            {

#define LT_END_TEST(__lt_test_name__) \
            } \
    }; \
    __lt_test_name__ ## _class __lt_test_name__; \

#define LT_BEGIN_AUTO_TEST(__lt_suite_name__, __lt_test_name__) LT_BEGIN_TEST(__lt_suite_name__, __lt_test_name__)

#define LT_END_AUTO_TEST(__lt_test_name__) \
    LT_END_TEST(__lt_test_name__) \

#define LT_SWITCH_MODE(__lt_mode__) \
        switch(__lt_mode__) \
        { \
            case(WARN): \
                throw littletest::warn_unattended(__lt_ss__.str()); \
            case(CHECK): \
                throw littletest::check_unattended(__lt_ss__.str()); \
            case(ASSERT): \
                throw littletest::assert_unattended(__lt_ss__.str()); \
        }

#define LT_SIMPLE_OP(__lt_name__, __lt_val__, __lt_file__, __lt_line__, __lt_mode__) \
    if(!((__lt_val__))) \
    { \
        std::stringstream __lt_ss__; \
        __lt_ss__ << "(" << __lt_file__ << ":" << __lt_line__ << ") - error in " << "\"" << __lt_name__ << "\""; \
        LT_SWITCH_MODE(__lt_mode__) \
    }

#define LT_THROW_OP(__lt_name__, __lt_operation__, __lt_file__, __lt_line__, __lt_mode__) \
    bool __lt_thrown__ = false; \
    std::stringstream __lt_ss__; \
    try \
    { \
        (__lt_operation__) ;\
        __lt_ss__ << "(" << __lt_file__ << ":" << __lt_line__ << ") - error in " << "\"" << __lt_name__ << "\": no exceptions thown by " << #__lt_operation__; \
        __lt_thrown__ = true; \
    } \
    catch(...) { } \
    if(__lt_thrown__) \
        LT_SWITCH_MODE(__lt_mode__)

#define LT_NOTHROW_OP(__lt_name__, __lt_operation__, __lt_file__, __lt_line__, __lt_mode__) \
    try \
    { \
        (__lt_operation__) ;\
    } \
    catch(...) \
    { \
        std::stringstream __lt_ss__; \
        __lt_ss__ << "(" << __lt_file__ << ":" << __lt_line__ << ") - error in " << "\"" << __lt_name__ << "\": exceptions thown by " << #__lt_operation__; \
        LT_SWITCH_MODE(__lt_mode__) \
    }

#define LT_COLLEQ_OP(__lt_name__, __lt_first_begin__, __lt_first_end__, __lt_second_begin__, __lt_file__, __lt_line__, __lt_mode__) \
    if(! std::equal((__lt_first_begin__), (__lt_first_end__), (__lt_second_begin__))) \
    { \
        std::stringstream __lt_ss__; \
        __lt_ss__ << "(" << __lt_file__ << ":" << __lt_line__ << ") - error in " << "\"" << __lt_name__ << "\": collections are different"; \
        LT_SWITCH_MODE(__lt_mode__) \
    }

#define LT_COLLNEQ_OP(__lt_name__, __lt_first_begin__, __lt_first_end__, __lt_second_begin__, __lt_file__, __lt_line__, __lt_mode__) \
    if(std::equal((__lt_first_begin__), (__lt_first_end__), (__lt_second_begin__))) \
    { \
        std::stringstream __lt_ss__; \
        __lt_ss__ << "(" << __lt_file__ << ":" << __lt_line__ << ") - error in " << "\"" << __lt_name__ << "\": collections are equal"; \
        LT_SWITCH_MODE(__lt_mode__) \
    }

#define LT_OP(__lt_name__, __lt_a__, __lt_b__, __lt_file__, __lt_line__, __lt_op__, __lt_mode__) \
    if((__lt_a__) __lt_op__ (__lt_b__)) \
    { \
        std::stringstream __lt_ss__; \
        __lt_ss__ << "(" << __lt_file__ << ":" << __lt_line__ << ") - error in " << "\"" << __lt_name__ << "\": " << (__lt_a__) << #__lt_op__ << (__lt_b__); \
        LT_SWITCH_MODE(__lt_mode__) \
    }

#define LT_CATCH_ERRORS \
    catch(littletest::check_unattended& __lt_cu__) \
    { \
        std::cout << "[CHECK FAILURE] " << __lt_cu__.what() << std::endl; \
        __lt_tr__->add_failure(); \
    } \
    catch(littletest::assert_unattended& __lt_au__) \
    { \
        std::cout << "[ASSERT FAILURE] " << __lt_au__.what() << std::endl; \
        __lt_tr__->add_failure(); \
        throw __lt_au__; \
    } \
    catch(littletest::warn_unattended& __lt_wu__) \
    { \
        std::cout << "[WARN] " << __lt_wu__.what() << std::endl; \
    }

#define LT_ADD_SUCCESS(__lt_mode__) \
    if(__lt_mode__) \
        __lt_tr__->add_success();

#define LT_EV(__lt_a__, __lt_b__, __lt_op__, __lt_mode__) \
    try \
    { \
        LT_OP(__lt_name__, (__lt_a__), (__lt_b__), __FILE__, __LINE__, __lt_op__, __lt_mode__); \
        LT_ADD_SUCCESS(__lt_mode__) \
    } \
    LT_CATCH_ERRORS

#define LT_SIMPLE_EV(__lt_val__, __lt_mode__) \
    try \
    { \
        LT_SIMPLE_OP(__lt_name__, (__lt_val__), __FILE__, __LINE__, __lt_mode__); \
        LT_ADD_SUCCESS(__lt_mode__) \
    } \
    LT_CATCH_ERRORS

#define LT_THROW_EV(__lt_operation__, __lt_mode__) \
    try \
    { \
        LT_THROW_OP(__lt_name__, (__lt_operation__), __FILE__, __LINE__, __lt_mode__); \
        LT_ADD_SUCCESS(__lt_mode__) \
    } \
    LT_CATCH_ERRORS

#define LT_NOTHROW_EV(__lt_operation__, __lt_mode__) \
    try \
    { \
        LT_NOTHROW_OP(__lt_name__, (__lt_operation__), __FILE__, __LINE__, __lt_mode__); \
        LT_ADD_SUCCESS(__lt_mode__) \
    } \
    LT_CATCH_ERRORS

#define LT_COLLEQ_EV(__lt_first_begin__, __lt_first_end__, __lt_second_begin__, __lt_mode__) \
    try \
    { \
        LT_COLLEQ_OP(__lt_name__, (__lt_first_begin__), (__lt_first_end__), (__lt_second_begin__), __FILE__, __LINE__, __lt_mode__); \
        LT_ADD_SUCCESS(__lt_mode__) \
    } \
    LT_CATCH_ERRORS

#define LT_COLLNEQ_EV(__lt_first_begin__, __lt_first_end__, __lt_second_begin__, __lt_mode__) \
    try \
    { \
        LT_COLLNEQ_OP(__lt_name__, (__lt_first_begin__), (__lt_first_end__), (__lt_second_begin__), __FILE__, __LINE__, __lt_mode__); \
        LT_ADD_SUCCESS(__lt_mode__) \
    } \
    LT_CATCH_ERRORS

#define LT_WARN(__lt_val__) LT_SIMPLE_EV((__lt_val__), WARN)
#define LT_WARN_EQ(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), !=, WARN)
#define LT_WARN_NEQ(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), ==, WARN)
#define LT_WARN_GT(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), <=, WARN)
#define LT_WARN_GTE(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), <, WARN)
#define LT_WARN_LT(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), >=, WARN)
#define LT_WARN_LTE(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), >, WARN)
#define LT_WARN_THROW(__lt_operation__) LT_THROW_EV((__lt_operation__), WARN)
#define LT_WARN_NOTHROW(__lt_operation__) LT_NOTHROW_EV((__lt_operation__), WARN)
#define LT_WARN_COLLECTIONS_EQ(__lt_first_begin__, __lt_first_end__, __lt_second_begin__) LT_COLLEQ_EV((__lt_first_begin__), (__lt_first_end__), (__lt_second_begin__), WARN)
#define LT_WARN_COLLECTIONS_NEQ(__lt_first_begin__, __lt_first_end__, __lt_second_begin__) LT_COLLNEQ_EV((__lt_first_begin__), (__lt_first_end__), (__lt_second_begin__), WARN)

#define LT_CHECK(__lt_val__) LT_SIMPLE_EV((__lt_val__), CHECK)
#define LT_CHECK_EQ(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), !=, CHECK)
#define LT_CHECK_NEQ(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), ==, CHECK)
#define LT_CHECK_GT(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), <=, CHECK)
#define LT_CHECK_GTE(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), <, CHECK)
#define LT_CHECK_LT(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), >=, CHECK)
#define LT_CHECK_LTE(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), >, CHECK)
#define LT_CHECK_THROW(__lt_operation__) LT_THROW_EV((__lt_operation__), CHECK)
#define LT_CHECK_NOTHROW(__lt_operation__) LT_NOTHROW_EV((__lt_operation__), CHECK)
#define LT_CHECK_COLLECTIONS_EQ(__lt_first_begin__, __lt_first_end__, __lt_second_begin__) LT_COLLEQ_EV((__lt_first_begin__), (__lt_first_end__), (__lt_second_begin__), CHECK)
#define LT_CHECK_COLLECTIONS_NEQ(__lt_first_begin__, __lt_first_end__, __lt_second_begin__) LT_COLLNEQ_EV((__lt_first_begin__), (__lt_first_end__), (__lt_second_begin__), CHECK)

#define LT_ASSERT(__lt_val__) LT_SIMPLE_EV((__lt_val__), ASSERT)
#define LT_ASSERT_EQ(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), !=, ASSERT)
#define LT_ASSERT_NEQ(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), ==, ASSERT)
#define LT_ASSERT_GT(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), <=, ASSERT)
#define LT_ASSERT_GTE(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), <, ASSERT)
#define LT_ASSERT_LT(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), >=, ASSERT)
#define LT_ASSERT_LTE(__lt_a__, __lt_b__) LT_EV((__lt_a__), (__lt_b__), >, ASSERT)
#define LT_ASSERT_THROW(__lt_operation__) LT_THROW_EV((__lt_operation__), ASSERT)
#define LT_ASSERT_NOTHROW(__lt_operation__) LT_NOTHROW_EV((__lt_operation__), ASSERT)
#define LT_ASSERT_COLLECTIONS_EQ(__lt_first_begin__, __lt_first_end__, __lt_second_begin__) LT_COLLEQ_EV((__lt_first_begin__), (__lt_first_end__), (__lt_second_begin__), ASSERT)
#define LT_ASSERT_COLLECTIONS_NEQ(__lt_first_begin__, __lt_first_end__, __lt_second_begin__) LT_COLLNEQ_EV((__lt_first_begin__), (__lt_first_end__), (__lt_second_begin__), ASSERT)

#define LT_FAIL(__lt_message__) \
    std::cout << "[ASSERT FAILURE] (" << __FILE__ << ":" << __LINE__ << ") - error in " << "\"" << (__lt_name__) << "\": " << (__lt_message__) << std::endl; \
    __lt_tr__->add_failure(); \
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

        void suite_tear_down()
        {
            static_cast<suite_impl*>(this)->tear_down();
        }

        suite() { }
        suite(const suite<suite_impl>&) { }
};

double calculate_duration(timeval* before, timeval* after)
{
    return ((after->tv_sec * 1000 + (after->tv_usec / 1000.0)) -
           (before->tv_sec * 1000 + (before->tv_usec / 1000.0)));
}

class test_base;

std::vector<test_base*> auto_test_vector;

class test_runner
{
    public:
        test_runner() :
            last_checkpoint_file(""),
            last_checkpoint_line(-1),
            test_counter(1),
            success_counter(0),
            failures_counter(0),
            good_time_total(0.0),
            total_set_up_time(0.0),
            total_tear_down_time(0.0),
            total_time(0.0)
        {
        }

        template <class test_impl>
        test_runner& run(test_impl* t)
        {
            std::cout << "Running test (" <<
                test_counter << "): " <<
                t->__lt_name__ << std::endl;

            t->run_test(this);

            test_counter++;
            return *this;
        }

        template <class test_impl>
        test_runner& operator()(test_impl& t)
        {
            return run(&t);
        }

        template <class test_impl>
        test_runner& operator()(test_impl* t)
        {
            return run(t);
        }

        void clear_runner()
        {
            last_checkpoint_file = "";
            last_checkpoint_line = -1;
            test_counter = 1;
            success_counter = 0;
            failures_counter = 0;
            good_time_total = 0.0,
            total_set_up_time = 0.0;
            total_tear_down_time = 0.0;
            total_time = 0.0;
        }

        int operator()()
        {
            std::cout << "** Runner terminated! **" << std::endl;
            std::cout << (test_counter - 1) << " tests executed" << std::endl;
            std::cout << (failures_counter + success_counter) << " checks" << std::endl;
            std::cout << "-> " << success_counter << " successes" << std::endl;
            std::cout << "-> " << failures_counter << " failures" << std::endl;
            std::cout << "Total run time: " << total_time << " ms"<< std::endl;
            std::cout << "Total time spent in tests: " << good_time_total << " ms" << std::endl;
            std::cout << "Average set up time: " << (total_set_up_time / test_counter) << " ms" << std::endl;
            std::cout << "Average tear down time: " << (total_tear_down_time / test_counter) << " ms" << std::endl;
            int to_ret = failures_counter;
            clear_runner();
            return to_ret;
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

        void add_good_time(double t)
        {
            good_time_total += t;
        }

        void add_set_up_time(double t)
        {
            total_set_up_time += t;
        }

        void add_tear_down_time(double t)
        {
            total_tear_down_time += t;
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
        double total_tear_down_time;
        double total_time;
};

class test_base
{
    public:
        const char* __lt_name__;
        virtual void run_test(test_runner*) { }
        virtual void operator()() { }
};

test_runner auto_test_runner;

template <class test_impl>
class test : public test_base
{
        virtual void run_test(test_runner* tr)
        {
            double set_up_duration = 0.0, tear_down_duration = 0.0, test_duration = 0.0;
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
                std::cout << "Exception during " << static_cast<test_impl* >(this)->__lt_name__ << " set up" << std::endl;
                std::cout << e.what() << std::endl;
            }
            catch(...)
            {
                std::cout << "Exception during " << static_cast<test_impl* >(this)->__lt_name__ << " set up" << std::endl;
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
                std::cout << "Exception during " << static_cast<test_impl* >(this)->__lt_name__ << " run" << std::endl;
                std::cout << e.what() << std::endl;
                if(tr->last_checkpoint_line != -1)
                    std::cout << "Last checkpoint in " << tr->last_checkpoint_file << ":" << tr->last_checkpoint_line << std::endl;
            }
            catch(...)
            {
                std::cout << "Exception during " << static_cast<test_impl* >(this)->__lt_name__ << " run" << std::endl;
                if(tr->last_checkpoint_line != -1)
                    std::cout << "Last checkpoint in " << tr->last_checkpoint_file << ":" << tr->last_checkpoint_line << std::endl;
            }
            gettimeofday(&after, NULL);

            test_duration = calculate_duration(&before, &after);

            tr->add_good_time(test_duration);

            std::cout << "- Time spent during \"" << static_cast<test_impl* >(this)->__lt_name__ << "\": " << test_duration << " ms"<< std::endl;

            try
            {
                gettimeofday(&before, NULL);
                static_cast<test_impl* >(this)->suite_tear_down();
                gettimeofday(&after, NULL);
                tear_down_duration = calculate_duration(&before, &after);
                tr->add_tear_down_time(tear_down_duration);
            }
            catch(std::exception& e)
            {
                std::cout << "Exception during " << static_cast<test_impl* >(this)->__lt_name__ << " tear down" << std::endl;
                std::cout << e.what() << std::endl;
            }
            catch(...)
            {
                std::cout << "Exception during " << static_cast<test_impl* >(this)->__lt_name__ << " tear down" << std::endl;
            }
            double total = set_up_duration + test_duration + tear_down_duration;
            tr->add_total_time(total);
        }
    protected:
        test() { }
        test(const test<test_impl>&) { }

        friend class test_runner;
};

}

#endif //_LITTLETEST_HPP_
