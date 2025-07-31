#include <iostream>
#include <csignal>
#include <dirent.h>
#include <unistd.h>
#include "defs.hpp"
#include "rex.hpp"
#include "json.hpp"

extern bool apirequest(const std::string& proot, const std::string& curmodule, pjson dgraph, pjson ctx, ptok k, const std::vector<actiondata> actions);
extern pjson gencontextelement(const std::string& text, bool isuser = true);
extern pjson gendefaultcontext(const std::string& module);

struct interpreter {

    const std::string& proot;
    std::string curmodule;
    pjson instance;
    std::vector<pjson>& stack;
    dialogues& d;
    pjson dgraph;
    pjson ctx;
    int aid;
    int curinst;
    
    // these helpers make atomic saves more convenient
    std::vector<pjson> pendingframes;
    std::string pendingctxname;

    // for matching context files
    rex rctx;
    rex rnum;

    interpreter(
        const std::string& proot,
        pjson instance,
        dialogues& d,
        pjson dgraph
    ) : proot(proot), instance(instance), stack(instance->getList()), d(d), dgraph(dgraph), rctx("ctx0-9+(" rident ")?\\.json"), rnum("0-9+") { // must never be constructed when the instance stack is empty; this is enforced by the driver

        loadagent();
        loadinstruction();
        loadmodulename();
        loadcontext();

    }

    bool step() {

        auto oldsize = stack.size();

        while (curinst >= d[aid].instructions.size()) { // pop current frame until an active one is found

            stack.pop_back();
            if (stack.empty()) {
                save(true); // flushes out all old context files
                return false;
            }

            loadagent();
            loadinstruction();
            loadmodulename();
            if (!stack.back()->getDict()["called"]->getBool()) loadcontext(); // if the current frame called rather than invoked the just-popped frame, it inherits its child's context window; otherwise it loads the one that previously existed

        }
        if (oldsize != stack.size()) save(true);

        auto& curframe = stack.back()->getDict();
        auto in = d[aid].instructions[curinst];
        bool shouldsave = false;
        int calltype = 0; // 0 -> no call; 1 -> invoke/recurse; 2 -> call

        pendingframes.clear();
        pendingctxname = "";
        
        switch (in->tok) {

            case goto_: {

                auto x = std::dynamic_pointer_cast<inst_goto>(in);
                curinst = d[aid].jumptable[x->lid] - 1; // -1 accounts for the curinst++ that happens later
                break;

            }
            case loadctx: {

                auto x = std::dynamic_pointer_cast<inst_loadctx>(in);
                loadcontext(dialogue::contextnames.queryname(x->cid));
                break;

            }
            case storectx: {

                auto x = std::dynamic_pointer_cast<inst_storectx>(in);
                pendingctxname = dialogue::contextnames.queryname(x->cid);
                shouldsave = true;
                break;

            }
            case info: {

                auto x = std::dynamic_pointer_cast<inst_textblock>(in);
                std::cout << x->text;
                break;

            }
            case autoprompt: {

                auto x = std::dynamic_pointer_cast<inst_textblock>(in);
                ctx->getList().push_back(gencontextelement(x->text));
                break;

            }
            case call:
            case invoke: {
                
                auto x = std::dynamic_pointer_cast<inst_ctrlflow>(in);
                curframe["called"]->setBool(in->tok == call); // indicates whether this frame called the next frame, so when the callee returns control to the caller, the caller will know if it should inherit the callee's context window

                pjson newframe = json::makeDict();
                auto istart = d[x->aid].jumptable[x->lid];

                newframe->getDict()["agent"] = json::makeInt(x->aid);
                newframe->getDict()["module"] = json::makeString(curmodule);
                newframe->getDict()["instruction"] = json::makeInt(istart);
                newframe->getDict()["called"] = json::makeBool(false);
                
                pendingframes.push_back(newframe);
                shouldsave = true;
                calltype = (in->tok == invoke) ? 1 : 2;
                break;

            }
            case recurse: {

                auto x = std::dynamic_pointer_cast<inst_ctrlflow>(in);
                auto istart = d[x->aid].jumptable[x->lid];
                curframe["called"]->setBool(false); 

                auto& mychildren = dgraph->getDict()["children"]->getDict()[curmodule]->getList();
                for (int i = mychildren.size() - 1; i >= 0; i--) { // push child frames to stack in reverse order to respect dependency graph

                    pjson newframe = json::makeDict();
                    newframe->getDict()["agent"] = json::makeInt(x->aid);
                    newframe->getDict()["module"] = json::makeString(mychildren[i]->getString());
                    newframe->getDict()["instruction"] = json::makeInt(istart);
                    newframe->getDict()["called"] = json::makeBool(false);

                    pendingframes.push_back(newframe);

                }

                shouldsave = true;
                calltype = 1;
                break;

            }
            case await: {

                auto x = std::dynamic_pointer_cast<inst_await>(in);

                switch (x->k) {

                    case reply: {

                        static std::vector<actiondata> no_actions; // this is so dumb
                        apirequest(proot, curmodule, dgraph, ctx, reply, no_actions);
                        break;

                    }
                    case action: {
                        
                        apirequest(
                            proot,
                            curmodule,
                            dgraph,
                            ctx,
                            action,
                            std::dynamic_pointer_cast<inst_awaitaction>(in)->actions
                        );
                        break;

                    }
                    case branch: {

                        static std::vector<actiondata> answer_action = actiondata { "answer", json::makeDict() };

                        bool option = apirequest(proot, curmodule, dgraph, ctx, branch, answer_action);
                        auto xx = std::dynamic_pointer_cast<inst_awaitbranch>(in);
                        int lidnew = option ? xx->lidyes : xx->lidno; 
                        curinst = d[aid].jumptable[lidnew] - 1; // -1 for curinst++

                    }

                }

                shouldsave = true;
                break;

            }
            case userbranch: {

                bool option;
                while (true) {
                    std::cout << "(Y/n)" << std::flush;
                    std::string optionstr; std::cin >> optionstr;
                    char ch = optionstr[0];
                    if (ch == 'Y' || ch == 'y') { option = true; break; }
                    if (ch == 'N' || ch == 'n') { option = false; break; }
                }
                auto x = std::dynamic_pointer_cast<inst_branch>(in);
                int lidnew = option ? x->lidyes : x->lidno;
                curinst = d[aid].jumptable[lidnew] - 1; // -1 for curinst++

                shouldsave = true;
                break;

            }
            case getreply: {

                std::string rep = "'getreply' failed; no agent reply found in context";
                
                auto& c = ctx->getList();
                for (int i = c.size() - 1; i >= 0; i--) {
                    auto& d = c[i]->getDict();
                    if (d["role"]->getString() == "model") {
                        auto& parts = d["parts"]->getList();
                        if (parts.size() > 1) { rep = d["parts"]->print(); break; }
                        auto& p = parts[0]->getDict();
                        if (p.find("text") != p.end()) rep = p["text"]->getString();
                        else if (p.find("functionCall") != p.end()) rep = parts[0]->print();
                        break;
                    }
                }

                std::cout << rep;
                if (rep[rep.size() - 1] != '\n') std::cout << "\n";
                std::cout << std::flush;
                break;

            }
            case pause_: {
                std::cout << "[ enter anything to resume ]" << std::flush;
                std::string x;
                std::getline(std::cin, x);
                break;
            }
            case prompt: {
                std::cout << ">>> " << std::flush;
                std::string x; std::getline(std::cin, x);
                ctx->getList().push_back(gencontextelement(x));

                shouldsave = true;
                break;
            }
        
        }

        curinst++;

        if (shouldsave) {
            stack.back()->getDict()["instruction"]->setInt(curinst);
            save(false);
        }

        if (calltype) {
            loadagent();
            loadinstruction();
            loadmodulename();
            if (calltype == 1) loadcontext(); // invoke/recurse get a fresh context; calls inherit the context of caller
        }

        return true;
        
    }

    void save(bool prunecontexts) {

        sigset_t newmask, oldmask;
        sigemptyset(&newmask);
        sigaddset(&newmask, SIGINT);
        sigprocmask(SIG_BLOCK, &newmask, &oldmask);

        std::string subdir = proot + hll_metadata_subdir;

        if (prunecontexts) { // chatgpt (with some changes)
            DIR* dir = opendir(subdir.c_str());
            if (dir) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    std::string fname(entry->d_name);
                    if (rctx.first(fname.c_str())) {
                        rnum.first(fname.c_str() + rctx.pos);
                        int ctxnum = std::stoi(std::string(fname.c_str() + rctx.pos + rnum.pos, rnum.len));
                        if (ctxnum > static_cast<int>(stack.size())) {
                            std::remove((subdir + fname).c_str());
                        }
                    }
                }
                closedir(dir);
            }
        }
        
        int oldstacksize = stack.size();

        for (auto newframe : pendingframes) stack.push_back(newframe);
        if (pendingctxname.size() > 0 && stack.size() > 0) ctx->save(getcontextfilename(pendingctxname));

        instance->save(subdir + "instance.json");
        dgraph->save(subdir + "dependency_graph.json"); 
        if (stack.size() > 0) ctx->save(getcontextfilename("", oldstacksize));

        pendingframes.clear();
        pendingctxname = "";

        sigprocmask(SIG_SETMASK, &oldmask, nullptr);

    }

    void loadagent() { aid = stack.back()->getDict()["agent"]->getInt(); }
    void loadinstruction() { curinst = stack.back()->getDict()["instruction"]->getInt(); }
    void loadmodulename() { curmodule = stack.back()->getDict()["module"]->getString(); }

    void loadcontext(std::string varname = "") {

        try { ctx = json::loadFromFile(getcontextfilename(varname)); }
        catch (...) { ctx = gendefaultcontext(curmodule); }

    }

    std::string getcontextfilename(std::string varname = "", int stacksize = -1) { 
        
        if (stacksize < 0) stacksize = stack.size();
        std::string fname = proot + hll_metadata_subdir + "ctx";
        if (varname.size() == 0) fname += std::to_string(stacksize);
        else fname += varname + "-" + curmodule;
        fname += ".json";
        return fname;
        
    }

};

extern void dispatch(dialogues& d, pjson instance, pjson dgraph, const std::string& proot) {

    interpreter i(proot, instance, d, dgraph);
    while (i.step());

}