#include <iostream>
#include <stdexcept>
#include <dirent.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <cstdlib>
#include <csignal>
#include "defs.hpp"
#include "json.hpp"

// SIGINT handler that exits cleanly
void handle_sigint(int) {
    std::cout << std::endl;
    std::exit(0);
}

extern void parse(dialogues&, const std::vector<std::string>&);
extern void dispatch(dialogues&, pjson, pjson, const std::string&);

void discoverfilenames(std::vector<std::string>& filenames, const std::string& dir) { // chatgpt
    DIR* dp = opendir(dir.c_str());
    if (!dp)
        throw std::runtime_error("Invalid directory: " + dir);

    struct dirent* entry;
    while ((entry = readdir(dp)) != nullptr) {
        std::string name = entry->d_name;
        if (entry->d_type == DT_REG) { // regular file
            filenames.push_back(name);
        }
    }

    closedir(dp);
}

void copyfiles(const std::vector<std::string>& includes, const std::string& dir, bool hllonly) { // chatgpt
    mkdir(dir.c_str(), 0755); // create dir if not exists

    for (const auto& path : includes) {
        DIR* dp = opendir(path.c_str());
        if (!dp) continue; // safe to skip as per your comment

        struct dirent* entry;
        while ((entry = readdir(dp)) != nullptr) {
            std::string name = entry->d_name;
            if (entry->d_type == DT_REG && name.size() > 4) {
                if (hllonly && name.substr(name.size() - 4) != ".hll") continue;
                std::ifstream src(path + "/" + name, std::ios::binary);
                std::ofstream dst(dir + "/" + name + (hllonly ? "" : ".global"), std::ios::binary);
                dst << src.rdbuf();
            }
        }

        closedir(dp);
    }
}

bool deletefolder(const std::string& dir) { // chatgpt
    std::cout << "Are you sure? Type \"I am sure\" to proceed: ";
    std::string confirmation;
    std::getline(std::cin, confirmation);

    if (confirmation != "I am sure") {
        std::cout << "Aborted.\n";
        return false;
    }

    struct stat info;
    if (stat(dir.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
        std::cout << "Directory does not exist or is not valid: " << dir << "\n";
        return true;
    }

    std::string command = "rm -rf \"" + dir + "\"";
    int ret = system(command.c_str());
    if (ret != 0) {
        std::cerr << "Failed to delete folder: " << dir << "\n";
    }
    return true;
}

void create(const std::string& pname, const std::string& root, const std::vector<std::string>& includes) {

    auto projects = json::loadFromFile(hll_projects_folder "projects.json", true);
    auto& dict = projects->getDict();

    if (dict.find(pname) != dict.end())
        throw std::runtime_error("Project with name '" + pname + "' already exists");

    { // check validity of hll dialogues
        dialogues d;
        parse(d, includes);
    }

    std::string proot = root + hll_subdir + pname + "/";

    auto dependencygraph = json::makeDict();
    auto modules = json::makeList();
    auto files = json::makeDict();
    auto dependencies = json::makeDict();
    auto children = json::makeDict();
    auto globalfiles = json::makeList();

    std::vector<std::string> filenames;
    discoverfilenames(filenames, root);

    for (const auto& discovered : filenames) globalfiles->getList().push_back(json::makeString(discovered));
    modules->getList().push_back(json::makeString("global"));
    modules->getList().push_back(json::makeString("root"));
    files->getDict()["global"] = globalfiles;
    files->getDict()["root"] = json::makeList();
    dependencies->getDict()["global"] = json::makeList();
    dependencies->getDict()["root"] = json::makeList();
    children->getDict()["global"] = json::makeList();
    children->getDict()["root"] = json::makeList();
    dependencygraph->getDict()["modules"] = modules;
    dependencygraph->getDict()["files"] = files;
    dependencygraph->getDict()["dependencies"] = dependencies;
    dependencygraph->getDict()["children"] = children;
    dependencygraph->save(proot + hll_metadata_subdir + "dependency_graph.json", true);

    copyfiles(includes, proot + hll_metadata_subdir, true);
    copyfiles({root}, proot, false);

    dict[pname] = json::makeString(proot);
    projects->save(hll_projects_folder "projects.json", true);

}

void run(const std::string& pname, const std::string& agent, const std::string& label) {

    auto projects = json::loadFromFile(hll_projects_folder "projects.json", true);
    auto& dict = projects->getDict();

    if (dict.find(pname) == dict.end())
        throw std::runtime_error("Project with name '" + pname + "' does not exist");

    std::string proot = dict[pname]->getString();
    dialogues d;
    parse(d, { proot + hll_metadata_subdir });

    bool exists = true;
    try { json::loadFromFile(proot + hll_metadata_subdir + "instance.json"); }
    catch (...) { exists = false; }

    if (exists) throw std::runtime_error("'" + pname + "' already has an active instance");

    int aid = dialogue::agentnames.query(agent);
    if (aid < 0) throw std::runtime_error("Invalid agent name '" + agent + "'");

    auto& dial = d[aid];
    int lid = -1;
    if (label.empty()) {
        for (auto plid : dial.entrypoints) {
            if (lid >= 0) throw std::runtime_error("Starting label argument is missing");
            lid = plid; // if there's only one public label, it's unambiguous and you don't need to supply it as an argument
        }
    }
    else {
        lid = dial.labelnames.query(label);
        if (lid < 0) 
            throw std::runtime_error("Agent '" + agent + "' has no label '" + label + "'");
        if (dial.entrypoints.find(lid) == dial.entrypoints.end())
            throw std::runtime_error("Agent '" + agent + "' label '" + label + "' is not public");
    }

    auto instance = json::makeList();
    auto frame = json::makeDict();
    auto istart = dial.jumptable[lid];
    frame->getDict()["agent"] = json::makeInt(aid);
    frame->getDict()["module"] = json::makeString("root");
    frame->getDict()["instruction"] = json::makeInt(istart);
    frame->getDict()["called"] = json::makeBool(false);
    instance->getList().push_back(frame);

    auto dependencygraph = json::loadFromFile(proot + hll_metadata_subdir + "dependency_graph.json");

    dispatch(d, instance, dependencygraph, proot);
    {
        std::string path = proot + hll_metadata_subdir + "instance.json";
        struct stat buffer;
        if (stat(path.c_str(), &buffer) == 0) {
            std::remove(path.c_str());
        }
    }

}

void resume(const std::string& pname) {

    auto projects = json::loadFromFile(hll_projects_folder "projects.json", true);
    auto& dict = projects->getDict();

    if (dict.find(pname) == dict.end())
        throw std::runtime_error("Project with name '" + pname + "' does not exist");

    std::string proot = dict[pname]->getString();
    dialogues d;
    parse(d, { proot + hll_metadata_subdir });

    pjson instance;
    try { instance = json::loadFromFile(proot + hll_metadata_subdir + "instance.json"); }
    catch (...) { throw std::runtime_error("'" + pname + "' does not have an active instance"); }

    auto dependencygraph = json::loadFromFile(proot + hll_metadata_subdir + "dependency_graph.json");

    dispatch(d, instance, dependencygraph, proot);
    {
        std::string path = proot + hll_metadata_subdir + "instance.json";
        struct stat buffer;
        if (stat(path.c_str(), &buffer) == 0) {
            std::remove(path.c_str());
        }
    }

}

void query() {

    auto projects = json::loadFromFile(hll_projects_folder "projects.json", true);
    auto& dict = projects->getDict();

    if (dict.empty()) {
        std::cout << "There are no projects\n";
        return;
    }

    for (auto& it : dict) {
        std::cout << it.first << " : ";
        bool exists = true;
        try { json::loadFromFile(it.second->getString() + hll_metadata_subdir + "instance.json"); }
        catch (...) { exists = false; }
        std::cout << (exists ? "active" : "inactive") << "\n";
    }

}

void delete_(const std::string& pname) {

    auto projects = json::loadFromFile(hll_projects_folder "projects.json", true);
    auto& dict = projects->getDict();

    if (dict.find(pname) == dict.end())
        throw std::runtime_error("Project with name '" + pname + "' does not exist");

    if (!deletefolder(dict[pname]->getString())) return;
    dict.erase(pname);
    projects->save(hll_projects_folder "projects.json");

}

int main(int argc, char** argv) { // chatgpt
    std::signal(SIGINT, handle_sigint);

    if (argc < 2) {
        std::cerr << "No command provided. Usage [create/run/resume/query/delete]\n";
        return 1;
    }

    std::string cmd = argv[1];

    try {
        if (cmd == "create") {
            if (argc < 4) throw std::runtime_error("Usage: create [pname] [root] [-I include1 -I include2 ...]");

            std::string pname = argv[2];
            std::string root = argv[3];

            std::vector<std::string> includes;
            for (int i = 4; i < argc; ++i) {
                if (std::string(argv[i]) == "-I") {
                    if (++i >= argc) throw std::runtime_error("Missing path after -I");
                    includes.push_back(argv[i]);
                } else {
                    throw std::runtime_error("Unexpected argument: " + std::string(argv[i]));
                }
            }

            create(pname, root, includes);
        } else if (cmd == "run") {
            if (argc < 4) throw std::runtime_error("Usage: run [pname] [agent] [label (optional)]");

            std::string pname = argv[2];
            std::string agent = argv[3];
            std::string label = (argc >= 5) ? argv[4] : "";

            run(pname, agent, label);
        } else if (cmd == "resume") {
            if (argc != 3) throw std::runtime_error("Usage: resume [pname]");
            resume(argv[2]);
        } else if (cmd == "query") {
            query();
        } else if (cmd == "delete") {
            if (argc != 3) throw std::runtime_error("Usage: delete [pname]");
            delete_(argv[2]);
        } else {
            throw std::runtime_error("Unknown command: " + cmd);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}
