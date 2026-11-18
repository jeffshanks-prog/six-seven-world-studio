/**
 * @file llaudioeq_faudio.h
 * @brief 6|7 World Studio: a 10-band equalizer as an FAudio effect (FAPO),
 *        run on the engine's per-type submix voices. Its filter bank is
 *        sw67core's (sw67/audio/equalizer.h), where it is tested.
 *
 *        FAudio's own FXEQ is not used: its Process is not implemented
 *        upstream and passes audio through unchanged.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#pragma once

#include <cstdint>

struct FAPO;

/// The effect's parameter block: each band's gain in dB.
struct SW67EqParameters
{
    float gainsDb[10];
};

/// A new equalizer effect with one reference, or null. Flat until its
/// parameters are set.
FAPO* sw67_create_eq_fapo();
