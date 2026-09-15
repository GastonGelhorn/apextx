# Control latency

The radio measures its own contribution to control latency and shows it in
System > Diagnostics, as minimum, average and maximum microseconds. That page
also measures the touch path from the panel interrupt to the first frame
presented after the press.

## What the number is

The time from the mixer cycle reading the wheel and the trigger to the frame
carrying those positions being handed to the module's UART.

That is the whole of the path the firmware owns:

1. The analogue inputs are read.
2. The mixes, curves, travel, trims and brake handling are evaluated.
3. The channel frame is built.
4. The buffer is handed to the module port.

All four happen inside one mixer cycle, in that order, with nothing buffered
between them. The mixer is scheduled on the AFHDS3 cadence, so a cycle begins
every 5 ms and the frame it produces leaves in that same cycle.

## What the number is not

It stops where the firmware's knowledge stops. It does not include:

- The time the frame spends on the wire. The link runs at 1.5 Mbaud 8N1, so a
  frame of *n* bytes takes about `n * 10 / 1.5` microseconds: tens of
  microseconds for a channel frame. Deterministic, and computable rather than
  measured.
- The internal module, which is a black box with its own scheduling.
- The radio link and the receiver.
- The servo, usually the largest single term in the whole chain and entirely
  outside the radio.

So this is not end-to-end latency, and a figure of, say, 200 microseconds does
not mean the car responds in 200 microseconds. Measuring the whole chain needs
an oscilloscope or a logic analyser: probe a servo output at the receiver and
trigger on the mixer cycle, which toggles a GPIO when the firmware is built
with `DEBUG_MIXER_SCHEDULER`.

## Where the time actually goes

The floor is not the code, it is the frame cadence. A control moved just after
a frame leaves waits for the next one, so the cadence alone contributes up to
5 ms, and 2.5 ms on average. Everything measured here sits inside that.

Lowering the cadence is not a free improvement: it is the rate the module and
receiver expect, and `docs/nb4/rf/qualification.json` records it as the
reviewed value. Treat it as fixed unless there is evidence from the module
itself.

One thing that could have added latency and does not: the jitter filter on the
analogue inputs. It is a moving average, but it passes any change larger than
ten ADC counts straight through, so it only smooths the tremble of a control
that is being held still. Real movement is unfiltered.

## Reading the figure

- **Minimum and maximum** are for the session, from power on. They are not
  reset by leaving the page.
- **Average** follows the radio: the running sum is halved periodically, so a
  change in behaviour shows up instead of being drowned by history.
- A measurement longer than one mixer period is discarded rather than
  recorded, because it means the task was preempted and one such outlier would
  own the stable maximum for the rest of the session. The `>5 ms` row shows
  both the number of discarded samples and the unfiltered raw maximum, so a
  real scheduling stall is visible instead of being silently hidden.
- Cycles that send configuration or poll status instead of channel positions
  are not timed at all.

## Touch response

The **Touch panel** row is also in microseconds and shows last, average and
maximum IRQ-to-present time. It includes the touch-controller read, event
dispatch, LVGL processing, drawing and the wait for the panel's vertical blank.
It therefore reflects what the interface itself can improve, rather than only
the I2C transaction.

The figure does not include the panel's analogue scan before its interrupt, or
LCD pixel response after presentation. A press whose next frame takes more than
250 ms is rejected as an interrupted/debug session rather than allowed to own
the maximum indefinitely. The counter is retained internally for tests and
diagnosis.

NB4 polls LVGL input every 20 ms, matching the interface/display task cadence.
The FT6236 `press down` event is accepted immediately as contact. A failed I2C
read keeps the last stable touch state, retries with bounded backoff, and cannot
turn uninitialised bytes into a phantom tap.

## Keeping it honest

`radio/src/tests/nb4_latency.cpp` covers the accounting: which cycles count,
which are discarded, raw stalls, touch presentation, tick wrap-around, and that
the averages converge. The simulator's microsecond tick does not advance, so
those tests drive a clock of their own; the physical figures can only be read
on the radio.
