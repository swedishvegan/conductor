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
        call, "call" sep
        invoke, "invoke" sep
        recurse, "recurse" sep
        await, "await" sep
        reply, rspc "reply" sep
        action, rspc "action" sep
        branch, rspc "branch" sep
        useraction, "action" sep
        userbranch, "branch" sep
        identifier, rspc rident ridentterm sep
        firstidentifier, rspc rident "," sep
        secondidentifier, "( |\t)*" rident ridentterm sep
        comment, rcmnt sep
        textblockline, ".*\n" sep
        textblockindent, rspc sep
        textblockcomment, rcmnt sep
        textblocknewline, rnewln sep
        actionidentifier, "( |\t)*" rident "," sep 
        actionidentifierwithargs, "( |\t)*" rident ":" rnewlncmnt sep
        finalactionidentifier, "( |\t)*" rident ridentterm sep
        actionargnewline, rnewlncmnt sep
        actionargname, rspc rident rspc "=" sep
        actionargcontent, ".*\n" sep
        actioncomma, "," sep
        actionspace, rspc sep
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
    if (t < _controlflow_tok) return _controlflow_tok;
    return t;
};

bool shouldignoretok(ptok t) {
    return t == comment || t == textblockindent || t == textblockcomment || t == textblocknewline || t == actionargnewline || t == actioncomma || t == actionspace || t == newline || t == epsilon;
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
        case useraction:
            succs[0] = actionspace; succs[1] = _none;
            break;
        case identifier:
        case secondidentifier:
            succs[0] = epsilon;
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
        case textblockcomment: // note that textblockcomment is never a successor of any textblock token. **comments within textblocks are treated as part of the textblock content itself, NOT as comments.** this is intentional -- sometimes the user may write markdown in a textblock which uses `#` and this should NOT be treated as a comment. textblockcomment just means a comment is allowed on the *same line* as the keyword `autoprompt` or `info`.
        case textblocknewline:
            succs[0] = textblockindent; succs[1] = textblocknewline; succs[2] = epsilon;
            break;
        case actionidentifier:
        case actioncomma:
        case actionspace:
            succs[0] = actionidentifier; succs[1] = actionidentifierwithargs; succs[2] = finalactionidentifier; succs[3] = epsilon;
            break;
        case actionargname:
            succs[0] = actionargcontent; succs[1] = _none;
            break;
        case actionidentifierwithargs:
        case actionargnewline:
        case actionargcontent: // comments are also ignored on the same line as actionargcontent, which is later parsed as a raw JSON string. this is intentional also -- what if the JSON contains the # character? you want this to be parsed as part of the JSON content, not as a comment
            succs[0] = actionargname; succs[1] = actionargnewline; succs[2] = actioncomma; succs[3] = epsilon;
            break;
        case finalactionidentifier:
            succs[0] = epsilon;
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
        case reply:
            succs[0] = getreply; succs[1] = pause_; succs[2] = prompt; succs[3] = label; succs[4] = publiclabel; succs[5] = goto_; succs[6] = loadctx; succs[7] = storectx; succs[8] = autoprompt; succs[9] = info; succs[10] = call; succs[11] = invoke; succs[12] = recurse, succs[13] = await; succs[14] = useraction; succs[15] = userbranch; succs[16] = comment; succs[17] = newline; succs[18] = eof; succs[19] = _none;
    }

}
#include <iostream>
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

            ptok succ = successors[i]; std::cout << "\tTrying succ: " << (int)succ << "\n";
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
            } std::cout << "Matched token " << (int)cur << "/" << (int)_len << " at line " << ptl.line << "\n";
            break;

        }
    }
}