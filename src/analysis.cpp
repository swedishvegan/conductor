#include "defs.hpp"

/*
performs turn analysis on parsed dialogues

a turn is defined like so:

 - ptok::prompt and ptok::autoprompt are user-turn
 - ptok::reply, ptok::action, and ptok::branch are agent-turn
 - all other instructions are no-turn

static analysis verifies four properties across every possible control flow path:
 1. there are no two agent-turn instructions without an intermediate user-turn instruction
 2. the first non-no-turn instruction after every public label is user-turn
 3. the last non-no-turn instruction before termination is agent-turn
 4. the first non-no-turn instruction preceding every ptok::getreply instruction exists and is agent-turn
*/

int getturn(ptok tok) {

    switch (tok) {
        case prompt:
        case autoprompt:
            return 1; // 1 = user-turn
        case await:
            return 0; // 0 = agent-turn
    }

    return -1; // no-turn

}

std::string analysisfailed(const std::string& aname, const char* what, const std::string& label) {

    return (
        "Static analysis on " +
        aname + 
        " failed\n" +
        what + 
        " at label '" +
        label +
        "'"
    );

}
#include <iostream>
void traverse(dialogue& dial, const std::string& aname, std::set<std::pair<int, int>>& visited, int turn, int lid) {

    std::pair<int, int> node{ turn, lid };
    if (visited.find(node) != visited.end()) return;
    visited.insert(node);

    int idx = dial.jumptable[lid];
    for (; idx < dial.instructions.size(); idx++) {
        
        /*
        turn == 1 -> expecting agent-turn (last turn was user)
        turn == 0 -> expecting user-turn (last turn was agent)
        turn == -1 -> all turns up to now were no-turn
        */

        ptok tok = dial.instructions[idx]->tok;

        if (tok == getreply && turn != 0)
            throw std::runtime_error(
                analysisfailed(
                    aname,
                    "Attempt to use getreply outside of agent turn",
                    dial.labelnames.queryname(lid)
                )
            );

        int next = getturn(tok);

        if (turn == -1) {
            if (next == 0)
                throw std::runtime_error(
                    analysisfailed(
                        aname,
                        "Control flow begins on agent turn",
                        dial.labelnames.queryname(lid)
                    )
                );
            turn = next;
        }
        else {
            if (turn == 0 && next == 0)
                throw std::runtime_error(
                    analysisfailed(
                        aname, 
                        "Two adjacent agent turns", 
                        dial.labelnames.queryname(lid)
                    )
                );
            if (next >= 0) turn = next;
        }

        if (
            tok == label || 
            tok == publiclabel ||
            tok == goto_ || 
            tok == userbranch ||
            (
                tok == await &&
                std::dynamic_pointer_cast<inst_await>(dial.instructions[idx])->k == branch
            )
        ) break;

    }

    if (idx == dial.instructions.size()) {

        if (turn == 1) throw std::runtime_error(
            analysisfailed(
                aname,
                "Control flow ends on user turn",
                dial.labelnames.queryname(lid)
            )
        );
        return;

    }

    int idlh, idrh = -1;

    switch (dial.instructions[idx]->tok) {
        case label:
        case publiclabel: {
            idlh = std::dynamic_pointer_cast<inst_label>(dial.instructions[idx])->lid;
            break;
        }
        case goto_: {
            idlh = std::dynamic_pointer_cast<inst_goto>(dial.instructions[idx])->lid;
            break;
        }
        case userbranch: {
            auto instr = std::dynamic_pointer_cast<inst_branch>(dial.instructions[idx]);
            idlh = instr->lidyes;
            idrh = instr->lidno;
            break;
        }
        case await: {
            auto instr = std::dynamic_pointer_cast<inst_awaitbranch>(dial.instructions[idx]);
            idlh = instr->lidyes;
            idrh = instr->lidno;
        }
    }

    traverse(dial, aname, visited, turn, idlh);
    if (idrh >= 0) traverse(dial, aname, visited, turn, idrh);

}

void analyze(dialogue& dial, const std::string& aname) {

    std::set<std::pair<int, int>> visited;
    for (int lid : dial.entrypoints)
        traverse(dial, aname, visited, -1, lid);

}