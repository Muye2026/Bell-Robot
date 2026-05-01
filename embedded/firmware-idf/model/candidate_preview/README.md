# Candidate preview for second-pass training

This folder is only for reviewing public candidate data before retraining.

## Confirmed direction

2026-05-01 confirmed previews:

- `candidate_overview_openverse_selected_v13.jpg`: seated person at desk / laptop / office candidates.
- `candidate_overview_openverse_emptydesk_v14.jpg`: empty desk / empty workstation candidates.

Constraints confirmed by user:

- Do not use non-commercial licenses.
- Do not use headshot-heavy webcam images.
- Do not use overhead hands / keyboard-only images.
- Prefer front or front-oblique half-body seated desk scenes.
- Include empty workstation negatives to reduce false countdown when no person is visible.

## Status

- These candidate files have not been added to `model/dataset`.
- `main/seat_model_data.h` has not been regenerated from these candidates.
- Keep generating preview sheets first; retraining requires explicit confirmation after the next usable batch is selected.
