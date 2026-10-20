/*
 * Orientation from the BMI270 accelerometer.
 *
 * ----------------------------------------------------------------------------
 * AXIS CONVENTION
 * ----------------------------------------------------------------------------
 * Measured on the device with the stage 1 bring-up screen, accelerometer in g
 * (M5Unified applies no axis remap for the Cardputer-ADV, so these are the raw
 * BMI270 directions):
 *
 *   lying screen up          x -0.02   y +0.00   z +1.00
 *   standing on left edge    x -1.02   y -0.01   z +0.03   (USB-C at the bottom)
 *   standing on right edge   x +0.99   y -0.01   z -0.03   (USB-C at the top)
 *
 * So: +z is out of the screen, x is the sand axis (the 240 px side of the
 * picture), y is the short edge and stays near zero in the side poses.
 *
 * The axis table is confirmed on the device: both standing sides, both flat
 * faces (screen up and screen down) and both long edges (the lying poses,
 * including the never-measured y sign) were verified with the sand of the
 * running app - every pose settles its pile the measured way.
 *
 * ----------------------------------------------------------------------------
 * FILTERING
 * ----------------------------------------------------------------------------
 * The accelerometer is raw and noisy, so:
 *   - each sample is low-pass filtered with an exponential moving average whose
 *     time constant is kFilterTauMs (time based, not per-frame, so the filter
 *     behaves the same at any loop rate),
 *   - a pose must be detected continuously for kStableMs before it is reported,
 *     which debounces the bumps of putting the device down,
 *   - a pose of UNSTABLE is reported on the same terms, so a quick flip goes
 *     straight from one side to the other instead of flapping through unstable,
 *   - while the gyroscope shows real motion the stability timer is restarted, so
 *     a flip is not accepted halfway through.
 */

#include "orientation.h"

#include <M5Cardputer.h>

#include <cmath>

#include "log.h"

namespace {

// Axis indices in imu_data_t::accel. See the axis convention above.
constexpr int kAxisSand = 0;    // the long side of the device: the sand axis
constexpr int kAxisNormal = 2;  // out of the screen

// Classification thresholds, in g.
constexpr float kDominantG = 0.80f;  // the gravity axis has to be this strong
constexpr float kOtherMaxG = 0.45f;  // and the other two axes this weak

// Debouncing.
constexpr uint32_t kStableMs   = 400;   // a pose has to hold this long
constexpr float    kFilterTauMs = 80.0f; // accelerometer smoothing time constant
constexpr float    kMotionDps   = 40.0f; // faster gyro = being moved by hand

struct Vec3
{
    float x;
    float y;
    float z;
};

Vec3 accelToVec(const m5::IMU_Class::imu_data_t& data)
{
    return Vec3{data.accel.x, data.accel.y, data.accel.z};
}

// Dominant-axis classification of one accelerometer reading.
Orientation classify(const Vec3& accel)
{
    const float magnitude[3] = {std::fabs(accel.x), std::fabs(accel.y), std::fabs(accel.z)};

    int         axis = 0;
    if (magnitude[1] > magnitude[axis]) {
        axis = 1;
    }
    if (magnitude[2] > magnitude[axis]) {
        axis = 2;
    }

    if (magnitude[axis] < kDominantG) {
        return Orientation::Unstable;
    }

    // The device is tilted or in between poses if the other axes carry weight.
    for (int i = 0; i < 3; ++i) {
        if (i != axis && magnitude[i] > kOtherMaxG) {
            return Orientation::Unstable;
        }
    }

    const float sign = axis == 0 ? accel.x : (axis == 1 ? accel.y : accel.z);

    switch (axis) {
        case kAxisNormal:
            // Screen down was measured only by its working behavior (the flat
            // sand settles to the mirrored pile), the sign follows the axis
            // table by symmetry.
            return sign > 0.0f ? Orientation::FlatUp : Orientation::FlatDown;

        case kAxisSand:
            // x < 0: left edge down (the end with the USB-C port),
            // x > 0: right edge down.
            return sign > 0.0f ? Orientation::SideB : Orientation::SideA;

        default:
            // Standing on a long edge: the short axis is vertical and the long
            // sand axis lies horizontal - the hourglass is lying ON ITS SIDE.
            // The sign of y says which of the two edges it rests on; both get
            // the lying-hourglass physics (the lateral settle) in the app, so
            // they are reported instead of folding into Unstable.
            return sign > 0.0f ? Orientation::LongYUp : Orientation::LongYDown;
    }
}

}  // namespace

const char* orientationName(Orientation orientation)
{
    switch (orientation) {
        case Orientation::FlatUp:    return "FlatUp";
        case Orientation::FlatDown:  return "FlatDown";
        case Orientation::SideA:     return "SideA";
        case Orientation::SideB:     return "SideB";
        case Orientation::LongYUp:   return "LongYUp";
        case Orientation::LongYDown: return "LongYDown";
        case Orientation::Unstable:  break;
    }
    return "Unstable";
}

bool isSide(Orientation orientation)
{
    return orientation == Orientation::SideA || orientation == Orientation::SideB;
}

Orientation oppositeSide(Orientation orientation)
{
    if (orientation == Orientation::SideA) {
        return Orientation::SideB;
    }
    if (orientation == Orientation::SideB) {
        return Orientation::SideA;
    }
    return orientation;
}

void OrientationTracker::begin(uint32_t nowMs)
{
    _filteredValid    = false;
    _instant          = Orientation::Unstable;
    _candidate        = Orientation::Unstable;
    _candidateSinceMs = nowMs;
    _orientation      = Orientation::Unstable;
}

Orientation OrientationTracker::update(uint32_t nowMs)
{
    if (M5.Imu.update() != m5::IMU_Class::sensor_mask_none) {
        const auto& data = M5.Imu.getImuData();
        const Vec3  raw  = accelToVec(data);

        const uint32_t dtMs = nowMs - _lastSampleMs;
        _lastSampleMs = nowMs;

        if (!_filteredValid) {
            _filtered[0] = raw.x;
            _filtered[1] = raw.y;
            _filtered[2] = raw.z;
            _filteredValid = true;
        } else {
            // Time-based EMA: alpha = dt / (tau + dt).
            float alpha = static_cast<float>(dtMs) / (kFilterTauMs + static_cast<float>(dtMs));
            if (alpha > 1.0f) {
                alpha = 1.0f;
            }
            _filtered[0] += alpha * (raw.x - _filtered[0]);
            _filtered[1] += alpha * (raw.y - _filtered[1]);
            _filtered[2] += alpha * (raw.z - _filtered[2]);
        }

        _instant = classify(Vec3{_filtered[0], _filtered[1], _filtered[2]});

        const float gyro = std::fabs(data.gyro.x) + std::fabs(data.gyro.y) +
                           std::fabs(data.gyro.z);
        if (gyro > kMotionDps) {
            // Being turned over: whatever the accelerometer says right now is
            // not a pose yet.
            _candidate        = Orientation::Unstable;
            _candidateSinceMs = nowMs;
        } else if (_instant != _candidate) {
            _candidate        = _instant;
            _candidateSinceMs = nowMs;
        }

            if (_candidate != _orientation && nowMs - _candidateSinceMs >= kStableMs) {
            hgLog("orientation: %s -> %s (accel %+.2f %+.2f %+.2f)",
                  orientationName(_orientation), orientationName(_candidate),
                  _filtered[0], _filtered[1], _filtered[2]);
            _orientation = _candidate;
        }
    }

    return _orientation;
}
