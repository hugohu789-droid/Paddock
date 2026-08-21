# Irrigation model

When water goes on, how much, how much of it arrives, and why the schedule did
nothing on the days it did nothing.

Code: `core/include/paddock/core/Irrigation.hpp`, `core/src/Irrigation.cpp`.
The decision flow as a diagram is in the [system
architecture](../architecture/system-architecture.md#flow-3-the-irrigation-decision).

## Scope

Paddock models irrigation as a **management rule over a soil-water state**. It
represents when irrigation is triggered, how much is asked for, what the system
can deliver, how much reaches the root zone, and what that does to the pasture
over a season.

It does not model hydraulics: no pipes, no pump curves, no pivot geometry, no
pressure. A system is three numbers — a maximum depth, an efficiency, and a
return interval — and that is the whole of the machine.

## The policy

| Setting | Field | Unit | Meaning |
|---|---|---|---|
| Trigger | `trigger_depletion_fraction` | 0–1 | Water when depletion reaches this share of total available water |
| Target | `target_depletion_fraction` | 0–1 | Refill until only this share is still depleted |
| Maximum depth | `maximum_application_mm` | mm | Most the policy will put on at once |
| Return interval | `minimum_return_days` | days | Days that must pass before the same ground is watered again |
| Enabled | `enabled` | — | Off by default, so a rain-fed run is what you get without asking |

**The policy is written in depletion; the interface reads in what is left.** A
trigger of 0.5 is "water when half the available water is gone", which the panel
shows as *water below 50%*. The conversion happens in one place.

## The system

| Setting | Unit | Meaning |
|---|---|---|
| Maximum application | mm | The most this machine can put on in a day |
| Application efficiency | 0–1 | Share of what is put out that reaches the root zone |

## The decision, in order

For each cell, each morning, `decide_irrigation` applies these in sequence and
records a reason at every exit:

1. **Irrigation off** → *"irrigation is off"*
2. **Soil holds no available water** → *"the soil holds no available water to refill"*
3. **Return interval not elapsed** → *"watered too recently"*
4. **Depletion below the trigger** → *"the profile is still wetter than the trigger"*
5. Water wanted = depletion − target depth
6. **Nothing wanted** → *"the profile is already at the target"*
7. Cap by the policy's maximum depth, then by the system's
8. `applied = effective ÷ efficiency`, `pumped = applied × 10 m³/ha`

`effective_mm` is what enters the water balance. `applied_mm` is what has to
come out of the machine to deliver it. They differ by the efficiency, and
conflating them is how a water-use figure comes out wrong in the farm's favour.

## Units

One millimetre over one hectare is 10 m³, which is 10 tonnes of water. That is
arithmetic rather than a model: 0.001 m × 10 000 m². Paddock-level irrigation is
reported in **mm**; farm-level water use in **m³ or ML**. The two are never
mixed in one column.

## Explainability

The reason a cell was not watered exists only at the moment of the decision: by
the evening the soil has had the rain, lost the day's evapotranspiration and
been given any water that went on, so the state no longer implies the reason. A
paddock watered at 45% can finish the day at 84%.

So the schedule keeps two things from each morning: the available fraction it
read, and the first reason it held water back. The run carries both, and the
inspector prints them. Nothing downstream re-evaluates a threshold.

## Conservation

Irrigation enters the water budget as an inflow, and the ledger closes over a
year like any other flux. The tally counts events, the mean depth of an event,
and the water pumped for the farm's area.

## Assumptions and limitations

- Uniform application across a watered cell
- No delivery constraint beyond the daily depth: no allocation limit, no consent
  volume, no source that can run dry
- No cost model — water is free in the current model, so an irrigated scenario
  looks better than it would to an accountant
- Efficiency is a single number, not a function of wind, temperature or method

## Tests

- `tests/unit/` — the decision at each guard, and the mm to m³ arithmetic
- `tests/config/PaddockInspectionTest.cpp` — the recorded reason travels to the
  inspector, and a day with no morning figure gets no explanation rather than a
  reconstructed one
- CI runs an irrigated year end to end and checks the pivots animate on a day
  that was actually watered
