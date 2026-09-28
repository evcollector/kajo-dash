# Project guidance for coding agents

## Shared UI component system

Before editing any LVGL screen, read `docs/ui-components.md`. It defines the
project's canonical buttons, navigation, panels, dialogs, text hierarchy,
spacing, colors, motion, translation behavior, and verification checklist.

- Use the named design tokens in `src/lvgl_app/ui_style.h`; do not copy raw
  menu colors, dimensions, or animation durations into new screen code.
- Reuse the canonical constructors listed in `docs/ui-components.md`. Do not
  hand-build a visually similar LVGL button, navigation control, dialog, or
  popup in a new location.
- Keep fixed menu chrome separate from each dashboard theme's customizable
  accent, background, and light/dark appearance.
- If the current component set cannot express a new design, extend the shared
  helper and document it instead of adding a one-off style implementation.
- Keep the visual component reference in `tools/layout_editor.html` synchronized
  whenever shared tokens or canonical component styling changes, so human
  designers and coding agents inspect the same design language.
- Add or update a 320x240 native-preview state and verify translated text for
  every material UI change.

## User-facing popup convention

All new compact, transient popup messages (toasts/notices) must reuse the
existing selector-housing motion and visual language. Do not introduce a
separate popup animation implementation.

- Use `slideSelectorPart(...)` from `src/lvgl_app/screens.cpp`.
- Use the shared `kSelectorSlideMs` duration (an alias of `cyd_ui::kMotionMs`,
  currently 140 ms) and its `lv_anim_path_ease_out` path.
- Match the Customize Theme / Reset Theme bottom housing: visible at
  `cyd_ui::kStatusToastVisibleY` and fully hidden below the 320x240 display at
  `cyd_ui::kStatusToastHiddenY` (currently global `y = 197` and `y = 244`).
  Use these tokens rather than the raw numbers.
- Create the popup in the hidden position first, call
  `lv_obj_update_layout(...)` and `lv_refr_now(NULL)`, and only then start its
  upward animation. This prevents LVGL from collapsing creation and movement
  into one frame and producing a visual pop.
- Keep the message visible for its intended timeout, animate it fully down to
  the hidden position, and delete it only after the downward animation has
  completed.
- Let `slideSelectorPart(...)` pause dashboard updates during the transition;
  do not animate the popup while expensive dashboard redraws compete with it.
- Take the styling from `showStatusNotice(...)` rather than restyling a notice:
  compact dark housing, orange outline, centered high-contrast text, and no
  nested/double border. Warning notices differ only in their longer timeout.
- Add a native-preview state for each new popup and verify it at 320x240.

The shared implementation is `showStatusNotice(...)` and
`statusToastTimerCb(...)` in `src/lvgl_app/screens.cpp`; add new messages to
its `StatusNotice` mapping rather than creating another toast object. Notice
strings are static program text (`lv_label_set_text_static`) so they do not
consume scarce LVGL heap on object-heavy screens. Add state-driven delivery
logic similar to `observeRideLoggingNotices()` when an operation completes
asynchronously. When a notice is raised just before a screen change, call
`queueStatusNotice(...)` instead; the next screen shows it through
`showPendingStatusNotice()` once it has been built, so the toast is not deleted
with the old screen.

Confirmation dialogs that require a user choice may remain modal and larger,
but any transient success, status, warning, or "already enabled" message must
follow the popup convention above.

## Pre-release compatibility policy

This project has no public releases or deployed user base. Firmware, desktop
tools, protocols, and internal data formats should move forward together.
Do not add compatibility shims for superseded prototype versions unless the
user explicitly requests one; prefer a clear version failure or a clean reset.

Revisit this section once a signed release is published, which `RELEASES.md`
records (a `v*` tag built by `kajo.bat` option 5, **R**): from then on,
settings, ride logs, and update protocols installed on real devices may need
an explicit migration path.
