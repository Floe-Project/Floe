// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "foundation/foundation.hpp"

#include "licence_texts.h"

struct ThirdPartyLicence {
    String name;
    String copyright;
    String licence;
};

static constexpr ThirdPartyLicence k_third_party_licence_texts[] = {
    {
        .name = "pffft",
        .copyright =
            "Copyright (c) 2013  Julien Pommier ( pommier@modartt.com )\nCopyright (c) 2004 the University Corporation for Atmospheric Research (\"UCAR\")",
        .licence = FFTPACK_LICENSE,
    },
    {
        .name = "Roboto Font",
        .copyright = "Copyright 2015 Google Inc. All Rights Reserved.",
        .licence = APACHE_2_0_LICENSE,
    },
    {
        .name = "LLVM",
        .copyright = "Copyright (c) LLVM Project contributors",
        .licence = APACHE_2_0_LICENSE,
    },
    {
        .name = "Mada Font",
        .copyright =
            "Copyright 2015-2022 The Mada Project Authors (https://github.com/aliftype/mada), with Reserved Font Name \"Source\".",
        .licence = OFL_1_1_LICENSE,
    },
    {
        .name = "Fira Sans Font",
        .copyright = "Copyright (c) 2012-2015, The Mozilla Foundation and Telefonica S.A.",
        .licence = OFL_1_1_LICENSE,
    },

    {
        .name = "Stillwell Major Tom Compressor",
        .copyright = "Copyright 2006 Thomas Scott Stillwell",
        .licence = BSD_3_CLAUSE_LICENSE,
    },
    {
        .name = "Font Awesome Font",
        .copyright = "Copyright (c) 2018, Font Awesome (https://fontawesome.com/)",
        .licence = OFL_1_1_LICENSE,
    },
    {
        .name = "xxHash",
        .copyright = "Copyright (C) 2012-2023 Yann Collet",
        .licence = BSD_2_CLAUSE_LICENSE,
    },
    {
        .name = "SerenityOS",
        .copyright =
            "Copyright (c) 2018-2021, Andreas Kling <kling@serenityos.org>\nCopyright (c) 2021, Ali Mohammad Pur <mpfard@serenityos.org>\nCopyright (c) 2021, Daniel Bertalan <dani@danielbertalan.dev>",
        .licence = BSD_2_CLAUSE_LICENSE,
    },
    {
        .name = "Vital",
        .copyright = "Copyright 2013-2019 Matt Tytel",
        .licence = GPL_3_LICENSE,
    },
    {
        .name = "JUCE core",
        .copyright = "Copyright (c) 2022 - Raw Material Software Limited",
        .licence = ISC_LICENSE,
    },
    {
        .name = "Pugl",
        .copyright = "Copyright 2012-2023 David Robillard",
        .licence = ISC_LICENSE,
    },
    {
        .name = "CLAP Wrapper",
        .copyright = "Copyright (c) 2022 defiantnerd",
        .licence = MIT_LICENSE,
    },
    {
        .name = "CLAP",
        .copyright = "Copyright (c) 2014...2022 Alexandre BIQUE",
        .licence = MIT_LICENSE,
    },
    {
        .name = "FFTConvolver",
        .copyright = "Copyright (c) 2017 HiFi-LoFi",
        .licence = MIT_LICENSE,
    },
    {
        .name = "Sentry Native SDK",
        .copyright = "Copyright (c) 2019 Sentry (https://sentry.io) and individual contributors",
        .licence = MIT_LICENSE,
    },
    {
        .name = "FLAC Codec",
        .copyright = "Copyright (C) 2000-2009  Josh Coalson\nCopyright (C) 2011-2023  Xiph.Org Foundation",
        .licence = BSD_3_CLAUSE_LICENSE,
    },
    {
        .name = "Layout",
        .copyright =
            "Copyright (c) 2016 Andrew Richards randrew@gmail.com\nCopyright (c) 2014 Leonard Ritter leonard.ritter@duangle.com",
        .licence = MIT_LICENSE,
    },
    {
        .name = "libbacktrace",
        .copyright = "2012-2021 Free Software Foundation, Inc.\nWritten by Ian Lance Taylor, Google.",
        .licence = BSD_3_CLAUSE_LICENSE,
    },
    {
        .name = "Lua",
        .copyright = "Copyright © 1994–2023 Lua.org, PUC-Rio.",
        .licence = MIT_LICENSE,
    },
    {
        .name = "miniz",
        .copyright =
            "Copyright 2013-2014 RAD Game Tools and Valve Software\nCopyright 2010-2014 Rich Geldreich and Tenacious Software LLC",
        .licence = MIT_LICENSE,
    },
    {
        .name = "VST3 base and VST3 public.sdk",
        .copyright = "(c) 2023, Steinberg Media Technologies GmbH, All Rights Reserved",
        .licence = BSD_3_CLAUSE_LICENSE,
    },
    {
        .name = "VST3 SDK Interfaces",
        .copyright = "(c) 2023, Steinberg Media Technologies GmbH, All Rights Reserved",
        .licence = GPL_3_LICENSE,
    },
    {
        .name = "dear imgui",
        .copyright = "Copyright (c) 2014-2024 Omar Cornut",
        .licence = MIT_LICENSE,
    },
    {
        .name = "VAStateVariableFilter",
        .copyright = "Copyright (c) 2015 Jordan Harris",
        .licence = MIT_LICENSE,
    },
    {
        .name = "Zig",
        .copyright = "Copyright (c) Zig contributors",
        .licence = MIT_LICENSE,
    },
    {
        .name = "Sane C++",
        .copyright = "Copyright (c) 2022 Stefano Cristiano",
        .licence = MIT_LICENSE,
    },
    {
        .name = "Folly",
        .copyright = "Copyright (c) Meta Platforms, Inc. and affiliates.",
        .licence = APACHE_2_0_LICENSE,
    },
};
