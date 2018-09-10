/**
 * @file	.
 *
 * @brief	Declares the RunAfterEx template class
 * 			The class instance is a special object to call any method after some delay applying desired LaunchScheme (see below)
 *
 * 			Samples:
 * 			1. Declare a class member to call argless method:
 * 				a) declare and implement a method
 *	@code		void MethodName();
 * 				b) declare a method caller
 *	@code		RunAfterEx<std::function<void()>> method_name_caller;
 *				c) init caller in constructor like this
 *	@code		method_name_caller([this]() { MethodName(); }, "MethodName()")
 *				d) to call MethodName() once with delay of 1 second:
 *	@code		method_name_caller.Schedule( 1000, LaunchScheme::single )
 *				d) call MethodName() periodically with delay of 200 milliseconds:
 *	@code		method_name_caller.Schedule( 200, LaunchScheme::periodic_single )
 *				e) stop periodic calling or cancel scheduled call
 *	@code		method_name_caller.Cancel();
 * 			2. Declare a class member to call 2-args method:
 * 				a) declare and implement a method
 *	@code		void MethodName( int arg1, int arg2 );
 * 				b) declare a method caller
 *	@code		std::function<void(int, int)> lambda = [this](int arg1, int arg2) {
					MethodName(arg1, arg2);
				};
				RunAfterEx<decltype(lambda)> method_name_caller;
 *				c) init caller in constructor like this
 *	@code		method_name_caller(std::move(lambda), "MethodName(int arg1, int arg2)")
 *				d) to call MethodName() once with delay of 1 second giving int parameters value1 and value2:
 *	@code		method_name_caller.Schedule( 1000, LaunchScheme::single, value1, value2 )
 *				d) to call MethodName() periodically with delay of 200 milliseconds
 *	@code		method_name_caller.Schedule( 200, LaunchScheme::periodic_single, value1, value2 )
 *				e) stop periodic calling or cancel scheduled call
 *	@code		method_name_caller.Cancel();
 */

 /**
  * Inspired by:
  *	void runAfter(const std::chrono::milliseconds& ms, std::function<void()> cb)
  *	{
  *		const auto tp = std::chrono::system_clock::now() + ms;
  *		std::thread tr([tp, cb, msg]() {
  *			std::this_thread::sleep_until(tp);
  *			LOG_WARN("Inserting callback ");
  *			CallsQueue::instance().insert(cb);
  *		});
  *		tr.detach();
  *	}
 */

#pragma once

// Uncomment to activate detailed log using timer_service (header 'timer_service.h' required>
//#define TIMER_SERVICE_LOG

#include <chrono>
#include <thread>
#include <string>
#include <atomic>
#include <lib/system/structures.hpp> // CallsQueue

#if defined(TIMER_SERVICE_LOG)
#include <sstream>
#include "timer_service.h"
#endif

/**
 * @enum	LaunchScheme
 *
 * @brief	Values that represent launch schenes
 */

enum class LaunchScheme {

	///< An enum constant representing the single option: launch once, cancel if previous schedule call still is not done
	single,

	///< An enum constant representing the periodic single option: launch periodically, schedule call cancels if already running one cycle
	periodic
};


/** @brief	The custom procedure */
using CustomVoidProc = std::function<void()>;

/**
 * @class	RunAfterEx
 *
 * @brief	A run after ex.
 *
 * @author	aae
 * @date	07.09.2018
 *
 * @tparam	TProc	Type of the procedure, default is void().
 * @tparam	TRes 	Timer resolution, default is std::chrono::milliseconds.
 */

template<typename TProc = std::function<void()>, typename TRes = std::chrono::milliseconds>
class RunAfterEx
{
public:

	RunAfterEx() = delete;
	RunAfterEx(const RunAfterEx&) = delete;
	void operator =(const RunAfterEx&) = delete;

	/**
	 * @fn	RunAfterEx(TProc&& proc, std::string&& comment) : _proc(std::forward<TProc>(proc)) , _comment(std::forward<std::string>(comment))
	 *
	 * @brief	Constructor
	 *
	 * @author	aae
	 * @date	07.09.2018
	 *
	 * @param [in,out]	proc   	The procedure.
	 * @param [in,out]	comment	The comment.
	 */

	RunAfterEx(TProc&& proc, std::string&& comment)
		: _proc(std::forward<TProc>(proc))
		, _comment(std::forward<std::string>(comment))
	{
	}

	/**
	 * @fn	template<typename TNumeric, typename... Args> void Schedule(TNumeric wait_for, LaunchScheme scheme, Args... args)
	 *
	 * @brief	Schedules
	 *
	 * @author	aae
	 * @date	07.09.2018
	 *
	 * @tparam	TNumeric	numeric type.
	 * @tparam	Args		list of the arguments.
	 * @param	wait_for	Time to wait for.
	 * @param	scheme  	The launch scheme.
	 * @param	args		Variable list of the arguments.
	 *
	 * ### tparam	Args	argument list.
	 */

	template<typename TNumeric, typename... Args>
	void Schedule(TNumeric wait_for, LaunchScheme scheme, Args... args)
	{

#if defined(TIMER_SERVICE_LOG)
		std::ostringstream os;
		os << "RunAfterEx: schedule (" << wait_for << ") " << _comment;
		timer_service.Mark(os.str(), -1);
#endif

		// test has not already launched in single cases
		if (_launched)
		{
#if defined(TIMER_SERVICE_LOG)
			timer_service.Mark(os.str() + " is canceled (already running)", -1);
#endif
			return;
		}

		_cancel = false;
		_launched = true;
		std::thread([this, wait_for, scheme, args...]() {

			std::this_thread::sleep_for(TRes(wait_for));

			// test cancel condition after waiting finished
			if (_cancel) {
				_launched = false;
#if defined(TIMER_SERVICE_LOG)
				std::ostringstream os;
				os << "RunAfterEx: " << _comment << " is canceled (while waiting)";
				timer_service.Mark(os.str(), -1);
#endif
				return;
			}

			switch (scheme) {
			case LaunchScheme::single:
				ExecuteProc(std::forward<TProc>(_proc), args...);
				break;
			case LaunchScheme::periodic:
				// launch periodically until external stop
				do {
					ExecuteProc(std::forward<TProc>(_proc), args...);
					if (_cancel) {
						break;
					}
					std::this_thread::sleep_for(TRes(wait_for));
					// test stop condition
				} while (!_cancel);
				break;
			}
#if defined(TIMER_SERVICE_LOG)
			std::ostringstream os2;
			os2 << "RunAfterEx: " << _comment << " is finished";
			timer_service.Mark(os2.str(), -1);
#endif
			_launched = false;
		}).detach();
	}

	/**
	 * @fn	void Cancel()
	 *
	 * @brief	Cancels this call, both single and periodic
	 *
	 * @author	aae
	 * @date	07.09.2018
	 */

	void Cancel()
	{
		_cancel = true;
	}

	/**
	 * @fn	bool IsScheduled() const
	 *
	 * @brief	Query if this object is scheduled for call
	 *
	 * @author	aae
	 * @date	10.09.2018
	 *
	 * @return	True if scheduled, false if not.
	 */

	bool IsScheduled() const
	{
		return _launched;
	}

	/**
	 * @fn	template<typename TNumeric> void WaitCancel(TNumeric wait_for )
	 *
	 * @brief	Wait cancel
	 *
	 * @author	aae
	 * @date	10.09.2018
	 *
	 * @tparam	TNumeric	numeric type.
	 * @param	wait_for	Time to wait for.
	 */

	template<typename TNumeric>
	void WaitCancel(TNumeric wait_for)
	{
		if (_launched) {
			_cancel = true;
			const TNumeric wait_min = wait_for < 20 ? wait_for : 20;
			TNumeric wait_remains = wait_for;
			while (_launched) {
				if (wait_remains < wait_min) {
					break; // cannot wait anymore
				}
				wait_remains -= wait_min;
				std::this_thread::sleep_for(TRes(wait_min));
			}
		}
	}

private:

	TProc _proc;
	std::string _comment;
	std::atomic_bool _launched{ false };
	std::atomic_bool _cancel{ false };
	std::atomic_uint32_t _count_queued{ 0 };
	std::atomic_uint32_t _count_done{ 0 };

	/**
	 * @fn	template<typename... Args> void ExecuteProc(std::function<void(Args...)>&& proc, Args... args)
	 *
	 * @brief	Executes the procedure operation
	 *
	 * @author	aae
	 * @date	07.09.2018
	 *
	 * @tparam	Args	list of the arguments.
	 * @param [in,out]	proc	The procedure.
	 * @param 		  	args	Variable list providing the arguments.
	 */

	template<typename... Args>
	void ExecuteProc(std::function<void(Args...)>&& proc, Args... args)
	{
		// test previous call has been executed by CallsQueue
		std::uint32_t queued = _count_queued;
		std::uint32_t done = _count_done;
		// TODO: from this point we can get a case when CallsQueue do last call just now but we still do not know about it;
		// current implementation ignores such a case as CallsQueue could make a call a little bit later :-)
		if (queued != done) {
			// CallsQueue guaranties every queued call is to be done, so somewhen we get queued == done!
//#if defined(TIMER_SERVICE_LOG)
//			std::ostringstream os;
//			os << "RunAfterEx: " << _comment << " rejected: queued " << queued << ", done " << done;
//			timer_service.Mark(os.str(), -1);
//#endif
			return;
		}
		// mark next queued call
		_count_queued.fetch_add(1);
		CallsQueue::instance().insert([this, proc, args...]() {
			proc(args...);
			// mark queued call done
			_count_done.fetch_add(1);
		});
#if defined(TIMER_SERVICE_LOG)
		std::ostringstream os;
		os << "RunAfterEx: " << _comment << " ...queued ok";
		timer_service.Mark(os.str(), -1);
#endif
	}
};
