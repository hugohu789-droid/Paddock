# Backlog

What is built, what is missing, and the order to build it in. CLAUDE.md holds
the milestone plan; this file holds the work items and says which of them the
simulation cannot close its loop without.

Two things are tracked separately from the rest and must not be confused with
them. **Evidence-blocked** items are waiting on a published source, not on
code — writing the code without the source would produce a result with a number
and a chart and nothing behind it. **Known-wrong** items are implemented but
simplified in a way that makes some output wrong in a stated direction.

Last reviewed: 18 August 2026.

## The loop, and where it is open

The simulation is a loop, and it is currently cut in two places:

```
  weather ─→ pasture grows ─→ mob eats ─→ mob's weight changes
                  ↑                              │
                  │                          (2) WIRED
            paddock rests                        ↓
                  ↑                        farmer moves mob
                  └───── (1) NOT WIRED ─────────┘
```

Gap (2) closed with B1. One left.

Everything else is in place. The pasture model grows and senesces on light,
temperature, water and nitrogen; the terrain model varies radiation by slope and
aspect; the animal model turns liveweight, age, diet and ground into an intake;
and as of the grazing work that intake comes off the sward with the budget
closing. What is missing is the feedback from being underfed, and the farmer who
moves stock — and without those two, a grazing system has no consequence and the
farm cannot be run for a year.

**Neither gap is blocked on evidence.** Both are mechanism. That matters,
because the items that *are* evidence-blocked (grazing distribution and
selectivity, below) turn out not to sit on the critical path: a closed loop can
graze a paddock uniformly. Spatial distribution within a paddock is a refinement
of a working loop, not a precondition for one.

## MVP: a farm that runs a year

The target is M3's acceptance, minus the parts the user has deferred: *load a
farm, run a full year, watch a stable grazing–regrowth loop*. In order.

| # | Item | Why it is on the critical path | Size |
|---|---|---|---|
| ~~B1~~ | ~~**Liveweight responds to intake**~~ | **Done.** Gap (2) is closed: `liveweight_response` inverts the requirement calculation and `advance_one_day` applies it, so an underfed mob loses weight | small |
| ~~B2~~ | ~~**Species definitions in TOML**~~ | **Done.** `SpeciesConfig` loads an animal class straight into the parameters the energy model uses, and `data/species/` is discovered by scanning | small |
| ~~B3~~ | ~~**A farm that owns paddocks, swards and mobs**~~ | **Done.** `core/Farm` holds the grid, the mask, the paddocks and the mobs, and steps a day. **The ground stays per cell**: a paddock is a set of cells, not a unit of pasture, so a shallow corner still dries out on its own and the map view can show it | large |
| B4 | **The farmer moves mobs** | Closes gap (1). Reads the grazing calendar, picks the next paddock by rest and cover, respects the graze-length and spell rules. The "shuffle" emerges here when paddocks are too few | medium |
| ~~B5~~ | ~~**Conservation across the whole farm**~~ | **Done with B3.** `FarmConservationTest` closes all three budgets to 1e-9 over 365 grazed days with two mobs, and has a negative control that fails when an offtake goes unrecorded | medium |
| B6 | **A year-long scenario that demonstrates the loop** | The acceptance artefact: one bundle, one command, a stable grazing–regrowth cycle with cover and liveweight series out | small |

One decision inside B3 is worth recording because everything downstream inherits
it: **what a mob eats is spread over its paddock's cells in proportion to what
each has above its residual.** Stock take more from the parts of a paddock that
carry more feed. That is an assumption about where the feed is, not a claim
about where animals walk — B16 would refine it — and it is the honest default
because it follows from the pasture rather than from a guess about behaviour.

Reaching B6 is the MVP. It does not need the 3D view, polygon editing, or real
LINZ boundaries — all three are M3 items but none of them changes whether the
simulation closes.

## After the MVP, in rough order

### Finishing M3

| # | Item | Note |
|---|---|---|
| B7 | Paddock drawing and editing on the 2D map | Task #22, deferred by the user. The file format it will read and write already exists and is tested, so this is an editor for a settled format, not a new way to define a farm |
| B8 | Real farm boundaries from LINZ | Open item 8. All three example farms ship with generated rectangles and locality coordinates marked `location_verified = false`. Switching one over is a change to its `[boundary]` section |
| B9 | 3D terrain view | Task #26. DEM relief, pasture colouring, livestock glyphs |

### Making the livestock model honest

These are gaps in what is already built, listed in `verify.md`. Each one makes
a specific output wrong today.

| # | Item | What is wrong without it |
|---|---|---|
| B10 | Lactation | There is no way to represent a cow in milk. A "dairy cow" in the model is a dry cow and eats about half what a milking one does |
| B11 | Dung and urine return | Eaten nitrogen leaves the modelled system. The budget closes, but a multi-year run strips the farm of nitrogen in a direction a real farm does not |
| B12 | Pregnancy | No cost of carrying a calf or lamb, so late-gestation demand is absent |
| B13 | Wool and velvet | Sheep and deer are cheaper to keep than they are |
| B14 | Pre-weaned animals | The milk factor is fixed at 1, which is only correct after weaning |
| B15 | Cold, heat, wet and wind | Weather costs the pasture but never the animal |

### Evidence-blocked

Not scheduled, because the work cannot honestly start until the source exists.

| # | Item | What is missing |
|---|---|---|
| B16 | Grazing distribution over a paddock | Open item 9. The *energy cost* of walking a slope is sourced (TMC Eq. 23) and implemented. The *preference* that follows — stock avoiding steep ground — is not. Gillingham et al. (1998) give pasture production by slope and aspect, not the animals' distribution over it |
| B17 | Grazing selectivity | Open item 10. Smith and Dawson (1976) state the direction — set stocking overgrazes clover — but not the magnitude. Until it exists, a system comparison cannot show the species-composition half of their finding |

Standard reference weight by breed and class is a third: `EVG` needs it, and it
sits in a TMC chapter not yet retrieved. It blocks B2's data files from being
complete rather than blocking code.

### Beyond M3

M4 (pests, diseases, environmental metrics), M5 (management layer, delivery) and
M6 (AI features) are described in CLAUDE.md and not restated here. One item is
worth pulling forward into the reader's view because it is already half-built:
the grazing calendar can express a management plan, and M5's scenario editor is
what lets a farmer write one without touching TOML.

## Cross-cutting

| # | Item | Note |
|---|---|---|
| B18 | Grazing calendar in TOML | The calendar can only be built in code today. This is the last step to putting the plan in a farmer's hands, and it is what B7's editor would ultimately write |
| B19 | Open-Meteo weather adapter | Default source, agreed earlier; VCSN as an option with its licence unverified |
| B20 | Ship `proj.db` with the Windows package | Otherwise a packaged build cannot do NZTM |
| B21 | Dataset and document licences | Open item 7. Gates what may be redistributed with a release, and now also covers the OVERSEER manual's no-promotion clause |

## How to read the sizes

`small` is a session: one header, one source file, one test suite. `medium` adds
a design decision worth writing down first. `large` means the shape is not
obvious yet and the first task is to decide it — B3 is the only one, and it is
large because it has to hold paddocks, swards, mobs and a calendar together
without any of them learning about the others.
