# Contributor License Agreement

> **Status: placeholder. Not legal advice, and not yet in force.**
>
> `.github/workflows/cla.yml` links contributors to this page, so it has to
> exist for the gate to make sense. The text below states the *intent* of the
> agreement in plain English. Replace it with wording reviewed by a New Zealand
> lawyer before asking anyone to sign, and before relying on it for a
> dual-licensing arrangement. Do not treat a signature collected against this
> draft as a signature against the final agreement.

## Why this exists

Paddock is released under GPL-3.0-or-later. Offering the same code under
different, commercial terms is only possible for a party that holds or has been
granted the necessary rights in *every* contribution. Without an agreement from
each contributor, a single external patch merged into the tree removes that
option permanently.

## What it is intended to say

- The contributor keeps copyright in their contribution.
- The contributor grants Gejile Hu a perpetual, worldwide, irrevocable licence
  to use, reproduce, modify and distribute the contribution, **including under
  licence terms other than the GPL**.
- The contributor grants a patent licence covering their contribution.
- The contributor confirms that the contribution is their own work, and that
  they have the right to grant it — in particular that it is not owned by an
  employer who has not agreed.
- The contribution is provided without warranty.

## How to sign

Comment on your pull request:

```
I have read the CLA Document and I hereby sign the CLA
```

The gate records the signature outside this repository, so signatures never
enter the source history.

## The gate's own shelf life

`.github/workflows/cla.yml` uses
[`contributor-assistant/github-action`](https://github.com/contributor-assistant/github-action),
pinned to v2.6.1. Two facts worth writing down rather than rediscovering:

- **The action is archived.** Its last push was 2026-03-23 and it accepts no
  further changes. It works, and its job is small enough that it is likely to
  keep working — but when a GitHub Actions runtime change eventually breaks it,
  nobody will fix it. The exits are the hosted service at cla-assistant.io, or
  replacing it with a short workflow of our own; the signature file format is
  plain JSON, so signatures already collected are not trapped.
- **`cla-assistant/github-action` is the old name.** GitHub still redirects it,
  which is why the first version of the workflow appeared to work.
- **It is falling off the runtime.** `v2.3.0` declares `using: node16` and
  `v2.6.1` declares `using: node20`; both are deprecated, and GitHub currently
  forces them onto Node 24 while printing a warning. Pinning v2.6.1 buys the
  newer of the two, but an archived action will never declare Node 24, so the
  day GitHub stops forcing is the day this gate stops working. That is the
  deadline to have replaced it by, and it is not a surprise.

## Setting the gate up again from scratch

Signatures are stored in the separate private repository
`Paddock-cla-signatures`, so they stay out of this project's history. Three
things have to be true before the gate can pass, and each of them failed once
on the way in — each with a message that pointed somewhere else:

1. **`CLA_SIGNATURES_TOKEN` is set on `Paddock`, not on the signatures
   repository.** Actions reads secrets from the repository the workflow runs
   in. A token stored beside the signatures is invisible to it, and the symptom
   is `PERSONAL_ACCESS_TOKEN:` logging as empty. A fine-grained token with
   *Contents: Read and write* on the signatures repository is enough; the
   README's "repo scope" describes the older classic token, which would grant
   far more.
2. **The signatures repository has at least one commit.** A repository created
   and never written to has no branches at all, whatever its configured default
   branch says, and the API answers `This repository is empty`.
3. **`signatures/version1/cla.json` exists, containing `{"signedContributors":
   []}`.** The action does not create it when the store is a remote repository.
   Until it exists the run fails with
   `Could not retrieve repository contents. Status: 404` — indistinguishable at
   a glance from a token that cannot see a private repository, which is what it
   looks like first.

## Open points to settle with counsel

- Whether a copyright **assignment** or a broad **licence** is the right
  instrument here. A licence is the lighter touch and is what the text above
  assumes; an assignment is stronger and harder to ask for.
- Whether a separate corporate CLA is needed for contributions made in the
  course of employment.
- Which jurisdiction governs, and how that interacts with contributors outside
  New Zealand.
- Whether the project should instead adopt an existing, widely reviewed
  agreement (for example the Apache Individual CLA, adapted) rather than a
  bespoke one.
