/**
 * @file pose_test.cpp
 * @brief Pose and Photo Studio: framings fill the frame they promise, from
 *        the side they promise; saved shots follow the avatar; poses step and
 *        wrap; shoot files are validated.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/pose/studio.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace sw67::pose;
using Catch::Approx;

namespace
{
constexpr double kPi = 3.14159265358979323846;

Subject standing(double yaw = 0.0)
{
    Subject s;
    s.position = {128.0, 64.0, 25.0};
    s.yawDeg = yaw;
    s.height = 2.0;
    return s;
}

// The vertical extent the camera sees at the focus distance.
double visibleSpan(const CameraPose& c, double fovDeg)
{
    return 2.0 * c.distance() * std::tan(fovDeg * kPi / 360.0);
}
} // namespace

TEST_CASE("Each framing shows the part of the avatar it names", "[pose]")
{
    const Subject s = standing();
    for (const Framing& f : builtinFramings())
    {
        const CameraPose c = frame(f, s, 60.0);
        CHECK(visibleSpan(c, 60.0) == Approx(f.spanFraction * s.height).epsilon(0.01));
        const double feet = s.position.z - s.height / 2.0;
        CHECK(c.focus.z == Approx(feet + f.centreFraction * s.height));
        CHECK(c.distance() >= 0.3);
    }
    const CameraPose head = frame(*findFraming("headshot"), s, 60.0);
    const CameraPose full = frame(*findFraming("full"), s, 60.0);
    CHECK(head.distance() < full.distance());
    CHECK(head.focus.z > full.focus.z);
}

TEST_CASE("Framings are taken from the side they say, whichever way the avatar faces", "[pose]")
{
    for (double yaw : {0.0, 90.0, 217.0})
    {
        const Subject s = standing(yaw);
        const double fx = std::cos(yaw * kPi / 180.0), fy = std::sin(yaw * kPi / 180.0);
        const CameraPose front = frame(*findFraming("half"), s, 60.0);
        CHECK((front.eye.x - front.focus.x) * fx + (front.eye.y - front.focus.y) * fy > 0.0);
        const CameraPose back = frame(*findFraming("back"), s, 60.0);
        CHECK((back.eye.x - back.focus.x) * fx + (back.eye.y - back.focus.y) * fy < 0.0);
        const CameraPose left = frame(*findFraming("profile_left"), s, 60.0);
        // The avatar's left is +90 degrees from where it faces.
        CHECK((left.eye.x - left.focus.x) * -fy + (left.eye.y - left.focus.y) * fx > 0.0);
    }
    const CameraPose low = frame(*findFraming("low_hero"), standing(), 60.0);
    CHECK(low.eye.z < low.focus.z);
    const CameraPose high = frame(*findFraming("high"), standing(), 60.0);
    CHECK(high.eye.z > high.focus.z);
}

TEST_CASE("A saved shot repeats wherever the avatar stands and faces", "[pose]")
{
    const Subject here = standing(30.0);
    const CameraPose taken{{130.0, 66.5, 26.2}, {128.2, 64.1, 25.6}};
    const Shot shot = shotFromCamera("Over the shoulder", taken, here, 45.0);
    const CameraPose again = placeShot(shot, here);
    CHECK(again.eye.x == Approx(taken.eye.x));
    CHECK(again.eye.y == Approx(taken.eye.y));
    CHECK(again.focus.z == Approx(taken.focus.z));

    Subject elsewhere = standing(120.0);
    elsewhere.position = {10.0, 200.0, 40.0};
    const CameraPose moved = placeShot(shot, elsewhere);
    CHECK(moved.distance() == Approx(taken.distance()));
    CHECK(moved.eye.z - elsewhere.position.z == Approx(taken.eye.z - here.position.z));
}

TEST_CASE("Poses step forwards and back and wrap", "[pose]")
{
    Photoshoot shoot;
    CHECK_FALSE(stepPose(shoot, std::nullopt, 1));
    shoot.poses = {{"a", "One"}, {"b", "Two"}, {"c", "Three"}};
    CHECK(stepPose(shoot, std::nullopt, 1) == std::size_t{0});
    CHECK(stepPose(shoot, std::nullopt, -1) == std::size_t{2});
    CHECK(stepPose(shoot, std::size_t{2}, 1) == std::size_t{0});
    CHECK(stepPose(shoot, std::size_t{0}, -1) == std::size_t{2});
    CHECK(stepPose(shoot, std::size_t{9}, 1) == std::size_t{0});
}

TEST_CASE("A photo shoot round-trips and bad entries are dropped", "[pose]")
{
    Photoshoot s;
    s.name = "Spring lookbook";
    s.poses = {{"00000000-0000-4000-8000-000000000001", "Lean"}};
    Shot shot;
    shot.name = "Wide";
    shot.eyeOffset = {3.0, 0.5, 0.4};
    shot.focusOffset = {0.0, 0.0, 0.3};
    shot.fovDeg = 50.0;
    shot.poseId = s.poses[0].itemId;
    s.shots = {shot};
    const ReadResult back = readPhotoshoot(writePhotoshoot(s));
    REQUIRE(back.shoot);
    REQUIRE(back.shoot->shots.size() == 1);
    CHECK(back.shoot->shots[0].eyeOffset.x == Approx(3.0));
    CHECK(back.shoot->shots[0].poseId == shot.poseId);
    CHECK(back.shoot->poses.size() == 1);

    const ReadResult bad = readPhotoshoot(R"({"format":"sw67.photoshoot","version":1,
        "poses":[{"itemId":"nope","name":"X"}],
        "shots":[{"name":"far","eye":[5000,0,0],"focus":[0,0,0]},{"name":"short","eye":[1,2],"focus":[0,0,0]}]})");
    REQUIRE(bad.shoot);
    CHECK(bad.shoot->poses.empty());
    CHECK(bad.shoot->shots.empty());
    CHECK(bad.notes.size() == 3);
    CHECK_FALSE(readPhotoshoot(R"({"format":"sw67.photoshoot","version":2})").shoot);
}

TEST_CASE("Snapshot names use the shoot's pattern and stay safe", "[pose]")
{
    Photoshoot s;
    s.name = "Spring/Look";
    CHECK(snapshotStem(s, "Wide", 7, "2026-11-17") == "Spring_Look_Wide_007");
    s.namePattern = "{date}-{shot}";
    CHECK(snapshotStem(s, "Close", 1, "2026-11-17") == "2026-11-17-Close");
}
