# Plan 141: reliable manual firmware CI

## Goal

Make a manual PlatformIO workflow run execute the complete firmware matrix.
Manual runs do not have a pull request base or push-before SHA.

## Implementation

- Treat `workflow_dispatch` as an explicit full firmware build.
- Keep changed-path filtering for pull request and push events.
- Compare a new branch push against Git's empty tree when its before SHA is
  absent. This produces one valid diff base even in a repository with multiple
  root histories.
- Extend the structural workflow checker with regressions for both rules.

## Verification

- Run the workflow checker unit tests.
- Run the workflow checker against every current workflow.
- Dispatch PlatformIO CI on the branch and confirm firmware jobs are created.
