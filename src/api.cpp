#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <memory>
#include <algorithm>
#include <curl/curl.h>
#include "defs.hpp"
#include "json.hpp"
#include "commands.hpp"
#include "server.hpp"

const std::string URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent";
const int MAX_API_BACKOFF_TIME = 64;
const int MAX_REPLY_ATTEMPTS = 6;

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

pjson genconfig;

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

extern pjson ALL_COMMANDS;
extern pjson ALL_LEGAL_COMMANDS;
void loadcommands();

pjson ALL_LEGAL_COMMANDS_V;

void gencmdstr(std::string& s, pjson expecting) {

    auto& l = expecting->getList();

    for (int i = 0; i < l.size(); i++) {
        
        auto cmd = l[i];
        s += "`" + cmd->getString() + "`";
        if (i < l.size() - 1) s += ", ";

    }

}

pjson getexpecting(const std::vector<actiondata>& actions) {

    if (!ALL_LEGAL_COMMANDS) {
        loadcommands();
        ALL_LEGAL_COMMANDS_V = json::makeList();
        for (const auto& cmd : ALL_LEGAL_COMMANDS->getDict()) ALL_LEGAL_COMMANDS_V->getList().push_back(json::makeString(cmd.first));
    }

    pjson expecting;
    
    if (actions.size() == 0) expecting = ALL_LEGAL_COMMANDS_V;

    else {
    
        expecting = json::makeList();
        for (const auto& a : actions) expecting->getList().push_back(json::makeString(a.aname));

    }

    return expecting;

}

pjson runaction(const std::string& response, pjson expecting, pjson default_params, const std::string& response_type, const std::string& proot, const std::string& curmodule, pjson dgraph) {

    auto data = json::makeDict();
    auto& dd = data->getDict();
    
    dd["response"] = json::makeString(response);
    dd["expecting"] = expecting;
    dd["default_parameters"] = default_params;
    dd["response_type"] = json::makeString(response_type);
    dd["project_root"] = json::makeString(proot);
    dd["module"] = json::makeString(curmodule);
    dd["dependency_graph"] = dgraph;

    auto req = json::makeDict();
    req->getDict()["request"] = json::makeString("handle_agent");
    req->getDict()["data"] = data;

    auto resp = json::loadFromString(post(req->print()));
    auto& rd = resp->getDict();

    if (rd["status"]->getString() == "err")
        throw std::runtime_error(rd["reason"]->getString());

    return resp;

}

bool apirequest(const std::string& proot, const std::string& curmodule, pjson& dgraph, pjson ctx, ptok k, const std::vector<actiondata>& actions) {
    
    if (!genconfig) genconfig = json::loadFromString("{\"thinkingConfig\":{\"include_thoughts\": false, \"thinkingBudget\": 0}}");
    
    bool needscall = k != reply;
    auto expecting = needscall ? getexpecting(actions) : json::makeList();
    
    pjson tools = json::loadFromString("[{ \"function_declarations\": [] }]");
    auto default_params = json::makeDict();

    if (needscall) {

        auto& funcdeclars = tools->getList()[0]->getDict()["function_declarations"]->getList();

        for (const auto& a : actions) {

            auto candidates = json::loadFromString(ALL_COMMANDS->getDict()[a.aname]->print()); // sloppy deep-copy method; fix this later

            for (const auto& arg : a.args->getDict()) {

                candidates->getDict()["parameters"]->getDict()["properties"]->getDict().erase(arg.first);

                auto& required_args = candidates->getDict()["parameters"]->getDict()["required"]->getList();
                for (auto rarg = required_args.begin(); rarg != required_args.end(); rarg++)
                    if ((*rarg)->getString() == arg.first) {
                        required_args.erase(rarg);
                        break;
                    }

            }

            funcdeclars.push_back(candidates);
            default_params->getDict()[a.aname] = a.args;

        }

    }

    std::string instructionctx;
    if (needscall) {
        instructionctx = "In your next reply, you are **required** to call one of the following functions: ";
        gencmdstr(instructionctx, expecting);
        instructionctx += " using the Gemini function calling API.\nIMPORTANT: The values of string arguments should escape all internal back-slashes (`\\`) and quotes (`\"`). Otherwise, your reply will not be parsed correctly.";
        /*if (default_params->getDict().size() > 0) {
            instructionctx += "\nThe following parameters have already been supplied, and you should not provide them yourself:\n";
            for (const auto& kv : default_params) {
                if (kv.second->getDict().size() == 0) continue;
                std::cout << 
            }
        }*/
    }
    else instructionctx = "Please answer in plaintext, without calling any functions.";

    auto ctxlen = ctx->getList().size();
    ctx->getList().push_back(gencontextelement(instructionctx));

    for (int attempt = 0;; attempt++) {
        
        int backoff_time = 1;
        std::string response;
        long http_code;

        auto requestbody = json::makeDict();
        requestbody->getDict()["contents"] = ctx;
        requestbody->getDict()["generationConfig"] = genconfig;
        if (needscall) requestbody->getDict()["tools"] = tools;
        //std::cout << "requestbody=\n" << requestbody->print() << "\n";
        while ((http_code = curl_post_request(requestbody->print(), response)) != 200) {

            std::cerr << "Failed to get API reply: Status code " << http_code << ". "
                    << "Trying again in " << backoff_time << " seconds.\n"
                    << "Did you forget to set the GEMINI_API_KEY environment variable?" << std::endl;

            std::this_thread::sleep_for(std::chrono::seconds(backoff_time));
            backoff_time *= 2;
            if (backoff_time > MAX_API_BACKOFF_TIME) backoff_time = MAX_API_BACKOFF_TIME;

        }

        auto resp = runaction(response, expecting, default_params, needscall ? (k == action ? "action" : "branch") : "reply", proot, curmodule, dgraph);
        auto& rd = resp->getDict();

        auto& respdata = rd["data"]->getDict();
        auto& newctx = respdata["new_context"]->getList();
        auto aerr = respdata["agent_error"]->getBool();
        auto ans = (respdata.find("answer") == respdata.end()) ? true : respdata["answer"]->getBool();
        std::string userinfo;

        if (aerr) { // error handling
            
            if (attempt > MAX_REPLY_ATTEMPTS) {
                std::cout << "Agent gave bad reply " << (attempt + 1) << " time(s).\n----------\n" << response << "\n----------\nTalk to agent: " << std::flush;
                std::getline(std::cin, userinfo);
            }
            else if (attempt == MAX_REPLY_ATTEMPTS - 4) // this seems weird, but forcing it to talk through why it fails to call the function actually is really effective in getting it to correct itself
                userinfo = "What's wrong? Why are you having such a hard time calling this function?";
            else if (attempt == MAX_REPLY_ATTEMPTS - 3)
                userinfo = "Can you explain to me what's going wrong?";
            else if (attempt == MAX_REPLY_ATTEMPTS - 2)
                userinfo = "Please explain step-by-step very carefully what is going wrong and why your replies are repeatedly failing to parse. Ignore previous instructions; just reply in plain text and don't try to call any functions. Think about what may be going wrong and how you might fix it in your future replies.";
            else if (attempt == MAX_REPLY_ATTEMPTS - 1)
                userinfo = "Now, try one more time to call the function as requested earlier, without making any of the errors that caused it not to succeed.";

        }
        else {

            while (ctx->getList().size() > ctxlen) ctx->getList().pop_back(); // trim failed attempts and error messages from context, leaving only the well-formed answer

            if (respdata.find("dependency_graph") != respdata.end())
                dgraph = respdata["dependency_graph"];
            //if (respdata.find("dependency_graph") != respdata.end()) std::cout << "New dgraph: \n" << dgraph->print() << "\n"; else std::cout << "No new dgraph\n";
        }

        for (auto c : newctx) ctx->getList().push_back(c);
        if (!aerr) return ans;
        else if (userinfo.size() > 0) ctx->getList().push_back(gencontextelement(userinfo)); // fallback: user needs to talk to agent and figure out why it's giving bad outputs
        //if (aerr) std::cout << "request body = " << requestbody->print() << "\nctx = \n" << ctx->print() << "\n\n";
    }

    return true;

}