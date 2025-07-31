
// chatgpt

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

#ifndef validate_inc
#define validate_inc 

struct ParamStatus {
    bool exists{false};
    std::optional<bool> valid;  // nullopt ≡ “unknown key” (not in schema)
};

using ValidationResult = std::map<std::string, ParamStatus>;

void validate_arguments(
    ValidationResult& res,
    const pjson& args,
    const pjson& function_declaration
);

#endif