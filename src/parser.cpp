#include "defs.hpp"
#include "rex.hpp"
#include "json.hpp"
#include "server.hpp"
#include "validate.hpp"

extern void lex(std::vector<ptoklex>&, const std::string&, const std::string&); // lexer.cpp
extern void analyze(dialogue&, const std::string&); // analysis.cpp

namespace fs = std::filesystem;

void queryfilenames(std::vector<std::string>& filenames, const std::vector<std::string>& paths) { // chatgpt

    for (const auto& path : paths) {

        try {
            if (!fs::exists(path)) throw std::runtime_error("Directory does not exist: " + path);

            if (!fs::is_directory(path)) throw std::runtime_error("Path is not a directory: " + path);

            for (const auto& entry : fs::directory_iterator(path)) 
                if (entry.is_regular_file() && entry.path().extension() == ".hll")
                    filenames.push_back(entry.path().string());
            
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to query filenames: " + std::string(e.what()));
        }

    }

}

void loadfiles(std::vector<std::string>& files, const std::vector<std::string>& filenames) { // chatgpt

    for (size_t i = 0; i < filenames.size(); ++i) {
        try {
            std::ifstream in(filenames[i]);
            std::ostringstream ss;
            ss << in.rdbuf();
            files[i] = ss.str() + "\n";

        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to load file " + filenames[i] + ": " + e.what());
        }
    }

}

std::string getname(const std::string& code, int start, int len, rex& r) {
    const char* s = code.c_str() + start;
    r.first(s, len);
    return std::string(s + r.pos, r.len);
}

std::string invalid_target(std::string filename, std::string type, std::string name, int line) {

    return (
        "Failed to parse `" +
        filename +
        "`\nInvalid " + 
        type +
        " name '" + 
        name + 
        "' on line " + 
        std::to_string(line)
    );

}

pjson ALL_COMMANDS;
pjson ALL_LEGAL_COMMANDS; // excludes `answer` which cannot be explicitly called except by the runtime

void loadcommands() {

    auto data = json::makeDict();
    data->getDict()["request"] = json::makeString("get_commands");
    data->getDict()["data"] = json::makeDict();

    auto res = json::loadFromString(post(data->print()));
    if (res->getDict()["status"]->getString() != "ok")
        throw std::runtime_error(
            res->getDict()["reason"]->getString()
        );

    ALL_COMMANDS = res->getDict()["data"];

    ALL_LEGAL_COMMANDS = json::makeDict();
    for (const auto& k : ALL_COMMANDS->getDict())
        if (k.first != "answer") ALL_LEGAL_COMMANDS->getDict()[k.first] = k.second;

}

std::string pverr(const std::string& aname, int line, const std::string& actname, const std::string& reason) {
    return (
        "Failed to parse `" + 
        aname +
        "`:\nInvalid action `" +
        actname +
        "` on line " +
        std::to_string(line) +
        ": " +
        reason
    );
}

void validateargs(const std::string& aname, const std::string& actname, int line, pjson args, bool isuser) {

    if (!ALL_LEGAL_COMMANDS) loadcommands();
    
    if (ALL_LEGAL_COMMANDS->getDict().find(actname) == ALL_LEGAL_COMMANDS->getDict().end())
        throw std::runtime_error(
            pverr(
                aname,
                line,
                actname,
                "Command does not exist"
            )
        );

    ValidationResult res;
    validate_arguments(res, args, ALL_LEGAL_COMMANDS->getDict()[actname]);

    for (const auto& val : res) {
        if (val.second.valid.has_value()) {
            if (*val.second.valid) continue; // valid -> ok
            else if (!isuser && !val.second.exists) continue; // agent action with missing argument -> ok (don't need to supply all arguments to agent)
        }
        throw std::runtime_error(
            pverr(
                aname,
                line,
                actname,
                "Argument `" +
                val.first + 
                "` does not exist or did not match expected format"
            )
        );
    }

}

nameregistry dialogue::contextnames;
nameregistry dialogue::agentnames;

std::unique_ptr<rex> r;
std::unique_ptr<rex> pa;
std::unique_ptr<rex> ind;
std::unique_ptr<rex> wsp;

void parse(dialogues& d, const std::vector<std::string>& paths) {

    if (!r) {
        r = std::make_unique<rex>(rident);
        pa = std::make_unique<rex>(".*(/|\\\\)");
        ind = std::make_unique<rex>("( |\t)*");
        wsp = std::make_unique<rex>("( |\t|\n)*");
    }

    std::vector<std::string> filenames;
    queryfilenames(filenames, paths);

    std::vector<std::string> files(filenames.size());
    loadfiles(files, filenames);

    // discover symbol definitions

    for (int i = 0; i < files.size(); i++) {

        pa->first(filenames[i].c_str());
        std::string aname = filenames[i].substr(pa->len, filenames[i].size() - pa->len - 4);
        int aid = dialogue::agentnames.registername(aname);
        if (aid < 0) throw std::runtime_error("Duplicate dialogue name: '" + aname + "'");

        auto& dial = d[aid];
        dial.code = files[i];
        lex(dial.tokens, aname, dial.code);
        int expecting = 0; // 1 for label, 2 for public label, 3 for ctx

        for (const auto& ptl : dial.tokens) {

            if (expecting > 2) {

                std::string cname = getname(dial.code, ptl.start, ptl.len, *r);
                int lid = dialogue::contextnames.registername(cname);
                if (lid < 0) throw std::runtime_error(("Failed to parse `" + aname) + "`\nContext '" + cname + "' on line " + std::to_string(ptl.line) + " is duplicated");

            }
            else if (expecting > 0) {

                std::string lname = getname(dial.code, ptl.start, ptl.len, *r);
                int lid = dial.labelnames.registername(lname);
                if (lid < 0) throw std::runtime_error(("Failed to parse `" + aname) + "`\nLabel '" + lname + "' on line " + std::to_string(ptl.line) + " is duplicated");
                if (expecting == 2) dial.entrypoints.insert(lid);

            }
            switch (ptl.tok) {
                case label: expecting = 1; break;
                case publiclabel: expecting = 2; break;
                case storectx: expecting = 3; break;
                default: expecting = 0;
            }

        }

    }

    // parse code

    for (auto& it : d) {

        auto& dial = it.second;
        std::string aname = dialogue::agentnames.queryname(it.first);
        const auto& tokens = dial.tokens;
        std::string idnamelh, idnamerh;
        int idlh, idrh;
        std::shared_ptr<inst> curaction;
        pjson curactionjson;

        for (int i = 0; i < tokens.size(); i++) {

            auto ptl = tokens[i];

            // collect identifier name(s) and textblock content if applicable

            switch (ptl.tok) {

                case label:
                case publiclabel:
                case storectx:
                case goto_:
                case loadctx: {

                    i++;
                    auto ptln = tokens[i];
                    idnamelh = getname(dial.code, ptln.start, ptln.len, *r);
                    idlh = (ptl.tok == loadctx || ptl.tok == storectx)
                        ? dialogue::contextnames.query(idnamelh)
                        : dial.labelnames.query(idnamelh);
                    
                    if (ptl.tok == goto_ || ptl.tok == loadctx) if (idlh < 0)
                        throw std::runtime_error(
                            invalid_target(aname, ptl.tok == goto_ ? "label" : "context", idnamelh, ptl.line)
                        );
                    break;

                }
                case call:
                case invoke:
                case recurse: {

                    i++;
                    auto ptln = tokens[i];
                    idnamelh = getname(dial.code, ptln.start, ptln.len, *r);
                    idnamerh = getname(dial.code, ptln.start + r->pos + r->len + 1, tokens[i + 1].len, *r);
                    idlh = dialogue::agentnames.query(idnamelh);
                    idrh = d[idlh].labelnames.query(idnamerh);
                    if (idlh < 0) throw std::runtime_error(
                        invalid_target(aname, "agent", idnamelh, ptl.line)
                    );
                    if (idrh < 0) throw std::runtime_error(
                        invalid_target(aname, "agent label", idnamerh, ptl.line)
                    );
                    if (d[idlh].entrypoints.find(idrh) == d[idlh].entrypoints.end()) throw std::runtime_error(
                        "Failed to parse `" + aname + "`\nCannot enter on private label '" + idnamerh + "' on line " + std::to_string(ptl.line)
                    );
                    break;

                }
                case await: {
                    
                    i++;
                    idnamelh = -1;

                    if (tokens[i].tok != branch) break;

                }
                case userbranch: {

                    i++;
                    auto ptln = tokens[i];
                    idnamelh = getname(dial.code, ptln.start, ptln.len, *r);
                    idnamerh = getname(dial.code, ptln.start + r->pos + r->len + 1, tokens[i + 1].len, *r);
                    idlh = dial.labelnames.query(idnamelh);
                    idrh = dial.labelnames.query(idnamerh);
                    if (idlh < 0) throw std::runtime_error(
                        invalid_target(aname, "label", idnamelh, ptl.line)
                    );
                    if (idrh < 0) throw std::runtime_error(
                        invalid_target(aname, "label", idnamerh, ptl.line)
                    );
                    i--;
                    break;

                }
                case info:
                case autoprompt: {

                    idnamelh = "";
                    i++;

                    while (i < tokens.size()) {

                        auto ptln = tokens[i];
                        
                        if (ptln.tok < textblockline || ptln.tok > textblocknewline) break;
                        if (ptln.tok != textblockline) continue;

                        ind->first(dial.code.c_str() + ptln.start, ptln.len);
                        int offset = ind->pos == 0 ? ind->len : 0;
                        idnamelh += std::string(dial.code.c_str() + ptln.start + offset, ptln.len - offset);
                        i++;

                    } i--;

                    if (wsp->match(idnamelh.c_str())) throw std::runtime_error(
                        "Failed to parse `" + aname + "`\nTextblock defined at line " + std::to_string(ptl.line) + " has no content"
                    );

                }
            }

            // populate the instructions vector

            switch (ptl.tok) {

                case label:
                case publiclabel: {

                    dial.instructions.push_back(std::make_shared<inst_label>(idlh, ptl.tok == publiclabel));
                    dial.jumptable[idlh] = (int)dial.instructions.size();
                    break;

                }
                case goto_: {

                    dial.instructions.push_back(std::make_shared<inst_goto>(idlh));
                    break;

                }
                case loadctx: {

                    dial.instructions.push_back(std::make_shared<inst_loadctx>(idlh));
                    break;

                }
                case storectx: {

                    dial.instructions.push_back(std::make_shared<inst_storectx>(idlh));
                    break;

                }
                case info:
                case autoprompt: {

                    dial.instructions.push_back(std::make_shared<inst_textblock>(ptl.tok, idnamelh));

                    break;

                }
                case call:
                case invoke:
                case recurse: {

                    dial.instructions.push_back(std::make_shared<inst_ctrlflow>(ptl.tok, idlh, idrh));
                    break;

                }
                case await: {

                    switch (tokens[i].tok) {

                        case reply: {
                            dial.instructions.push_back(std::make_shared<inst_awaitreply>());
                            break;
                        }
                        case action: {
                            curaction = std::make_shared<inst_awaitaction>();
                            dial.instructions.push_back(curaction);
                            break;
                        }
                        case branch: {
                            dial.instructions.push_back(std::make_shared<inst_awaitbranch>(idlh, idrh));
                        }

                    }

                    break;

                }
                case useraction: {

                    curaction = std::make_shared<inst_action>();
                    dial.instructions.push_back(curaction);
                    break;

                }
                case userbranch: {

                    dial.instructions.push_back(std::make_shared<inst_branch>(idlh, idrh));
                    break;

                }
                case getreply:
                case pause_:
                case prompt: {

                    dial.instructions.push_back(std::make_shared<inst>(ptl.tok));
                    break;

                }
                case actionidentifier:
                case finalactionidentifier: {

                    std::string actname = getname(dial.code, ptl.start, ptl.len, *r);
                    actiondata data { actname, json::makeDict() };

                    if (curaction->tok == useraction)
                        std::dynamic_pointer_cast<inst_action>(curaction)->actions.push_back(data);
                    else 
                        std::dynamic_pointer_cast<inst_awaitaction>(curaction)->actions.push_back(data);

                    break;

                }
                case actionidentifierwithargs: {

                    std::string actname = getname(dial.code, ptl.start, ptl.len, *r);
                    actiondata data { actname, json::makeDict() };
                    std::string key;
                    
                    while (true) {

                        i++;
                        if (i >= tokens.size()) break;

                        auto ptln = tokens[i];

                        if (ptln.tok < actionargnewline || ptln.tok > actionargcontent) break;

                        if (ptln.tok == actionargnewline) continue;

                        else if (ptln.tok == actionargname) key = getname(dial.code, ptln.start, ptln.len, *r);

                        else { // actionargcontent 
                            try {
                                auto value = json::loadFromString(
                                    dial.code.substr(ptln.start, ptln.len)
                                );
                                data.args->getDict()[key] = value;
                            }
                            catch (...) {
                                throw std::runtime_error(
                                    "Failed to parse `" +
                                    aname + 
                                    "`\nValue of argument `" +
                                    key +
                                    "` at line " +
                                    std::to_string(ptln.line) +
                                    " is not valid JSON"
                                );
                            }
                        }

                    } i--;

                    auto& actions = 
                        (curaction->tok == useraction)
                        ? std::dynamic_pointer_cast<inst_action>(curaction)->actions
                        : 
                        std::dynamic_pointer_cast<inst_awaitaction>(curaction)->actions
                    ;
                    
                    if (curaction->tok != useraction) for (const auto& d : actions)
                        if (d.aname == actname)
                            throw std::runtime_error(
                                "Failed to parse `" +
                                aname + "`\nDuplicate agent action `" +
                                actname + "` at line " +
                                std::to_string(ptl.line)
                            );

                    validateargs(aname, actname, ptl.line, data.args, curaction->tok == useraction);
                    actions.push_back(data);

                    break;

                }
            }
        }
    }

    for (auto& it : d) // static analysis
        analyze(it.second, dialogue::agentnames.queryname(it.first));

}