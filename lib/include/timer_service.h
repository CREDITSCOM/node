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
 * @brief	A service for accessing timer information. Использование. Удобнее всего объявить один
 * 			экземпляр, можно глобально. Например, TimerService&lt;&gt; timer_service объявляет
 * 			экземпляр сервиса, измеряющего время в миллисекундах. Для получения текущего времени
 * 			необходимо вызвать
 * 			@code auto dt = timer_service.Mark( text );
 * 			В данном примере сервис сохраняет метку времени с комментарием, а переменная dt
 * 			получает значение текущего времени. Вторым необязательным аргументом можно передать
 * 			произвольный целочисленный идентификатор, для сохранения с меткой времени. При помощи
 * 			идентификатора можно пометить логически связанные метки времени,чтобы отличать их от
 * 			прочих меток, также сохраненных сервисом. Вызовы можно выполнять из любого потока.
 * 			
 * 			Другой возможностью, предоставляемой сервисом, является отложенный на заданное время
 * 			асинхронный вызов переданной пользоваетльской процедуры. Например,
 * 			@code timer_service.Launch( std::chrono::milliseconds(500), []() { job(1000); } )
 * 			инициирует запуск job(1000) через 500 мсек в отдельном потоке и сразу возвращает
 * 			управление в точку вызова.
 * 			
 * 			Можно одновременно передать на отложенное выполнение пользовательскую функцию типа
 * 			void() или [](){} и сохранить метку времени. Это делается вызовом метода
 * 			MarkAndLaunch(), например dt = timer_service.MarkAndLaunch(
 * 			@code std::chrono::milliseconds(500), []() { job(1000); }, text );
 * 			Здесь первый параметр задает время ожидания до вызова функции, переданной вторым
 * 			параметром, а text (третий параметр) сохраняется с меткой времени
 * 			
 * 			В любое время при помощи метода Data() можно получить доступ к накопленным меткам
 * 			времени с комментариями. Поскольку каждый элемент списка меток времени имеет тип
 * 			std::tuple&lt;&gt; для извлечения непосредственно данных предусмотрены методы
 * 			TimePointFrom(), GroupIdFrom(), InfoFrom()
 * 			
 * 			В конце использования сервиса, если были запланированы отложенные вызовы
 * 			пользовательских процедур, необходимом вызвать метод WaitLaunchedJobs(), который
 * 			дожидается завершения отложенных вызовов и возвращает их количество.
 * 			
 * 			TODO:
 * 			 1) Предусмотреть возможность отмены незавершенных вызовов, хотя бы в период
 * 			ожидания заданного времени
 * 			 2) Снимать с контроля самостоятельно завершенные вызовы
 * 			процедур  
 * 			 3) Сохранять ошибки вызовов, если они были
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
	 * @fn	template<typename TS> long long TimerService::Mark(TS && info, int group_id = 0)
	 *
	 * @brief	Stores a current time mark with group_id and text info attached
	 *
	 * @author	aae
	 * @date	05.09.2018
	 *
	 * @tparam	TS	Type of the info (text type).
	 * @param [in,out]	info		The custom text information stored with time mark.
	 * @param 		  	group_id	(Optional) Identifier for the group of time marks to set logic
	 * 								relations between some marks.
	 *
	 * @return	Current time measured in resolution units (milliseconds by default).
	 */

	template<typename TS>
	long long Mark(TS && info, int group_id = 0)
	{
		TimePoint tp = Now();
		MarkData d = std::make_tuple(tp, group_id, info);
		{
			Lock lock(_mtx);
			this->_data.push_back(d);
		}
		return std::chrono::duration_cast<TRes>(tp - _t_start).count();
	}

	long long Time ()
	{
		return std::chrono::duration_cast<TRes>(Now() - _t_start).count ();
	}

	/**
	 * @fn	void TimerService::Launch(TRes&& wait_for, Callback&& proc)
	 *
	 * @brief	Launches 'proc' (async) after 'wait_for' expired
	 *
	 * @author	aae
	 * @date	05.09.2018
	 *
	 * @param [in,out]	wait_for	The time period to wait before launch 'proc' async. Measured in
	 * 								timer resolution units (TRes is param of TimeService template, default
	 * 								is milliseconds)
	 * @param [in,out]	proc		The procedure to launch async after 'wait_for' expire.
	 */

	void Launch(TRes&& wait_for, Callback&& proc)
	{
		try {
			std::thread thr([wait_for, proc]() {
				std::this_thread::sleep_for(wait_for);
				proc();
			});
			Lock lock(_mtx);
			_launched.push_back(std::move(thr));
		}
		catch (...) {
		}
	}

	/**
	 * @fn	template<typename TS> long long TimerService::MarkAndLaunch(TRes&& wait_for, Callback&& proc, TS && info, int group_id = 0)
	 *
	 * @brief	Stores a current time mark with group_id and text info attached, then launches 'proc'
	 * 			(async) after 'wait_for' expired
	 *
	 * @author	aae
	 * @date	05.09.2018
	 *
	 * @tparam	TS	Type of the info (text).
	 * @param [in,out]	wait_for	The time period to wait before launch 'proc' async. Measured in
	 * 								timer resolution units (TRes is param of TimeService template, default
	 * 								is milliseconds)
	 * @param [in,out]	proc		The procedure to launch async after 'wait_for' expire.
	 * @param [in,out]	info		The custom text information stored with time mark.
	 * @param 		  	group_id	(Optional) Identifier for the group of time marks to set logic
	 * 								relations between some marks.
	 *
	 * @return	Current time measured in resolution units (milliseconds by default).
	 *
	 * ### tparam	TS	Type of the info (text type).
	 */

	template<typename TS>
	long long MarkAndLaunch(TRes&& wait_for, Callback&& proc, TS && info, int group_id = 0)
	{
		Launch(std::forward<TRes>(wait_for), std::forward<Callback>(proc));
		return Mark(std::forward<TS>(info), group_id);
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
			Lock lock(_mtx);
			for (auto& t : _launched) {
				if (t.joinable()) {
					cnt++;
					t.join();
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
		Lock lock(_mtx);
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

private:

	static const int TimePointValue = 0;
	static const int GroupIdValue = 1;
	static const int InfoValue = 2;

	std::vector<MarkData> _data;
	std::vector<std::thread> _launched;
	TimePoint _t_start;
	std::mutex _mtx;

	TimePoint Now()
	{
		return Clock::now();
	}

	void ConsoleOut(const MarkData & d)
	{
		using namespace std::chrono;
		std::cout << "TS: " << duration_cast<milliseconds>(TimePointFrom(d) - _t_start).count() << ": [" << GroupIdFrom(d) << "] " << InfoFrom(d) << std::endl;
	}

public:
	void ConsoleOut()
	{
		std::cout << "TS: -- begin content --" << std::endl;
		if (!_data.empty()) {
			Lock lock(_mtx);
			for (const auto& it : _data) {
				ConsoleOut(it);
			}
		}
		std::cout << "TS: -- end content --" << std::endl;
	}
};

// Windows specific:
__declspec(selectany) TimerService<> timer_service;

//extern TimerService<> timer_service;
