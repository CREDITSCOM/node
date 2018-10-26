#ifndef SIGNALS_H
#define SIGNALS_H

#include <functional>
#include <vector>

#define signals public
#define slots public
#define emit

namespace cs
{
    class Connector;

    /*!
        Base preudo signal
    */
    template<typename T>
    class Signal;

    /*!
        Signal needed specialization
    */
    template <typename Return, typename... InArgs>
    class Signal<Return(InArgs...)>
    {
    public:
        using Argument = std::function<Return(InArgs...)>;
        using Signature = Return(InArgs...);
        using Slots = std::vector<Argument>;

        /*!
            Generates signal

            @param args Any count of template parameters
        */
        template<typename... Args>
        inline void operator() (Args&&... args) const
        {
            for (auto& elem : m_slots) {
                elem(std::forward<Args>(args)...);
            }
        }

        Signal() = default;
        Signal(const Signal&) = delete;
        Signal& operator=(const Signal&) = delete;

        Signal(Signal&& signal):
            m_slots(std::move(signal.m_slots))
        {
            signal.m_slots.clear();
        }

        auto& operator=(Signal&& signal)
        {
            m_slots = std::move(signal.m_slots);
            signal.m_slots.clear();

            return *this;
        }

        ~Signal()
        {
            (*this) = nullptr;
        }

    private:

        // adds slot to signal
        template<typename T>
        auto& add(T&& s)
        {
            Argument arg = s;

            if (!arg) {
                return *this;
            }

            m_slots.push_back(arg);
            return *this;
        }

        // clears all signal slots
        auto& operator=(void* ptr)
        {
            if (ptr == nullptr) {
                m_slots.clear();
            }

            return *this;
        }

        // returns size of slots
        std::size_t size() const noexcept
        {
            return m_slots.size();
        }

        // all connected slots
        Slots m_slots;

        friend class Connector;

        template<typename T>
        friend class Signal;
    };

    /*!
        Signal for function prototype
    */
    template<typename T>
    class Signal<std::function<T>>
    {
    public:
        using Argument = std::function<T>;
        using Signature = T;
        using Slots = std::vector<Argument>;

        /*!
            Generates signal

            @param args Any count of template parameters
        */
        template<typename... Args>
        inline void operator() (Args&&... args) const
        {
            m_signal(std::forward<Args>(args)...);
        }

        // creation
        Signal() = default;
        Signal(const Signal&) = delete;
        Signal& operator=(const Signal&) = delete;

        Signal(Signal&& signal):
            m_signal(std::move(signal.m_signal))
        {
        }

        auto& operator=(Signal&& signal)
        {
            m_signal = std::move(signal.m_signal);
            return *this;
        }

        ~Signal()
        {
            (*this) = nullptr;
        }

    private:

        // adds slot to signal
        template<typename U>
        inline auto& add(U&& s)
        {
            m_signal.add(std::forward<U>(s));
            return *this;
        }

        // clears all slots
        inline auto& operator=(void* ptr)
        {
            m_signal = ptr;
            return *this;
        }

        // returns size of signal slots
        inline std::size_t size() const noexcept
        {
            return m_signal.size();
        }

        Signal<Signature> m_signal;
        friend class Connector;
    };

    // helper namespace
    namespace Args
    {
        template <typename T>
        struct GetArguments : GetArguments<decltype(&T::operator())> {};

        template <typename T, typename... Args>
        struct GetArguments<T(*)(Args...)> : std::integral_constant<unsigned, sizeof...(Args)> {};

        template <typename T, typename C, typename... Args>
        struct GetArguments<T(C::*)(Args...)> : std::integral_constant<unsigned, sizeof...(Args)> {};

        template <typename T, typename C, typename... Args>
        struct GetArguments<T(C::*)(Args...) const> : std::integral_constant<unsigned, sizeof...(Args)> {};

        // bindings
        template<int>
        class CheckArgs
        {
        public:

            template<typename T, typename Slot>
            void connect(const T& slotObj, Slot&& slot);
        };

        template<>
        class CheckArgs<0>
        {
        public:

            template<typename T, typename Slot>
            auto connect(const T& slotObj, Slot&& slot)
            {
                return std::bind(std::forward<Slot>(slot), slotObj);
            }
        };

        template<>
        class CheckArgs<1>
        {
        public:

            template<typename T, typename Slot>
            auto connect(const T& slotObj, Slot&& slot)
            {
                return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1);
            }
        };

        template<>
        class CheckArgs<2>
        {
        public:

            template<typename T, typename Slot>
            auto connect(const T& slotObj, Slot&& slot)
            {
                return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2);
            }
        };

        template<>
        class CheckArgs<3>
        {
        public:

            template<typename T, typename Slot>
            auto connect(const T& slotObj, Slot&& slot)
            {
                return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
            }
        };

        template<>
        class CheckArgs<4>
        {
        public:

            template<typename T, typename Slot>
            auto connect(const T& slotObj, Slot&& slot)
            {
                return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                    std::placeholders::_4);
            }
        };

        template<>
        class CheckArgs<5>
        {
        public:

            template<typename T, typename Slot>
            auto connect(const T& slotObj, Slot&& slot)
            {
                return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                    std::placeholders::_4, std::placeholders::_5);
            }
        };

        template<>
        class CheckArgs<6>
        {
        public:

            template<typename T, typename Slot>
            auto connect(const T& slotObj, Slot&& slot)
            {
                return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                    std::placeholders::_4, std::placeholders::_5, std::placeholders::_6);
            }
        };

        template<>
        class CheckArgs<7>
        {
        public:

            template<typename T, typename Slot>
            auto connect(const T& slotObj, Slot&& slot)
            {
                return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                    std::placeholders::_4, std::placeholders::_5, std::placeholders::_6, std::placeholders::_7);
            }
        };

        template<>
        class CheckArgs<8>
        {
        public:

            template<typename T, typename Slot>
            auto connect(const T& slotObj, Slot&& slot)
            {
                return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                    std::placeholders::_4, std::placeholders::_5, std::placeholders::_6, std::placeholders::_7,
                    std::placeholders::_8);
            }
        };

        template<>
        class CheckArgs<9>
        {
        public:

            template<typename T, typename Slot>
            auto connect(const T& slotObj, Slot&& slot)
            {
                return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                    std::placeholders::_4, std::placeholders::_5, std::placeholders::_6, std::placeholders::_7,
                    std::placeholders::_8, std::placeholders::_9);
            }
        };

        template<>
        class CheckArgs<10>
        {
        public:

            template<typename T, typename Slot>
            auto connect(const T& slotObj, Slot&& slot)
            {
                return std::bind(std::forward<Slot>(slot), slotObj, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                    std::placeholders::_4, std::placeholders::_5, std::placeholders::_6, std::placeholders::_7,
                    std::placeholders::_8, std::placeholders::_9, std::placeholders::_10);
            }
        };
    }

    /*!
        Signal - slot connection entity
    */
    class Connector
    {
    public:

        /*!
            Connects signal with callback

            @param signal Any signal object
            @param slotObj Method object owner
            @param slot Pointer to method
        */
        template<typename Signal, typename T, typename Slot>
        inline static void connect(Signal& signal, const T& slotObj, Slot&& slot)
        {
            constexpr int size = Args::GetArguments<Slot>();
            signal.add(Args::CheckArgs<size>().connect(slotObj, std::forward<Slot>(slot)));
        }

        /*!
            Connects signal with callback

            @param signal Any signal object
            @param slotObj Method object owner
            @param slot Pointer to method
        */
        template<typename Signal, typename T, typename Slot>
        inline static void connect(const Signal& signal, const T& slotObj, Slot&& slot)
        {
            constexpr int size = Args::GetArguments<Slot>();
            const_cast<Signal*>(&signal)->add(Args::CheckArgs<size>().connect(slotObj, std::forward<Slot>(slot)));
        }

        /*!
            Connects signal with callback

            @param signal Any signal object
            @param slot Function/lambda/closure
        */
        template<typename Signal, typename Slot>
        inline static void connect(Signal& signal, Slot&& slot)
        {
            signal.add(std::forward<Slot>(slot));
        }

        /*!
            Connects signal with lambda or function

            @param signal Any signal object
            @param slot Function or lambda/closure
        */
        template<typename Signal, typename Slot>
        inline static void connect(const Signal& signal, Slot&& slot)
        {
            const_cast<Signal*>(&signal)->add(std::forward<Slot>(slot));
        }

        /*!
            Connects signal pointer with lambda or function

            @param signal Any signal pointer
            @param slot Function or lambda/closure
        */
        template<typename Signal, typename Slot>
        inline static void connect(Signal* signal, Slot&& slot)
        {
            cs::Connector::connect(*signal, std::forward<Slot>(slot));
        }

        /*!
            Connects signal pointer with objects method

            @param signal Any signal pointer
            @param slotObj Pointer to slot object
            @param slot Method pointer
        */
        template<typename Signal, typename T, typename Slot>
        inline static void connect(Signal* signal, const T& slotObj, Slot&& slot)
        {
            cs::Connector::connect(*signal, slotObj, std::forward<Slot>(slot));
        }

        /*!
            Connects const signal pointer with objects method

            @param signal Any const signal pointer
            @param slotObj Pointer to slot object
            @param slot Method pointer
        */
        template<typename Signal, typename T, typename Slot>
        inline static void connect(const Signal* signal, const T& slotObj, Slot&& slot)
        {
            cs::Connector::connect(*signal, slotObj, std::forward<Slot>(slot));
        }

        /*!
            Drops all signal connections

            @param signal Any signal object
        */
        template<typename Signal>
        inline static void disconnect(Signal& signal)
        {
            signal = nullptr;
        }

        /*!
            Returns signal callbacks size

            @return Returns any signal object callbacks count
        */
        template<typename Signal>
        inline static std::size_t callbacks(Signal& signal)
        {
            return signal.size();
        }

    private:
        explicit Connector() = delete;
        Connector(const Connector&) = delete;
    };
}

#endif    // SIGNALS_H

