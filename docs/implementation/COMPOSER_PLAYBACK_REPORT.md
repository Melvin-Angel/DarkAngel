# Composer preview playback and camera usability

9 October 2026, after ed58d2a. M4/M5 remain In progress; live EOS remains externally blocked.

Delivered: isolated animation preview Play/Pause/Reset, presentation-only rate and loop controls, automatic end-stop, right-drag orbit and distance. Scrubbing/dragging pauses playback; workflow/asset/focus changes stop its clock. The clock advances only compatible prepared frozen clips, with bounded frame delta, and never enters native gameplay activation/motor/action-event execution. Clip captions show the source name and frozen generation. Temporary clip-package manifests are removed after their descriptors/archives are loaded; old pose candidates remain intact on rejected preparation.

Asset creation/duplicate/revert/reload actions are now a compact collapsible section. Timeline rows and pose preview fit more usefully together. Swap-chain creation/resizing follows the actual Win32 client size; tall hidden windows clamped by Windows no longer produce mismatched GUI clipping/scaling and blank capture padding. The supplied window size remains a request subject to native limits.

Focused verification: `python scripts/verify_composer.py --playback --build` builds the full editor; default-rate scripted preview advances to tick20, then stops at tick52 after60 frames with gameplay/source unchanged. The original 120-tick light smoke also passes. D3D12 captures were inspected after the window/layout correction. Receipt: `evidence/composer-playback.json`. Rate/loop/orbit/reset controls are compiled/rendered; physical control automation and full frame-performance/provider qualification are not claimed. No historical suite chain is repeated. Initial JSON/string-comparison compilation failure and the corrected final build log are both retained.

Next: a focused Animation workflow using the delivered Composer/clip-preview tools, followed by shared compatible picker and attribute-authoring improvements. Graph/layer/mask tools and complete Character/Player workflows remain planned. M4/M5 acceptance status is unchanged.
