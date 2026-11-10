/*
 * Copyright (C) 2026 LogSquirl Contributors
 *
 * This file is part of LogSquirl.
 *
 * LogSquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LogSquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LogSquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <optional>

#include "decompressor.h"
#include "logfilelifetime.h"

// The steps that open a Log File in a window, in their order (#949). Each
// was a fix of its own; the window carries them out and asks nextOpenStep()
// which comes next whenever it learned something.
enum class OpenStep {
    // The plugins have not loaded yet: whether a converter opens the file is
    // only known once they have (#303).
    WaitForPlugins,
    // The Log File is open, or was converted, in a window: its tab is brought
    // to the front (#615). Or it is on its way, by a Conversion or a content
    // claim under way: nothing more is done.
    ShowOpenTab,
    // A converter plugin for the file's extension converts it.
    ConvertByExtension,
    // The converter plugins are asked whether one claims the Ordinary Log
    // File by its content, as it is on disk, before it is decompressed
    // (#861, ADR 0028).
    AskContentClaim,
    // The converter plugin that claimed it by its content converts it.
    ConvertByClaim,
    // No converter plugin takes it: the file is looked at to know whether it
    // is compressed or an archive.
    LookForCompression,
    // It is decompressed, or its archive extracted, and what comes out is
    // opened.
    Decompress,
    // It is opened as it is.
    Open,
};

// What the converter plugins answered when asked whether one claims the Log
// File by its content.
enum class ContentClaim {
    // Not asked (yet).
    NotAsked,
    // None claims it, or none checks content.
    NotClaimed,
    // One claims it.
    Claimed,
};

// What is known so far about the Log File being opened. What is not known
// yet keeps its default: the step that learns it comes before any that needs
// it.
struct OpenFacts {
    bool pluginsLoaded = false;
    // Open, or converted, in any window; or on its way, by a Conversion or a
    // content claim under way.
    bool alreadyOpen = false;
    // A converter plugin handles the file's extension.
    bool extensionConverter = false;
    LogFileLifetime lifetime = LogFileLifetime::Ordinary;
    ContentClaim contentClaim = ContentClaim::NotAsked;
    // Unknown until the file is looked at.
    std::optional<DecompressAction> decompressAction = std::nullopt;
    // Whether the File Access Policy has archives and compressed files
    // extracted.
    bool extractArchives = true;
};

// The next step to open the Log File, from what is known so far. Pure: it
// reads no file and asks no plugin.
OpenStep nextOpenStep( const OpenFacts& known );
