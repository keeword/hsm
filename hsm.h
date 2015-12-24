#ifndef _HSM_H_
#define _HSM_H_

#include <iostream>
#include <memory>
#include <vector>
#include <list>
#include <string>
#include <cassert>
#include <typeinfo>
#include <boost/mpl/vector.hpp>
#include <boost/mpl/is_sequence.hpp>
#include <boost/mpl/front.hpp>
#include <boost/mpl/back.hpp>
#include <boost/mpl/identity.hpp>
#include <boost/mpl/pop_front.hpp>
#include <boost/mpl/if.hpp>

#define hsm_true   true
#define hsm_false  false
#define hsm_assert assert
#define hsm_new    new
#define hsm_delete delete
#define hsm_ptr    std::shared_ptr
#define std_vector std::vector
#define hsm_vector boost::mpl::vector
#define hsm_assert_msg(cond, msg) assert((cond) && (msg))

typedef bool hsm_bool;
typedef char hsm_char;

#define ABORT_WHEN_AN_EVENT_NOT_HANDLE   true
#define ABORT_WHEN_RECEIVE_UNKNOWN_EVENT true

namespace hsm {
        
// a wrapper to storage the type of state and event
struct TypeInfoStorage
{
        TypeInfoStorage() : type_info(0)         {}
        TypeInfoStorage(const std::type_info& t) { type_info = &t; }
        hsm_bool operator==(const TypeInfoStorage& rhs) const
        {
                return *type_info == *rhs.type_info;
        }
        const std::type_info* type_info;
};

typedef const TypeInfoStorage& StateTypeInfo;
typedef const TypeInfoStorage& EventTypeInfo;

// get the type of state (or event),
// all the object of one certain state (or event) shared the same TypeInfoStorage instance
template <typename StateType>
StateTypeInfo GetStateType()
{
        static TypeInfoStorage StateTypeInfo(typeid(StateType));
        return StateTypeInfo;
}

template <typename EventType>
EventTypeInfo GetEventType()
{
        static TypeInfoStorage EventTypeInfo(typeid(EventType));
        return EventTypeInfo;
}

struct State;

// State creation interface
struct StateFactory
{
        virtual StateTypeInfo GetType() const = 0;
        virtual State* AllocateState()  const = 0;
};

inline bool operator==(const StateFactory& lhs, const StateFactory& rhs) { return lhs.GetType() == rhs.GetType(); }
inline bool operator!=(const StateFactory& lhs, const StateFactory& rhs) { return !(lhs == rhs); }

// get a StateFactory instance used to create a State
template <typename TargetState>
const StateFactory& GetStateFactory();

// ConcreteStateFactory is the actual state creator; these are allocated statically in the transition
// functions (below) and stored within Transition instances.
template <typename TargetState>
struct ConcreteStateFactory : StateFactory
{
        virtual StateTypeInfo GetType() const
        {
                return GetStateType<TargetState>();
        }

        virtual State* AllocateState() const
        {
                return hsm_new TargetState();
        }
private:
        // Only GetStateFactory can create this type
        friend const StateFactory& GetStateFactory<TargetState>();
        ConcreteStateFactory() {}
};

template <typename TargetState>
const StateFactory& GetStateFactory()
{
        static ConcreteStateFactory<TargetState> instance;
        return instance;
}

// Transition contain two member: transition type and a StateFactory of target state.
// There are lot of transition type in the UML Statechart, I just implement one:
// Sibling Transition, means transit from one state to another state at the same layer.
// Transition is used as the result of user's react function, so I need some other type to 
// mark this, that's the work of No Transition and End Transition. Since they are not a real transition,
// so I contain the pointer to StateFactory instead of a state instance, then create the target state 
// when I actually need it.
struct Transition
{
        enum Type { Sibling, No, End };

        // Default is no transition
        Transition() : transition_type_(Transition::No), state_factory_(0) {}

        // Real transition to another state
        Transition(Transition::Type transitionType, const StateFactory& stateFactory)
                : transition_type_(transitionType)
                , state_factory_(&stateFactory) {}

        // Not real transition
        Transition(Transition::Type transitionType) 
                : transition_type_(transitionType)
                , state_factory_(0) {}

        // Accessors
        Transition::Type    transition_type()   const { return transition_type_; }
        const StateFactory& state_factory()     const { hsm_assert(state_factory_ != 0); return *state_factory_; }
        StateTypeInfo       target_state_type() const { hsm_assert(state_factory_ != 0); return state_factory_->GetType(); }   

        // Check type of transition
        hsm_bool IsSibling()    const { return transition_type_ == Sibling;    }
        hsm_bool IsNo()         const { return transition_type_ == No;         }

private:
        Transition::Type    transition_type_;
        const StateFactory* state_factory_;
};

typedef Transition result;

// SiblingTransition
inline Transition SiblingTransition(const StateFactory& stateFactory)
{
        return Transition(Transition::Sibling, stateFactory);
}
template <typename TargetState>
Transition SiblingTransition()
{
        return Transition(Transition::Sibling, GetStateFactory<TargetState>());
}

// NoTransition
inline Transition NoTransition()
{
        return Transition();
}

// EndTransition, not found the event in the state stack, return this
inline Transition EndTransition()
{
        return Transition(Transition::End);
}

// Dispatcher is the bridge between State and Event
template <typename State, typename Event>
struct Dispatcher
{
        typedef State StateType;
        typedef Event EventType;
};

struct Event;
class StateMachine;

inline void InitState(State* state, StateMachine* state_machine, size_t stack_depth);

// State is the base class of all the state. This implementation has some different from 
// the ideal HSM, indeed, all the state instance are independent, they don't know what's
// their outer state and what's their inner state, they seems to have thesis relationship
// by saving them in a State Stack, state in low layer is the substate of state in high 
// layer, but that maybe be cause some mistake of the relationship sometimes, and that's
// hard to transit from a substate to another substate, for example, there are two state 
// chain: stateA->substateA->subsubstateA, stateB->substateB->subsubstateB,
// if I want to transit from subsubstateA to subsubstateB, It's hard to do that now.
// So next step is make States has real relationship, not just a state stack.
struct State
{
        State() : state_machine_(0) , stack_depth_(0) {}

        virtual ~State() {}

        typedef Event event_base_type;
        typedef State state_base_type;
        typedef hsm_ptr<const Event> event_base_ptr_type;

        // RTTI interface
        virtual TypeInfoStorage GetType() const { return typeid(*this); }

        // Accessors
        StateMachine& state_machine()             { hsm_assert(state_machine_ != 0); return *state_machine_; }        
        const StateMachine& state_machine() const { hsm_assert(state_machine_ != 0); return *state_machine_; }

        // these four function are interface to user, in a react function,
        // the last sentence MUST be 'return (one of the four function);'

        // Finished doing something, but no transition
        inline Transition finish()  { return NoTransition(); }

        // do nothing, just discard the event
        inline Transition discard() { return NoTransition(); }

        // transit to a Sibling State
        template <typename TargetState>
        inline Transition transit() { return SiblingTransition<TargetState>(); }

        // defer the event, when a transition happened, will call the deferred event again,
        // as they are just arrive
        inline Transition defer();

        // Overridable functions
        // these two virtual function can be override in a certain state
        // on_enter is invoked when a State is created; 
        virtual void on_enter() {}
        // on_exit is invoked just before a State is destroyed
        virtual void on_exit() {}
        
private:
        friend inline void InitState(State* state, StateMachine* state_machine, size_t stack_depth);
        friend class StateMachine;

        // this function is used to call the right react function in user's state, see below
        virtual Transition Dispatch(state_base_type *, const event_base_ptr_type & ) { return NoTransition(); };

        // some state may have a initialize state, when enter these state, 
        // they will transit to the initialize state immediately
        virtual hsm_bool   HasInitializeState() { return hsm_false; }
        virtual Transition GetInitializeState() { return NoTransition(); }

        StateMachine* state_machine_;
        size_t        stack_depth_;
};

// our hsm implementation is event-driven struct, 
// an event arrive, process it, then wait for next event
// this's the base event struct
struct Event 
{
        Event() {}
        virtual ~Event() {}

        // use shared_ptr to store events, then we can send an event to the state machine
        // and has no need to delete it manual
        typedef hsm_ptr<const Event> event_base_ptr_type;

        // RTTI interface
        virtual TypeInfoStorage GetType() const { return typeid(*this); }

        // clone an input event to the shared_ptr
        virtual event_base_ptr_type Clone() const { return event_base_ptr_type(); }
};

// user should derive event from event_base but not struct Event,
// this implement the Clone function, each derived struct can use it to 
// clone themself
template <typename SelfType>
struct event_base : Event
{
        typedef hsm_ptr<const SelfType> self_ptr_type;

        virtual event_base_ptr_type Clone() const
        {
                return self_ptr_type(new SelfType(* static_cast<const SelfType*>(this)));
        }
};

template< class T >
struct make_list : public boost::mpl::eval_if<
        boost::mpl::is_sequence< T >,
        boost::mpl::identity< T >,
        boost::mpl::identity< hsm_vector< T > > > {};

// just like the event_base struct, user's state should derive from it.
// the second parameter is InitType, means the initialize state, default is None
template <typename SelfType, typename InitType = SelfType>
struct state_base : State
{
        // if a state want to react some event, it should re-typdef the reactions like that:
        // typedef hsm_vector<(events you're interest)> reactions
        // the default reactions is not empty, because I do iterator of the list later,
        // but that routine is hard to handle an empty list
        typedef hsm_vector<event_base_type> reactions;

        // default react for events, if the user declared to handle some event,
        // but no appropriate react function, then will call this, and send out a warnning,
        // if the marco ABORT_WHEN_AN_EVENT_NOT_HANDLE is ture, abort finally.
        // change the marco at the top of this file.
        Transition react()  
        { 
                return NoTransition();
        }
        Transition react(const Event & e)
        { 
                std::cout << "You MUST add a static function 'react' to handle the event '";
                std::cout << typeid(e).name() << "'" << std::endl;
                hsm_assert(!ABORT_WHEN_AN_EVENT_NOT_HANDLE);
                return NoTransition();
        }

        // this function and the two struct below is used to dispatch the event to right state,
        // two parameter, a point to state and a reference of event, check if the state declared
        // to handle this event, yes, call the appropriate react function, no, return EndTransition
        // the difficulty is we don't know the Type of state and event, I used metaprogramming here.
        // the instantiation of the two template may run like that:
        // function dispatch(i, s, e):
        //     if (i is the last event in reaction list):
        //         return ...;
        //     else 
        //         if i == e:
        //             call react funtion for i;
        //         else 
        //             i = the next event in reaction list
        //             dispatch(i, s, e)
        virtual Transition Dispatch(state_base_type *s, const event_base_ptr_type & e)
        {
                typedef typename make_list<typename SelfType::reactions>::type list;
                typedef typename boost::mpl::front<list>::type first;
                typedef typename boost::mpl::back<list>::type  last;

                return DispatchImpl<first, last, list>::Dispatch(s, e);
        }

        template <typename Begin, typename End, typename Sequence>
        struct DispatchImpl
        {
                static Transition Dispatch(state_base_type *s, const event_base_ptr_type & e)
                {
                        if (GetEventType<Begin>() == e->GetType()) {
                                // use struct Dispatcher to find out the real type of event,
                                // then cast it, so that we can call the right react function.
                                // beacuse react function is not virtual, so we need to cast 
                                // state to the right type too
                                typedef Dispatcher<SelfType, Begin> DispatcherType;
                                return static_cast<SelfType*>(s)->react(
                                        *std::dynamic_pointer_cast<
                                        const typename DispatcherType::EventType
                                        >(e).get());
                        } else {
                                typedef typename boost::mpl::pop_front<Sequence>::type list;
                                typedef typename boost::mpl::front<list>::type         first;
                                typedef End last;
                                return DispatchImpl<first, last, list>::Dispatch(s, e);
                        }
                }

        };

        template <typename End, typename Sequence>
        struct DispatchImpl<End, End, Sequence>
        {
                static Transition Dispatch(state_base_type *s, const event_base_ptr_type & e)
                {
                        if (GetEventType<End>() == e->GetType()) {
                                typedef Dispatcher<SelfType, End> DispatcherType;
                                return static_cast<SelfType*>(s)->react(
                                        *std::dynamic_pointer_cast<
                                        const typename DispatcherType::EventType
                                        >(e).get());
                        } else {
                                return EndTransition();
                        }
                }
        };

        // has an initialize state? we know it at compile time
        virtual hsm_bool HasInitializeState() 
        {
                typedef typename boost::mpl::if_c<
                        boost::is_same<SelfType, InitType>::value,
                        boost::mpl::false_,
                        boost::mpl::true_
                        >::type type;

                return type::value;
        }

        virtual Transition GetInitializeState()
        {
                return SiblingTransition(GetStateFactory<InitType>());
        }
};

// provides convenient typed access to the Owner. 
template <typename SelfType, typename OwnerType, typename InitType = SelfType>
struct state_with_owner : state_base<SelfType, InitType>
{
        using state_base<SelfType, InitType>::state_machine;

        const OwnerType& owner() const
        {
                return *static_cast<const OwnerType*>(state_machine().owner());
        }
};

typedef void Owner;

class StateMachine
{
public:
        StateMachine()  : owner_(0) {}
        ~StateMachine() { stop(); }

        typedef Event event_base_type;
        typedef std::list<hsm_ptr<const Event> > event_queue_type;
        typedef std::vector<State*> state_stack_type;

        // initializes the state machine
        template <typename InitialStateType>
        void initialize(Owner* owner = 0);

        // Pops all states off the state stack, including initial state, 
        // invoking on_exit on each one in inner-to-outer order.
        void stop();

        // main interface to user
        void process_event(const event_base_type &);

        // accessors
        inline Owner* owner()             { return owner_; }
        inline const Owner* owner() const { return owner_; }

private:
        friend struct State;

        void ProcessQueueEvent();
        void ProcessDeferredEvent();

        void CreateAndPushInitialState(const Transition& transition);

        // Returns state at input depth, or NULL if depth is invalid
        State* GetStateAtDepth(size_t depth);

        inline void PushState(State* state);
        inline void PushStateAtDepth(int depth, State* state);
        inline void PopState();
        inline void PopStateAtDepth(int depth);

        // defer event
        inline void DeferEvent();

        Owner* owner_;

        Transition initial_transition_;
        state_stack_type state_stack_;

        event_queue_type event_queue_;
        event_queue_type deferred_queue_;
};

inline void InitState(State* state, StateMachine* state_machine, size_t stack_depth)
{
        hsm_assert(state_machine != 0);

        state->state_machine_ = state_machine;
        state->stack_depth_   = stack_depth;
}

inline State* CreateState(const Transition& transition, StateMachine* state_machine, size_t stack_depth)
{
        State* state = transition.state_factory().AllocateState();
        InitState(state, state_machine, stack_depth);
        return state;
}

inline void DestroyState(State* state)
{
        hsm_assert(state != 0);

        hsm_delete(state);
}

inline void InvokeOnEnter(State* state)
{
        hsm_assert(state != 0);

        state->on_enter();
}

inline void InvokeOnExit(State* state)
{
        hsm_assert(state != 0);

        state->on_exit();
}

inline Transition State::defer()
{
        state_machine_->DeferEvent();
        return NoTransition();
}

inline void StateMachine::PushState(State* state)
{
        hsm_assert(state != 0);

        state_stack_.push_back(state);
        InvokeOnEnter(state);

        if (state->HasInitializeState()) {
                CreateAndPushInitialState(state->GetInitializeState());
        }
}

inline void StateMachine::PopState()
{
        hsm_assert(!state_stack_.empty());

        State* state = state_stack_.back();
        InvokeOnExit(state);
        state_stack_.pop_back();
        DestroyState(state);
}

inline void StateMachine::PopStateAtDepth(int depth)
{
        // depth must in range [0, stack.size)
        hsm_assert(depth < state_stack_.size());
        hsm_assert(depth >= 0 );

        for (int i = state_stack_.size() - 1; i >= depth; --i)
                PopState();
}

inline void StateMachine::CreateAndPushInitialState(const Transition& transition)
{
        State* initial_state = CreateState(transition, this, 0);
        PushState(initial_state);
}

template <typename InitialStateType>
void StateMachine::initialize(Owner *owner)
{
        // can not re-initialize a state machine
        hsm_assert(initial_transition_.IsNo());
        hsm_assert(state_stack_.empty());

        initial_transition_ = SiblingTransition(GetStateFactory<InitialStateType>());
        owner_              = owner;

        CreateAndPushInitialState(initial_transition_);
}

inline void StateMachine::stop()
{
        if (state_stack_.size() > 0)
                PopStateAtDepth(0);

        hsm_assert(state_stack_.empty());
}

inline State* StateMachine::GetStateAtDepth(size_t depth)
{
        hsm_assert(depth < state_stack_.size());

        return state_stack_[depth];
}

inline void StateMachine::DeferEvent()
{
        hsm_assert(!event_queue_.empty());

        deferred_queue_.push_back(event_queue_.front());
}

void StateMachine::ProcessQueueEvent()
{
        for (size_t depth = 0; depth < state_stack_.size(); ++depth)
        {
                State* state = GetStateAtDepth(depth);
                const Transition& transition = state->Dispatch(state, event_queue_.front());

                switch (transition.transition_type())
                {
                        case Transition::End:
                        {
                                continue;
                        }
                        break;

                        case Transition::No:
                        {
                                return ;
                        }
                        break;

                        case Transition::Sibling:
                        {
                                PopStateAtDepth(depth);

                                State* target_state = CreateState(transition, this, depth);
                                PushState(target_state);

                                ProcessDeferredEvent();

                                return ;
                        }
                        break;
                } // end switch on transition type
        }

        std::cout << "Can not handle a unknown event\n";
        hsm_assert(!ABORT_WHEN_RECEIVE_UNKNOWN_EVENT);
        return ;
}

void StateMachine::ProcessDeferredEvent()
{
        for (size_t i = deferred_queue_.size(); i > 0; --i)
        {
                event_queue_.push_front(deferred_queue_.front());
                ProcessQueueEvent();
                deferred_queue_.pop_front();
                event_queue_.pop_front();
        }
}

void StateMachine::process_event(const event_base_type & event)
{
        event_queue_.push_back(event.Clone());

        while (!event_queue_.empty()) {
                ProcessQueueEvent();
                event_queue_.pop_front();
        }
}

} // namespace hsm

#endif // _HSM_H_
