#pragma once
#include <string>
namespace progressive {
int parseEventVersion(const std::string& json);
bool isEventVersionSupported(int version);
std::string getLatestEventVersion();
}
