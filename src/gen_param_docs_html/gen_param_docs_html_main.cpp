// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "os/threading.hpp"
#include "utils/logger/logger.hpp"

#include "plugin/effects/effect_infos.hpp"
#include "plugin/param_info.hpp"

static Bitset<k_num_parameters> g_param_documented {};

static String ParamsHtmlTable(ArenaAllocator& arena,
                              String title,
                              Optional<String> pretext,
                              bool small_title,
                              Array<ParameterModule, 4> modules) {
    DynamicArray<char> builder {arena};

    String const heading_tag = small_title ? "h5" : "h2";

    fmt::Append(builder, "<{}>{}</{}>\n", heading_tag, title, heading_tag);
    if (pretext) fmt::Append(builder, "<p>{}</p>\n", *pretext);

    bool first_item = true;

    for (auto [i, p] : Enumerate(k_param_infos)) {
        if (p.module_parts == modules) {
            if (first_item) {
                first_item = false;
                fmt::Append(builder, "<table class=\"param-table\">\n");
                fmt::Append(builder, "<tr>");
                fmt::Append(builder, "<th>Name</th>");
                fmt::Append(builder, "<th>Type</th>");
                fmt::Append(builder, "<th>Description</th>");
                fmt::Append(builder, "</tr>");
            }

            String const type_text = ({
                String s {};
                switch (p.value_type) {
                    case ParamValueType::Float: s = "Knob"; break;
                    case ParamValueType::Menu: s = "Menu"; break;
                    case ParamValueType::Bool: s = "Switch"; break;
                    case ParamValueType::Int: s = "Number"; break;
                }
                s;
            });

            fmt::Append(builder, "<tr>");
            fmt::Append(builder, "<td>{}</td>", p.name);
            fmt::Append(builder, "<td>{}</td>", type_text);
            fmt::Append(builder, "<td>{}</td>", p.tooltip);
            fmt::Append(builder, "</tr>");

            g_param_documented.Set(i);
        }
    }

    ASSERT(!first_item);
    fmt::Append(builder, "</table>\n");

    return builder.ToOwnedSpan();
}

int main(int argc, char** argv) {
    SetThreadName("Main");

    if (argc != 2) {
        cli_out.ErrorLn("Error: expected 1 argument - the output path to write the generated HTML");
        return 1;
    }

    ArenaAllocator arena {PageAllocator::Instance(), 16 * 1024};
    DynamicArray<char> result {arena};

    dyn::AppendSpan(result,
                    ParamsHtmlTable(arena,
                                    "Master Parameters",
                                    "Parameters at the top level of Floe",
                                    false,
                                    {ParameterModule::Master}));
    dyn::AppendSpan(result,
                    ParamsHtmlTable(arena,
                                    "Layer Parameters",
                                    "Parameters for each of Floe's 3 layers",
                                    false,
                                    {ParameterModule::Layer1}));
    dyn::AppendSpan(result,
                    ParamsHtmlTable(arena,
                                    "Layer Volume Envelope Parameters",
                                    nullopt,
                                    true,
                                    {ParameterModule::Layer1, ParameterModule::VolEnv}));
    dyn::AppendSpan(result,
                    ParamsHtmlTable(arena,
                                    "Layer Loop Parameters",
                                    nullopt,
                                    true,
                                    {ParameterModule::Layer1, ParameterModule::Loop}));
    dyn::AppendSpan(result,
                    ParamsHtmlTable(arena,
                                    "Layer Filter Parameters",
                                    nullopt,
                                    true,
                                    {ParameterModule::Layer1, ParameterModule::Filter}));
    dyn::AppendSpan(result,
                    ParamsHtmlTable(arena,
                                    "Layer LFO Parameters",
                                    nullopt,
                                    true,
                                    {ParameterModule::Layer1, ParameterModule::Lfo}));
    dyn::AppendSpan(result,
                    ParamsHtmlTable(arena,
                                    "Layer EQ Parameters",
                                    nullopt,
                                    true,
                                    {ParameterModule::Layer1, ParameterModule::Eq}));
    dyn::AppendSpan(result,
                    ParamsHtmlTable(arena,
                                    "Layer EQ Band Parameters",
                                    nullopt,
                                    true,
                                    {ParameterModule::Layer1, ParameterModule::Eq, ParameterModule::Band1}));
    dyn::AppendSpan(result,
                    ParamsHtmlTable(arena,
                                    "Layer Midi Parameters",
                                    nullopt,
                                    true,
                                    {ParameterModule::Layer1, ParameterModule::Midi}));

    for (auto& info : k_effect_info) {
        auto const modules = k_param_infos[ToInt(info.on_param_index)].module_parts;
        dyn::AppendSpan(result, ParamsHtmlTable(arena, info.name, info.description, false, modules));
    }

    for (auto const i : Range(k_num_parameters)) {
        if (!g_param_documented.Get(i)) {
            if (k_param_infos[i].module_parts[0] == ParameterModule::Layer2 ||
                k_param_infos[i].module_parts[0] == ParameterModule::Layer3) {
                continue;
            }
            if (k_param_infos[i].module_parts[2] == ParameterModule::Band2) continue;

            cli_out.ErrorLn("Param {} {} is not included in HTML table",
                            k_param_infos[i].name,
                            k_param_infos[i].ModuleString());
            PanicIfReached();
        }
    }

    auto const out_path = FromNullTerminated(argv[1]);
    if (auto o = WriteFile(out_path, result.Items()); o.HasError()) {
        cli_out.ErrorLn("Failed to write file {}: {}", out_path, o.Error());
        return 1;
    }

    cli_out.InfoLn("Successfully wrote file {}", out_path);
    return 0;
}
