# Triage

How pull requests and issues on Vikey are labelled, checked and closed. The board is public: every comment is
written for the person who opened the PR or issue.

## Labels

**Area** (`area:` labels), by the files a PR changes or the app an issue is about. A PR or issue can have several.

| Label | Covers |
|---|---|
| `area:macos` | `App/`, `project.yml`, the Mac app |
| `area:windows` | `Windows/` |
| `area:linux` | `Linux/` |
| `area:ios` | `iOS/` |
| `area:android` | `Android/` |
| `area:shared` | changes the build of every platform (macOS, iOS, Windows, Linux, Android): `Engine/` |
| `area:tooling` | changes that leave the app builds as they are: `Tools/` (tests, simulations, release scripts), `.github/` (CI), the site, docs |

**Kind**, on PRs and issues:

| Label | Meaning |
|---|---|
| `correctness` | Something works wrong and should be fixed: a crash, wrong output, lost text, a security or privacy problem. |
| `taste` | A design or behaviour choice: what a word should become (`loom` → `lôm` or `loom`), whether auto switch should trigger, how something looks. The maintainer decides these. When it is unclear whether something is a bug or a choice, it is `taste`. |

**Reproduction**, on bug reports:

| Label | Meaning |
|---|---|
| `reproducible` | Reproduced on the released build, or confirmed by reading the current code. |
| `not-reproducible` | A reproduction was actually attempted on the released build and the bug did not happen. A comment says what was tried and asks for what is missing. |
| `outdated` | Already fixed or no longer applies. |

No reproduction label is added when no attempt was made, for example when it needs hardware, a game or an app that is not available, or more than medium effort. Feature requests also get no reproduction label.

## Issues

1. Read the issue and its comments. Check whether a merged PR or a release already fixed it.
2. Close it, with a short comment, when it is already fixed (say which version), a duplicate (link the original),
   spam, or not about Vikey.
3. Otherwise add the `area:` labels and `correctness` or `taste`.
4. Try to reproduce bugs with low to medium effort, on the **released** build of the app: the Windows and Linux
   builds from vikey.app in a virtual machine, the Mac app, the iOS simulator or the Android emulator. Reading the
   current code to confirm a reported cause also counts. Add `reproducible` or `not-reproducible` only after an
   actual attempt; without one, leave the reproduction label off.
5. When something is missing (version, steps, which app), ask the reporter in a comment, in the language they wrote in.
6. `taste` issues are left for the maintainer. No fix is proposed or merged for them without the maintainer's decision.

## Pull requests

1. Add the `area:` labels and `correctness` or `taste`.
2. Review the diff: correctness, edge cases, consistency with [DECISIONS.md](DECISIONS.md) and the platform code,
   and anything security-sensitive (`.github/`, `Tools/release/`, signing, the updater, code that downloads or
   runs other code). Then approve, comment, or request changes.
3. A PR that adds a feature or changes behaviour (what is typed, when auto switch triggers, what the app shows)
   must include a screenshot or a short demo video of the new behaviour. Without one, ask for it in a comment and
   request changes; the PR is not approved until it is there.
4. Code from contributors is read, never run on maintainer machines. CI for a PR from a fork is approved to run when
   the PR is triaged: it runs on GitHub-hosted runners, without secrets and with a read-only token.
5. CodeRabbit reviews every PR automatically ([.coderabbit.yaml](.coderabbit.yaml)), and a bot comment compares the
   typing engine's test and simulation metrics with `main` ([.github/workflows/pr-report.yml](.github/workflows/pr-report.yml)).
6. Only the maintainer merges.
