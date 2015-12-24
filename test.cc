#include <iostream>
#include "hsm.h"

using namespace hsm;

class MyOwner
{
public:
    MyOwner();
    std::string data;

private:
    StateMachine mStateMachine;
};

struct MyEvent
{
        struct event_open : event_base<event_open> 
        {
                std::string data;
                event_open(const std::string d) : data(d) {}
        };
        struct event_close : event_base<event_close> 
        {
        };
};

struct MyStates
{
        struct Second : state_with_owner<Second, MyOwner>
        {
                typedef hsm_vector<
                        MyEvent::event_open,
                        MyEvent::event_close
                                > reactions;

                void on_enter()
                {
                        std::cout << "Second enter\n";
                }

                void on_exit()
                {
                        std::cout << "Second exit\n";
                }

                result react(const MyEvent::event_open &)
                {
                        std::cout << owner().data << std::endl;
                        std::cout << "handle event_open at state Second\n";
                        return transit<Third>();
                }

                result react(const MyEvent::event_close &)
                {
                        std::cout << "defer event_close at state Second\n";
                        return defer();
                }

        };

        struct First : state_base<First, Second>
        {
                void on_enter()
                {
                        std::cout << "first enter\n";
                }

                void on_exit()
                {
                        std::cout << "first exit\n";
                }
        };

        struct Third : state_base<Third>
        {
                typedef hsm_vector<
                        MyEvent::event_close
                        > reactions;

                void on_enter()
                {
                        std::cout << "third enter\n";
                }

                void on_exit()
                {
                        std::cout << "third exit\n";
                }

                result react(const MyEvent::event_close &)
                {
                        std::cout << "handle event_close at state Third\n";
                        return finish();
                }
        };
};

MyOwner::MyOwner()
{
        data = "Hello World!";
        mStateMachine.initialize<MyStates::First>(this);
        mStateMachine.process_event(MyEvent::event_close());
        mStateMachine.process_event(MyEvent::event_open("Hi"));
        mStateMachine.stop();
}

int main()
{
        MyOwner myOwner;
}
