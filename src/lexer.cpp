#include "defs.hpp"
#include "rex.hpp"

struct ptokpat {
    ptok tok; const char* pat;
    constexpr ptokpat(ptok tok, const char* pat) : tok(tok), pat(pat) { }
};

#define sep ),ptokpat(

constexpr ptokpat pats[] = {
    ptokpat(
        getreply, "getreply" sep
        pause_, "pause" sep
        prompt, "prompt" sep
        label, "label" sep
        publiclabel, "\\*label" sep
        goto_, "goto" sep 
        loadctx, "loadctx" sep
        storectx, "storectx" sep
        autoprompt, "autoprompt" sep
        info, "info" sep
        list, rsp"list" sep
        read_, rsp"read" sep
        write_, rsp"write" sep
        append, rsp"append" sep
        listmodules, rsp"querymodules" sep
        createmodule, rsp"createmodule" sep
        customcmd, rsp "<" rident ">" sep
        call, "call" sep
        invoke, "invoke" sep
        recurse, "recurse" sep
        await, "await" sep
        reply, rsp"reply" sep
        action, rsp"action" sep
        branch, rsp"branch" sep
        userbranch, "branch" sep
        identifier, rsp rident ridentterm sep
        firstidentifier, rsp rident "," sep
        secondidentifier, "( |\t)*" rident ridentterm sep
        comment, rcmnt sep
        textblockline, ".*\n" sep
        textblockindent, rsp sep
        textblockcomment, rcmnt sep
        textblocknewline, rnewln sep
        newline, rnewln sep
        eof, "( |\t|\n)*"
    )
};

#undef sep

std::unique_ptr<rex> ptokrexes[_len];
bool ptokrexesinitialized = false;

void initializerexes() {
    for (int i = 0; i < sizeof(pats) / sizeof(ptokpat); i++) {
        ptokpat pat = pats[i];
        ptokrexes[pat.tok] = std::make_unique<rex>(pat.pat);
    }
    ptokrexesinitialized = true;
}

ptok gettokclass(ptok t) {
    if (t < _simplecmd_tok) return _simplecmd_tok;
    if (t < _referenceidentifier_tok) return _referenceidentifier_tok;
    if (t < _referencetextblock_tok) return _referencetextblock_tok;
    if (t <  _actiontype_tok) return _actiontype_tok;
    if (t < _controlflow_tok) return _controlflow_tok;
    return t;
};

bool shouldignoretok(ptok t) {
    return t == comment || t == textblockindent || t == textblockcomment || t == textblocknewline || t == newline || t == epsilon;
}

void genlegalsuccessors(ptok cur, ptok* succs) {

    cur = gettokclass(cur);

    switch(cur) {
        case _referenceidentifier_tok:
            succs[0] = identifier; succs[1] = _none;
            break;
        case _referencetextblock_tok:
            succs[0] = textblockcomment; succs[1] = textblocknewline; succs[2] = _none;
            break;
        case await:
            succs[0] = reply; succs[1] = action; succs[2] = branch; succs[3] = _none;
            break;
        case action:
            succs[0] = list; succs[1] = read_; succs[2] = write_; succs[3] = append; succs[4] = listmodules; succs[5] = createmodule; succs[6] = customcmd; succs[7] = _none;
            break;
        case identifier:
        case secondidentifier:
            succs[0] = epsilon; succs[1] = _none;
            break;
        case firstidentifier:
            succs[0] = secondidentifier; succs[1] = _none;
            break;
        case textblockline:
            succs[0] = textblockindent; succs[1] = textblocknewline; succs[2] = eof; succs[3] = epsilon;
            break;
        case textblockindent:
            succs[0] = textblockline; succs[1] = eof; succs[2] = _none;
            break;
        case textblockcomment:
        case textblocknewline:
            succs[0] = textblockindent; succs[1] = textblocknewline; succs[2] = epsilon;
            break;
        case _controlflow_tok:
        case branch:
        case userbranch:
            succs[0] = firstidentifier; succs[1] = _none;
            break;
        case comment:
        case newline:
        case epsilon:
        case _simplecmd_tok:
        case _actiontype_tok:
        case reply:
            succs[0] = getreply; succs[1] = pause_; succs[2] = prompt; succs[3] = label; succs[4] = publiclabel; succs[5] = goto_; succs[6] = loadctx; succs[7] = storectx; succs[8] = autoprompt; succs[9] = info; succs[10] = call; succs[11] = invoke; succs[12] = recurse, succs[13] = await; succs[14] = userbranch; succs[15] = comment; succs[16] = newline; succs[17] = eof; succs[18] = _none;
    }

}

void lex(std::vector<ptoklex>& lexed, const std::string& filepath, const std::string& codestr) {

    if (!ptokrexesinitialized) initializerexes();

    const char* code = codestr.c_str();
    const char* cstart = code;
    ptok cur = epsilon;
    int line = 1;
    ptok successors[_len];
    
    while (cur != eof) {

        genlegalsuccessors(cur, successors);

        for (int i = 0;; i++) {

            ptok succ = successors[i];
            if (succ == _none) throw std::runtime_error(("Failed to lex " + filepath) + "\nNo valid next tokens found at line " + std::to_string(line));

            if (succ == eof) {
                if (ptokrexes[eof]->match(code)) { cur = succ; break; }
                continue;
            }
            
            int matchlen = (succ == epsilon) ? 0 : ptokrexes[succ]->matchbeg(code);
            if (matchlen < 0) continue;

            cur = succ;
            ptoklex ptl { cur, (int)(code - cstart), matchlen, line };
            if (!shouldignoretok(cur)) lexed.push_back(ptl);
            for (int i = 0; i < matchlen; i++) {
                if (*code == '\n') line++;
                code++;
            }
            break;

        }
    }
}