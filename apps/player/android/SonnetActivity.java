package io.github.pacheco95.sonnet;

import android.util.Log;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.libsdl.app.SDLActivity;

/**
 * The player's activity (docs/player.md, "Running on Android"). SDL's Java side does the rest:
 * it loads the player library and calls its SDL_main on its own thread.
 */
public class SonnetActivity extends SDLActivity {
    /** The launch intent's string extra that carries the player's arguments. */
    public static final String ARGUMENTS_EXTRA = "args";

    /** SDL is linked into the player statically, so the player is the only library to load. */
    @Override
    protected String[] getLibraries() {
        return new String[] {"sonnet_player"};
    }

    /**
     * The player's arguments, from one string extra: {@code am start ... --es args "<arguments>"}.
     * They are split on whitespace, and double quotes keep an argument with spaces together.
     */
    @Override
    protected String[] getArguments() {
        final String extra = getIntent().getStringExtra(ARGUMENTS_EXTRA);
        final String[] arguments = extra == null ? new String[0] : split(extra);
        // Until the engine's log reaches logcat, this is how a device run shows what it was given.
        Log.i("Sonnet", "arguments: " + Arrays.toString(arguments));
        return arguments;
    }

    static String[] split(String line) {
        final List<String> arguments = new ArrayList<>();
        final StringBuilder current = new StringBuilder();
        boolean quoted = false;
        boolean inArgument = false;
        for (int i = 0; i < line.length(); ++i) {
            final char c = line.charAt(i);
            if (c == '"') {
                quoted = !quoted;
                inArgument = true;
            } else if (Character.isWhitespace(c) && !quoted) {
                if (inArgument) {
                    arguments.add(current.toString());
                    current.setLength(0);
                    inArgument = false;
                }
            } else {
                current.append(c);
                inArgument = true;
            }
        }
        if (inArgument) {
            arguments.add(current.toString());
        }
        return arguments.toArray(new String[0]);
    }
}
