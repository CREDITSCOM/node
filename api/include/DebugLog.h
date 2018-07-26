#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#define DEBUG_LOG

#ifdef DEBUG_LOG
#include <iostream>
#endif

#ifdef DEBUG_LOG

inline void Log()
{
	std::cerr << std::endl;
}

template <typename T, typename... Args>
inline void Log(T t, Args&&... args)
{
	std::cerr << t;
	Log(args...);
}

#else

template <typename... Args>
inline void Log(Args&&... args) {}

#endif 

#ifdef NDEBUG
template <typename T, typename... Args>
inline void DebugLog(T t, Args&&... args)
{

}
#else
template <typename T, typename... Args>
inline void DebugLog(T t, Args&&... args)
{
    std::cerr << t;
    Log(args...);
}
#endif


#endif // DEBUG_LOG_H
