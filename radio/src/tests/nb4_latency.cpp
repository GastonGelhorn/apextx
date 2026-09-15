/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The control-latency accounting. The simulator's microsecond tick never
// advances, so these tests drive a clock of their own and check what the
// statistics do with it: which cycles count, which are discarded, and that
// the mean keeps following the radio instead of freezing.

#include "gtests.h"

#if defined(RADIO_NB4_FAMILY)

#include "nb4_latency.h"

namespace {

uint32_t fakeNow = 0;
uint32_t fakeClock() { return fakeNow; }

struct Clock {
  Clock()
  {
    fakeNow = 0;
    nb4LatencySetClock(fakeClock);
    nb4LatencyReset();
  }
  ~Clock()
  {
    nb4LatencySetClock(nullptr);
    nb4LatencyReset();
  }
};

// One mixer cycle that sampled the controls and sent them `us` later.
void cycle(uint32_t us)
{
  nb4LatencySampled();
  nb4LatencyCarriesChannels();
  fakeNow += us;
  nb4LatencySent();
}

}  // namespace

TEST(Nb4Latency, MeasuresSamplingToHandoverForCyclesThatCarryChannels)
{
  Clock clock;
  cycle(120);
  cycle(80);
  cycle(220);

  const auto stats = nb4LatencyRead();
  EXPECT_EQ(stats.frames, 3u);
  EXPECT_EQ(stats.lastUs, 220);
  EXPECT_EQ(stats.minUs, 80);
  EXPECT_EQ(stats.maxUs, 220);
  EXPECT_EQ(stats.averageUs, (120 + 80 + 220) / 3);
  EXPECT_EQ(stats.ignored, 0u);
}

TEST(Nb4Latency, ACycleThatSendsSomethingElseIsNotTimed)
{
  Clock clock;
  cycle(100);

  // A configuration or status cycle: sampled, sent, no channel positions.
  nb4LatencySampled();
  fakeNow += 4000;
  nb4LatencySent();

  const auto stats = nb4LatencyRead();
  EXPECT_EQ(stats.frames, 1u);
  EXPECT_EQ(stats.maxUs, 100) << "a status frame must not inflate the maximum";
  EXPECT_EQ(stats.averageUs, 100);
}

TEST(Nb4Latency, AnImplausibleMeasurementIsCountedButNotRecorded)
{
  Clock clock;
  cycle(150);
  cycle(Nb4LatencyPlausibleUs + 1);

  const auto stats = nb4LatencyRead();
  EXPECT_EQ(stats.frames, 1u) << "the outlier must not count as a frame";
  EXPECT_EQ(stats.ignored, 1u);
  EXPECT_EQ(stats.maxUs, 150) << "one preemption would own the maximum forever";
  EXPECT_EQ(stats.averageUs, 150);
}

TEST(Nb4Latency, SendingWithoutHavingSampledRecordsNothing)
{
  Clock clock;
  nb4LatencyCarriesChannels();
  fakeNow += 300;
  nb4LatencySent();

  EXPECT_EQ(nb4LatencyRead().frames, 0u);
}

TEST(Nb4Latency, AWrappedTickStillYieldsTheElapsedTime)
{
  Clock clock;
  fakeNow = 0xFFFFFFF0;
  nb4LatencySampled();
  nb4LatencyCarriesChannels();
  fakeNow += 200;  // wraps through zero
  nb4LatencySent();

  const auto stats = nb4LatencyRead();
  EXPECT_EQ(stats.frames, 1u);
  EXPECT_EQ(stats.lastUs, 200);
}

TEST(Nb4Latency, TheMeanKeepsFollowingTheRadioOverALongSession)
{
  Clock clock;
  // A long run at one figure, then a lasting change. A mean that never
  // renormalises would barely move; this one has to converge.
  for (uint32_t i = 0; i < 200000; ++i) cycle(100);
  EXPECT_EQ(nb4LatencyRead().averageUs, 100);

  for (uint32_t i = 0; i < 2000000; ++i) cycle(300);
  const auto stats = nb4LatencyRead();
  EXPECT_GT(stats.averageUs, 290) << "the mean stopped following the radio";
  EXPECT_LE(stats.averageUs, 300);
  EXPECT_EQ(stats.minUs, 100) << "the minimum is the session's, not the window's";
}

TEST(Nb4Latency, ResetClearsEverything)
{
  Clock clock;
  cycle(100);
  nb4LatencyReset();

  const auto stats = nb4LatencyRead();
  EXPECT_EQ(stats.frames, 0u);
  EXPECT_EQ(stats.lastUs, 0);
  EXPECT_EQ(stats.minUs, 0);
  EXPECT_EQ(stats.maxUs, 0);
  EXPECT_EQ(stats.averageUs, 0);
  EXPECT_EQ(stats.ignored, 0u);
}

#endif  // RADIO_NB4_FAMILY
