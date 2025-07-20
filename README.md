# HLL

**These docs were AI-generated using HLL.** To see a working example of HLL code and how I used HLL to do this, see Section 4.

# 1. Language Specification

Welcome to the Humanity's Last Language (HLL) User's Manual. This section will introduce you to the core syntax and semantics of HLL, providing a comprehensive guide to writing your own agentic programs.

## 1.1 Core Concepts

HLL is an agentic programming language designed to facilitate complex interactions between multiple AI agents and human users, all operating within a structured, virtual filesystem.

### 1.1.1 Agents and Dialogues

In HLL, a program is structured as one or more "dialogues," each carried out by an "agent." Each `.hll` file you write defines a single agent's dialogue. The filename (without the `.hll` extension) serves as the agent's name.

### 1.1.2 Turns

HLL dialogues are turn-based, ensuring a structured flow of conversation between the agent and the user. There are three types of turns:

*   **User-turn:** Initiated by the user (e.g., providing input). Instructions like `prompt` and `autoprompt` initiate a user-turn.
*   **Agent-turn:** Initiated by the agent (e.g., providing a reply or executing an action). Instructions like `reply`, `action`, and `branch` (when initiated by the agent) are agent-turns.
*   **No-turn:** Instructions that do not involve a direct conversational exchange (e.g., `info`, `goto`).

HLL enforces strict turn-taking rules through static analysis to ensure stable dialogue flow. These rules are:

1.  There must be no two consecutive agent-turn instructions without an intermediate user-turn instruction.
2.  The first non-no-turn instruction after every public label must be a user-turn.
3.  The last non-no-turn instruction before termination must be an agent-turn.
4.  The first non-no-turn instruction preceding every `getreply` instruction must exist and be an agent-turn.

Violations of these rules will result in parsing errors.

### 1.1.3 Labels and Control Flow

Labels are fundamental for defining specific points within a dialogue and controlling program flow.

*   `label <name>`: Defines a private label. Private labels can only be referenced from within the same agent's dialogue.
*   `*label <name>`: Defines a public label. Public labels serve as entry points for other agents to `call`, `invoke`, or `recurse` on this dialogue, or for the HLL runtime to start execution.

The `goto` instruction allows for unconditional jumps within the current agent's dialogue:

*   `goto <label_name>`: Transfers control to the instruction immediately following the specified label.

### 1.1.4 Context

Agents operate within a "context window," which is essentially the conversational history with the Large Language Model (LLM). This context is critical for the LLM to understand the ongoing dialogue and generate coherent responses.

*   `loadctx <context_name>`: Loads a previously saved context into the current agent's context window. 
*   `storectx <context_name>`: Saves the current context window under a specified name.

Explicitly loading/saving context can be useful in certain situations where you want to strategically prune the context window to only include certain content. **You do not need to explicitly save contexts.** This is just a convenience feature; even if you don't save them manually, the runtime still snapshots your context to keep it up-to-date.

## 1.2 Instruction Set: Syntax and Semantics

This section details the various instructions available in HLL, explaining their syntax and behavior.

### 1.2.1 Simple Commands

These instructions perform basic actions within the dialogue.

*   `getreply`
    Retrieves the latest agent's reply or the output of the most recent command execution from the context and prints it to the console. This instruction is a no-turn.
    ```hll
    autoprompt
        Please list the files in the current module.
    await action list
    getreply # Displays the output of the 'list' action
    ```

*   `pause`
    Halts the execution of the HLL program until the user presses Enter. This is a no-turn instruction.
    ```hll
    info
        This is a message to the user.
    pause # Program will wait here for user input
    ```

*   `prompt`
    Displays ">>> " to the user, waits for their input, and then adds the input to the agent's context window. This initiates a user-turn.
    ```hll
    prompt # User can type a message here
    await reply
    getreply
    ```

### 1.2.2 Control Flow and Interaction

These instructions manage the flow of the dialogue and handle interactions with external systems (the LLM) and human users.

*   `label <name>`
    Defines a private label.
    ```hll
    label my_private_label
    info
        This is a private section.
    ```

*   `*label <name>`
    Defines a public label, marking it as an entry point for other agents or the runtime to initiate a dialogue from.
    ```hll
    *label start_dialogue
    autoprompt
        Hello!
    ```

*   `goto <label_name>`
    Unconditionally transfers control to the specified label within the current agent's dialogue.
    ```hll
    goto end_dialogue
    info
        This will not be executed.
    label end_dialogue
    info
        Dialogue finished.
    ```

*   `loadctx <context_name>`
    Loads the named context into the agent's context window. If the context does not exist, a new, empty context is used.
    ```hll
    loadctx previous_conversation
    autoprompt
        Continuing our last discussion.
    ```

*   `storectx <context_name>`
    Saves the current agent's context window under the specified name.
    ```hll
    storectx current_state
    info
        Context saved for later.
    ```

*   `autoprompt <textblock>`
    Sends an automated prompt (a multi-line text block) to the agent. This initiates a user-turn, as the agent perceives this as user input.
    ```hll
    autoprompt
        This is an automated message.
        Please acknowledge.
    await reply
    ```

*   `info <textblock>`
    Displays the given text block directly to the human user without affecting the agent's context. This is a no-turn instruction.
    ```hll
    info
        Only the human user will see this.
        The AI agent's context is unaffected.
    ```

*   `call <agent_name>, <label_name>`
    Invokes a dialogue within the current module. The calling agent's context is **inherited** by the called agent, and after the called agent is done, the caller inherits the callee's context. When the called agent's dialogue completes, control returns to the instruction after `call`. Think of a call as being roughly analogous to an inline function. This is a no-turn instruction.
    ```hll
    call my_other_agent, process_data
    info
        Returned from other agent.
    ```

*   `invoke <agent_name>, <label_name>`
    Invokes a dialogue within the current module. The invoked agent receives a **fresh context**; the caller's context is not inherited. When the invoked agent's dialogue completes, control returns to the instruction after `invoke`. This is a no-turn instruction.
    ```hll
    invoke data_logger, start_logging
    info
        Logging started in a separate context.
    ```

*   `recurse <agent_name>, <label_name>`
    Invokes all direct child modules of the current module. If the current module has no children, this is a no-op. The invoked agent receives a **fresh context** like in `invoke`. This is a no-turn instruction.
    ```hll
    recurse worker, start # This will run the `worker` dialogue of all child modules
    ```

*   `await <type>`
    Pauses execution and waits for a specific type of response from the agent.

    *   `await reply`
        Waits for the agent to generate a free-form text reply. This is an agent-turn.
        ```hll
        autoprompt
            What is your favorite color?
        await reply # Waits for the agent to answer
        getreply # Prints out the agent's reply
        ```

    *   `await action <action_type>`
        Waits for the agent to call a specific built-in action. This is an agent-turn. The `action_type` can be one of the following:
        *   `list`: The agent should call the `LIST` function.
        *   `read`: The agent should call the `READ` function.
        *   `write`: The agent should call the `WRITE` function.
        *   `append`: The agent should call the `APPEND` function.
        *   `querymodules`: The agent should call the `LIST_MODULES` function.
        *   `createmodule`: The agent should call the `CREATE_MODULE` function.
        ```hll
        autoprompt
            Please list the files in the current module.
        await action list # Waits for the agent to call the LIST function
        getreply
        ```

    *   `await branch <label_yes>, <label_no>`
        Waits for the agent to provide a YES or NO answer (by calling the `ANSWER` function). Execution branches to `label_yes` if the answer is "YES", and to `label_no` if the answer is "NO". This is an agent-turn.
        ```hll
        autoprompt
            Is this code complete?
        await branch code_complete, code_needs_work
        label code_complete
        info
            Agent says code is complete.
        goto end
        label code_needs_work
        info
            Agent says code needs more work.
        label end
        ```

*   `branch <label_yes>, <label_no>`
    Prompts the human user with "(Y/n)" and waits for their input. Execution branches to `label_yes` if the user enters 'Y' or 'y', and to `label_no` if the user enters 'N' or 'n'. This is a no-turn.
    ```hll
    branch continue_task, cancel_task
    label continue_task
    info
        User chose to continue.
    goto next_step
    label cancel_task
    info
        User chose to cancel.
    label next_step
    ```

### 1.2.3 Text Blocks

Text blocks are used for multi-line content within `autoprompt` and `info` instructions. They are defined by subsequent indented lines following the instruction.

```hll
autoprompt
    This is the first line of the autoprompt.
    This is the second line,
    and this is the third.
    
    Empty lines are ignored.
    This line is part of the text block.
```

### 1.2.4 Comments

Single-line comments in HLL begin with a hash symbol `#`. Any text after `#` on the same line is ignored by the lexer, **except** inside a text block, where they are treated as part of the text.

```hll
# This is a single-line comment.
autoprompt # This comment explains the instruction
    Hello. # This is part of the text block and is NOT a comment
```

## 1.3 Code Structure and Best Practices

Following these guidelines will help you write clear, maintainable, and statically sound HLL dialogues.

*   **File Naming:** Each HLL dialogue should reside in a `.hll` file. The filename becomes the agent's identifier. For instance, `my_agent.hll` defines an agent named `my_agent`.
*   **Public vs. Private Labels:** Use public labels (`*label`) sparingly, only for intended entry points where other agents or the runtime should begin execution. Use private labels (`label`) for internal control flow within a single agent's dialogue.
*   **Static Analysis Compliance:** Always keep the four static analysis rules (see Section 1.1.2) in mind to prevent runtime errors related to turn-taking. For example, avoid having two `await` instructions back-to-back, as this would result in two agent-turns without an intervening user-turn.
*   **Readability:** Employ meaningful label names, clear text blocks, and consistent indentation. Use comments to explain complex logic or non-obvious design choices.
*   **Modularity:** For larger projects, consider breaking down functionality into multiple agents and leveraging `call` and `invoke` to manage inter-agent communication.

This concludes the Language Specification section of the HLL User's Manual. You should now have a foundational understanding of HLL's syntax and how to structure basic agentic programs. The next section will delve into the HLL Runtime itself, explaining how to interact with the HLL binary and manage your projects.

# 2. HLL Runtime Specification

This document details how to use the HLL (Humanity's Last Language) binary to manage and run your HLL projects.

## 2.1 Installation

The HLL binary is assumed to be compiled and available in your system's PATH. Ensure that the `GEMINI_API_KEY` environment variable is set with your Gemini API key. Without this, the HLL runtime will not be able to interact with the language model.

```bash
export GEMINI_API_KEY="YOUR_API_KEY"
```

## 2.2 Commands

The HLL binary provides a set of commands for project management and execution.

### `hll create [pname] [root] [-I include1 -I include2 ...]`

This command creates a new HLL project.

*   `[pname]`: The name of your new HLL project. This must be unique and not clash with existing project names.
*   `[root]`: The path to the root directory where your HLL project files will be stored. This directory will contain all the project's associated metadata and module files.
*   `[-I include1 -I include2 ...]`: (Optional) One or more include directories. These directories should contain `.hll` dialogue files that you wish to include in your project. All `.hll` files found in these directories will be parsed and made available to your HLL project. Other file types in `[root]` will also be copied into your project directory for the agent to access.

**Example:**
```bash
hll create my_project ~/hll_projects -I ~/my_hll_dialogues -I ~/shared_hll_libs
```

The HLL runtime creates a sandboxed subdirectory of the root at `[root]/hll/[pname]`, where it copies all project files and `.hll` dialogues. This allows that any later modifications to the original files won't corrupt the integrity of the HLL project.

### `hll run [pname] [agent] [label (optional)]`

This command initiates a new run of an HLL project.

*   `[pname]`: The name of the HLL project to run. This project must have been previously created using the `create` command.
*   `[agent]`: The name of the agent (dialogue) to start execution from. This corresponds to the filename (without the `.hll` extension) of an HLL dialogue file within your project's include paths.
*   `[label (optional)]`: An optional public label within the specified agent's dialogue to begin execution. If omitted, and the agent has only one public label, that label will be used as the starting point. If there are multiple public labels and no label is specified, an error will occur.

**Example:**
```bash
hll run my_project start_agent initial_prompt
```

### `hll resume [pname]`

This command resumes an active HLL project from its last saved state.

*   `[pname]`: The name of the HLL project to resume.

**Example:**
```bash
hll resume my_project
```

### `hll query`

This command lists all existing HLL projects and their current status (active/inactive). An "active" project means there is a running or paused instance of the dialogue that has not terminated.

**Example:**
```bash
hll query
```

### `hll delete [pname]`

This command deletes an HLL project. This operation is irreversible and will remove all project files and associated data.

*   `[pname]`: The name of the HLL project to delete.

**Example:**
```bash
hll delete my_project
```

## 2.3 Interactive Prompts

During the execution of an HLL dialogue, you may encounter interactive prompts from the agent.

*   **`>>>`**: This indicates that the agent is expecting user input (e.g., in response to a `prompt` instruction). Type your response and press Enter.
*   **`(Y/n)`**: This indicates a binary choice (Yes/No) from the agent (e.g., from a `userbranch` instruction). Type `Y` or `y` for Yes, or `N` or `n` for No, and press Enter.
*   **`[ enter anything to resume ]`**: This is a `pause` instruction, indicating the agent is waiting for you to signal continuation. Press Enter (or type anything and press Enter) to proceed.

## 2.4 Error Handling

The HLL runtime will output error messages to `stderr` if issues occur, such as invalid command arguments, missing environment variables, or problems with file operations. Pay attention to these messages for debugging. In case of API failures, the system will attempt to backoff and retry requests. If repeated failures occur, you may be prompted to provide manual input to the agent to help it resolve the issue.

## 2.5 Interrupting Execution

You can gracefully exit an HLL run at any time by pressing `Ctrl+C`. This will terminate the current execution.

# 3. The Virtual Module-Based Filesystem

Understanding HLL's internal architecture, particularly its virtual module-based filesystem, is crucial for effectively designing and managing agentic programs. Unlike traditional programming languages that operate directly on your local disk, HLL creates an abstracted, isolated environment for agents to interact with files and other modules. This section will delve into the intricacies of this virtual filesystem (VFS) to provide you with the knowledge needed to write more effective prompts and debug issues.

## 3.1 The Virtual Filesystem (VFS) and Modules

At its core, HLL operates on a **virtual filesystem (VFS)**. This abstraction provides isolation, controlled access, and facilitates the module system.

A **module** in HLL is an isolated container within this VFS. Each module can hold its own set of files and is the environment in which specific agents or sets of dialogues reside and operate. An agent's "view" of the filesystem, and thus its capabilities, is entirely dictated by the module it is currently residing in.

## 3.2 The `global` Module: A Special Case

The `global` module is unique in the HLL ecosystem and stands apart from the regular module dependency graph.

*   **Universal Access:** Every other module in an HLL project has **read and write access** to the `global` module. This makes `global` an ideal place for:
    *   Shared libraries or common code that all modules might need to access.
    *   Configuration files or data that needs to be universally available.
*   **Purpose:** It acts as a common ground, a shared repository for resources that don't fit into the hierarchical or dependent structure of other modules.

## 3.3 Module Hierarchy: Children and Dependencies

Beyond the `global` module, HLL projects are structured using a hierarchy of modules defined by "children" and "dependencies."

### 3.3.1 Children Modules

A **child module** is a module explicitly created by another module (its "parent"). This creates a direct, hierarchical relationship.

*   **Permissions:** The parent module has **read and write access** to its child modules. The child module itself also has full read/write access to its own files.
*   **Purpose:** Child modules are excellent for:
    *   Breaking down large projects into smaller, more manageable sub-projects.
    *   Delegating specific tasks to specialized sub-agents that operate within their own isolated environment.

### 3.3.2 Dependency Modules

A **dependency module** is a module that another module (the "dependent" module) relies on for code or data.

*   **Permissions:** A dependent module has **read-only access** to its dependency modules. It cannot modify files within its dependencies.
*   **Purpose:** This read-only access enforces encapsulation and promotes code reuse without accidental or intentional modification of external components. It's a way to import functionality or data without owning it.
*   **Creation Constraints:** When creating a new module, its dependencies can *only* be selected from:
    1.  The module currently active (`current module`).
    2.  Any child modules of the current module.
    3.  Any modules that the current module itself depends on.
    This strict rule ensures that the module graph remains a Directed Acyclic Graph (DAG), preventing circular dependencies and maintaining a clear flow of control and data.

## 3.4 File Permissions within the VFS: A Summary

To reiterate the crucial permission rules governing file access:

*   **Current Module:** Full **read/write** access to its own files.
*   **Child Modules (of the current module):** Full **read/write** access to their files.
*   **Dependency Modules (of the current module):** **Read-only** access to their files.
*   **`global` Module:** Full **read/write** access from any module.

Understanding these rules is paramount. If an agent attempts to perform an operation (e.g., `WRITE` to a dependency, `READ` a module it has no access to), it will result in a "permission denied" error, or a "file not found" error if the file does not exist within the accessible scope.

## 3.5 Filesystem Interaction Commands and the VFS

The HLL runtime provides a set of commands that agents use to interact with this VFS. Their behavior is directly governed by the VFS and its permission system:

*   **`LIST`**: Lists files within a specified module (or the current module if not specified). The results will only include files the current agent has permission to view.
*   **`READ`**: Retrieves the content of a file. This command will fail if the agent does not have read permissions for the target file's module.
*   **`WRITE` / `APPEND`**: Modifies or creates a file. `WRITE` truncates existing content, while `APPEND` adds to it. These commands will only succeed if the agent has write permissions for the target module (i.e., it's the current module, a child module, or the `global` module).
*   **`LIST_MODULES`**: Provides a comprehensive overview of the module landscape from the perspective of the current module. It categorizes modules into "Current module," "Children of current module," "Dependencies of current module," and "All existing modules."
*   **`CREATE_MODULE`**: Adds a new module to the HLL project's VFS, establishing its relationship (as a child) to the module executing the command and defining its initial dependencies according to the rules outlined above.

## 3.6 The Hidden Metadata Directory (`.hll/`)

Every HLL project has a special, hidden directory named `.hll/` located at its project root (e.g., `~/<project_name>/hll/<project_name>/.hll/`). This directory is critical for the HLL runtime's internal operations and contains vital project metadata:

*   **`dependency_graph.json`**: This file is the authoritative source for the entire module graph, detailing all modules, their contained files, and their child/dependency relationships. It's the blueprint of your project's VFS.
*   **`instance.json`**: This file stores the current execution state of a running HLL instance, including the call stack of active agent frames. It's what allows HLL to resume interrupted operations.
*   **`ctx*.json`**: These are context window snapshots, another part of what allows HLL to be safely interrupted and resumed.
*   **Copied `.hll` Dialogue Files:** The original HLL dialogue files (`.hll` extension) that define your agents' behaviors are copied into this directory from the `--include` paths specified during project creation. The runtime then parses these copies.

**Crucial Warning:** Users should generally **never manually modify** the contents of the `.hll/` directory directly. Doing so can corrupt your HLL project's state, leading to unpredictable behavior or rendering the project unrunnable. These files are for the HLL runtime's internal management.

## 3.7 Implications for Prompt Engineering

Understanding the VFS is paramount for effective prompt engineering in HLL:

*   **Agent's Worldview:** Always remember that an agent's "knowledge" of files and modules is derived *solely* from the `dependency_graph.json` and its current module context. If a file or module isn't listed there or isn't accessible due to permissions, the agent genuinely "doesn't know" about it or "can't access" it.
*   **Specific Instructions:** When asking an agent to interact with the filesystem, be precise with module names and file paths, keeping the permission model in mind.
    *   Instead of "List all files," specify "List files in the `global` module" or "List files in the current module."
    *   When creating or modifying files, ensure the agent is in a module with write permissions for the target location.
*   **Debugging:** If an agent reports "file not found," "permission denied," or behaves unexpectedly when interacting with files, the first place to investigate (after checking your prompt) should be the module's position in the `dependency_graph.json` and the VFS permission rules.
*   **Project Structure:** Design your HLL projects by thoughtfully considering the module hierarchy. Break down tasks into sub-modules, and use dependencies to share common resources while maintaining clear ownership and preventing unintended modifications.

By grasping these internal workings, you can transition from a basic user of HLL to a skilled architect of sophisticated agentic systems. You'll be able to anticipate agent behavior, troubleshoot more effectively, and design truly robust and intelligent multi-agent workflows.

# 4. Real-world HLL Example 

In order to generate the docs above, I wrote a simple HLL workflow. Here is how it works:
1. The `start` agent reads all the files in the project directory, which are the source files for the HLL compiler and runtime, along with a few examples of real HLL code.
2. The `start` agent drafts headers for each section one at a time based on its newfounding understanding of HLL from reading the code.
3. The `start` agent invokes the `review` agent to look over each draft with fresh eyes and provide critiques.
4. Based on the `review` agent's comments, it either revises the draft or moves on to the next one.
5. This continues until `start` has no more docs left to write.

The code for `start.hll` is below:
```

*label start

autoprompt
    Please take a look at the contents of the global module. There should be some number of C/C++ files, ending in ".h*" or ".c*". There may also be some files ending in ".hll".

await action list # At this point, agent is aware of the module's contents



label keep_reading

autoprompt
    Please read the first such file that you have not already read, in alphabetical order. Be very careful not to miss any files.

await action read

autoprompt
    Are there more C/C++ files you still haven't read?

await branch keep_reading, done_reading # Keep looping until agent is done reading all files



label done_reading 

autoprompt
    What is the purpose of this code? Can you explain at a high level?

await reply

autoprompt # Finally, we explain the main task to the agent

    This is the code for an agentic programming language called "Humanity's Last Language" or HLL for short. In fact, you are operating within an instance of HLL right now, running on the very code you just read.

    Your task is to write a thorough user's manual for this language. Imagine someone has this software installed but knows nothing about HLL. you must include enough information so that they know everything they could ever possibly need to know about using this language and runtime.

    The manual will be broken up into three separate docs:

    1. Language specification; teaches the reader all about the ins and outs of the HLL syntax, with helpful code examples where applicable
    2. Runtime specification; teaches the reader how to use the HLL binary itself
    3. Internals; teaches the reader a litle bit about the virtual module-based filesystem that exists behind the scenes, so that the user can write more helpful and descriptive prompts by understanding what the agents are seeing when they exist inside an HLL instance

storectx background_info



label working

loadctx background_info # Optimization: prunes irrelevant parts of the context window for each new subtask

autoprompt

    Your context window has been pruned to conserve on tokens. Some other workers before you may or may not have already written documentations for some of the previous sections. If so, they will look like `doc_section_{N}.md` where 1 <= N <= 3.

    Please look at the files in the current module to see what's changed since you last looked.

await action list

autoprompt
    Are there any remaining docs that have not yet been written?

await branch write_docs, end



label write_docs

autoprompt # I've found that letting it "brainstorm" before doing the final task allows it to give higher quality results

    You will now begin working on the next section of the docs that has not already been written.

    This is your time to think out loud and brainstorm. What are some subtleties and details that you need to ensure to include in **this section** of the doc, so that the reader has all the information they would need?

    **You do not need to write the final doc at this time.** Right now, you will just plan ahead for things you will need to consider when you *do* write it.

await reply

autoprompt
    Having carefully considered this task, please write the official documentation for this section. Write the content to the file `doc_section_{N}.md` in the current module, where N is the first section number that hasn't already been documented.

await action write 

info
    Agent wrote a doc



label sending_to_review

autoprompt
    Your work is being sent to another agent to be reviewed.

invoke review, start # Reviewer will look over the doc and proofread it

autoprompt 
    The reviewer has looked over your docs and left some commentary. Keep in mind that **the reviewer can make mistakes too** and **you are not required to make any changes** if you disagree with their feedback.
    
autoprompt
    They left their comments in a file in the current module called `review.txt`. Please read this file.

await action read

autoprompt
    Please take a minute to brainstorm now. Did they advise you to make any changes? Do you agree with their commentary? If changes are warranted, how will you make the next draft more robust?

await reply

autoprompt
    Are you going to revise the documentation?

await branch revise_docs, working # If the docs don't need revision, move on to start working on the next doc



label revise_docs

autoprompt
    Please rewrite this doc based on the changes you outlined above.

await action write

info
    Agent revised their doc

goto sending_to_review # Revised doc gets reviewed again, by a fresh reviewer



label end

info
    Docs are finished
```
The code for `review.hll` is below:
```

# Agent identifies the most recently written doc, reads it, and provides feedback by leaving it in a `review.txt` file

*label start 

loadctx background_info # Pre-load the context from when the `start` agent first reviewed all the files

autoprompt
    Your context has been pruned to conserve tokens. Another agent has already started working on drafting sections of the doc. If you look inside this module, you should see files named like this: `doc_section_{N}.md` where 1 <= N <= 3. Please look inside the module now.

await action list

autoprompt
    Please read the most recently written doc; i.e. the one with the highest value of N.

await action read

autoprompt # Reviewer brainstorms on the quality of the doc

    Your job is to review this doc and decide whether it needs further refinement. Would a user who has never seen HLL before be able to read this doc and comprehend it fully? Would the user be able to utilize the subset of HLL described in this doc *effectively* after reading it? Furthermore, does the doc fully cover all the details that it is meant to cover (excluding the domains of the other docs)?

    Please consider these factors carefully, because the correctness of the docs is crucial.

await reply

autoprompt
    Having thought about it, do you approve of this doc?

await branch approved, not_approved



label approved

info
    Reviewer approved the doc

autoprompt
    Please write "I approve" into a file in this module called `review.txt`.

await action write 

goto end



label not_approved

info
    Reviewer did not approve the doc

autoprompt
    Please write your critiques into a file in this module called `review.txt` so that the original agent can make revisions to their doc.

await action write



label end
```