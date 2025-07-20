#ifndef _json_inc
#define _json_inc

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <variant>
#include <cstdint>

// basic json implementation

struct json;
using pjson = std::shared_ptr<json>;

struct json {

    enum class dtype { dict, list, lstring, lint, ldouble, lbool, lnull };

    json(dtype);
    static pjson loadFromString(const std::string& s); // throws runtime_error if string is not valid json
    static pjson loadFromFile(const std::string& filepath, bool force = false); // if force is false, throws runtime_error if filepath is invalid or file does not contain valid json
    static pjson makeList();
    static pjson makeDict();
    static pjson makeString(std::string = "");
    static pjson makeInt(int64_t = 0);
    static pjson makeFloat(double = 0);
    static pjson makeBool(bool = false);
    static pjson makeNull();

    dtype getDtype();

    // getters and setters throw runtime_error if the type being queried or set does not match the object's internal type
    
    std::vector<pjson>& getList();
    std::map<std::string, pjson>& getDict();
    std::string getString();
    int64_t getInt();
    double getFloat();
    bool getBool();

    void setString(const std::string&);
    void setInt(int64_t);
    void setFloat(double);
    void setBool(bool);

    std::string print(); // outputs formatted json string with readable tabbing
    void save(const std::string& filepath, bool force = false); // if force is true, it creates all intermediate directories

protected:

    dtype ty;
    std::map<std::string, pjson> m;
    std::vector<pjson> v;
    std::string s;
    uint64_t i;
    double f;
    bool b;

};

#endif