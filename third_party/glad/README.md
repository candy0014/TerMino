# OpenGL loader

Generated with **glad2 2.0.8**, using its packaged Khronos specification:

```sh
python -m glad --reproducible --api gl:core=3.3 --extensions '' --out-path third_party/glad c
```

No extensions or built-in loader; `SDL_GL_GetProcAddress` supplies functions.
The generator is a development tool, not a build dependency. Generated files
are checked in so Linux and Windows build the identical API without Python or
network access for code generation. See `LICENSE` and the generated file headers.

Source: https://github.com/Dav1dde/glad/tree/v2.0.8
