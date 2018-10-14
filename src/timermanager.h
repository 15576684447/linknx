/*
    LinKNX KNX home automation platform
    Copyright (C) 2007 Jean-François Meessen <linknx@ouaye.net>
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef TIMERMANAGER_H
#define TIMERMANAGER_H

#include <list>
#include <string>
#include <map>
#include "config.h"
#include "logger.h"
#include "threads.h"
#include "ticpp.h"
#include "objectcontroller.h"

class DateTime
{
public:
	DateTime(time_t dateTime);

public:
	void advanceToHour(int hour, bool resetsMinute)
	{
		if (goToNextOccurrence(hour, brokenDownTime_m.tm_hour, 0, 23) && resetsMinute)
		{
			brokenDownTime_m.tm_min = 0;
		}
	}

	void advanceToMinute(int minute)
	{
		goToNextOccurrence(minute, brokenDownTime_m.tm_min, 0, 59);
	}

	void addMinutes(int count)
	{
		++brokenDownTime_m.tm_min;
		simplify();
	}

	void addMonths(int count)
	{
		++brokenDownTime_m.tm_mon;
		simplify();
	}

	void advanceToDayOfMonth(int day, bool resetsHour, bool resetsMinute)
	{
		if (day < 1 || day > 31) throw ticpp::Exception("Day of month specification is outside bounds.");

		// Go to next month if day of month is already behind us in the current
		// month.
		if (day < brokenDownTime_m.tm_mday) addMonths(1);

		if (brokenDownTime_m.tm_mday != day)
		{
			// We have to switch days, so reset hour and minutes.
			if (resetsMinute) brokenDownTime_m.tm_min = 0;
			if (resetsHour) brokenDownTime_m.tm_hour = 0;

			// Blindly set day of month without taking care if this day exists (April 31, for instance).
			// Then let mktime adjust the month if necessary, by switching to
			// following month.
			brokenDownTime_m.tm_mday = day;
			simplify();

			// At that point, simplify() may have switched to the following
			// month. Let's force day of month again. Our calendar guarantees
			// that the day we want exists in the month chosen by mktime (as
			// 31-day months are between shorter months. That's why there is no
			// need to simplify again.
			brokenDownTime_m.tm_mday = day;
		}
	}

	void advanceToMonth(int month, bool resetsDay, bool resetsHour, bool resetsMinute)
	{
		if (goToNextOccurrence(month, brokenDownTime_m.tm_mon, 0, 11))
		{
			if (resetsMinute) brokenDownTime_m.tm_min = 0;
			if (resetsHour) brokenDownTime_m.tm_hour = 0;
			if (resetsDay) brokenDownTime_m.tm_mday = 1;
		}
	}

	bool tryAdvanceToYear(int year, bool resetsMonth, bool resetsDay, bool resetsHour, bool resetsMinute)
	{
		if (brokenDownTime_m.tm_year > year) return false;

		if (brokenDownTime_m.tm_year != year)
		{
			// Save initial values.
			int initialDayOfMonth = brokenDownTime_m.tm_mday;
			int initialMonth = brokenDownTime_m.tm_mon;

			// Attempt to change year.
			brokenDownTime_m.tm_year = year;
			simplify(); // Can cause a change in month and day. For instance if initial date was 29/02/2016 and year is set to 2017 (there is no leap day in 2017).

			// Check year change did not break month and day or month, day and year represent
			// an inconsistent constraint.
			if (brokenDownTime_m.tm_mon != initialMonth || brokenDownTime_m.tm_mday != initialDayOfMonth) return false;

			if (resetsMinute) brokenDownTime_m.tm_min = 0;
			if (resetsHour) brokenDownTime_m.tm_hour = 0;
			if (resetsDay) brokenDownTime_m.tm_mday = 1;
			if (resetsMonth) brokenDownTime_m.tm_mon = 0;
		}
		return true;
	}

	tm *getBrokenDownTime() {return &brokenDownTime_m;}

private:
    void simplify();
	bool goToNextOccurrence(int target, int &field, int minValue, int maxValue)
	{
		if (target < minValue || target > maxValue) throw ticpp::Exception("Time specification is outside bounds.");

		// Increase value to reach the next occurrence. Field may overflow its
		// maxValue but mktime will take care of adjusting other fields in a
		// second step.
		// Do it iteratively, as for months, the dayOfMonth can be incompatible
		// with the current month. In this case, simplify() would turn month
		// causing target to be different from brokenDownTime_m.tm_mon.
		bool hasChanged = false;
		while (target != field)
		{
			int previousValue = field;
			field += (target - field) % (maxValue - minValue + 1);
			hasChanged |= field != previousValue;

			// Resolve overflow by adjusting day, month, etc if necessary.
			simplify();
		}

		return hasChanged;
	}

private:
	tm brokenDownTime_m;
};

class TimerTask
{
public:
    virtual ~TimerTask() {};
    virtual void onTimer(time_t time) = 0;
    virtual void reschedule(time_t from = 0) = 0;
    virtual time_t getExecTime() = 0;
    virtual void statusXml(ticpp::Element* pStatus) = 0;
};

class TimeSpec
{
public:
    enum ExceptionDays
    {
        No,
        Yes,
        DontCare
    };

    enum WeekDays
    {
        Mon = 0x01,
        Tue = 0x02,
        Wed = 0x04,
        Thu = 0x08,
        Fri = 0x10,
        Sat = 0x20,
        Sun = 0x40,
        All = 0x00
    };

    TimeSpec();
    TimeSpec(int min, int hour, int mday, int mon, int year);
    TimeSpec(int min, int hour, int wdays=All, ExceptionDays exception=DontCare);
    virtual ~TimeSpec() {};

    static TimeSpec* create(ticpp::Element* pConfig, ChangeListener* cl);
    static TimeSpec* create(const std::string& type, ChangeListener* cl);

    virtual void importXml(ticpp::Element* pConfig);
    virtual void exportXml(ticpp::Element* pConfig);

    virtual void getData(int *min, int *hour, int *mday, int *mon, int *year, int *wdays, ExceptionDays *exception, const struct tm * timeinfo);
    virtual bool adjustTime(struct tm * timeinfo) { return false; };
protected:
    //		int sec_m;
    int min_m;
    int hour_m;
    int mday_m;
    int mon_m;
    int year_m;
    int wdays_m;
    ExceptionDays exception_m;

};

class VariableTimeSpec : public TimeSpec
{
public:
    VariableTimeSpec(ChangeListener* cl);
    virtual ~VariableTimeSpec();

    virtual void importXml(ticpp::Element* pConfig);
    virtual void exportXml(ticpp::Element* pConfig);

    virtual void getData(int *min, int *hour, int *mday, int *mon, int *year, int *wdays, ExceptionDays *exception, const struct tm * timeinfo);
protected:
    TimeObject* time_m;
    DateObject* date_m;
    ChangeListener* cl_m;
    int offset_m;
};

class PeriodicTask : public TimerTask, public ChangeListener
{
public:
    PeriodicTask(ChangeListener* cl);
    virtual ~PeriodicTask();

    virtual void onTimer(time_t time);
    virtual void reschedule(time_t from);
    virtual time_t getExecTime() { return nextExecTime_m; };
    virtual void statusXml(ticpp::Element* pStatus);

    void setAt(TimeSpec* at) { at_m = at; };
    void setUntil(TimeSpec* until) { until_m = until; };
    void setDuring(int during) { during_m = during; };
    virtual void onChange(Object* object);

protected:
    TimeSpec *at_m, *until_m;
    int during_m, after_m;
    time_t nextExecTime_m;
    ChangeListener* cl_m;
    bool value_m;

    time_t findNext(time_t start, TimeSpec* next);
    time_t mktimeNoDst(struct tm * timeinfo);
    static Logger& logger_m;
};

class FixedTimeTask : public TimerTask
{
public:
    FixedTimeTask();
    virtual ~FixedTimeTask();

    virtual void onTimer(time_t time) = 0;
    virtual void reschedule(time_t from);
    virtual time_t getExecTime() { return execTime_m; };
    virtual void statusXml(ticpp::Element* pStatus);

protected:
    time_t execTime_m;
    static Logger& logger_m;
};

class TimerManager : protected Thread
{
public:
    enum TimerCheck
    {
        Immediate,
        Short,
        Long
    };

    TimerManager();
    virtual ~TimerManager();

    TimerCheck checkTaskList(time_t now);

    void addTask(TimerTask* task);
    void removeTask(TimerTask* task);

    void startManager() { Start(); };
    void stopManager() { Stop(); };

    virtual void statusXml(ticpp::Element* pStatus);

private:
    void Run (pth_sem_t * stop);

    typedef std::list<TimerTask*> TaskList_t;
    TaskList_t taskList_m;
    static Logger& logger_m;
};

class DaySpec
{
public:
    DaySpec() : mday_m(-1), mon_m(-1), year_m(-1) {};

    void importXml(ticpp::Element* pConfig);
    void exportXml(ticpp::Element* pConfig);

    int mday_m;
    int mon_m;
    int year_m;
};

class ExceptionDays
{
public:
    ExceptionDays();
    virtual ~ExceptionDays();

    void clear();
    void addDay(DaySpec* date);
    void removeDay(DaySpec* date);

    void importXml(ticpp::Element* pConfig);
    void exportXml(ticpp::Element* pConfig);

    bool isException(time_t time);

private:
    typedef std::list<DaySpec*> DaysList_t;
    DaysList_t daysList_m;
    static ExceptionDays* instance_m;
};

#endif
