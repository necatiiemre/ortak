#include "ErrorUtils.h"
#include "DebugLog.h" 
#include <sstream>


void checkReturnCode(int8_t code, const std::string& context)
{
    if (code != CODE_SUCCESS)
    {
        std::ostringstream oss;
        if (!context.empty())
            oss << "Error in: " << context << " | Code: " << static_cast<int>(code);
        else
            oss << "Error Code: " << static_cast<int>(code);

        throw std::runtime_error(oss.str());
    }
}
