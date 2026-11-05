# Contributing

`main` contains validated releases. `dev` is the integration branch.

1. Start each feature from current `dev` on `feature/<description>`. Use
   `fix/<description>` for a defect. Keep one concern per branch.
2. Use small conventional commits (`feat:`, `fix:`, `docs:`, `test:` or
   `build:`) with your configured Git author identity.
3. Run the checks in README.md. For camera changes, test both success and
   shutdown on hardware; distinguish data-path tests from image-quality and
   conferencing-client tests.
4. Open a pull request to `dev` and wait for all CI checks. Review the diff
   and retain a merge commit for the feature boundary.
5. Delete the completed feature branch locally and remotely after merging.
6. Promote `dev` to `main` through a release pull request after validation.
   Keep `dev`; do not delete it after a release.

Do not commit local handover notes, device recordings, credentials, build
outputs or machine-specific state. Preserve upstream history and notices.
Do not claim distribution, hardware or conferencing-client validation that
has not actually been performed.
