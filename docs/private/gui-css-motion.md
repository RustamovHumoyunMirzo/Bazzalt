# Runtime CSS and motion

RuntimeCSS and InlineCSS live in GuiElement as ephemeral per-instance layers. They are deliberately absent from component serialization; StyleProperties remains the saved @require input map. Public GUI/GuiCSS wrappers dispatch edits through GUI module ABI version 7. The core never links the CSS parser. Keep C++ and Lua bindings/reference inventories synchronized.

ModuleEditCSS validates the complete candidate before publishing. Property setters require exactly one declaration, rule edits exactly one rule, keyframe edits exactly one at-rule. Top-level replacements use lexer byte offsets to retain unrelated source/comments; nested condition blocks require stylesheet replacement. Reads of the runtime layer do not silently merge the shared asset. No imports, arbitrary filesystem paths, network access or eval are introduced.

PrepareStyles adds runtime sheets to the owning subtree scope and inline rules to one internal entity state. Inline normal specificity is above authored normal selectors; !important remains a separate priority. Motion is applied to the resulting cascade before layout and paint. Motion state belongs to GuiSystem and is pruned for deleted/unstyled entities. Multiple renders at the same Time::GetTime do not advance it. Runtime sheets have a bounded parse cache; never cache mutable entity state in it.

Motion.cpp parses keyframe blocks without allowing their declarations into the normal cascade. Animation clocks retain pause elapsed time; definition changes restart. Transition targets and displayed values are separate so interruptions start from the displayed result. Colors and compatible numeric token structures interpolate; incompatible data is discrete. Preserve parser and per-element limits and finite times. Existing typed layout/paint validation remains the final authority on supported values.

Tests: CSSTests covers deterministic timestamp sampling, interruption, keyframes, pause and important priority. GuiTests covers live rule/inline changes and atomic rejection. LuaRuntimeTests exercises the actual bound handle. Build only affected targets against existing dependency SDKs; dependency builds belong in CI.
