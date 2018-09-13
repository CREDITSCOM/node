#pragma once
#include <mutex>
#include <atomic>
#include <vector>
#include <chrono>
#include <string>
#include <tuple>
#include <future>
#include <utility>
#include <iostream>

/**
 * @class	TimerService
 *
 * @brief	A service for accessing and accumulating timer information. Also, it may be used
 * 			for delayed launch of custom method
 * 			�������������. ������� ����� �������� ���� ���������, ����� ���������.
 * 			��������,
 * @code	TimerService<> timer_service
 * 			��������� ��������� �������, ����������� ����� � �������������. ����� ����������
 * 			�� ������ ������ (�.�. �������� ���������� �������) ��� �� ���������� ������ Reset(),
 * 			���� ��� ����� ���������.
 * 			��� ��������� �������� ������� ���������� �������:
 * @code	auto dt = timer_service.Time();
 * 			��� �������������� ��������� ������� � ����������� ��� �������� �� ���������� ��������� �
 * 			���������������� ��������� ������:
 * @code	auto dt = timer_service.TimeStore( text );
 * 			� ������ ������� ������ ��������� ����� ������� � ������������, � ���������� dt
 * 			�������� �������� �������� �������. ������ �������������� ���������� ����� ��������
 * 			������������ ������������� �������������, ��� ���������� � ������ �������. ��� ������
 * 			�������������� ����� �������� ��������� ��������� ����� �������,����� �������� �� ��
 * 			������ �����, ����� ����������� ��������. ������ ����� ��������� �� ������ ������.
 * 			
 * 			������ ������������, ��������������� ��������, �������� ���������� �� �������� �����
 * 			����������� ����� ���������� ���������������� ���������. ����� �������� �� ����������
 * 			���������� ���������������� ������� ���� void() ��� ������-��������� [](){}. ��������,
 * @code	timer_service.Launch( 500, []() { job(1000); } )
 * 			���������� ������ job(1000) ����� 500 (��-���������, ����) � ��������� ������ � ����� ����������
 * 			���������� � ����� ������. ���������� ����� ����� ������ �� ���������� ������, �� ����,
 * 			� ������� ��� ������������ �����! �.�. ����� ������ ���� ���������������� � ���������
 * 			������������ �� ������
 * 			
 * 			����������� � ������������� ������ ������ ����� ��������� ����� �������. ��� �������� �������
 * 			MarkAndLaunch(), ��������
 * @code	dt = timer_service.MarkAndLaunch(500, []() { job(1000); }, "some info" );
 * 			����� ������ �������� ������ ����� �������� �� ������ �������, ���������� ������
 * 			����������, � ����� (������ ��������) ����������� � ������ �������
 * 			
 * 			� ����� ����� ��� ������ ������ Data() ����� �������� ������ � ����������� ������
 * 			������� � �������������. ��������� ������ ������� ������ ����� ������� ����� ���
 * 			std::tuple<> ��� ���������� ��������������� ������ ������������� ��������� ������
 * 			TimePointFrom(data), GroupIdFrom(data), InfoFrom(data), ��� data - ������� ������ ����� �������
 * 			
 * 			� ����� ������������� �������, ���� ���� ������������� ���������� ������
 * 			���������������� ��������, ����������� ������� ����� WaitLaunchedJobs(), �������
 * 			���������� ���������� ���������� ������� � ���������� �� ����������.
 * 			
 * 			TODO:
 * 			 1) ������������� ����������� ������ ������������� �������, ���� �� � ������
 * 			�������� ��������� �������
 * 			 2) ������� � �������� �������������� ����������� ������
 * 			��������  
 * 			 3) ��������� ������ �������, ���� ��� ����
 *
 * @author	aae
 * @date	05.09.2018
 *
 * @tparam	TRes	Timer resolution, default is std::chrono::milliseconds
 *
 * ### tparam	TRes	Timer resolution, default is std::chrono::milliseconds
 */

template<typename TRes = std::chrono::milliseconds>
class TimerService
{
public:

	/** @brief	Lock type*/
	using Lock = std::lock_guard<std::mutex>;

	/** @brief	Clock used */
	using Clock = std::chrono::steady_clock;

	/** @brief	The time point type depends on clock type*/
	using TimePoint = Clock::time_point;

	/** @brief	Information describing the time mark */
	using MarkData = std::tuple< TimePoint /*moment*/, int /*group_id*/, std::string /*info*/ >;

	/** @brief	The callback procedure to launch async*/
	using Callback = std::function<void()>;

	/**
	 * @fn	TimerService::TimerService()
	 *
	 * @brief	Default constructor. Sets current time as start time
	 *
	 * @author	aae
	 * @date	05.09.2018
	 */

	TimerService()
	{
		_t_start = Now();
	}

	/**
	 * @fn	~TimerService()
	 *
	 * @brief	To prevent unjoined threads from access to data stops them if any
	 *
	 * @author	User
	 * @date	12.09.2018
	 */

	~TimerService()
	{
		WaitLaunchedJobs();
	}

	/**
	 * @fn	void Reset()
	 *
	 * @brief	Resets this object. Sets current time as start time and clears stored time marks
	 *
	 * @author	User
	 * @date	06.09.2018
	 */

	void Reset( bool wait_launched_jobs = false )
	{
		_t_start = Now();
		ClearData();
		if (wait_launched_jobs) {
			WaitLaunchedJobs();
		}
	}

	/**
	 * @fn	template<typename TText> long long TimerService::TimeStore(TText && info, int group_id = 0)
	 *
	 * @brief	Stores a current time mark with group_id and text info attached
	 *
	 * @author	aae
	 * @date	05.09.2018
	 *
	 * @tparam	TText	Text type of the info.
	 * @param [in,out]	info		The custom text information stored with time mark.
	 * @param 		  	group_id	(Optional) Identifier for the group of time marks to set logic
	 * 								relations between some marks.
	 *
	 * @return	Current time measured in resolution units (milliseconds by default).
	 */

	template<typename TText>
	long long TimeStore(TText && info, int group_id = 0)
	{
		TimePoint tp = Now();
		MarkData d = std::make_tuple(tp, group_id, info);
		{
			Lock lock(_mtx_data);
			this->_data.push_back(d);
		}
		return std::chrono::duration_cast<TRes>(tp - _t_start).count();
	}

    /**
     * @fn  template<typename TText> long long TimeConsoleOut(TText && info, int group_id = 0)
     *
     * @brief   Time console out
     *
     * @author  User
     * @date    13.09.2018
     *
     * @tparam  TText   Type of the text.
     * @param [in,out]  info        The information.
     * @param           group_id    (Optional) Identifier for the group.
     *
     * @return  A long.
     */

    template<typename TText>
    long long TimeConsoleOut(TText && info, int group_id = 0)
    {
        TimePoint tp = Now();
        ConsoleOut( std::make_tuple(tp, group_id, info) );
        return std::chrono::duration_cast<TRes>(tp - _t_start).count();
    }

    /**
	 * @fn	long long TimerService::Time ()
	 *
	 * @brief	Gets the current time measured from last Reset() or constructing of the TimerService object
	 *
	 * @author	User
	 * @date	12.09.2018
	 *
	 * @return	A long.
	 */

	long long Time () const
	{
		return std::chrono::duration_cast<TRes>(Now() - _t_start).count ();
	}

	/**
	 * @fn	template<typename TNumeric> void TimerService::Launch(TNumeric wait_for, Callback&& proc)
	 *
	 * @brief	Launches 'proc' (async) after 'wait_for' expired
	 *
	 * @author	aae
	 * @date	05.09.2018
	 *
	 * @tparam	TNumeric	Numeric type of wait_for.
	 * @param 		  	wait_for	The time period to wait before launch 'proc' async. Measured in
	 * 								timer resolution units (TRes is param of TimeService template,
	 * 								default is milliseconds)
	 * @param [in,out]	proc		The procedure to launch async after 'wait_for' expire.
	 */

	template<typename TNumeric>
	void Launch(TNumeric wait_for, Callback&& proc)
	{
		try {
			std::thread thr([wait_for, proc]() {
				std::this_thread::sleep_for(TRes(wait_for));
				proc();
			});
			Lock lock(_mtx_jobs);
			_launched.push_back(std::move(thr));
		}
		catch (...) {
		}
	}

	/**
	 * @fn	int TimerService::WaitLaunchedJobs()
	 *
	 * @brief	Wait launched jobs if any
	 *
	 * @author	aae
	 * @date	05.09.2018
	 *
	 * @return	Count of finished jobs.
	 */

	int WaitLaunchedJobs()
	{
		int cnt = 0;
		if (!_launched.empty()) {
			Lock lock(_mtx_jobs);
			for(auto& t : _launched) {
				if(t.joinable()) {
					++cnt;
					try {
						t.join();
					}
					catch(const std::system_error&) {
					}
				}
			}
			_launched.clear();
		}
		return cnt;
	}

	/**
	 * @fn	const std::vector<MarkData>& TimerService::Data()
	 *
	 * @brief	Gets the list of all time marks. Method is not thread-safe and must not be called simultaneously with collecting new time marks
	 *
	 * @author	aae
	 * @date	05.09.2018
	 *
	 * @return	A reference to a const std::vector&lt;MarkData&gt;
	 */

	const std::vector<MarkData>& Data() const
	{
		return _data;
	}

	/**
	 * @fn	void TimerService::ClearData()
	 *
	 * @brief	Clears the data. Method is thread-safe, so it could be called from any thread at any time
	 *
	 * @author	aae
	 * @date	05.09.2018
	 */

	void ClearData()
	{
		Lock lock(_mtx_data);
		if (!_data.empty()) {
			this->_data.clear();
		}
	}

	/**
	 * @fn	const TimePoint& TimerService::TimePointFrom(const MarkData& data) const
	 *
	 * @brief	Extracts time point from item in list of time marks
	 *
	 * @author	aae
	 * @date	05.09.2018
	 *
	 * @param	data	The time mark item.
	 *
	 * @return	A reference to a const TimePoint.
	 */

	const TimePoint& TimePointFrom(const MarkData& data) const
	{
		return std::get<TimerService::TimePointValue>(data);
	}

	/**
	 * @fn	int TimerService::GroupIdFrom(const MarkData& data) const
	 *
	 * @brief	Extracts group identifier from item in list of time marks
	 *
	 * @author	aae
	 * @date	05.09.2018
	 *
	 * @param	data	The time mark item.
	 *
	 * @return	An int.
	 */

	int GroupIdFrom(const MarkData& data) const
	{
		return std::get<TimerService::GroupIdValue>(data);
	}

	/**
	 * @fn	const std::string& TimerService::InfoFrom(const MarkData& data) const
	 *
	 * @brief	Extracts text information identifier from item in list of time marks
	 *
	 * @author	aae
	 * @date	05.09.2018
	 *
	 * @param	data	The time mark item.
	 *
	 * @return	A reference to a const std::string.
	 */

	const std::string& InfoFrom(const MarkData& data) const
	{
		return std::get<TimerService::InfoValue>(data);
	}

	/**
	 * @fn	void TimerService::ConsoleOut()
	 *
	 * @brief	Perform output to console (std::cout) of all content. Method is thread-safe relatively own data
	 * 			but unsafe relatively std::cout
	 *
	 * @author	aae
	 * @date	12.09.2018
	 */

	void ConsoleOut()
	{
		std::cout << "TS: -- begin content --" << std::endl;
		if(!_data.empty()) {
			Lock lock(_mtx_data);
			for(const auto& it : _data) {
				ConsoleOut(it);
			}
		}
		std::cout << "TS: -- end content --" << std::endl;
	}

private:

	static const int TimePointValue = 0;
	static const int GroupIdValue = 1;
	static const int InfoValue = 2;

	std::vector<MarkData> _data;
	std::vector<std::thread> _launched;
	TimePoint _t_start;
	std::mutex _mtx_data;
	std::mutex _mtx_jobs;

	TimePoint Now() const
	{
		return Clock::now();
	}

	void ConsoleOut(const MarkData & d) const
	{
		using namespace std::chrono;
		std::cout << "TS: " << duration_cast<milliseconds>(TimePointFrom(d) - _t_start).count() << ": [" << GroupIdFrom(d) << "] " << InfoFrom(d) << std::endl;
	}

};

//#if !defined(_MSC_VER)
//extern TimerService<> timer_service;
//#else
//// MS specific:
//__declspec(selectany) TimerService<> timer_service;
//#endif
