//
// C++ Implementation: Log
//
// Description: 
//
//
// Author: Erik Ogenvik <erik@ogenvik.org>, (C) 2009
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.//
//
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "Log.h"
#include "LogObserver.h"
#include "LoggingInstance.h"
#include <iostream>
#include <algorithm>

namespace Ember {

/**
 * @brief An observer which writes to std::cout.
 */
class StdOutLogObserver : public LogObserver {
public:
	StdOutLogObserver() {
		Log::sObserverList.push_back(this);
	}

	void onNewMessage(const std::string& message,
					  const std::string& file,
					  const int& line,
					  const Log::MessageImportance& importance) override {
		std::cout << message << std::endl;
	}
};

Log::ObserverList Log::sObserverList;

int Log::sNumberOfExternalObservers = 0;

StdOutLogObserver Log::sStdOutLogObserver;

void Log::log(const char* message) {
	sendMessage(std::string(message), "", -1, MessageImportance::INFO);
}

void Log::log(MessageImportance importance, const char* message) {
	sendMessage(std::string(message), "", -1, importance);
}

void Log::addObserver(LogObserver* observer) {
	//test on already existing observer
	if (std::find(sObserverList.begin(), sObserverList.end(), observer) == sObserverList.end()) {
		if (sNumberOfExternalObservers == 0) {
			sObserverList.pop_front();
		}
		//no existing observer, add a new
		sObserverList.push_back(observer);
		sNumberOfExternalObservers++;
	}
}

int Log::removeObserver(LogObserver* observer) {
	auto I = std::find(sObserverList.begin(), sObserverList.end(), observer);
	if (I != sObserverList.end()) {
		sObserverList.erase(I);
		sNumberOfExternalObservers--;
		if (sNumberOfExternalObservers == 0) {
			sObserverList.push_back(&sStdOutLogObserver);
		}
		return 0;
	}
	return -1;
}

Log::HexNumber Log::hexNumber(const int intDecimal) {
	return HexNumber{intDecimal};
}

void Log::sendMessage(const std::string& message, const std::string& file, const int line, const MessageImportance importance) {

	for (auto& observer : sObserverList) {
		if (static_cast<int>(importance) >= static_cast<int>(observer->getFilter())) {
			observer->onNewMessage(message, file, line, importance);
		}
	}
}

}
