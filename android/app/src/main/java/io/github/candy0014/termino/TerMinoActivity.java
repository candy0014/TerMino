package io.github.candy0014.termino;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.TerMinoEdit;
import android.os.Bundle;
import android.widget.RelativeLayout;

public class TerMinoActivity extends SDLActivity {
    @Override protected String[] getLibraries() { return new String[] {"SDL3", "main"}; }
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        mTextEdit = new TerMinoEdit(this);
        mLayout.addView(mTextEdit, new RelativeLayout.LayoutParams(0, 0));
    }
}
