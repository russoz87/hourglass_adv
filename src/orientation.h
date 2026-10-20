/*
 * Which way gravity pulls the device, derived from the accelerometer.
 *
 * The hourglass has one axis: the long side of the device. Standing on one of
 * its short edges makes the sand flow, standing on the other edge makes it flow
 * back, lying flat stops it. Everything in between is unstable.
 *
 * The accelerometer of the BMI270 is noisy and unfiltered, so the tracker both
 * smooths the samples and requires a pose to hold for a while before it is
 * reported. See orientation.cpp for the thresholds and for the measured axis
 * convention.
 */
#pragma once

#include <cstdint>

enum class Orientation : uint8_t
{
    Unstable,  // moving, tilted, or resting between poses: no usable axis
    FlatUp,    // screen facing up
    FlatDown,  // screen facing down
    SideA,     // standing on one short edge
    SideB,     // standing on the other short edge
    LongYUp,   // resting on a long edge, device +y up: the glass lies on its side
    LongYDown, // resting on a long edge, device +y down: the other long edge
};

const char* orientationName(Orientation orientation);

bool isSide(Orientation orientation);

// The other side, or the input itself for the flat/unstable poses.
Orientation oppositeSide(Orientation orientation);

class OrientationTracker
{
public:
    void begin(uint32_t nowMs);

    // Reads a fresh IMU sample (when there is one) and returns the debounced
    // orientation. Cheap to call every loop; it only evaluates new samples.
    Orientation update(uint32_t nowMs);

    Orientation orientation() const { return _orientation; }

private:
    float       _filtered[3]      = {0.0f, 0.0f, 0.0f};
    bool        _filteredValid    = false;
    uint32_t    _lastSampleMs     = 0;
    Orientation _instant          = Orientation::Unstable;
    Orientation _candidate        = Orientation::Unstable;
    uint32_t    _candidateSinceMs = 0;
    Orientation _orientation      = Orientation::Unstable;
};
