#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <memory>
#include <curl/curl.h>
#include "defs.hpp"
#include "json.hpp"
#include "commands.hpp"

const std::string URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent";
const int MAX_API_BACKOFF_TIME = 64;
const int MAX_REPLY_ATTEMPTS = 1;

class CurlClient { // chatgpt
public:
    static CurlClient& getInstance() {
        static CurlClient instance;
        return instance;
    }

    CURL* getHandle() const { return curl; }
    struct curl_slist* getHeaders() const { return headers; }

private:
    CURL* curl = nullptr;
    struct curl_slist* headers = nullptr;

    CurlClient() {
        curl_global_init(CURL_GLOBAL_ALL);
        curl = curl_easy_init();

        if (!curl) {
            throw std::runtime_error("Failed to initialize libcurl handle");
        }

        const char* api_key = std::getenv("GEMINI_API_KEY");
        if (!api_key) {
            throw std::runtime_error("GEMINI_API_KEY environment variable not set.");
        }

        auto api_key_header = std::string("x-goog-api-key: " + std::string(api_key));

        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, api_key_header.c_str());
    }

    ~CurlClient() {
        if (curl) curl_easy_cleanup(curl);
        if (headers) curl_slist_free_all(headers);
        curl_global_cleanup();
    }

    CurlClient(const CurlClient&) = delete;
    CurlClient& operator=(const CurlClient&) = delete;
};

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) { // chatgpt 
    size_t totalSize = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}


long curl_post_request(const std::string& payload, std::string& response_body) { // chatgpt
    CurlClient& client = CurlClient::getInstance();
    CURL* curl = client.getHandle();

    if (!curl) return -1;

    response_body.clear();
    curl_easy_reset(curl);  // Important: Reset between uses

    curl_easy_setopt(curl, CURLOPT_URL, URL.c_str()); // <-- fix is here
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, client.getHeaders());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);

    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        return -1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    return http_code;
}

pjson gencontextelement(const std::string& text, bool isuser = true) {

    auto tmp = json::makeString(text);
    auto elem = json::makeDict();
    elem->getDict()["text"] = tmp;
    tmp = elem;

    elem = json::makeList();
    elem->getList().push_back(tmp);
    tmp = elem;

    elem = json::makeDict();
    elem->getDict()["parts"] = tmp;
    elem->getDict()["role"] = json::makeString(isuser ? "user" : "model");

    return elem;

}

extern std::string expand_user_path(const std::string& path); // json.cpp

pjson gendefaultcontext(const std::string& module) {

    std::string prompt;
    std::ifstream promptfile(expand_user_path(hll_projects_folder "hll_initial_prompt.txt"));
    if (promptfile) {
        std::ostringstream ss;
        ss << promptfile.rdbuf();
        prompt = ss.str();
    }

    auto ctx = json::makeList();
    ctx->getList().push_back(gencontextelement(prompt + "\nYou are currently residing in a module named `" + module + "`.", true));
    ctx->getList().push_back(gencontextelement("Understood.", false)); // injecting agent reply into the context -- user gives instructions and agent replies "Understood."

    return ctx;

}

pjson cmd_list, cmd_read, cmd_write, cmd_append, cmd_listmodules, cmd_createmodule, cmd_branch;
std::map<ptok, pjson> commandmap;
pjson yes; // dummy object to signify that runcommand returns a yes choice if the command was ANSWER
pjson genconfig;

void initcommands() { 

    cmd_list = json::loadFromString(LIST_COMMAND);
    cmd_read = json::loadFromString(READ_COMMAND);
    cmd_write = json::loadFromString(WRITE_COMMAND);
    cmd_append = json::loadFromString(APPEND_COMMAND);
    cmd_listmodules = json::loadFromString(LIST_MODULES_COMMAND);
    cmd_createmodule = json::loadFromString(CREATE_MODULE_COMMAND);
    cmd_branch = json::loadFromString(ANSWER_COMMAND);

    /*commandmap[list] = cmd_list;
    commandmap[read_] = cmd_read;
    commandmap[write_] = cmd_write;
    commandmap[append] = cmd_append;
    commandmap[listmodules] = cmd_listmodules;
    commandmap[createmodule] = cmd_createmodule;
    commandmap[branch] = cmd_branch;*/

    yes = json::makeInt(0);
    genconfig = json::loadFromString("{\"thinkingConfig\":{\"include_thoughts\": false, \"thinkingBudget\": 0}}");

}

bool argexists(std::map<std::string, pjson>& args, const std::string& arg) { return args.find(arg) != args.end(); }

bool moduleexists(const std::string& modname, pjson dgraph) {
    auto& l = dgraph->getDict()["modules"]->getList();
    for (auto m : l) if (m->getString() == modname) return true;
    return false;
}

bool ischild(const std::string& parent, const std::string& child, pjson dgraph) {
    auto& l = dgraph->getDict()["children"]->getDict()[parent]->getList();
    for (auto m : l) if (m->getString() == child) return true;
    return false;
}

bool isdependency(const std::string& dependent, const std::string& dependency, pjson dgraph) {
    auto& l = dgraph->getDict()["dependencies"]->getDict()[dependent]->getList();
    for (auto m : l) if (m->getString() == dependency) return true;
    return false;
}

bool fileexists(const std::string& modname, const std::string& filename, pjson dgraph) {
    auto& l = dgraph->getDict()["files"]->getDict()[modname]->getList();
    for (auto m : l) if (m->getString() == filename) return true;
    return false;
}

pjson runcommand(
    const std::string& proot, 
    const std::string& curmodule,
    pjson cmd, 
    const std::string& cmdname,
    ptok ak, 
    pjson dgraph,
    std::string& failreason
) {

    auto fname = cmd->getDict()["name"]->getString();
    if (fname != cmdname) {
        failreason = "Invalid command '" + fname + "'.";
        return nullptr;
    }

    auto args = cmd->getDict()["args"]->getDict();

    switch (ak) {

        /*case list: {

            std::string modname = argexists(args, "module") ? args["module"]->getString() : "";
            if (modname == "") modname = curmodule;

            if (!moduleexists(modname, dgraph)) {
                failreason = "Invalid module name '" + modname + "'.";
                return nullptr;
            }

            bool islegal = (modname == curmodule) || (modname == "global") || ischild(curmodule, modname, dgraph) || isdependency(curmodule, modname, dgraph);
            
            if (!islegal) {
                failreason = "You do not have the necessary permissions to view the module '" + modname + "'.";
                return nullptr;
            }

            std::string res = "**Contents of '" + modname + "' module:**\n";
            auto& files = dgraph->getDict()["files"]->getDict()[modname]->getList();
            for (auto f : files) res += f->getString() + "\n";

            failreason = "";
            return gencontextelement(res);
            
        }
        case read_: {

            std::string modname = argexists(args, "module") ? args["module"]->getString() : "";
            if (modname == "") modname = curmodule;

            if (!moduleexists(modname, dgraph)) {
                failreason = "Invalid module name '" + modname + "'.";
                return nullptr;
            }

            bool islegal = (modname == curmodule) || (modname == "global") || ischild(curmodule, modname, dgraph) || isdependency(curmodule, modname, dgraph);
            
            if (!islegal) {
                failreason = "You do not have the necessary permissions to view the module '" + modname + "'.";
                return nullptr;
            }

            std::string filename = argexists(args, "path") ? args["path"]->getString() : "";

            if (filename.size() == 0) {
                failreason = "Filename 'path' argument not specified.";
                return nullptr;
            }

            if (!fileexists(modname, filename, dgraph)) {
                failreason = "File '" + filename + "' does not exist within module '" + modname + "'.";
                return nullptr;
            }

            std::string filepath = proot + filename;
            std::string filecontents;
            
            std::ifstream file(filepath + "." + modname);
            if (file) {
                std::ostringstream ss;
                ss << file.rdbuf();
                filecontents = ss.str();
            }

            std::string res = "**Contents of '" + filename + "' file:**\n" + filecontents;

            failreason = "";
            return gencontextelement(res);

        }
        case write_:
        case append: {

            bool shouldappend = ak == append;

            std::string modname = argexists(args, "module") ? args["module"]->getString() : "";
            if (modname == "") modname = curmodule;

            if (!moduleexists(modname, dgraph)) {
                failreason = "Invalid module name '" + modname + "'.";
                return nullptr;
            }

            bool islegal = (modname == curmodule) || (modname == "global") || ischild(curmodule, modname, dgraph);
            
            if (!islegal) {
                failreason = "You do not have the necessary permissions to modify the module '" + modname + "'.";
                return nullptr;
            }

            std::string filename = argexists(args, "path") ? args["path"]->getString() : "";

            if (filename.size() == 0) {
                failreason = "Filename 'path' argument not specified.";
                return nullptr;
            }

            std::string content = argexists(args, "content") ? args["content"]->getString() : "";

            if (content.size() == 0) {
                failreason = "No content for file was specified. If you meant to write a blank file, write a single space ' ' instead so as not to flag this error.";
                return nullptr;
            }

            std::string filepath = proot + filename;
            std::ofstream file(filepath + "." + modname, shouldappend ? std::ios::app : std::ios::trunc);
            if (!file) {
                failreason = "Failed to open file '" + filepath + "' for writing.";
                return nullptr;
            }
            file << content;

            if (!fileexists(modname, filename, dgraph)) dgraph->getDict()["files"]->getDict()[modname]->getList().push_back(json::makeString(filename));

            std::string res = "**Content successfully " + std::string(append ? "appended" : "written") + " to '" + filename + "'.**";

            failreason = "";
            return gencontextelement(res);

        }
        case listmodules: {

            auto& children = dgraph->getDict()["children"]->getDict()[curmodule]->getList();
            auto& dependencies = dgraph->getDict()["dependencies"]->getDict()[curmodule]->getList();
            auto& allmodules = dgraph->getDict()["modules"]->getList();
            
            std::string res = "**Current module:** " + curmodule + "\n**Children of current module:** ";
            for (int i = 0; i < children.size(); i++) {
                res += children[i]->getString();
                if (i != children.size() - 1) res += ", ";
            }
            if (children.size() == 0) res += "[no children]";
            res += "\n**Dependencies of current module:** ";
            for (int i = 0; i < dependencies.size(); i++) {
                res += dependencies[i]->getString();
                if (i != dependencies.size() - 1) res += ", ";
            }
            if (dependencies.size() == 0) res += "[no dependencies]";
            res += "\n**All existing modules:** ";
            for (int i = 0; i < allmodules.size(); i++) {
                res += allmodules[i]->getString();
                if (i != allmodules.size() - 1) res += ", ";
            }

            failreason = "";
            return gencontextelement(res);

        }
        case createmodule: {

            std::string modname = argexists(args, "module_name") ? args["module_name"]->getString() : "";

            if (modname.size() == 0) {
                failreason = "Name of module to create not specified.";
                return nullptr;
            }

            if (moduleexists(modname, dgraph)) {
                failreason = "A module with name '" + modname + "' already exists.";
                return nullptr;
            }

            if (!argexists(args, "dependencies")) {
                failreason = "Dependencies argument not specified.";
                return nullptr;
            }

            auto& dependencies = args["dependencies"]->getList();

            for (auto m : dependencies) {
                auto ms = m->getString();
                if (ms == modname) {
                    failreason = "Module cannot depend on itself.";
                    return nullptr;
                }
                if (!moduleexists(ms, dgraph)) {
                    failreason = "Invalid dependency module '" + ms + "'. No module with this name exists.";
                    return nullptr;
                }
                bool islegal = (ms == curmodule) || ischild(curmodule, ms, dgraph) || isdependency(curmodule, ms, dgraph);
                if (!islegal) {
                    failreason = "Invalid dependency module '" + ms + "'. Dependencies can only be selected from the current module, its children, and its dependencies.";
                    return nullptr;
                }
            }

            auto& dgd = dgraph->getDict();
            dgd["children"]->getDict()[curmodule]->getList().push_back(json::makeString(modname));
            dgd["children"]->getDict()[modname] = json::makeList();
            dgd["dependencies"]->getDict()[modname] = args["dependencies"];
            dgd["files"]->getDict()[modname] = json::makeList();
            dgd["modules"]->getList().push_back(json::makeString(modname));

            std::string res = "**Module '" + modname + "' successfully created.**";
            failreason = "";
            return gencontextelement(res);

        }
        case branch: {

            std::string ans = argexists(args, "answer") ? args["answer"]->getString() : "";

            if (ans.size() == 0) {
                failreason = "Answer not given.";
                return nullptr;
            }

            if (ans == "YES" || ans == "NO") {
                failreason = "";
                return (ans == "YES") ? yes : nullptr;
            }

            failreason = "Expected YES/NO for answer.";
            return nullptr;

        }*/

    }

    return nullptr;

}

bool apirequest(const std::string& proot, const std::string& curmodule, pjson dgraph, pjson ctx, ptok k, ptok ak) {

    if (!cmd_list) initcommands();

    std::string cmdname;

    switch (k) {

        case reply: break;
        case action: {

            switch (ak) {

                /*case list: { cmdname = "LIST"; break; }
                case read_: { cmdname = "READ"; break; }
                case write_: { cmdname = "WRITE"; break; }
                case append: { cmdname = "APPEND"; break; }
                case listmodules: { cmdname = "LIST_MODULES"; break; }
                case createmodule: { cmdname = "CREATE_MODULE"; break; }*/

            }
            break;

        }
        case branch: { cmdname = "ANSWER"; break; }

    }

    bool needscall = cmdname.size() > 0;

    ctx->getList().push_back(gencontextelement(
            needscall
                ? "Please call the " + cmdname + " function."
                : "Please answer in plaintext, without calling any functions."
        ));
    auto ctxlen = ctx->getList().size();

    for (int attempt = 0;; attempt++) {

        int backoff_time = 1;
        std::string response;
        long http_code;

        auto requestbody = json::makeDict();
        requestbody->getDict()["contents"] = ctx;
        requestbody->getDict()["generationConfig"] = genconfig;
        if (k != reply) requestbody->getDict()["tools"] = commandmap[
            (k == branch) ? branch : ak
        ];

        while ((http_code = curl_post_request(requestbody->print(), response)) != 200) {

            std::cerr << "Failed to get API reply: Status code " << http_code << ". "
                    << "Trying again in " << backoff_time << " seconds.\n"
                    << "Did you forget to set the GEMINI_API_KEY environment variable?" << std::endl;

            std::this_thread::sleep_for(std::chrono::seconds(backoff_time));
            backoff_time *= 2;
            if (backoff_time > MAX_API_BACKOFF_TIME) backoff_time = MAX_API_BACKOFF_TIME;

        }

        auto resp = json::loadFromString(response);
        pjson agentrep;
        pjson commandrep;
        std::string reason = "Failed to parse reply.";
        if (needscall) reason += " Make sure you call the necessary function.";
        else reason += " No text content found.";

        while (true) {

            auto& candidates = resp->getDict()["candidates"]->getList();
            
            if (candidates.size() == 0) break;

            auto& c = candidates[0]->getDict();
            if (c.find("content") == c.end()) break;

            agentrep = c["content"];
            auto& content = agentrep->getDict();
            if (content.find("parts") == content.end()) break;

            auto& parts = content["parts"]->getList();
            if (parts.size() == 0) break;

            int i = 0;
            for (; i < parts.size(); i++) {

                auto& p = parts[i]->getDict();
                if (needscall && p.find("functionCall") != p.end()) break;
                if (!needscall && p.find("text") != p.end()) break;
                
            }

            if (i == parts.size()) break;

            auto& p = parts[i]->getDict();

            if (needscall) {

                commandrep = runcommand(
                    proot,
                    curmodule,
                    p["functionCall"],
                    cmdname,
                    k == action ? ak : branch, 
                    dgraph,
                    reason
                );
                break;

            }

            reason = "";
            break;

        }

        if (reason == "") { // no error

            while (ctx->getList().size() > ctxlen) ctx->getList().pop_back(); // trim failed attempts and error messages from context, leaving only the well-formed answer

            ctx->getList().push_back(agentrep); // agent's reply
            if (k == action) ctx->getList().push_back(commandrep); // system response to agent's command
            if (k == branch) return commandrep == yes;
            return true; // dummy, not used if k != branch

        }
        else { // error handling

            ctx->getList().push_back(agentrep ? agentrep : gencontextelement("[No content provided]", false));

            if (attempt >= MAX_REPLY_ATTEMPTS) {
                std::cout << "Agent gave bad reply " << (attempt + 1) << " time(s).\n----------\n" << response << "\n----------\nTalk to agent: " << std::flush;
                std::string userinfo; std::getline(std::cin, userinfo);
                ctx->getList().push_back(gencontextelement(userinfo)); // fallback: user needs to talk to agent and figure out why it's giving bad outputs
            }
            else {
                ctx->getList().push_back(gencontextelement(reason));
            }

        }

    }

    return true;

}