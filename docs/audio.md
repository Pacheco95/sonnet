# audio

Sound for the world's entities, behind `IAudioDevice`, with miniaudio as the only implementation ([ADR-0010](decisions/0010-audio-and-animation.md)). Depends on `world` publicly and on miniaudio privately: no miniaudio header appears in the module's public ones.

| Header | Contents |
|---|---|
| `Components.h` | `AudioSource`, `AudioListener` and `registerComponents` |
| `AudioDevice.h` | `IAudioDevice`, `AudioDesc`, `SoundInfo` and `createAudioDevice` |

## Components

The components are plain structs registered with the world's reflection ([world.md](world.md#components)), so the inspector edits them, scenes save them and scripts read and write them like the core ones. `createAudioDevice` registers them; `registerComponents` does it alone, for tools that load scenes without playing them, and does nothing the second time.

- `AudioSource` names a sound asset and plays it from its entity while `playing` is set, with a volume, a pitch (a playback rate: 2 is an octave up and twice as fast) and a loop flag. A sound that does not loop clears `playing` when it ends, so setting the field again plays it from the start; that is how a script rings a sound more than once, since a component is all `scripting` needs to reach it (ADR-0010).
- A `spatial` source is panned and attenuated from the listener: full volume within `minDistance`, falling off inversely with distance, and no quieter beyond `maxDistance`. A source that is not spatial is heard at its volume wherever it is, for music and interface sounds.
- `AudioListener` is a tag: the scene is heard from its entity, facing the entity's -Z. The first enabled one wins; without one the application's fallback listener is used, which the editor sets to its camera every frame.

## Playing

`createAudioDevice` adds one system to the world, in `PreRender` after the transform system so sources and the listener are placed where this frame draws them. It is not a simulation system: in play mode it starts, updates and ends voices, and in edit mode it stops every source's voice, so leaving play mode is silent even without `stopAll`. A disabled entity, a cleared `playing`, a source whose entity is gone and a sound that ended all end the voice; changing the sound starts the new one.

A sound that is missing from the project or cannot be decoded is reported once and plays nothing, and its `playing` stays set, so a fixed file is picked up by the next frame. The asset database notices a changed file ([assets.md](assets.md#hot-reload)) and hands out a new revision, which ends the voices playing the old sound and starts them again on the new one.

`preview` plays a sound once, unspatialised, in edit mode too: the asset inspector's Play button ([editor.md](editor.md#asset-browser)). `stopAll` ends everything, which is what stopping play mode does.

## miniaudio

- One `ma_engine` mixes, resamples and spatialises, on its own audio thread when there is an output device. The main thread only starts, stops and moves sounds, which miniaudio's setters allow from any thread.
- A sound is decoded whole into memory on first use, to 32-bit float at the file's own rate, and every voice of it reads that one copy through a buffer reference; the engine resamples. WAV, FLAC and MP3 come from miniaudio's own decoders, Ogg Vorbis from stb_vorbis, which `src/MiniaudioImplementation.cpp` compiles into the same translation unit as miniaudio itself ([build.md](build.md#dependency-policy)). Streaming long music from disk is later work.
- `AudioDesc::output` opens the system's default device. Without one, asked for or because none could be opened, which is the case in CI and in the tests, the system mixes each frame's worth of audio on the main thread and `lastMix` returns it: the tests listen to what would have been played, per channel.
- The engine's own format is the device's, 48 kHz stereo by default; `setVolume` scales everything.

## Tests

`audio_tests` covers sources playing in play mode only and falling silent when it stops, with the entity disabled or with `playing` cleared; a sound that ends clearing `playing` and playing again when it is set; panning and attenuation from a listener entity, from a turned listener and from the fallback; sounds that cannot be decoded or found; a changed file restarting its sources; the preview in edit mode; and the components' registration and scene round trip. They run without an output device and assert on the mixed frames.
