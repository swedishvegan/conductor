#ifndef _defs_inc
#define _defs_inc

#include <map>
#include <set>
#include <vector>
#include <string>
#include <exception>
#include <memory>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "json.hpp"

// filepath defs

#define hll_projects_folder "~/.local/share/hll/"
#define hll_subdir "/hll/"
#define hll_metadata_subdir "/.hll/"

// lexer defs

#define rspc "( |\t)+"
#define rnewln "( |\t)*\n"
#define rcmnt "( |\t)*#.*\n"
#define rnewlncmnt "( |\t)*(#.*)?\n"
#define rident "(a-z|A-Z|_)(a-z|A-Z|0-9|_)*"
#define ridentterm "( |\t|\n|(#.*\n))"

enum ptok : uint8_t { 
    getreply, pause_, prompt, _simplecmd_tok,
    label, publiclabel, goto_, loadctx, storectx, _referenceidentifier_tok,
    autoprompt, info, _referencetextblock_tok,
    call, invoke, recurse, _controlflow_tok,
    await,
    reply,
    action,
    branch,
    useraction,
    userbranch,
    identifier,
    firstidentifier,
    secondidentifier,
    comment,
    textblockline,
    textblockindent,
    textblockcomment,
    textblocknewline,
    actionidentifier,
    actionidentifierwithargs,
    finalactionidentifier,
    actionargnewline,
    actionargname,
    actionargcontent,
    actioncomma,
    actionspace,
    newline,
    eof,
    epsilon,
    _len,
    _none
};

struct ptoklex {
    ptok tok;
    int start;
    int len;
    int line = 0;
};

// parser defs

template <typename T>
struct nameregistry_T { // symbol register and search; written by chatgpt

    std::map<std::string, int> map;
    std::map<int, std::string> reversemap;
    int nextid = 0;

    int registername(const std::string& s) {
        if (map.count(s)) return -1;
        int id = nextid++;
        map[s] = id;
        reversemap[id] = s;
        return id;
    }

    int query(const std::string& s) const {
        auto it = map.find(s);
        return (it != map.end()) ? it->second : -1;
    }

    std::string queryname (int id) {
        auto it = reversemap.find(id);
        return (it != reversemap.end()) ? it->second : "";
    }

};
using nameregistry = nameregistry_T<int>;

struct inst { // base instruction class
    ptok tok;
    inst(ptok tok) : tok(tok) {}
    virtual ~inst() = default;
};

struct dialogue { // master container for a parsed HLL dialogue
    static nameregistry contextnames;
    static nameregistry agentnames;
    nameregistry labelnames;
    std::vector<ptoklex> tokens;
    std::vector<std::shared_ptr<inst>> instructions;
    std::set<int> entrypoints; // public label ids
    std::map<int, int> jumptable; // maps lid -> instruction index
    std::string code; // raw code
};
using dialogues = std::map<int, dialogue>;

struct actiondata { // represents an action call with default arguments encoded in JSON
    std::string aname;
    pjson args;
};

// instruction specialties that need specific data attached to them

struct inst_label : public inst {
    int lid;
    bool ispublic;
    inst_label(int lid, bool ispublic) : inst(label), lid(lid), ispublic(ispublic) {}
};

struct inst_goto : public inst {
    int lid;
    inst_goto(int lid) : inst(goto_), lid(lid) {}
};

struct inst_loadctx : public inst {
    int cid;
    inst_loadctx(int cid) : inst(loadctx), cid(cid) {}  
};

struct inst_storectx : public inst {
    int cid;
    inst_storectx(int cid) : inst(storectx), cid(cid) {}
};

struct inst_textblock : public inst {
    std::string text;
    inst_textblock(ptok tok, std::string text) : inst(tok), text(text) {}
};

struct inst_ctrlflow : public inst {
    int aid, lid;
    inst_ctrlflow(ptok tok, int aid, int lid) : inst(tok), aid(aid), lid(lid) {}
};

struct inst_await : public inst {
    ptok k;
    inst_await(ptok k) : inst(await), k(k) {}
};

struct inst_awaitreply : public inst_await {
    inst_awaitreply() : inst_await(reply) {}
};

struct inst_awaitaction : public inst_await {
    std::vector<actiondata> actions;
    inst_awaitaction() : inst_await(action) {}
};

struct inst_awaitbranch : public inst_await {
    int lidyes, lidno;
    inst_awaitbranch(int lidyes, int lidno) : inst_await(branch), lidyes(lidyes), lidno(lidno) {}
};

struct inst_action : public inst {
    std::vector<actiondata> actions;
    inst_action() : inst(useraction) {}
};

struct inst_branch : public inst {
    int lidyes, lidno;
    inst_branch(int lidyes, int lidno) : inst(userbranch), lidyes(lidyes), lidno(lidno) {}
};

#endif
