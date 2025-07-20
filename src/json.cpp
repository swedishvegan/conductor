
// this entire file was written by chatgpt, i just showed it json.hpp and told it to implement the api

#include "json.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <iomanip>
#include <sys/stat.h>   // for mkdir
#include <unistd.h>     // for access
#include <cerrno>       // for errno
#include <cstring>      // for strerror
#include <cstdlib> // for getenv

using namespace std;

namespace {

shared_ptr<json> parseValue(const string &src, size_t &pos);
inline void skipWs(const string &src, size_t &pos) { while (pos < src.size() && isspace(static_cast<unsigned char>(src[pos]))) ++pos; }

string escapeString(const string &s) {
    string out; out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    stringstream ss; ss << "\\u" << hex << setw(4) << setfill('0') << (int)(unsigned char)c;
                    out += ss.str();
                } else out.push_back(c);
        }
    }
    return out;
}

void render(const shared_ptr<json> &node, stringstream &out, int indent, int depth) {
    string pad(depth * indent, ' ');
    switch (node->getDtype()) {
        case json::dtype::dict: {
            const auto &mp = node->getDict(); out << '{';
            if (!mp.empty()) {
                out << '\n'; bool first = true;
                for (const auto &kv : mp) {
                    if (!first) out << ",\n"; first = false;
                    out << string((depth + 1) * indent, ' ') << '"' << escapeString(kv.first) << "\": ";
                    render(kv.second, out, indent, depth + 1);
                }
                out << '\n' << pad;
            }
            out << '}'; break; }
        case json::dtype::list: {
            const auto &vec = node->getList(); out << '[';
            if (!vec.empty()) {
                out << '\n'; bool first = true;
                for (const auto &el : vec) {
                    if (!first) out << ",\n"; first = false;
                    out << string((depth + 1) * indent, ' ');
                    render(el, out, indent, depth + 1);
                }
                out << '\n' << pad;
            }
            out << ']'; break; }
        case json::dtype::lstring: out << '"' << escapeString(node->getString()) << '"'; break;
        case json::dtype::lint:    out << node->getInt();   break;
        case json::dtype::ldouble: out << node->getFloat(); break;
        case json::dtype::lbool:   out << (node->getBool() ? "true" : "false"); break;
        case json::dtype::lnull:   out << "null"; break;
    }
}

inline bool match(const string &src, size_t &pos, char expected) {
    skipWs(src, pos); if (pos < src.size() && src[pos] == expected) { ++pos; return true; } return false;
}

shared_ptr<json> parseStringLit(const string &src, size_t &pos) {
    if (src[pos] != '"') throw runtime_error("Expected string opening quote"); ++pos; string out;
    while (pos < src.size()) {
        char c = src[pos++]; if (c == '"') break;
        if (c == '\\') {
            if (pos >= src.size()) throw runtime_error("Bad escape"); char esc = src[pos++];
            switch (esc) {
                case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
                case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
                case 'r': out += '\r'; break; case 't': out += '\t'; break;
                case 'u': {
                    if (pos + 4 > src.size()) throw runtime_error("Bad unicode escape"); unsigned int code = 0;
                    for (int i = 0; i < 4; ++i) { char h = src[pos++]; code <<= 4;
                        if      (h >= '0' && h <= '9') code += h - '0';
                        else if (h >= 'a' && h <= 'f') code += 10 + h - 'a';
                        else if (h >= 'A' && h <= 'F') code += 10 + h - 'A'; else throw runtime_error("Bad unicode"); }
                    if (code <= 0x7F) out.push_back((char)code);
                    else if (code <= 0x7FF) { out.push_back((char)(0xC0 | ((code >> 6) & 0x1F))); out.push_back((char)(0x80 | (code & 0x3F))); }
                    else { out.push_back((char)(0xE0 | ((code >> 12) & 0x0F))); out.push_back((char)(0x80 | ((code >> 6) & 0x3F))); out.push_back((char)(0x80 | (code & 0x3F))); }
                    break; }
                default: throw runtime_error("Invalid escape char");
            }
        } else out.push_back(c);
    }
    return json::makeString(move(out));
}

shared_ptr<json> parseNumber(const string &src, size_t &pos) {
    size_t start = pos; if (src[pos] == '-') ++pos; while (pos < src.size() && isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
    bool isFloat = false; if (pos < src.size() && src[pos] == '.') { isFloat = true; ++pos; while (pos < src.size() && isdigit(static_cast<unsigned char>(src[pos]))) ++pos; }
    if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) { isFloat = true; ++pos; if (src[pos] == '+' || src[pos] == '-') ++pos; while (pos < src.size() && isdigit(static_cast<unsigned char>(src[pos]))) ++pos; }
    string num = src.substr(start, pos - start); return isFloat ? json::makeFloat(stod(num)) : json::makeInt(stoll(num));
}

shared_ptr<json> parseArray(const string &src, size_t &pos) {
    if (src[pos] != '[') throw runtime_error("Expected '['"); ++pos; auto list = json::makeList(); skipWs(src, pos);
    if (match(src, pos, ']')) return list; while (true) { list->getList().push_back(parseValue(src, pos)); if (match(src, pos, ']')) break; if (!match(src, pos, ',')) throw runtime_error("Expected ',' in array"); }
    return list;
}

shared_ptr<json> parseObject(const string &src, size_t &pos) {
    if (src[pos] != '{') throw runtime_error("Expected '{'"); ++pos; auto dict = json::makeDict(); skipWs(src, pos);
    if (match(src, pos, '}')) return dict; while (true) { skipWs(src, pos); if (src[pos] != '"') throw runtime_error("Expected string key"); string key = parseStringLit(src, pos)->getString(); if (!match(src, pos, ':')) throw runtime_error("Expected ':' after key"); dict->getDict()[key] = parseValue(src, pos); if (match(src, pos, '}')) break; if (!match(src, pos, ',')) throw runtime_error("Expected ',' in object"); }
    return dict;
}

shared_ptr<json> parseValue(const string &src, size_t &pos) {
    skipWs(src, pos); if (pos >= src.size()) throw runtime_error("Unexpected EOF"); char c = src[pos];
    switch (c) {
        case '{': return parseObject(src, pos); case '[': return parseArray(src, pos); case '"': return parseStringLit(src, pos);
        default:
            if (c == '-' || isdigit(static_cast<unsigned char>(c))) return parseNumber(src, pos);
            if (src.compare(pos, 4, "true") == 0 ) { pos += 4; return json::makeBool(true ); }
            if (src.compare(pos, 5, "false") == 0) { pos += 5; return json::makeBool(false); }
            if (src.compare(pos, 4, "null") == 0) { pos += 4; return json::makeNull(); }
            throw runtime_error("Invalid json value"); }
}

} // namespace

// ─────────────────────────── json methods ────────────────────────────────

json::json(dtype d) : ty(d), i(0), f(0.0), b(false) {}

// Factories
shared_ptr<json> json::makeList()   { return make_shared<json>(dtype::list); }
shared_ptr<json> json::makeDict()   { return make_shared<json>(dtype::dict); }
shared_ptr<json> json::makeString(string str) { auto p = make_shared<json>(dtype::lstring); p->s = move(str); return p; }
shared_ptr<json> json::makeInt(int64_t val)  { auto p = make_shared<json>(dtype::lint);    p->i = val; return p; }
shared_ptr<json> json::makeFloat(double val) { auto p = make_shared<json>(dtype::ldouble); p->f = val; return p; }
shared_ptr<json> json::makeBool(bool val)    { auto p = make_shared<json>(dtype::lbool);   p->b = val; return p; }
shared_ptr<json> json::makeNull() { return make_shared<json>(dtype::lnull); }

// Loaders
shared_ptr<json> json::loadFromString(const string& src) {
    size_t pos = 0;
    auto root = parseValue(src, pos);
    skipWs(src, pos);
    if (pos != src.size()) throw runtime_error("Trailing characters after json");
    return root;
}

std::string expand_user_path(const std::string& path) {
    if (!path.empty() && path[0] == '~') {
        const char* home = getenv("HOME");
        if (!home) throw std::runtime_error("Cannot resolve ~: HOME environment variable not set");
        return std::string(home) + path.substr(1);  // replace ~ with $HOME
    }
    return path;
}

shared_ptr<json> json::loadFromFile(const string& path_, bool force) {
    std::string path = expand_user_path(path_);
    ifstream in(path);
    if (!in) {
        if (force) return json::makeDict();
        throw runtime_error("Unable to open file: " + path);
    }
    stringstream buf; buf << in.rdbuf();
    return loadFromString(buf.str());
}

// Getters
json::dtype json::getDtype() { return ty; }
vector<shared_ptr<json>>& json::getList()  { if (ty != dtype::list)    throw runtime_error("Not a list");  return v; }
map<string, shared_ptr<json>>& json::getDict(){ if (ty != dtype::dict) throw runtime_error("Not a dict"); return m; }
string  json::getString() { if (ty != dtype::lstring)  throw runtime_error("Not a string"); return s; }
int64_t json::getInt()    { if (ty != dtype::lint)     throw runtime_error("Not an int");    return static_cast<int64_t>(i); }
double  json::getFloat()  { if (ty != dtype::ldouble)  throw runtime_error("Not a float");  return f; }
bool    json::getBool()   { if (ty != dtype::lbool)    throw runtime_error("Not a bool");   return b; }

// Setters
void json::setString(const string &val){ if (ty != dtype::lstring) throw runtime_error("Not a string"); s = val; }
void json::setInt(int64_t val)         { if (ty != dtype::lint)    throw runtime_error("Not an int");    i = val; }
void json::setFloat(double val)        { if (ty != dtype::ldouble) throw runtime_error("Not a float");  f = val; }
void json::setBool(bool val)           { if (ty != dtype::lbool)   throw runtime_error("Not a bool");   b = val; }

// Pretty printer (Option A: non-owning shared_ptr)
string json::print() {
    const int indent = 4;
    stringstream out;

    // Non-owning shared_ptr so we can reuse the render() helper without
    // inheriting enable_shared_from_this.
    shared_ptr<json> self(this, [](json*){});
    render(self, out, indent, 0);
    return out.str();
}

void json::save(const std::string& filepath_in, bool force) {
    auto filepath = expand_user_path(filepath_in);
    auto create_directories = [](const std::string& path) {
        size_t pos = 0;
        do {
            pos = path.find('/', pos + 1);
            std::string subdir = path.substr(0, pos);
            if (subdir.empty()) continue;

            if (access(subdir.c_str(), F_OK) != 0) {
                if (mkdir(subdir.c_str(), 0755) != 0 && errno != EEXIST) {
                    throw std::runtime_error("Failed to create directory '" + subdir + "': " + strerror(errno));
                }
            }
        } while (pos != std::string::npos);
    };

    // Extract directory part of the filepath
    size_t lastSlash = filepath.find_last_of('/');
    if (lastSlash != std::string::npos) {
        std::string dir = filepath.substr(0, lastSlash);
        if (access(dir.c_str(), F_OK) != 0) {
            if (force) {
                create_directories(dir);
            } else {
                throw std::runtime_error("Directory does not exist: " + dir);
            }
        }
    }

    std::ofstream out(filepath.c_str());
    if (!out) {
        throw std::runtime_error("Failed to open file for writing: " + filepath);
    }

    out << this->print();
    if (!out) {
        throw std::runtime_error("Failed to write JSON to file: " + filepath);
    }
}