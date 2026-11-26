# Adding controls and filter types without breaking old projects

DAWs save SN Synth's state inside each project, and some (Logic, via Audio Units)
also record automation against each control's position. These rules keep old
projects opening and sounding the same.

## Controls (parameters)
1. **Never rename or reuse a parameter ID.** The ID string (e.g. `"f1_cutoff"`) is
   what a saved project and its automation point at. Change the display name freely.
2. **Never remove a parameter.** If a control is retired, keep it, hide it, and
   ignore its value (or convert it, see below).
3. **New parameters get the next version hint.** Every current parameter uses
   `ParameterID { id, 1 }`. Anything added in a later release uses
   `ParameterID { id, 2 }` (then 3, ...). Audio Units uses this number to keep
   automation lanes in place. Never lower a number.
4. **Pick the default so old projects sound unchanged.** A project saved before the
   control existed loads it at its default.
5. **Don't change a range or curve** of an existing control. If you must, bump the
   format version and convert the saved value.

## Filter types and other choice lists
- Only **append** to a choice list (such as the filter types). Never insert in the
  middle or reorder: saved projects store the position number.

## Saved-state format version
- `StateIDs::currentFormatVersion` (Parameters.h) is written into every save as
  `formatVersion`. Saves from before it existed read as 1.
- When the meaning of saved data changes (a range, the chain order, the drum
  pattern format, mod routings), bump the number and add the conversion in
  `setStateInformation`, keyed on `loadedFormatVersion`.
- A save from a *newer* build loads everything this build recognises and ignores
  the rest.

## Before releasing
Run the offline checks (`SNSynthTests`). The save-and-reload checks must still pass.
