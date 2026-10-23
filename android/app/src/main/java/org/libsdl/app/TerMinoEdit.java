package org.libsdl.app;

import android.content.Context;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputConnectionWrapper;

/** Keep SDL's composition/Unicode handling and provide a single-line Done action. */
public final class TerMinoEdit extends SDLDummyEdit {
    public TerMinoEdit(Context context) { super(context); }

    @Override public InputConnection onCreateInputConnection(EditorInfo attributes) {
        InputConnection connection = super.onCreateInputConnection(attributes);
        attributes.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI |
                EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_ACTION_DONE;
        return new InputConnectionWrapper(connection, false) {
            @Override public boolean performEditorAction(int action) {
                finishComposingText();
                SDLInputConnection.nativeGenerateScancodeForUnichar('\n');
                return true;
            }
        };
    }
}
