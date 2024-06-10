#!/bin/bash
# Copyright 2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

# Exit the script if any command fails
set -e

test_type=$1
binaries_dir=$2

case $test_type in
    "unit")
        chmod +x $binaries_dir/tests
        $binaries_dir/tests --log-level=debug
        ;;
    "vst3val")
        chmod +x $binaries_dir/VST3-Validator
        $binaries_dir/VST3-Validator $binaries_dir/Floe.vst3
        ;;
    "pluginval")
        filename="pluginval_Linux.zip"
        if [ "$(uname)" == "Darwin" ]; then
            filename="pluginval_macOS.zip"
        fi
        curl -L "https://github.com/Tracktion/pluginval/releases/download/v1.0.3/${filename}" -o pluginval.zip
        
        unzip pluginval.zip
        if [ "$(uname)" == "Darwin" ]; then
            ./pluginval.app/Contents/MacOS/pluginval $binaries_dir/Floe.vst3 
        else
            ./pluginval $binaries_dir/Floe.vst3 
        fi
        ;;
    "clapval")
        release_tag="0.3.2"
        filename="clap-validator-${release_tag}-ubuntu-18.04.tar.gz"
        if [ "$(uname)" == "Darwin" ]; then
            filename="clap-validator-${release_tag}-macos-universal.tar.gz"
        fi
        curl -L "https://github.com/free-audio/clap-validator/releases/download/${release_tag}/${filename}" -o clapval.tar.gz
        tar -xf clapval.tar.gz
        if [ "$(uname)" == "Darwin" ]; then
            ./binaries/clap-validator validate $binaries_dir/Floe.clap
        else
            ./clap-validator validate --in-process $binaries_dir/Floe.clap
        fi
        ;;
    *)
        echo "Invalid test_type."
        exit 1
        ;;
esac

echo "Script completed successfully."
