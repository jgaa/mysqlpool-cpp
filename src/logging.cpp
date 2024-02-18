

#include "mysqlpool/logging.h"

using namespace std;

namespace jgaa::mysqlpool {

Logger &Logger::Instance() noexcept
{
    static Logger logger;
    return logger;
}

LogEvent::~LogEvent()
{
    Logger::Instance().onEvent(level_, msg_.str());
}

} // ns

