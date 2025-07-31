
// chatgpt 

#include "validate.hpp"

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
namespace detail {

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

inline bool is_number(json::dtype t) {
    return t == json::dtype::lint || t == json::dtype::ldouble;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Forward declaration
// ---------------------------------------------------------------------------
static bool validate_value(const pjson& value, const pjson& schema);

// ---------------------------------------------------------------------------
// Top‑level equivalent to validate_arguments(...)
// ---------------------------------------------------------------------------
void validate_arguments(
    ValidationResult& out,
    const pjson& args,
    const pjson& function_declaration
) {
    if (!args || !function_declaration)
        throw std::runtime_error("Null json pointer supplied.");

    // --- fetch parameters ---------------------------------------------------
    const auto& decl = function_declaration->getDict();
    auto p_it = decl.find("parameters");
    if (p_it == decl.end() || p_it->second->getDtype() != json::dtype::dict)
        throw std::runtime_error("function_declaration.parameters is missing or not an object");
    const auto& params = p_it->second->getDict();

    // Must be an object‑typed parameter block (Gemini/OpenAPI assumption)
    auto type_it = params.find("type");
    if (type_it == params.end() || detail::to_lower(type_it->second->getString()) != "object")
        throw std::runtime_error("This helper assumes parameters.type == \"object\"");

    // --- collect properties / required -------------------------------------
    std::map<std::string, pjson> properties;
    if (auto pr = params.find("properties");
        pr != params.end() && pr->second->getDtype() == json::dtype::dict)
        properties = pr->second->getDict();

    std::set<std::string> required;
    if (auto rq = params.find("required");
        rq != params.end() && rq->second->getDtype() == json::dtype::list) {
        for (const auto& j : rq->second->getList())
            if (j->getDtype() == json::dtype::lstring) required.insert(j->getString());
    }

    // --- build result -------------------------------------------------------
    const auto& arg_dict = args->getDict();

    // Expected keys
    for (const auto& [name, subschema] : properties) {
        bool exists = arg_dict.count(name);
        std::optional<bool> valid;

        if (exists) {
            valid = validate_value(arg_dict.at(name), subschema);
        } else {
            valid = !required.count(name);  // missing → ok iff not required
        }

        out[name] = {exists, valid};
    }

    // Unexpected keys
    for (const auto& [name, _] : arg_dict)
        if (!properties.count(name)) out[name] = {true, std::nullopt};
}

// ---------------------------------------------------------------------------
// Recursive validator (subset of OpenAPI / Gemini facets)
// ---------------------------------------------------------------------------
static bool validate_value(const pjson& value, const pjson& schema) {
    if (!schema || schema->getDtype() != json::dtype::dict) return true;  // permissive

    const auto& sch = schema->getDict();

    // 1. nullable -----------------------------------------------------------
    if (!value || value->getDtype() == json::dtype::lnull) {
        auto n_it = sch.find("nullable");
        return (n_it != sch.end() && n_it->second->getDtype() == json::dtype::lbool &&
                n_it->second->getBool());
    }

    // 2. enum ---------------------------------------------------------------
    if (auto e_it = sch.find("enum");
        e_it != sch.end() && e_it->second->getDtype() == json::dtype::list) {
        const auto& lst = e_it->second->getList();
        bool in_enum = std::any_of(lst.begin(), lst.end(), [&](const pjson& j) {
            if (!j) return false;
            if (j->getDtype() != value->getDtype()) return false;
            switch (j->getDtype()) {
                case json::dtype::lstring:  return j->getString() == value->getString();
                case json::dtype::lint:     return j->getInt()    == value->getInt();
                case json::dtype::ldouble:  return j->getFloat()  == value->getFloat();
                case json::dtype::lbool:    return j->getBool()   == value->getBool();
                default:                    return false;
            }
        });
        if (!in_enum) return false;
    }

    // 3. primitive type checks ---------------------------------------------
    std::string t;
    if (auto t_it = sch.find("type"); t_it != sch.end() && t_it->second->getDtype() == json::dtype::lstring)
        t = detail::to_lower(t_it->second->getString());

    if (t == "string")  return value->getDtype() == json::dtype::lstring;
    if (t == "integer") return value->getDtype() == json::dtype::lint;
    if (t == "number")  return detail::is_number(value->getDtype());
    if (t == "boolean") return value->getDtype() == json::dtype::lbool;

    // 4. array --------------------------------------------------------------
    if (t == "array") {
        if (value->getDtype() != json::dtype::list) return false;
        auto items_it = sch.find("items");
        if (items_it == sch.end()) return true;  // no item schema → accept any list
        for (const auto& elem : value->getList())
            if (!validate_value(elem, items_it->second)) return false;
        return true;
    }

    // 5. object -------------------------------------------------------------
    if (t == "object") {
        if (value->getDtype() != json::dtype::dict) return false;
        const auto& vd = value->getDict();

        // required
        std::set<std::string> req;
        if (auto rq = sch.find("required");
            rq != sch.end() && rq->second->getDtype() == json::dtype::list) {
            for (const auto& j : rq->second->getList())
                if (j->getDtype() == json::dtype::lstring) req.insert(j->getString());
        }
        for (const auto& r : req)
            if (!vd.count(r)) return false;

        // properties
        if (auto pr = sch.find("properties");
            pr != sch.end() && pr->second->getDtype() == json::dtype::dict) {
            for (const auto& [key, subs] : pr->second->getDict())
                if (vd.count(key) && !validate_value(vd.at(key), subs)) return false;
        }
        // unknown keys are allowed
        return true;
    }

    // 6. unrecognised / missing type facet → treat as valid
    return true;
}