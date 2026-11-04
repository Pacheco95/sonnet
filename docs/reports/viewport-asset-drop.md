# Viewport asset drop verification

Issue [#449](https://github.com/Pacheco95/sonnet/issues/449), part of
[#441](https://github.com/Pacheco95/sonnet/issues/441). Linux Debug, Lavapipe,
Vulkan validation enabled. These are the editor's own window captures, taken
mid-drag by the headless ImGui test on a temporary copy of the Basic sample
([Dropping assets on the viewport](../editor.md#dropping-assets-on-the-viewport)).
The run ended with no validation message.

## Crate prefab, snapped

![The crate prefab held over the ground with Ctrl held, standing snapped on it](viewport-asset-drop/crate-snapped.png)

The crate prefab is held over the ground with Ctrl held, which turns Snap to
grid on. Its preview stands on the floor at a whole-metre landing point, its
bounds resting on the surface rather than sinking into it. The Game view does not
show it: the viewport counts 20 draws and the Game view 18, the crate's two
meshes. The release adds `instantiate Crate`, which the test then undoes.

## Ground material on the sphere

![The ground material held over the sphere, drawn with it and outlined in the accent colour](viewport-asset-drop/ground-material.png)

The ground material is held over the sphere. The sphere is drawn with the
material's checker and outlined in the accent colour; the Game view still shows
it with its own material. The release adds `set material of Sphere`.

## Reproduce

```sh
SONNET_KEEP_TEST_TMPDIR=1 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ./build/linux-debug/modules/editor/editor_tests '[.drop-captures]'
```

The test process points the temporary directory at one of its own and removes it
on exit ([Build](../build.md)). `SONNET_KEEP_TEST_TMPDIR=1` keeps it, and the
last line on stderr names it, so the images are in
`/tmp/sonnet-editor_tests-<pid>/sonnet-drop-captures`. The `[editor][drop]`
tests drive the same drags and check the placement, the snapping, the material
and script targets, and that nothing changes before the release. This
verification does not cover other desktop platforms or a physical mouse run.
