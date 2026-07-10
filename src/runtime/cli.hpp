#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace bastion {

struct CliOptions {
    std::string command = "scenario";
    std::string scenario = "snapshot";
    bool json = false;
    bool help = false;
    bool list = false;
};

CliOptions parse_cli(int argc, char** argv);
int run_cli(int argc, char** argv, std::ostream& out, std::ostream& err);
void write_help(std::ostream& out);
void write_scenario_list(std::ostream& out, bool json);

} // namespace bastion

