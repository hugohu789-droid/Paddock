# Flagship demo script — irrigation off against irrigation on

Sixty to ninety seconds, one farm, one decision variable. Every figure below is
what the shipped bundles produce; none of it is typed in.

**What this demonstration claims, in one sentence:** Paddock lets you compare a
controlled farm-system scenario, see what changed, and inspect why the model
produced the result.

It does **not** give farm advice, and it does not recommend an optimum. Say so
out loud if the question comes up — the answer is better than the question.

---

## The path

Run the whole thing from one command, and let the page do the talking:

```bash
paddock dashboard data/scenarios/demo-irrigation-off data/scenarios/demo-irrigation-on
```

### 1. The decision question — 10 seconds

> "This is a 200-hectare Canterbury sheep block, run for the 2023-24 year on
> real recorded weather. The question is the one a consultant actually gets:
> what would irrigation do here?"

### 2. The baseline — 10 seconds

> "This is the farm rain-fed. It grew **6,613 kg of dry matter a hectare**, and
> it spent **233 days** of the year with growth limited by water."

### 3. The alternative — 10 seconds

> "This is the same farm with a centre-pivot rule: water when the soil drops to
> 40% of available water, refill to 85%, 25 mm at a time."

### 4. What Changed? — 15 seconds

*Point at the top block of the page.*

> "Before any result, the tool says what it changed. **One category differs:
> irrigation.** Run, farm, ground, weather, soil, pasture, stock, grazing
> policy, money and rules are all identical, and that is checked by hashing the
> input files rather than by anybody's assurance. So whatever the results show
> is attributable to the water and to nothing else."

### 5. The outcome differences — 25 seconds

*The four that carry evidence. Read the right-hand column out loud — it is the
point.*

| | Off | On | |
|---|---|---|---|
| **Irrigation response** | 1.00 | **1.79 × rain-fed** | benchmarked |
| **Pasture grown** | 6,613 | **11,846 kg DM/ha** | calibrated |
| **Dry matter per mm** | 12.5 | **14.0 kg DM/ha/mm** | calibrated |
| **Growth-limited days** | 233 | **15 days** | conserved |

> "The response ratio is the one to look at. **1.79 times** — and that is not
> the model marking its own homework. AgResearch's Winchmore trial ran dryland
> against irrigated for twenty-five years; across fifty treatment-years the
> response ranges from 1.25 to 2.40, and this sits at the **48th percentile** of
> them. The dry matter per mm is 14.0 against Martin's Canterbury ceiling of
> about 20."

### 6. Inspector — why, not just what — 15 seconds

*Pick a day in February on the timeline, then a paddock.*

> "Every number here is explainable. Pick a dry day and the Inspector shows the
> soil water on the morning the decision was made, the trigger it was compared
> against, what the rule decided, and what the pasture then did with it. It is
> reading what the model recorded, not recomputing a story afterwards."

### 7. The validation boundary — 10 seconds

*Point at the confidence column, and at "Not on this page".*

> "Every row says how far it can be trusted, and the page carries a list of what
> it deliberately leaves off. **Animal production, stocking, feed bought and
> money are not on this page** — they are the least validated part of the model
> and they are marked exploratory wherever they do appear. What is on the page
> is grass and water, which is what this model has been checked on."

### 8. Closing — 5 seconds

> "Same bundle, same seed, byte-identical result — so anything you see here, you
> can hand to somebody else and they will get the same thing. Paddock lets you
> compare a controlled scenario, see what changed, and inspect why."

---

## If you are asked

**"So should this farm irrigate?"**
> "That is a question about capital, consent and water cost, and this model
> carries none of those. What it can tell you is the pasture response, and how
> that compares with a measured trial."

**"What is the stocking rate you would run?"**
> "Not something to take from this. Stocking sits downstream of the animal
> model, which is the part still marked exploratory."

**"Why is the lowest cover marked differently?"**
> "It is exploratory rather than calibrated. It depends on how much the stock
> ate, and the ewe intake model has an open evidence question — the tool says so
> rather than letting the number pass."

---

## Before you present

- `paddock dashboard data/scenarios/demo-irrigation-off data/scenarios/demo-irrigation-on`
  should print **What changed** with `Irrigation` as the only changed category.
- No `!!` warning block should appear. If one does, the run has left the
  supported animal-condition range and the animal and money figures on that page
  are not to be read - stop and check the bundle has not been edited.
- A desktop build draws the farm on its real surveyed ground. A core-only build
  prints a line saying it is drawing the farm flat; the pasture and water
  figures are unaffected, but do not present the 3D view from that build.
