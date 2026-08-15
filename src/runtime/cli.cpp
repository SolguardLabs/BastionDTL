#include "runtime/cli.hpp"

#include "common/json.hpp"
#include "runtime/scenarios.hpp"
#include "runtime/version.hpp"

#include <iostream>
#include <sstream>

namespace bastion {

namespace {

bool is_json_flag(std::string_view arg) {
    return arg == "--json" || arg == "-j";
}

} // namespace

CliOptions parse_cli(int argc, char** argv) {
    CliOptions options;
    std::vector<std::string> args;
    for (int index = 1; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }
    if (args.empty()) {
        options.command = "scenario";
        options.scenario = "snapshot";
        options.json = true;
        return options;
    }
    for (std::size_t index = 0; index < args.size(); ++index) {
        const auto& arg = args[index];
        if (arg == "--help" || arg == "-h" || arg == "help") {
            options.help = true;
            return options;
        }
        if (is_json_flag(arg)) {
            options.json = true;
            continue;
        }
        if (arg == "list") {
            options.list = true;
            options.command = "list";
            continue;
        }
        if (arg == "scenario" || arg == "run") {
            options.command = "scenario";
            if (index + 1 < args.size() && !is_json_flag(args[index + 1])) {
                options.scenario = args[++index];
            }
            continue;
        }
        if (arg.rfind("--scenario=", 0) == 0) {
            options.scenario = arg.substr(std::string("--scenario=").size());
            options.command = "scenario";
            continue;
        }
        if (options.command == "scenario" && options.scenario == "snapshot") {
            options.scenario = arg;
            continue;
        }
        fail("unknown argument");
    }
    return options;
}

int run_cli(int argc, char** argv, std::ostream& out, std::ostream& err) {
    try {
        const auto options = parse_cli(argc, argv);
        if (options.help) {
            write_help(out);
            return 0;
        }
        if (options.list) {
            write_scenario_list(out, options.json);
            return 0;
        }
        auto report = run_scenario(options.scenario);
        if (options.json) {
            JsonWriter json(out);
            report.write_json(json);
            out << "\n";
        } else {
            out << "BastionDTL scenario: " << report.scenario << "\n";
            out << "ok: " << bool_text(report.ok) << "\n";
            out << "epoch: " << report.ledger.epoch().value << "\n";
            out << "stateDigest: " << report.ledger.state_digest() << "\n";
            for (const auto& check : report.checks) {
                out << "- " << check.name << ": " << bool_text(check.ok) << " (" << check.detail << ")\n";
            }
        }
        return report.ok ? 0 : 2;
    } catch (const DomainError& error) {
        err << error.what() << "\n";
        return 1;
    } catch (const std::exception& error) {
        err << "unexpected error: " << error.what() << "\n";
        return 1;
    }
}

void write_help(std::ostream& out) {
    out << build_info().display() << "\n";
    out << "Custody settlement service\n\n";
    out << "Usage:\n";
    out << "  bastiondtl scenario <name> [--json]\n";
    out << "  bastiondtl list [--json]\n";
    out << "  bastiondtl --help\n\n";
    out << "Scenarios:\n";
    for (const auto& name : scenario_names()) {
        out << "  " << name << "\n";
    }
}

void write_scenario_list(std::ostream& out, bool json_output) {
    if (!json_output) {
        for (const auto& name : scenario_names()) {
            out << name << "\n";
        }
        return;
    }
    JsonWriter json(out);
    json.begin_object();
    json.key("scenarios");
    json.begin_array();
    for (const auto& name : scenario_names()) {
        json.string(name);
    }
    json.end_array();
    json.end_object();
    out << "\n";
}

} // namespace bastion
