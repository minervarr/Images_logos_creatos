package io.nava.imageslogoscreator;

import io.nava.appshell.AppShellActivity;

// The only Java in this app. Subclassing AppShellActivity is what wires the
// IME to the native text field; the static initializer loads this app's own
// library - only the consumer knows its name, which is why this cannot live
// in app_shell.
public class MainActivity extends AppShellActivity {
    static {
        System.loadLibrary("imageslogoscreator");
    }
}
