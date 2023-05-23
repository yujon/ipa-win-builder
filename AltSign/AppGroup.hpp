//
//  AppGroup.hpp
//  AltSign-Windows
//

#pragma once

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <string>

#include <plist/plist.h>

class AppGroup
{
public:
	AppGroup();
	~AppGroup();

	AppGroup(plist_t plist) /* throws */;

	std::string name() const;
	std::string identifier() const;
	std::string groupIdentifier() const;

	friend std::ostream& operator<<(std::ostream& os, const AppGroup& appGroup);

private:
	std::string _name;
	std::string _identifier;
	std::string _groupIdentifier;
};

#pragma GCC visibility pop