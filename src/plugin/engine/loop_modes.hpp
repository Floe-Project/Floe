// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

#include "state/instrument.hpp"

struct LoopModeValidResult {
    bool valid;
    String invalid_reason;
};

PUBLIC LoopModeValidResult LoopModeIsValid(param_values::LoopMode mode,
                                           sample_lib::LoopOverview loop_overview) {
    switch (mode) {
        case param_values::LoopMode::InstrumentDefault: return {true};
        case param_values::LoopMode::BuiltInLoopStandard: {
            if (!loop_overview.has_loops) return {false, "There's no built-in loops in this instrument"};
            if (!loop_overview.has_loops_convertible_to_mode[ToInt(sample_lib::Loop::Mode::Standard)])
                return {false,
                        "Built-in loops cannot be changed to standard wrap-around mode in this instrument"};
            return {true};
        }
        case param_values::LoopMode::BuiltInLoopPingPong: {
            if (!loop_overview.has_loops) return {false, "There's no built-in loops in this instrument"};
            if (!loop_overview.has_loops_convertible_to_mode[ToInt(sample_lib::Loop::Mode::PingPong)])
                return {false, "Built-in loops cannot be changed to ping-pong mode in this instrument"};
            return {true};
        }
        case param_values::LoopMode::None: {
            if (loop_overview.all_regions_require_looping)
                return {false, "Built-in loops cannot be turned off in this instrument"};
            return {true};
        }
        case param_values::LoopMode::PingPong:
        case param_values::LoopMode::Standard: {
            if (!loop_overview.has_loops) return {true};
            if (!loop_overview.user_defined_loops_allowed)
                return {false, "Built-in loops cannot be overriden in this instrument"};
            return {true};
        }
        case param_values::LoopMode::Count: break;
    }
    PanicIfReached();
    return {};
}

PUBLIC LoopModeValidResult LoopModeIsValid(param_values::LoopMode mode, Instrument const& inst) {
    switch (inst.tag) {
        case InstrumentType::None: return {false, "No instrument selected"};
        case InstrumentType::WaveformSynth: {
            switch (mode) {
                case param_values::LoopMode::InstrumentDefault: return {true};
                case param_values::LoopMode::BuiltInLoopStandard:
                case param_values::LoopMode::BuiltInLoopPingPong:
                case param_values::LoopMode::None:
                case param_values::LoopMode::Standard:
                case param_values::LoopMode::PingPong:
                    return {false, "You cannot change waveform instrument loops"};
                case param_values::LoopMode::Count: break;
            }
            break;
        }
        case InstrumentType::Sampler: {
            auto const& sampled_inst = inst.GetFromTag<InstrumentType::Sampler>()->instrument;
            return LoopModeIsValid(mode, sampled_inst.loop_overview);
        }
    }
    PanicIfReached();
    return {};
}

enum class LoopBehaviourId : u8 {
    NoLoop,
    BuiltinLoopStandard,
    BuiltinLoopPingPong,
    CustomLoopStandard,
    CustomLoopPingPong,
    MixedLoops,
    MixedNonLoopsAndLoops,
};

enum class BehaviourReason : u8 {
    Desired,
    WaveformsAlwaysUseBuiltInLoops,
    ContainsMixedNonLoopsAndLoops,
    ContainsNoLoops,
    DefaultInstrumentBehaviour,
    SomeRegionsCannotUseStandardLoops,
    SomeRegionsCannotUsePingPongLoops,
    ContainsRegionsThatRequireLooping,
    BuiltinLoopsCannotBeCustomised,
};

struct LoopBehaviour {
    LoopBehaviourId value;
    BehaviourReason reason;
};

struct LoopBehaviourInfo {
    String name;
    String description;
    String reason;
    bool editable;
};

PUBLIC String BehaviourReasonString(BehaviourReason b) {
    switch (b) {
        case BehaviourReason::Desired: return "";
        case BehaviourReason::WaveformsAlwaysUseBuiltInLoops:
            return "Waveform instruments always use built-in loops.";
        case BehaviourReason::ContainsMixedNonLoopsAndLoops:
            return "Some regions have built-in loops, some don't.";
        case BehaviourReason::ContainsNoLoops: return "It contains no built-in loops.";
        case BehaviourReason::DefaultInstrumentBehaviour:
            return "This is the default behaviour for this instrument.";
        case BehaviourReason::SomeRegionsCannotUseStandardLoops:
            return "Some regions cannot use standard wrap-around loops.";
        case BehaviourReason::SomeRegionsCannotUsePingPongLoops:
            return "Some regions cannot use ping-pong loops.";
        case BehaviourReason::ContainsRegionsThatRequireLooping:
            return "It contains regions that require looping.";
        case BehaviourReason::BuiltinLoopsCannotBeCustomised:
            return "Its built-in loops cannot be customised.";
    }
}

PUBLIC LoopBehaviourInfo GetLoopBehaviourInfo(LoopBehaviourId l) {
    switch (l) {
        case LoopBehaviourId::NoLoop:
            return {
                .name = "No Loop",
                .description = "No looping will be applied to this instrument.",
                .editable = false,
            };
        case LoopBehaviourId::BuiltinLoopStandard:
            return {
                .name = "Built-in Loop - Standard",
                .description =
                    "Every region in this instrument will use built-in loops in standard wrap-around mode.",
                .editable = false,
            };
        case LoopBehaviourId::BuiltinLoopPingPong:
            return {
                .name = "Built-in Loop - Ping-pong",
                .description = "Every region in this instrument will use built-in loops in ping-pong mode.",
                .editable = false,
            };
        case LoopBehaviourId::CustomLoopStandard:
            return {
                .name = "Custom Loop - Standard",
                .description =
                    "Custom loop points will be applied to this instrument and use standard wrap-around mode.",
                .editable = true,
            };
        case LoopBehaviourId::CustomLoopPingPong:
            return {
                .name = "Custom Loop - Ping-pong",
                .description =
                    "Custom loop points will be applied to this instrument and use ping-pong mode.",
                .editable = true,
            };
        case LoopBehaviourId::MixedLoops:
            return {
                .name = "Mixed Loops",
                .description =
                    "All regions use built-in loops, but some are standard and some are ping-pong.",
                .editable = false,
            };
        case LoopBehaviourId::MixedNonLoopsAndLoops:
            return {
                .name = "Mixed Loops and Non-Loops",
                .description = "Some regions have built-in loops, some don't.",
                .editable = false,
            };
    }
}

// TODO: lots of duplication in this function.
PUBLIC LoopBehaviour ActualLoopBehaviour(Instrument const& inst, param_values::LoopMode desired_loop_mode) {
    using namespace param_values;

    switch (inst.tag) {
        case InstrumentType::None:
            return {
                LoopBehaviourId::NoLoop,
            };

        case InstrumentType::WaveformSynth:
            return {
                LoopBehaviourId::BuiltinLoopStandard,
                BehaviourReason::WaveformsAlwaysUseBuiltInLoops,
            };

        case InstrumentType::Sampler: {
            auto const& sampled_inst = inst.GetFromTag<InstrumentType::Sampler>()->instrument;
            auto const loop_overview = sampled_inst.loop_overview;

            switch (desired_loop_mode) {
                case LoopMode::InstrumentDefault: {
                    if (loop_overview.has_loops && loop_overview.has_non_loops)
                        return {
                            LoopBehaviourId::MixedNonLoopsAndLoops,
                            BehaviourReason::ContainsMixedNonLoopsAndLoops,
                        };

                    if (loop_overview.has_non_loops)
                        return {
                            LoopBehaviourId::NoLoop,
                            BehaviourReason::ContainsNoLoops,
                        };

                    ASSERT(loop_overview.has_loops);

                    if (loop_overview.all_loops_mode) {
                        switch (*loop_overview.all_loops_mode) {
                            case sample_lib::Loop::Mode::Standard:
                                return {
                                    LoopBehaviourId::BuiltinLoopStandard,
                                    BehaviourReason::DefaultInstrumentBehaviour,
                                };

                            case sample_lib::Loop::Mode::PingPong:
                                return {
                                    LoopBehaviourId::BuiltinLoopPingPong,
                                    BehaviourReason::DefaultInstrumentBehaviour,
                                };

                            case sample_lib::Loop::Mode::Count: PanicIfReached(); break;
                        }
                    }

                    return {
                        LoopBehaviourId::MixedLoops,
                        BehaviourReason::DefaultInstrumentBehaviour,
                    };
                }

                case LoopMode::BuiltInLoopStandard: {
                    if (loop_overview.has_loops && loop_overview.has_non_loops)
                        return {
                            LoopBehaviourId::MixedNonLoopsAndLoops,
                            BehaviourReason::ContainsMixedNonLoopsAndLoops,
                        };

                    if (loop_overview.has_non_loops)
                        return {
                            LoopBehaviourId::NoLoop,
                            BehaviourReason::ContainsNoLoops,
                        };

                    ASSERT(loop_overview.has_loops);

                    if (loop_overview.all_loops_convertible_to_mode[ToInt(sample_lib::Loop::Mode::Standard)])
                        return {LoopBehaviourId::BuiltinLoopStandard};

                    return {
                        LoopBehaviourId::MixedLoops,
                        BehaviourReason::SomeRegionsCannotUseStandardLoops,
                    };
                }

                case LoopMode::BuiltInLoopPingPong: {
                    if (loop_overview.has_loops && loop_overview.has_non_loops)
                        return {
                            LoopBehaviourId::MixedNonLoopsAndLoops,
                            BehaviourReason::ContainsMixedNonLoopsAndLoops,
                        };

                    if (loop_overview.has_non_loops)
                        return {
                            LoopBehaviourId::NoLoop,
                            BehaviourReason::ContainsNoLoops,
                        };

                    ASSERT(loop_overview.has_loops);

                    if (loop_overview.all_loops_convertible_to_mode[ToInt(sample_lib::Loop::Mode::PingPong)])
                        return {LoopBehaviourId::BuiltinLoopPingPong};

                    return {
                        LoopBehaviourId::MixedLoops,
                        BehaviourReason::SomeRegionsCannotUsePingPongLoops,
                    };
                }

                case LoopMode::None: {
                    if (loop_overview.all_regions_require_looping) {
                        if (loop_overview.all_loops_mode) {
                            switch (*loop_overview.all_loops_mode) {
                                case sample_lib::Loop::Mode::Standard:
                                    return {
                                        LoopBehaviourId::BuiltinLoopStandard,
                                        BehaviourReason::ContainsRegionsThatRequireLooping,
                                    };

                                case sample_lib::Loop::Mode::PingPong:
                                    return {
                                        LoopBehaviourId::BuiltinLoopPingPong,
                                        BehaviourReason::ContainsRegionsThatRequireLooping,
                                    };

                                case sample_lib::Loop::Mode::Count: PanicIfReached(); break;
                            }
                        }

                        return {
                            LoopBehaviourId::MixedLoops,
                            BehaviourReason::ContainsRegionsThatRequireLooping,
                        };
                    }

                    return {LoopBehaviourId::NoLoop};
                }

                case LoopMode::Standard: {
                    if (!loop_overview.user_defined_loops_allowed) {
                        if (loop_overview.has_loops && loop_overview.has_non_loops)
                            return {
                                LoopBehaviourId::MixedNonLoopsAndLoops,
                                BehaviourReason::BuiltinLoopsCannotBeCustomised,
                            };

                        if (loop_overview.has_non_loops && !loop_overview.all_regions_require_looping)
                            return {
                                LoopBehaviourId::NoLoop,
                                BehaviourReason::BuiltinLoopsCannotBeCustomised,
                            };

                        ASSERT(loop_overview.has_loops);

                        if (loop_overview.all_loops_mode) {
                            switch (*loop_overview.all_loops_mode) {
                                case sample_lib::Loop::Mode::Standard:
                                    return {
                                        LoopBehaviourId::BuiltinLoopStandard,
                                        BehaviourReason::BuiltinLoopsCannotBeCustomised,
                                    };

                                case sample_lib::Loop::Mode::PingPong:
                                    return {
                                        LoopBehaviourId::BuiltinLoopPingPong,
                                        BehaviourReason::BuiltinLoopsCannotBeCustomised,
                                    };

                                case sample_lib::Loop::Mode::Count: PanicIfReached(); break;
                            }
                        }

                        return {
                            LoopBehaviourId::MixedLoops,
                            BehaviourReason::BuiltinLoopsCannotBeCustomised,
                        };
                    }

                    return {LoopBehaviourId::CustomLoopStandard};
                }
                case LoopMode::PingPong: {
                    if (!loop_overview.user_defined_loops_allowed) {
                        if (loop_overview.has_loops && loop_overview.has_non_loops)
                            return {
                                LoopBehaviourId::MixedNonLoopsAndLoops,
                                BehaviourReason::BuiltinLoopsCannotBeCustomised,
                            };

                        if (loop_overview.has_non_loops && !loop_overview.all_regions_require_looping)
                            return {
                                LoopBehaviourId::NoLoop,
                                BehaviourReason::BuiltinLoopsCannotBeCustomised,
                            };

                        ASSERT(loop_overview.has_loops);

                        if (loop_overview.all_loops_mode) {
                            switch (*loop_overview.all_loops_mode) {
                                case sample_lib::Loop::Mode::Standard:
                                    return {
                                        LoopBehaviourId::BuiltinLoopStandard,
                                        BehaviourReason::BuiltinLoopsCannotBeCustomised,
                                    };

                                case sample_lib::Loop::Mode::PingPong:
                                    return {
                                        LoopBehaviourId::BuiltinLoopPingPong,
                                        BehaviourReason::BuiltinLoopsCannotBeCustomised,
                                    };

                                case sample_lib::Loop::Mode::Count: PanicIfReached(); break;
                            }
                        }

                        return {
                            LoopBehaviourId::MixedLoops,
                            BehaviourReason::BuiltinLoopsCannotBeCustomised,
                        };
                    }

                    return {LoopBehaviourId::CustomLoopPingPong};
                }

                case LoopMode::Count: break;
            }
        }
    }

    PanicIfReached();
    return {};
}

PUBLIC String LoopModeDescription(param_values::LoopMode mode) {
    switch (mode) {
        case param_values::LoopMode::InstrumentDefault:
            return "Let the instrument decide which regions loop and whether they ping pong or not";
        case param_values::LoopMode::BuiltInLoopStandard:
            return "Let the instrument decide which regions loop, but request standard wrap-around looping mode where possible";
        case param_values::LoopMode::BuiltInLoopPingPong:
            return "Let the instrument decide which regions loop, but request ping-pong looping mode where possible";
        case param_values::LoopMode::None: return "No looping will be applied to this instrument";
        case param_values::LoopMode::Standard:
            return "Set custom loop points for the instrument, using standard wrap-around mode";
        case param_values::LoopMode::PingPong:
            return "Set custom loop points for the instrument, using ping-pong mode";
        case param_values::LoopMode::Count: break;
    }
    PanicIfReached();
    return {};
}
